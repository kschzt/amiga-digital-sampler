#ifndef DSP_H
#define DSP_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    float prev_in;
    float prev_out;
} dcblock_t;

typedef struct {
    float e1, e2;
} nshaper_t;

typedef struct {
    bool dither;
    float gain;
    float target_rate;
} dsp_config_t;

void dsp_init(dcblock_t *dc, nshaper_t *ns);
float dsp_dcblock(dcblock_t *st, float x);
uint8_t dsp_quantize(nshaper_t *st, float x, bool dither);

#endif
