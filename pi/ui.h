#ifndef UI_H
#define UI_H

#include <stdbool.h>
#include <stdint.h>
#include <pthread.h>
#include "dsp.h"   // for dsp_config_t

// ------------------------------------------------------------
// UI shared state structure
// ------------------------------------------------------------
typedef struct {
    dsp_config_t *cfg;          // Points to global DSP config
    pthread_mutex_t *cfg_lock;  // Protects cfg

    // Dynamic audio metrics (written by audio thread)
    float vu_level;             // Smoothed absolute level (0..1)
    float peak_level;           // Slow decay peak (0..1)
    bool clipped;               // Set when a clip is detected
    uint64_t clip_count;        // Total clips
    uint64_t last_clip_time_ms; // When last clip occurred (for clearing)

    float quant_noise;          // Smoothed quantizer error magnitude
    float dc_offset;            // Smoothed DC offset
    float dsp_load;             // Realtime audio thread load %

    const char *preset_name;    // UI-visible name
    int preset_index;           // 0-based
    int preset_count;

    // Sampler activity (set by GPIO monitor thread)
    bool sampler_active;        // true when Pico asserts activity pin

    // Internal timing for UI refresh
    uint64_t last_ui_update_ms;
} ui_state_t;

// ------------------------------------------------------------
// Public API
// ------------------------------------------------------------

// Called once at startup (main.c)
//   cfg, cfg_lock, preset_count, preset_name initially set there
void ui_init(ui_state_t *us);

// Start UI thread (non-blocking)
int ui_thread_create(pthread_t *th, ui_state_t *us);

// Terminal cleanup on exit (restores cooked mode)
void ui_shutdown(void);

// Utility the audio thread calls periodically to update stats
void ui_update_audio_metrics(
    ui_state_t *us,
    float abs_sample,          // absolute sample value 0..1
    float quant_error,         // oversampled quantizer error (shaped-quantized)
    float dc,                  // raw dc-blocked sample
    float load,                // dsp load 0..1
    uint64_t now_ms            // timestamp
);

// UI thread draw function
void ui_draw(const ui_state_t *us);

#endif
