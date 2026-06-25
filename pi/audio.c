// ===== audio.c =====
#include "audio.h"
#include "ui.h"
#include "presets.h"

#include <pthread.h>
#include <sched.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
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
        int err = snd_pcm_open(&pcm, ALSA_DEVICE, SND_PCM_STREAM_CAPTURE, 0);
        if (err < 0 || !pcm) {
            fprintf(stderr,
                "Cannot open ALSA capture device %s: %s\n"
                "Is the HiFiBerry Digi+ I/O HAT enabled? Check `arecord -l`.\n",
                ALSA_DEVICE, snd_strerror(err));
            exit(1);
        }

        snd_pcm_hw_params_t *p;
        snd_pcm_hw_params_alloca(&p);
        snd_pcm_hw_params_any(pcm, p);
        snd_pcm_hw_params_set_access(pcm, p, SND_PCM_ACCESS_RW_INTERLEAVED);
        snd_pcm_hw_params_set_format(pcm, p, ALSA_FORMAT);
        snd_pcm_hw_params_set_channels(pcm, p, ALSA_CH);
        snd_pcm_hw_params_set_rate(pcm, p, ALSA_RATE, 0);

        // Explicitly size the capture buffer. Left to driver defaults this
        // can be small enough that a brief scheduling stall overruns once per
        // buffer-fill (~hundreds of ms) -> a steady audible click. A generous
        // buffer gives the DSP loop slack to catch up after any hiccup.
        snd_pcm_uframes_t period = ALSA_FRAMES;
        snd_pcm_uframes_t buffer = ALSA_FRAMES * 32;   // ~170 ms of headroom
        snd_pcm_hw_params_set_period_size_near(pcm, p, &period, 0);
        snd_pcm_hw_params_set_buffer_size_near(pcm, p, &buffer, 0);

        int perr = snd_pcm_hw_params(pcm, p);
        if (perr < 0)
            fprintf(stderr, "ALSA hw_params: %s\n", snd_strerror(perr));
        snd_pcm_prepare(pcm);
    }

    // Run the capture/DSP thread at real-time priority so the kernel can't
    // preempt it mid-block. A stall here means the capture buffer overruns,
    // which is exactly the periodic click we're chasing. Needs CAP_SYS_NICE
    // (run as root or grant rtprio); degrades gracefully if unavailable.
    if (!tm->test_tone && !tm->test_ramp) {
        struct sched_param sp = { .sched_priority = 50 };
        if (pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp) != 0)
            fprintf(stderr,
                "Note: could not set real-time priority; run with enough "
                "privileges (root / rtprio) to avoid capture xruns.\n");
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
                // Overrun (or other recoverable error): count it so it's
                // visible in the UI, recover, and keep going. Each overrun is
                // a dropped chunk == one audible click.
                ui.xruns++;
                snd_pcm_recover(pcm, frames, 1);
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
