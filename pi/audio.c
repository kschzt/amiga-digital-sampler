// ===== audio.c =====
#include "audio.h"
#include "ui.h"
#include "presets.h"

#include <pthread.h>
#include <math.h>
#include <stdio.h>
#include <time.h>

extern ui_state_t ui;
extern pthread_mutex_t cfg_lock;

#define ALSA_DEVICE "hw:0,0"
#define ALSA_RATE   48000
#define ALSA_CH     2
#define ALSA_FORMAT SND_PCM_FORMAT_S24_LE

// time helpers --------------------------------------------------------
static inline uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ((uint64_t)ts.tv_sec * 1000000000ULL) + ts.tv_nsec;
}

static inline uint64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ((uint64_t)ts.tv_sec * 1000ULL) + ts.tv_nsec / 1000000ULL;
}

// --------------------------------------------------------------------
static void *audio_thread(void *arg)
{
    audio_args_t *aa = arg;
    ringbuf_t *rb = aa->rb;
    testmode_t *tm = &aa->test;

    // DSP state
    dcblock_t dc;
    fir_t fir;
    postfir_t postfir;
    nshaper_t ns;
    dsp_init(&dc, &fir, &postfir, &ns);

    snd_pcm_t *pcm = NULL;

    #define ALSA_FRAMES 256
    int32_t alsa_buf[ALSA_FRAMES * 2];

    // open ALSA if not test mode
    if (!tm->test_tone && !tm->test_ramp) {
        snd_pcm_open(&pcm, ALSA_DEVICE, SND_PCM_STREAM_CAPTURE, 0);

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

    float phase = 0.0f;
    float phase_inc = 2.f * M_PI * tm->test_freq / ALSA_RATE;
    float ds_acc = 0.0f;

    for (;;) {

        // --- load live DSP config ---
        pthread_mutex_lock(ui.cfg_lock);
        dsp_config_t cfg = *ui.cfg;
        pthread_mutex_unlock(ui.cfg_lock);

        uint64_t start_ns = now_ns();

        // -----------------------------------------------------------------
        // TEST MODE
        // -----------------------------------------------------------------
        if (tm->test_tone || tm->test_ramp) {

            float x;

            if (tm->test_ramp) {
                static uint8_t rv = 0;
                x = (rv++ / 127.5f) - 1.f;
            } else {
                x = sinf(phase) * 0.9f;
                phase += phase_inc;
                if (phase >= 2.f * M_PI) phase -= 2.f * M_PI;
            }

            // DC-block
            float dc_x = dsp_dcblock(&dc, x);

            // pre-FIR
            if (cfg.filter)
                dc_x = dsp_fir(&fir, dc_x);

            // compressor
            if (cfg.compress)
                dc_x = dsp_compress(&ns, dc_x);

            // saturator
            if (cfg.saturate)
                dc_x = dsp_saturate(dc_x);

            // quantizer
            float before = dc_x;
            float q_over = dsp_quantize_oversample(
                &ns, before, cfg.shape, cfg.dither
            );
            float qerr = before - q_over;

            float qf = cfg.filter ? dsp_postfir(&postfir, q_over) : q_over;

            // decimate 48k → 28k
            ds_acc += cfg.target_rate;
            if (ds_acc >= ALSA_RATE) {
                ds_acc -= ALSA_RATE;

                uint8_t q = dsp_quantize_final(&ns, qf, cfg.shape);
                ringbuf_push(rb, q);
            }

            // compute dsp load
            float dsp_load = (float)(now_ns() - start_ns) /
                             (1000000000.0f / ALSA_RATE);

            // send metrics
            ui_update_audio_metrics(&ui,
                fabsf(before),
                qerr,
                dc_x,
                dsp_load,
                now_ms()
            );

            // pacing
            struct timespec ts = {0, 20833};
            nanosleep(&ts, NULL);
        }

        // -----------------------------------------------------------------
        // ALSA CAPTURE MODE
        // -----------------------------------------------------------------
        else
        {
            int frames = snd_pcm_readi(pcm, alsa_buf, ALSA_FRAMES);
            if (frames < 0) {
                snd_pcm_prepare(pcm);
                continue;
            }

            for (int i = 0; i < frames; i++) {

                int32_t rawL = alsa_buf[i * 2]     & 0x00FFFFFF;
                int32_t rawR = alsa_buf[i * 2 + 1] & 0x00FFFFFF;

                if (rawL & 0x800000) rawL |= 0xFF000000;
                if (rawR & 0x800000) rawR |= 0xFF000000;

                float L = rawL / 8388608.0f;
                float R = rawR / 8388608.0f;

                float x = ((L + R) * 0.5f) * cfg.gain;

                // --- DSP chain ---
                float dc_x = dsp_dcblock(&dc, x);

                if (cfg.filter)
                    dc_x = dsp_fir(&fir, dc_x);

                if (cfg.compress)
                    dc_x = dsp_compress(&ns, dc_x);

                if (cfg.saturate)
                    dc_x = dsp_saturate(dc_x);

                float before = dc_x;

                float q_over = dsp_quantize_oversample(
                    &ns, before, cfg.shape, cfg.dither
                );
                float qerr = before - q_over;

                float qf = cfg.filter ? dsp_postfir(&postfir, q_over) : q_over;

                // decimation
                ds_acc += cfg.target_rate;
                if (ds_acc >= ALSA_RATE) {
                    ds_acc -= ALSA_RATE;

                    uint8_t q = dsp_quantize_final(&ns, qf, cfg.shape);
                    ringbuf_push(rb, q);
                }

                float dsp_load = (float)(now_ns() - start_ns) /
                                 (1000000000.0f / ALSA_RATE);
                start_ns = now_ns(); // next frame timing

                ui_update_audio_metrics(&ui,
                    fabsf(before),
                    qerr,
                    dc_x,
                    dsp_load,
                    now_ms()
                );
            }
        }
    }

    return NULL;
}

int audio_thread_create(pthread_t *th, audio_args_t *aa)
{
    return pthread_create(th, NULL, audio_thread, aa);
}
