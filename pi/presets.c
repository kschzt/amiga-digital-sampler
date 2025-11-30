#include "presets.h"
#include <stddef.h>

// -----------------------------------------------------------------------------
// Musically meaningful presets only
// -----------------------------------------------------------------------------

// The 8 presets we designed together
static const preset_t PRESETS[] = {

    // 0 — RAW
    { "Raw", 
        .filter = false, 
        .shape = false,
        .dither = false,
        .compress = false,
        .saturate = false
    },

    // 1 — RAW + SAT
    { "Raw + Saturation", 
        .filter = false, 
        .shape = false,
        .dither = false,
        .compress = false,
        .saturate = true
    },

    // 2 — FILTER ONLY (clean 14 kHz LPF)
    { "Filter Only",
        .filter = true,
        .shape = false,
        .dither = false,
        .compress = false,
        .saturate = false
    },

    // 3 — SHAPE (clean 8-bit conversion)
    { "Shaper",
        .filter = true,      // shaping requires anti-alias
        .shape = true,
        .dither = false,
        .compress = false,
        .saturate = false
    },

    // 4 — SHAPE + DITHER (ultra-smooth)
    { "Shaper + Dither",
        .filter = true,
        .shape = true,
        .dither = true,
        .compress = false,
        .saturate = false
    },

    // 5 — DIRTY (aliasing lo-fi mode)
    { "Dirty LoFi",
        .filter = false,
        .shape = true,      // aliasing permitted intentionally
        .dither = false,
        .compress = false,
        .saturate = false
    },

    // 6 — COMP + SAT (nice punchy thickener)
    { "Comp + Sat",
        .filter = false,
        .shape = false,
        .dither = false,
        .compress = true,
        .saturate = true
    },

    // 7 — CLEAN + COMP (pro-audio mode)
    { "Clean + Compressor",
        .filter = true,
        .shape = true,
        .dither = true,
        .compress = true,
        .saturate = false
    },
};

static const int PRESET_TOTAL = sizeof(PRESETS) / sizeof(PRESETS[0]);

// -----------------------------------------------------------------------------
// Accessors
// -----------------------------------------------------------------------------

int preset_count(void)
{
    return PRESET_TOTAL;
}

const preset_t *preset_get(int index)
{
    if (index < 0 || index >= PRESET_TOTAL)
        return NULL;
    return &PRESETS[index];
}

void preset_apply(int index, dsp_config_t *cfg)
{
    const preset_t *p = preset_get(index);
    if (!p) return;

    cfg->filter   = p->filter;
    cfg->shape    = p->shape;
    cfg->dither   = p->dither;
    cfg->compress = p->compress;
    cfg->saturate = p->saturate;
}
