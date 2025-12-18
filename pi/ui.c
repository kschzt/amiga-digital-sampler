// ======================= ui.c (FINAL VERSION) ==========================
#include "ui.h"
#include "presets.h"

#include <stdio.h>
#include <unistd.h>
#include <termios.h>
#include <fcntl.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <math.h>

// globals from main.c
extern ui_state_t ui;
extern pthread_mutex_t cfg_lock;

static struct termios orig_term;
static int tty_fd = -1;

// -----------------------------------------------------------------------------
// Time Helpers
// -----------------------------------------------------------------------------
static inline uint64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ((uint64_t)ts.tv_sec * 1000ULL) + (ts.tv_nsec / 1000000ULL);
}

// -----------------------------------------------------------------------------
// Terminal Helpers
// -----------------------------------------------------------------------------
static void term_raw_mode(void) {
    struct termios t;
    tcgetattr(STDIN_FILENO, &orig_term);

    t = orig_term;
    t.c_lflag &= ~(ICANON | ECHO);
    t.c_cc[VMIN]  = 0;
    t.c_cc[VTIME] = 0;

    tcsetattr(STDIN_FILENO, TCSANOW, &t);
    fcntl(STDIN_FILENO, F_SETFL, O_NONBLOCK);

    // Open real TTY for keyboard input (SSH-safe)
    tty_fd = open("/dev/tty", O_RDONLY | O_NONBLOCK);
    if (tty_fd < 0) perror("open /dev/tty");
}

void ui_shutdown(void) {
    tcsetattr(STDIN_FILENO, TCSANOW, &orig_term);
    printf("\033[?25h"); // show cursor
}

// -----------------------------------------------------------------------------
// Initializer
// -----------------------------------------------------------------------------
void ui_init(ui_state_t *us) {
    us->vu_level = 0;
    us->peak_level = 0;
    us->clipped = false;
    us->clip_count = 0;
    us->last_clip_time_ms = 0;
    us->quant_noise = 0;
    us->dc_offset = 0;
    us->dsp_load = 0;
    us->sampler_active = false;

    term_raw_mode();

    printf("\033[2J\033[H\033[?25l"); // clear screen once + hide cursor
    fflush(stdout);
}

// -----------------------------------------------------------------------------
// Update metrics (called by audio thread)
// -----------------------------------------------------------------------------
void ui_update_audio_metrics(ui_state_t *us,
                             float abs_sample,
                             float quant_error,
                             float dc,
                             float load,
                             uint64_t ts)
{
    us->vu_level = us->vu_level * 0.90f + abs_sample * 0.10f;

    float decay = 0.995f;
    if (us->peak_level * decay < abs_sample)
        us->peak_level = abs_sample;
    else
        us->peak_level *= decay;

    if (abs_sample >= 0.99f) {
        us->clipped = true;
        us->clip_count++;
        us->last_clip_time_ms = ts;
    } else {
        if (us->clipped && (ts - us->last_clip_time_ms) > 1000)
            us->clipped = false;
    }

    us->quant_noise = us->quant_noise * 0.99f + fabsf(quant_error) * 0.01f;
    us->dc_offset   = us->dc_offset   * 0.999f + dc * 0.001f;
    us->dsp_load    = us->dsp_load    * 0.90f  + load * 0.10f;
}

// -----------------------------------------------------------------------------
// VU Bar Rendering
// -----------------------------------------------------------------------------
static void vu_bar(char *dst, int width, float level)
{
    int visible = 0;

    for (int i = 0; i < width; i++) {
        float p = (float)i / (float)(width - 1);

        if (level >= p) {
            if      (p > 0.90f) strcat(dst, "\033[31m█\033[0m");
            else if (p > 0.70f) strcat(dst, "\033[33m█\033[0m");
            else if (p > 0.25f) strcat(dst, "\033[32m█\033[0m");
            else                strcat(dst, "\033[90m▒\033[0m");
        } else {
            strcat(dst, " ");
        }

        visible++;
    }

    // pad display width
    int pad = width - visible;
    for (int i = 0; i < pad; i++)
        strcat(dst, " ");
}

static void pad_string(char *dst, const char *src, int width)
{
    snprintf(dst, 128, "%-*s", width, src);
}

// -----------------------------------------------------------------------------
// UI Draw
// -----------------------------------------------------------------------------
void ui_draw(const ui_state_t *us) {
    printf("\033[H");  // redraw from top, no flicker

    float vu = us->vu_level;
    float pk = us->peak_level;
    float noise = us->quant_noise + 1e-12f;

    float vu_db = (vu > 1e-9f) ? 20.0f * log10f(vu) : -90.0f;
    float pk_db = (pk > 1e-9f) ? 20.0f * log10f(pk) : -90.0f;
    float noise_db = 20.0f * log10f(noise);

    char vu_str[512] = {0};
    char pk_str[512] = {0};
    vu_bar(vu_str, 30, vu);
    vu_bar(pk_str, 30, pk);

    // ON/OFF block indicators
    #define ON  "\033[32m■\033[0m"
    #define OFF "\033[31m■\033[0m"

    char preset_buf[128];
    pad_string(preset_buf, us->preset_name, 24);

    printf(
"Preset:  \033[36m%s\033[0m    Sampler: %s\n\n"

"DSP Status:                               Levels:\n"
"  Filter:     %s                 VU:   [%-30s]   %6.1f dBFS%20s\n"
"  Shaper:     %s                 Peak: [%-30s]   %6.1f dBFS%20s\n"
"  Dither:     %s                 Clip:  %s (%llu)\n"
"  Compressor: %s\n"
"  Saturate:   %s\n\n"

"Stats:\n"
"  DSP Load:            %4.1f%%\n"
"  Quantizer Noise:     %6.1f dBFS\n"
"  DC Offset:           %+0.4f\n\n"

"Keys: 1–8 presets  •  d s f c t x  •  q=quit\n",

        preset_buf,
        us->sampler_active ? "\033[32mACTIVE\033[0m" : "\033[90midle  \033[0m",

        us->cfg->filter ? ON : OFF,  vu_str, vu_db, "",
        us->cfg->shape  ? ON : OFF,  pk_str, pk_db, "",

        us->cfg->dither ? ON : OFF,
        us->clipped ? "\033[31mYES\033[0m" : "NO",
        (unsigned long long)us->clip_count,

        us->cfg->compress ? ON : OFF,
        us->cfg->saturate  ? ON : OFF,

        us->dsp_load * 100.0f,
        noise_db,
        us->dc_offset
    );

    fflush(stdout);
}

// -----------------------------------------------------------------------------
// Keyboard handling
// -----------------------------------------------------------------------------
static void apply_preset_index(int idx) {
    pthread_mutex_lock(ui.cfg_lock);
    preset_apply(idx, ui.cfg);
    ui.preset_index = idx;
    ui.preset_name = preset_get(idx)->name;
    pthread_mutex_unlock(ui.cfg_lock);
}

static void handle_key(int c) {
    if (c >= '1' && c <= '8') {
        apply_preset_index(c - '1');
        return;
    }

    pthread_mutex_lock(ui.cfg_lock);
    switch (c) {
        case 'd': ui.cfg->dither   = !ui.cfg->dither;   break;
        case 's': ui.cfg->shape    = !ui.cfg->shape;    break;
        case 'f': ui.cfg->filter   = !ui.cfg->filter;   break;
        case 'c': ui.cfg->compress = !ui.cfg->compress; break;
        case 't': ui.cfg->saturate = !ui.cfg->saturate; break;
        case 'x':
            ui.peak_level = 0;
            ui.clipped = false;
            ui.clip_count = 0;
            break;
        case 'q':
            ui_shutdown();
            exit(0);
    }
    pthread_mutex_unlock(ui.cfg_lock);
}

// -----------------------------------------------------------------------------
// UI Thread
// -----------------------------------------------------------------------------
static void *ui_thread(void *arg) {
    (void)arg;

    for (;;) {
        usleep(80000); // smooth 12.5 FPS redraw
        char ch;
        if (tty_fd >= 0 && read(tty_fd, &ch, 1) == 1)
            handle_key(ch);

        ui_draw(&ui);
    }
    return NULL;
}

int ui_thread_create(pthread_t *th, ui_state_t *us) {
    (void)us;
    return pthread_create(th, NULL, ui_thread, NULL);
}
