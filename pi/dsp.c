#include "dsp.h"
#include <math.h>

// FIR, 14kHz cutoff @ 48kHz
static const float fir_coeffs[FIR_TAPS] = {
    0.000000000000000000f, -0.000009487693207800f,
   -0.000028241026544721f, 0.000130423760155713f,
    0.000000000000000000f, -0.000419065937722088f,
    0.000339186194228387f, 0.000712175138665707f,
   -0.001246734350036165f, -0.000517170810723269f,
    0.002708786953393130f, -0.000931979532220456f,
   -0.004077439249330360f, 0.004291842541225787f,
    0.003865175002367077f, -0.009412796841769195f,
    0.000000000000000010f, 0.014607065201196956f,
   -0.009343353008992155f, -0.016288286793672682f,
    0.024600990798602992f, 0.009104094583609177f,
   -0.043964637021778927f, 0.014467884274793870f,
    0.063427611801429598f, -0.071611468588267765f,
   -0.077949100894862816f, 0.305879101861475322f,
    0.583330847275969511f, 0.305879101861475322f,
   -0.077949100894862816f, -0.071611468588267765f,
    0.063427611801429612f, 0.014467884274793875f,
   -0.043964637021778927f, 0.009104094583609180f,
    0.024600990798602999f, -0.016288286793672689f,
   -0.009343353008992160f, 0.014607065201196956f,
    0.000000000000000010f, -0.009412796841769201f,
    0.003865175002367079f, 0.004291842541225787f,
   -0.004077439249330368f, -0.000931979532220457f,
    0.002708786953393132f, -0.000517170810723270f,
   -0.001246734350036166f, 0.000712175138665707f,
    0.000339186194228387f, -0.000419065937722088f,
    0.000000000000000000f, 0.000130423760155713f,
   -0.000028241026544721f, -0.000009487693207800f,
    0.000000000000000000f
};


// Post-quantization FIR taps are the same

// Fast xorshift RNG
static uint32_t rng_state = 0x12345678;
static inline float fast_rand(void) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return (float)(rng_state & 0xFFFF) / 65536.0f - 0.5f;
}

void dsp_init(dcblock_t *dc, fir_t *fir, postfir_t *postfir, nshaper_t *ns)
{
    dc->prev_in = 0.0f;
    dc->prev_out = 0.0f;

    for (int i = 0; i < FIR_TAPS; i++)
        fir->hist[i] = 0.0f;
    fir->pos = 0;

    for (int i = 0; i < POST_FIR_TAPS; i++)
        postfir->hist[i] = 0.0f;
    postfir->pos = 0;

    ns->e1 = ns->e2 = ns->e3 = 0.0f;
    ns->e1_out = ns->e2_out = 0.0f;
    ns->dither_hp = 0.0f;
    ns->comp_env = 0.0f;
}

// --------------------------------------------------
// DC Block
// --------------------------------------------------
float dsp_dcblock(dcblock_t *st, float x)
{
    float y = x - st->prev_in + 0.995f * st->prev_out;
    st->prev_in = x;
    st->prev_out = y;
    return y;
}

// --------------------------------------------------
// Pre-FIR
// --------------------------------------------------
float dsp_fir(fir_t *st, float x)
{
    st->hist[st->pos] = x;
    float acc = 0.0f;
    int idx = st->pos;

    for (int t = 0; t < FIR_TAPS; t++) {
        acc += fir_coeffs[t] * st->hist[idx];
        if (--idx < 0) idx = FIR_TAPS - 1;
    }
    if (++st->pos >= FIR_TAPS) st->pos = 0;
    return acc;
}

// --------------------------------------------------
// Post-FIR
// --------------------------------------------------
float dsp_postfir(postfir_t *st, float x)
{
    st->hist[st->pos] = x;
    float acc = 0.0f;
    int idx = st->pos;

    for (int t = 0; t < POST_FIR_TAPS; t++) {
        acc += fir_coeffs[t] * st->hist[idx];
        if (--idx < 0) idx = POST_FIR_TAPS - 1;
    }
    if (++st->pos >= POST_FIR_TAPS) st->pos = 0;
    return acc;
}

// --------------------------------------------------
// Compressor
// --------------------------------------------------
float dsp_compress(nshaper_t *st, float x)
{
    float env = fabsf(x);
    if (env > st->comp_env)
        st->comp_env = st->comp_env * 0.95f + env * 0.05f;
    else
        st->comp_env = st->comp_env * 0.9999f + env * 0.0001f;

    float th = 0.7f;
    if (st->comp_env > th) {
        float over = st->comp_env - th;
        float gain = (th + over * 0.33f) / st->comp_env;
        x *= gain;
    }
    return x;
}

// --------------------------------------------------
// Soft Saturator
// --------------------------------------------------
float dsp_saturate(float x)
{
    float ax = fabsf(x);
    if (ax < 0.8f) return x;
    if (ax > 1.5f) return (x > 0) ? 1.0f : -1.0f;

    float t = (ax - 0.8f) / 0.7f;
    float s = 0.8f + 0.2f * (1.0f - (1.0f - t) * (1.0f - t));
    return (x > 0 ? s : -s);
}

// --------------------------------------------------
// Oversample Quantizer (48k)
// --------------------------------------------------
float dsp_quantize_oversample(nshaper_t *st, float x,
                              bool shape, bool dither)
{
    float shaped = x;

    if (shape) {
        shaped = x + 1.8f * st->e1 - 1.1f * st->e2 + 0.3f * st->e3;
    }

    if (dither) {
        float white = fast_rand() + fast_rand();
        float hp = white - st->dither_hp;
        st->dither_hp = white * 0.5f;
        shaped += hp * (0.5f / 256.0f);
    }

    if (shaped > 0.98f) shaped = 0.98f;
    if (shaped < -0.98f) shaped = -0.98f;

    int q = (int)roundf(shaped * 127.0f);
    float quantized = (float)q / 127.0f;

    if (shape) {
        st->e3 = st->e2;
        st->e2 = st->e1;
        st->e1 = shaped - quantized;
    } else {
        st->e1 = st->e2 = st->e3 = 0.0f;
    }

    return quantized;
}

// --------------------------------------------------
// Final Quantizer (8-bit @ target_rate)
// --------------------------------------------------
uint8_t dsp_quantize_final(nshaper_t *st, float x, bool shape)
{
    float shaped = x;
    if (shape) {
        shaped = x + 0.5f * st->e1_out - 0.1f * st->e2_out;
    }

    if (shaped > 0.98f) shaped = 0.98f;
    if (shaped < -0.98f) shaped = -0.98f;

    int q = (int)roundf(shaped * 127.0f);
    float quant = (float)q / 127.0f;

    if (shape) {
        st->e2_out = st->e1_out;
        st->e1_out = shaped - quant;
    } else {
        st->e1_out = st->e2_out = 0.0f;
    }

    q += 128;
    if (q < 0) q = 0;
    if (q > 255) q = 255;
    return (uint8_t)q;
}

