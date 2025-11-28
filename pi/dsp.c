#include "dsp.h"
#include <stdlib.h>
#include <math.h>

// Fast xorshift RNG for dither
static uint32_t rng_state = 0x12345678;

static inline float fast_dither(void) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    float r1 = (float)(rng_state & 0xFFFF) / 65536.0f;
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    float r2 = (float)(rng_state & 0xFFFF) / 65536.0f;
    return r1 - r2;  // TPDF: triangular distribution
}

void dsp_init(dcblock_t *dc, nshaper_t *ns)
{
    dc->prev_in = 0.0f;
    dc->prev_out = 0.0f;

    ns->e1 = 0.0f;
    ns->e2 = 0.0f;
}

// ------------------------
// DC Blocker - highpass at ~5Hz
// ------------------------
float dsp_dcblock(dcblock_t *st, float x)
{
    // y[n] = x[n] - x[n-1] + R * y[n-1], R ~= 0.995 for ~5Hz @ 48kHz
    float y = x - st->prev_in + 0.995f * st->prev_out;
    st->prev_in = x;
    st->prev_out = y;
    return y;
}

// ------------------------
// Second-order noise-shaped 8-bit quantizer
// ------------------------
uint8_t dsp_quantize(nshaper_t *st, float x, bool dither)
{
    // Second-order noise shaping
    float shaped = x + 1.6f * st->e1 - 0.65f * st->e2;

    // TPDF dither
    if (dither)
        shaped += fast_dither() * (1.0f / 256.0f);

    // Soft clamp
    if (shaped > 0.99f) shaped = 0.99f;
    if (shaped < -0.99f) shaped = -0.99f;

    // Quantize to signed 8-bit
    int q = (int)roundf(shaped * 127.0f);

    // Error feedback
    float quantized = (float)q / 127.0f;
    st->e2 = st->e1;
    st->e1 = shaped - quantized;

    // Convert to unsigned
    q += 128;
    if (q < 0) q = 0;
    if (q > 255) q = 255;

    return (uint8_t)q;
}
