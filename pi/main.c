#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>

#include "dsp.h"
#include "audio.h"
#include "spi.h"
#include "ringbuf.h"

#define RB_SIZE 8192

static void usage()
{
    printf(
        "sampler [options]\n"
        "  --filter         Enable 14kHz anti-aliasing LPF (pre + post)\n"
        "  --shape          Enable 2nd/3rd-order noise shaping (auto-enables filter)\n"
        "  --dither         Enable HP-TPDF dither\n"
        "  --compress       Enable gentle compressor\n"
        "  --saturate       Enable soft saturation\n"
        "  --gain X         Set input gain (default 1.0)\n"
        "  --rate Hz        Set target sample rate (default ~28149.96)\n"
        "  --test-tone      Generate sine wave\n"
        "  --tone-freq Hz   Frequency of test tone (default 1000)\n"
        "  --test-ramp      Generate test ramp (0..255 looping)\n"
    );
    exit(0);
}

int main(int argc, char **argv)
{
    // Default DSP config
    dsp_config_t cfg = {
        .filter = false,
        .shape = false,
        .dither = false,
        .compress = false,
        .saturate = false,
        .gain = 1.0f,
        .target_rate = 28149.96f,
    };

    testmode_t tm = {0};
    tm.test_freq = 1000.f;

    bool filter_explicit = false;   // track if user explicitly set --filter

    // -------------------------------------------------------
    // Parse arguments
    // -------------------------------------------------------
    for (int i = 1; i < argc; i++) {

        if (!strcmp(argv[i], "--filter")) {
            cfg.filter = true;
            filter_explicit = true;
        }

        else if (!strcmp(argv[i], "--shape")) {
            cfg.shape = true;
        }

        else if (!strcmp(argv[i], "--dither")) {
            cfg.dither = true;
        }

        else if (!strcmp(argv[i], "--compress")) {
            cfg.compress = true;
        }

        else if (!strcmp(argv[i], "--saturate")) {
            cfg.saturate = true;
        }

        else if (!strcmp(argv[i], "--gain") && i + 1 < argc) {
            cfg.gain = atof(argv[++i]);
        }

        else if (!strcmp(argv[i], "--rate") && i + 1 < argc) {
            cfg.target_rate = atof(argv[++i]);
        }

        else if (!strcmp(argv[i], "--test-tone")) {
            tm.test_tone = true;
        }

        else if (!strcmp(argv[i], "--tone-freq") && i + 1 < argc) {
            tm.test_tone = true;
            tm.test_freq = atof(argv[++i]);
        }

        else if (!strcmp(argv[i], "--test-ramp")) {
            tm.test_ramp = true;
        }

        else {
            usage();
        }
    }

    if (tm.test_tone && tm.test_ramp) {
        fprintf(stderr, "--test-tone OR --test-ramp, not both\n");
        exit(1);
    }

    // -------------------------------------------------------
    // Automatic dependency: shape → filter (unless overridden)
    // -------------------------------------------------------
    if (cfg.shape && !filter_explicit) {
        cfg.filter = true;
    }

    fprintf(stderr,
        "[CFG] filter=%d shape=%d dither=%d compress=%d saturate=%d gain=%.2f rate=%.2f\n",
        cfg.filter, cfg.shape, cfg.dither, cfg.compress, cfg.saturate,
        cfg.gain, cfg.target_rate
    );

    fprintf(stderr,
        "[Test] tone=%d ramp=%d freq=%.2f\n",
        tm.test_tone, tm.test_ramp, tm.test_freq
    );

    // -------------------------------------------------------
    // Ringbuffer
    // -------------------------------------------------------
    ringbuf_t rb;
    if (ringbuf_init(&rb, RB_SIZE) < 0) {
        fprintf(stderr, "ringbuf init failed\n");
        exit(1);
    }

    // -------------------------------------------------------
    // Launch audio + SPI threads
    // -------------------------------------------------------
    pthread_t th_audio, th_spi;

    static audio_args_t aa;
    aa.rb = &rb;
    aa.cfg = cfg;
    aa.test = tm;

    spi_args_t sa = {
        .rb = &rb,
        .target_rate = cfg.target_rate
    };

    audio_thread_create(&th_audio, &aa);
    spi_thread_create(&th_spi, &sa);

    printf("Sampler ready. Press Ctrl+C.\n");

    // -------------------------------------------------------
    // Main thread sleeps forever
    // -------------------------------------------------------
    for (;;)
        pause();

    return 0;
}
