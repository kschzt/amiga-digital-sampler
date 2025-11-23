#ifndef AUDIO_H
#define AUDIO_H

#include <alsa/asoundlib.h>
#include <stdbool.h>
#include "dsp.h"
#include "ringbuf.h"

typedef struct {
    bool test_tone;
    bool test_ramp;
    float test_freq;
} testmode_t;

typedef struct {
    ringbuf_t *rb;
    dsp_config_t cfg;
    testmode_t test;
} audio_args_t;

int audio_thread_create(pthread_t *th, audio_args_t *aa);

#endif
