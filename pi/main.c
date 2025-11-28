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
//#define RB_SIZE 2048

static void usage() {
    printf("sampler [--dither] [--gain X] [--rate Hz] "
           "[--test-tone] [--tone-freq Hz] [--test-ramp]\n");
    exit(0);
}

int main(int argc, char **argv)
{
    dsp_config_t cfg = {
        .dither = false,
        .gain = 1.0f,
        // ProTracker A-3 sampling: period 127÷2=63.5→63, PAL 
        // samplingRate = 3546895 / (floor[period/2]*2)
        // A-3 period = 127, floor[127/2] = 63, 63 * 2 = 126
        // 3546895 / 126 = 28149.9603174
        .target_rate = 28149.96,
        //.target_rate = 28150,
    };

    testmode_t tm = {0};
    tm.test_freq = 1000.f;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--dither"))
            cfg.dither = true;

        else if (!strcmp(argv[i], "--gain") && i+1 < argc)
            cfg.gain = atof(argv[++i]);

        else if (!strcmp(argv[i], "--compress"))
            cfg.compress = true;

        else if (!strcmp(argv[i], "--saturate"))
            cfg.saturate = true;

        else if (!strcmp(argv[i], "--rate") && i+1 < argc)
            cfg.target_rate = atoi(argv[++i]);

        else if (!strcmp(argv[i], "--test-tone"))
            tm.test_tone = true;

        else if (!strcmp(argv[i], "--tone-freq") && i+1 < argc) {
            tm.test_tone = true;
            tm.test_freq = atof(argv[++i]);
        }

        else if (!strcmp(argv[i], "--test-ramp"))
            tm.test_ramp = true;

        else usage();
    }

    fprintf(stderr, "TM: tone=%d ramp=%d freq=%f\n",
        tm.test_tone, tm.test_ramp, tm.test_freq);


    if (tm.test_tone && tm.test_ramp) {
        fprintf(stderr, "--test-tone OR --test-ramp, not both\n");
        exit(1);
    }

    ringbuf_t rb;
    if (ringbuf_init(&rb, RB_SIZE) < 0) {
        fprintf(stderr, "ringbuf init failed\n");
        exit(1);
    }

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

    for (;;) pause();
    return 0;
}
