#include "dsp.h"
#include <math.h>

// Minimum-phase input FIR, 14kHz cutoff @ 48kHz
static const float fir_coeffs[FIR_TAPS] = {
    0.2035f, 0.3120f, 0.2506f, 0.1360f, 0.0344f,
   -0.0298f,-0.0518f,-0.0430f,-0.0231f,-0.0042f,
    0.0078f, 0.0118f, 0.0098f, 0.0052f, 0.0010f,
   -0.0018f,-0.0027f,-0.0022f,-0.0012f,-0.0004f,
    0.0001f
};

// Post-quantization FIR, 13kHz cutoff @ 48kHz
// Removes shaped noise above Nyquist before decimation
static const float post_fir_coeffs[POST_FIR_TAPS] = {
    0.009f, 0.020f, 0.039f, 0.063f, 0.086f,
    0.105f, 0.117f, 0.122f,  // center
    0.117f, 0.105f, 0.086f, 0.063f, 0.039f,
    0.020f, 0.009f
};

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

    ns->e1 = 0.0f;
    ns->e2 = 0.0f;
    ns->e3 = 0.0f;
    ns->e1_out = 0.0f;
    ns->e2_out = 0.0f;
    ns->dither_hp = 0.0f;
    ns->comp_env = 0.0f;
}

// ------------------------
// DC Blocker
// ------------------------
float dsp_dcblock(dcblock_t *st, float x)
{
    float y = x - st->prev_in + 0.995f * st->prev_out;
    st->prev_in = x;
    st->prev_out = y;
    return y;
}

// ------------------------
// Minimum-phase input FIR
// ------------------------
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

// ------------------------
// Post-quantization FIR (removes HF noise before decimation)
// ------------------------
float dsp_postfir(postfir_t *st, float x)
{
    st->hist[st->pos] = x;
    float acc = 0.0f;
    int idx = st->pos;

    for (int t = 0; t < POST_FIR_TAPS; t++) {
        acc += post_fir_coeffs[t] * st->hist[idx];
        if (--idx < 0) idx = POST_FIR_TAPS - 1;
    }

    if (++st->pos >= POST_FIR_TAPS) st->pos = 0;
    return acc;
}

// ------------------------
// Gentle compressor
// ------------------------
float dsp_compress(nshaper_t *st, float x)
{
    float env_in = fabsf(x);
    
    if (env_in > st->comp_env)
        st->comp_env = st->comp_env * 0.95f + env_in * 0.05f;
    else
        st->comp_env = st->comp_env * 0.9999f + env_in * 0.0001f;

    float threshold = 0.7f;
    if (st->comp_env > threshold) {
        float over = st->comp_env - threshold;
        float gain = (threshold + over * 0.33f) / st->comp_env;
        x *= gain;
    }
    return x;
}

// ------------------------
// Soft saturation
// ------------------------
float dsp_saturate(float x)
{
    float ax = fabsf(x);
    if (ax < 0.8f) return x;
    if (ax > 1.5f) return (x > 0) ? 1.0f : -1.0f;
    
    float sign = (x > 0) ? 1.0f : -1.0f;
    float t = (ax - 0.8f) / 0.7f;
    return sign * (0.8f + 0.2f * (1.0f - (1.0f - t) * (1.0f - t)));
}

// ------------------------
// Oversampled quantizer (runs at 48kHz)
// Aggressive noise shaping - pushes noise to 16-24kHz
// Returns FLOAT so we can filter before decimation
// ------------------------
float dsp_quantize_oversample(nshaper_t *st, float x, bool dither)
{
    // 3rd-order, aggressive (safe because post-filter removes HF)
    float shaped = x + 1.8f * st->e1 - 1.1f * st->e2 + 0.3f * st->e3;

    // HP-TPDF dither
    if (dither) {
        float white = fast_rand() + fast_rand();
        float hp_dither = white - st->dither_hp;
        st->dither_hp = white * 0.5f;
        shaped += hp_dither * (0.5f / 256.0f);
    }

    if (shaped > 0.98f) shaped = 0.98f;
    if (shaped < -0.98f) shaped = -0.98f;

    // Quantize
    int q = (int)roundf(shaped * 127.0f);
    float quantized = (float)q / 127.0f;

    // Error feedback
    st->e3 = st->e2;
    st->e2 = st->e1;
    st->e1 = shaped - quantized;

    // Return as float for post-filtering
    return quantized;
}

// ------------------------
// Final output quantizer (runs at 28kHz)
// Gentle shaping - most noise already removed by post-filter
// ------------------------
uint8_t dsp_quantize_final(nshaper_t *st, float x, bool dither)
{
    // 2nd-order, gentle (signal already cleaned up)
    float shaped = x + 0.5f * st->e1_out - 0.1f * st->e2_out;

    if (shaped > 0.98f) shaped = 0.98f;
    if (shaped < -0.98f) shaped = -0.98f;

    int q = (int)roundf(shaped * 127.0f);
    float quantized = (float)q / 127.0f;

    st->e2_out = st->e1_out;
    st->e1_out = shaped - quantized;

    q += 128;
    if (q < 0) q = 0;
    if (q > 255) q = 255;

    return (uint8_t)q;
}
