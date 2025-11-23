#include "audio.h"
#include <pthread.h>
#include <math.h>
#include <stdio.h>
#include <time.h>

#define ALSA_DEVICE "hw:0,0"
#define ALSA_RATE   48000
#define ALSA_CH     2
#define ALSA_FORMAT SND_PCM_FORMAT_S32_LE

static void *audio_thread(void *arg)
{
    audio_args_t *aa = arg;
    ringbuf_t *rb = aa->rb;
    dsp_config_t cfg = aa->cfg;
    testmode_t *tm = &aa->test;

    dcblock_t dc; fir_t fir; nshaper_t ns;
    dsp_init(&dc, &fir, &ns);

    snd_pcm_t *pcm = NULL;
    int32_t alsa_frame[2];

    if (!tm->test_tone && !tm->test_ramp) {
        snd_pcm_open(&pcm, ALSA_DEVICE, SND_PCM_STREAM_CAPTURE,
                     SND_PCM_NONBLOCK);

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
    uint32_t ds_acc = 0;

    // For test mode timing
    struct timespec next_sample_time;
    clock_gettime(CLOCK_MONOTONIC, &next_sample_time);
    const uint64_t sample_period_ns = 1000000000ULL / 48000; // ~20833ns per sample

    for (;;) {
        float x;

        /* --- INPUT SAMPLE @48k --- */
        if (tm->test_ramp || tm->test_tone) {
            // Wait for next 48kHz sample time
            clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next_sample_time, NULL);
            
            if (tm->test_ramp) {
                static uint8_t rv = 0;
                x = (rv++ / 127.5f) - 1.f;
            } else { // test_tone
                x = sinf(phase) * 0.9f;
                phase += phase_inc;
                if (phase >= 2.f * M_PI) phase -= 2.f * M_PI;
            }
            
            // Schedule next sample
            next_sample_time.tv_nsec += sample_period_ns;
            if (next_sample_time.tv_nsec >= 1000000000) {
                next_sample_time.tv_sec++;
                next_sample_time.tv_nsec -= 1000000000;
            }
        }
        else {
            /* ALSA: read exactly 1 frame if available */
            if (snd_pcm_readi(pcm, alsa_frame, 1) == 1) {
                float L = alsa_frame[0] / 8388608.f;
                float R = alsa_frame[1] / 8388608.f;
                x = ((L + R) * 0.5f) * cfg.gain;
            } else {
                continue;   // no new ALSA sample this iteration
            }
        }

        /* --- DSP chain --- */
        x = dsp_dcblock(&dc, x);
        x = dsp_fir(&fir, x);

        /* --- 48k -> target_rate downsample --- */
        ds_acc += cfg.target_rate;
        if (ds_acc >= ALSA_RATE) {
            ds_acc -= ALSA_RATE;

            uint8_t q = dsp_quantize(&ns, x, cfg.dither);
            ringbuf_push(rb, q);
        }
    }

    return NULL;
}
int audio_thread_create(pthread_t *th, audio_args_t *aa)
{
    return pthread_create(th, NULL, audio_thread, aa);
}
