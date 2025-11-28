#ifndef PRESETS_H
#define PRESETS_H

#include <stdbool.h>
#include "dsp.h"

// The preset descriptor
typedef struct {
    const char *name;
    bool filter;
    bool shape;
    bool dither;
    bool compress;
    bool saturate;
} preset_t;

// Accessors
int preset_count(void);
const preset_t *preset_get(int index);

// Apply preset settings to a dsp_config_t
void preset_apply(int index, dsp_config_t *cfg);

#endif
