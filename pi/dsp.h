#ifndef DSP_H
#define DSP_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    float prev_input;
    float prev_output;
} dcblock_t;

typedef struct {
    int32_t hist[15];
    int pos;
} fir_t;

typedef struct {
    float error;
} nshaper_t;

typedef struct {
    bool dither;
    float gain;
    int target_rate;
    int fir_taps;
} dsp_config_t;

void dsp_init(dcblock_t *dc, fir_t *fir, nshaper_t *ns);

float dsp_dcblock(dcblock_t *st, float x);
float dsp_fir(fir_t *st, float x);

uint8_t dsp_quantize(
    nshaper_t *st,
    float x,
    bool dither
);

#endif
