#include "dsp.h"
#include <stdlib.h>
#include <math.h>

//
// FIR coefficients (14 kHz LPF @ 48 kHz)
// scaled to float [-1,1]
//
static const float fir_coeffs[15] = {
    512,  866, 1543, 2517, 3704,
    4961, 6130, 7042, 7540,
    7540, 7042, 6130, 4961,
    3704, 2517
};

static float fir_norm[15];
static float fir_gain = 0.0f;

void dsp_init(dcblock_t *dc, fir_t *fir, nshaper_t *ns)
{
    dc->prev_input  = 0.f;
    dc->prev_output = 0.f;

    fir->pos = 0;
    fir_gain = 0.f;

    // Load coefficients (convert Q15 to float) and compute gain
    for (int i = 0; i < 15; i++) {
        fir->hist[i] = 0;
        fir_norm[i] = fir_coeffs[i] / 32768.0f;
        fir_gain += fir_norm[i];
    }

    // Normalize to unity gain
    for (int i = 0; i < 15; i++) {
        fir_norm[i] /= fir_gain;
    }

    ns->error = 0.f;
}

// ------------------------
// DC BLOCK
// ------------------------
float dsp_dcblock(dcblock_t *st, float x)
{
    float y = x - st->prev_input + 0.995f * st->prev_output;
    st->prev_input = x;
    st->prev_output = y;
    return y;
}

// ------------------------
// FIR LPF
// ------------------------
float dsp_fir(fir_t *st, float x)
{
    st->hist[st->pos] = (int32_t)(x * 32768.0f);
    float acc = 0.f;
    int i = st->pos;

    for (int t = 0; t < 15; t++) {
        acc += fir_norm[t] * (float)st->hist[i];
        if (--i < 0) i = 14;
    }

    if (++st->pos >= 15) st->pos = 0;
    return acc / 32768.0f;
}

// ------------------------
// Noise-shaped 8-bit quantizer
// ------------------------
uint8_t dsp_quantize(
    nshaper_t *st,
    float x,
    bool dither
) {
    // error feedback (noise shaping)
    float s = x + st->error;

    // optional TPDF dither (~0.5 LSB)
    if (dither) {
        float r = ((float)rand() / RAND_MAX) - ((float)rand() / RAND_MAX);
        s += r * (1.f / 256.f);
    }

    // clamp -1..+1
    if (s > 1.f) s = 1.f;
    if (s < -1.f) s = -1.f;

    // convert to signed 8-bit -128..127
    int q = (int)(s * 127.f);

    // update error
    st->error = s - ((float)q / 127.f);

    // convert signed → unsigned 0..255
    q += 128;
    if (q < 0) q = 0;
    if (q > 255) q = 255;

    return (uint8_t)q;
}
