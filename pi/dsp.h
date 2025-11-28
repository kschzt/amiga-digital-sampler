#ifndef DSP_H
#define DSP_H

#include <stdint.h>
#include <stdbool.h>

#define FIR_TAPS 21
#define POST_FIR_TAPS 15

typedef struct {
    float prev_in;
    float prev_out;
} dcblock_t;

typedef struct {
    float hist[FIR_TAPS];
    int pos;
} fir_t;

typedef struct {
    float hist[POST_FIR_TAPS];
    int pos;
} postfir_t;

typedef struct {
    float e1, e2, e3;      // oversampled quantizer errors
    float e1_out, e2_out;  // output quantizer errors
    float dither_hp;       // HP dither state
    float comp_env;        // compressor envelope
} nshaper_t;

typedef struct {
    bool dither;
    bool compress;
    bool saturate;
    float gain;
    float target_rate;
} dsp_config_t;

void dsp_init(dcblock_t *dc, fir_t *fir, postfir_t *postfir, nshaper_t *ns);
float dsp_dcblock(dcblock_t *st, float x);
float dsp_fir(fir_t *st, float x);
float dsp_postfir(postfir_t *st, float x);
float dsp_compress(nshaper_t *st, float x);
float dsp_saturate(float x);
float dsp_quantize_oversample(nshaper_t *st, float x, bool dither);
uint8_t dsp_quantize_final(nshaper_t *st, float x, bool dither);

#endif
