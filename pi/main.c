#define _GNU_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <linux/spi/spidev.h>
#include <sys/ioctl.h>
#include <alsa/asoundlib.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <math.h>
#include <time.h>

#include "dsp.h"

#define SPI_DEV "/dev/spidev0.0"
#define SPI_SPEED 8000000
#define BURST 32

// ALSA
#define ALSA_DEVICE "hw:1,0"
#define ALSA_RATE   48000
#define ALSA_CH     2
#define ALSA_FORMAT SND_PCM_FORMAT_S32_LE

// ProTracker A-3 default
#define DEFAULT_RATE 27928

static void usage() {
    printf("sampler [--dither] [--gain X] [--rate Hz] [--test-tone] [--tone-freq Hz]\n");
    exit(0);
}

int main(int argc, char **argv)
{
    // -------------------------
    // CLI ARGS
    // -------------------------
    dsp_config_t cfg = {
        .dither = false,
        .gain = 1.0f,
        .target_rate = DEFAULT_RATE,
        .fir_taps = 15
    };

    bool test_tone = false;
    float test_freq = 1000.f;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--dither")) cfg.dither = true;
        else if (!strcmp(argv[i], "--gain") && i+1 < argc)
            cfg.gain = atof(argv[++i]);
        else if (!strcmp(argv[i], "--rate") && i+1 < argc)
            cfg.target_rate = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--test-tone"))
            test_tone = true;
        else if (!strcmp(argv[i], "--tone-freq") && i+1 < argc) {
            test_tone = true;
            test_freq = atof(argv[++i]);
        }
        else usage();
    }

    // -------------------------
    // SPI SETUP
    // -------------------------
    int spi_fd = open(SPI_DEV, O_RDWR);

    uint8_t mode = SPI_MODE_0;
    uint8_t bits = 8;
    uint32_t speed = SPI_SPEED;

    ioctl(spi_fd, SPI_IOC_WR_MODE, &mode);
    ioctl(spi_fd, SPI_IOC_WR_BITS_PER_WORD, &bits);
    ioctl(spi_fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed);

    struct spi_ioc_transfer xfer = {
        .tx_buf = 0,
        .rx_buf = 0,
        .len    = BURST,
        .speed_hz = SPI_SPEED,
        .bits_per_word = 8
    };

    // -------------------------
    // ALSA SETUP (skipped in test-tone mode)
    // -------------------------
    snd_pcm_t *pcm = NULL;
    snd_pcm_hw_params_t *params;

    if (!test_tone) {
        snd_pcm_open(&pcm, ALSA_DEVICE, SND_PCM_STREAM_CAPTURE, 0);
        snd_pcm_hw_params_alloca(&params);
        snd_pcm_hw_params_any(pcm, params);
        snd_pcm_hw_params_set_access(pcm, params, SND_PCM_ACCESS_RW_INTERLEAVED);
        snd_pcm_hw_params_set_format(pcm, params, ALSA_FORMAT);
        snd_pcm_hw_params_set_channels(pcm, params, ALSA_CH);
        snd_pcm_hw_params_set_rate(pcm, params, ALSA_RATE, 0);

        snd_pcm_uframes_t period = 128;
        snd_pcm_uframes_t buffer = 512;
        snd_pcm_hw_params_set_period_size_near(pcm, params, &period, 0);
        snd_pcm_hw_params_set_buffer_size_near(pcm, params, &buffer);

        snd_pcm_hw_params(pcm, params);
        snd_pcm_prepare(pcm);
    }

    int32_t alsa_buf[128 * ALSA_CH];
    uint8_t spi_buf[BURST];

    // -------------------------
    // DSP state init
    // -------------------------
    dcblock_t dc;
    fir_t fir;
    nshaper_t ns;
    dsp_init(&dc, &fir, &ns);

    uint32_t tick = 0;
    float peak = 0.f;
    uint32_t ds_acc = 0;

    // Test tone phase
    float phase = 0.0f;
    float phase_inc = 2.f * M_PI * test_freq / ALSA_RATE;

    // Watchdog clocks
    struct timespec t0, t1;
    struct timespec meter_t0, meter_t1;
    clock_gettime(CLOCK_MONOTONIC, &meter_t0);

    printf("Sampler running (press Ctrl+C to stop)\n");

    while (1) {
        clock_gettime(CLOCK_MONOTONIC, &t0);

        if (!test_tone)
            snd_pcm_readi(pcm, alsa_buf, 128);

        for (int i = 0; i < 128 * ALSA_CH; i += 2) {

            float x;

            // ---------------------------
            // SAMPLE INPUT
            // ---------------------------
            if (test_tone) {
                x = sinf(phase);
                phase += phase_inc;
                if (phase > 2.f * M_PI) phase -= 2.f * M_PI;
            } else {
                float L = alsa_buf[i]   / 8388608.f;
                float R = alsa_buf[i+1] / 8388608.f;
                x = ((L + R) * 0.5f) * cfg.gain;
            }

            float absx = fabsf(x);
            if (absx > peak) peak = absx;

            // ---------------------------
            // DSP
            // ---------------------------
            x = dsp_dcblock(&dc, x);
            x = dsp_fir(&fir, x);

            // ---------------------------
            // DOWNSAMPLE
            // ---------------------------
            ds_acc += cfg.target_rate;
            if (ds_acc >= ALSA_RATE) {
                ds_acc -= ALSA_RATE;

                uint8_t q = dsp_quantize(&ns, x, cfg.dither);

                spi_buf[tick % BURST] = q;
                tick++;

		static int once = 0;

                if ((tick % BURST) == 0) {
                    xfer.tx_buf = (unsigned long)spi_buf;
                    ioctl(spi_fd, SPI_IOC_MESSAGE(1), &xfer);
		    if (!once) { printf("SPI BURST SENT\n"); once = 1; }
                }
            }
        }

        // ---------------------------
        // STATUS LINE (every 200ms)
        // ---------------------------
        clock_gettime(CLOCK_MONOTONIC, &meter_t1);

        long mdiff = (meter_t1.tv_sec - meter_t0.tv_sec) * 1000000L +
                     (meter_t1.tv_nsec - meter_t0.tv_nsec) / 1000L;

        if (mdiff >= 200000) {  // 200 ms
            float db = (peak > 0.f) ? (20.f * log10f(peak)) : -99.f;

            printf(
                "\r\033[92mPeak:\033[0m %6.1f dBFS | "
                "\033[96mFreq:\033[0m %7.1f Hz | "
                "\033[95mDither:\033[0m %s | "
                "\033[94mRate:\033[0m %d Hz        ",
                db,
                test_tone ? test_freq : 0.0f,
                cfg.dither ? "ON " : "OFF",
                cfg.target_rate
            );
            fflush(stdout);

            peak = 0.f;
            meter_t0 = meter_t1;
        }

        // ---------------------------
        // WATCHDOG
        // ---------------------------
        clock_gettime(CLOCK_MONOTONIC, &t1);
        long usec = (t1.tv_sec - t0.tv_sec) * 1000000L +
                    (t1.tv_nsec - t0.tv_nsec) / 1000L;

        if (usec > 5000) {
            fprintf(stderr,
                "\n\033[91mWATCHDOG:\033[0m Slow frame %ld us\n", usec);
        }
    }

    return 0;
}
