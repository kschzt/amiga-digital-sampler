#include "audio.h"
#include <pthread.h>
#include <math.h>
#include <stdio.h>
#include <time.h>

#define ALSA_DEVICE "hw:0,0"
#define ALSA_RATE   48000
#define ALSA_CH     2
#define ALSA_FORMAT SND_PCM_FORMAT_S24_LE

static void *audio_thread(void *arg)
{
    audio_args_t *aa = arg;
    ringbuf_t *rb = aa->rb;
    dsp_config_t cfg = aa->cfg;
    testmode_t *tm = &aa->test;

    dcblock_t dc; fir_t fir; postfir_t postfir; nshaper_t ns;
    dsp_init(&dc, &fir, &postfir, &ns);

    snd_pcm_t *pcm = NULL;

    #define ALSA_FRAMES 256
    int32_t alsa_buf[ALSA_FRAMES * 2];

    if (!tm->test_tone && !tm->test_ramp) {
        int err = snd_pcm_open(&pcm, ALSA_DEVICE, SND_PCM_STREAM_CAPTURE, 0);
        if (err < 0) {
            fprintf(stderr, "Cannot open audio: %s\n", snd_strerror(err));
            return NULL;
        }

        snd_pcm_hw_params_t *p;
        snd_pcm_hw_params_alloca(&p);
        snd_pcm_hw_params_any(pcm, p);
        snd_pcm_hw_params_set_access(pcm, p, SND_PCM_ACCESS_RW_INTERLEAVED);
        snd_pcm_hw_params_set_format(pcm, p, ALSA_FORMAT);
        snd_pcm_hw_params_set_channels(pcm, p, ALSA_CH);
        snd_pcm_hw_params_set_rate(pcm, p, ALSA_RATE, 0);

        snd_pcm_hw_params(pcm, p);
        snd_pcm_prepare(pcm);
    }

    float phase = 0.f;
    float phase_inc = 2.f * M_PI * tm->test_freq / ALSA_RATE;
    float ds_acc = 0.0f;

    for (;;) {
        if (tm->test_ramp || tm->test_tone) {
            float x;
            if (tm->test_ramp) {
                static uint8_t rv = 0;
                x = (rv++ / 127.5f) - 1.f;
            } else {
                x = sinf(phase) * 0.9f;
                phase += phase_inc;
                if (phase >= 2.f * M_PI) phase -= 2.f * M_PI;
            }

            // Process through oversampled chain
            float q_over = dsp_quantize_oversample(&ns, x, cfg.dither);
            float q_filtered = dsp_postfir(&postfir, q_over);

            ds_acc += cfg.target_rate;
            if (ds_acc >= (float)ALSA_RATE) {
                ds_acc -= (float)ALSA_RATE;
                uint8_t q = dsp_quantize_final(&ns, q_filtered, false);
                ringbuf_push(rb, q);
            }

            struct timespec ts = { .tv_sec = 0, .tv_nsec = 20833 };
            nanosleep(&ts, NULL);
        }
        else {
            int frames = snd_pcm_readi(pcm, alsa_buf, ALSA_FRAMES);

            if (frames == -EPIPE) {
                snd_pcm_prepare(pcm);
                continue;
            } else if (frames < 0) {
                fprintf(stderr, "ALSA error: %s\n", snd_strerror(frames));
                snd_pcm_prepare(pcm);
                continue;
            }

            for (int i = 0; i < frames; i++) {
                int32_t rawL = alsa_buf[i * 2] & 0x00FFFFFF;
                int32_t rawR = alsa_buf[i * 2 + 1] & 0x00FFFFFF;

                if (rawL & 0x800000) rawL |= 0xFF000000;
                if (rawR & 0x800000) rawR |= 0xFF000000;

                float L = rawL / 8388608.0f;
                float R = rawR / 8388608.0f;
                float x = (L + R) * 0.5f * cfg.gain;

                // Pre-processing at 48kHz
                x = dsp_dcblock(&dc, x);
                x = dsp_fir(&fir, x);
                if (cfg.compress) x = dsp_compress(&ns, x);
                if (cfg.saturate) x = dsp_saturate(x);

                // Quantize at 48kHz (aggressive shaping, noise pushed to 16-24kHz)
                float q_over = dsp_quantize_oversample(&ns, x, cfg.dither);

                // Filter removes HF noise before decimation
                float q_filtered = dsp_postfir(&postfir, q_over);

                // Decimate to 28kHz
                ds_acc += cfg.target_rate;
                if (ds_acc >= (float)ALSA_RATE) {
                    ds_acc -= (float)ALSA_RATE;
                    // Final gentle quantization
                    uint8_t q = dsp_quantize_final(&ns, q_filtered, false);
                    ringbuf_push(rb, q);
                }
            }
        }
    }

    return NULL;
}

int audio_thread_create(pthread_t *th, audio_args_t *aa)
{
    return pthread_create(th, NULL, audio_thread, aa);
}
