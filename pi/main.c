#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>

#include "dsp.h"
#include "audio.h"
#include "spi.h"
#include "ringbuf.h"
#include "ui.h"
#include "presets.h"
#include "gpio_monitor.h"

// Globals required everywhere
ui_state_t ui;
pthread_mutex_t cfg_lock = PTHREAD_MUTEX_INITIALIZER;

#define RB_SIZE 8192

static void usage() {
    printf(
        "sampler [options]\n"
        "  --gain X\n"
        "  --rate Hz\n"
        "  --test-tone\n"
        "  --tone-freq Hz\n"
        "  --test-ramp\n"
    );
    exit(0);
}

int main(int argc, char **argv)
{
    signal(SIGCHLD, SIG_IGN);  // Auto-reap script children

    dsp_config_t cfg = {
        .filter=false, .shape=false, .dither=false,
        .compress=false, .saturate=false,
        .gain=1.0f, .target_rate=28149.96f,
    };

    testmode_t tm={0};
    tm.test_freq=1000;

    for(int i=1;i<argc;i++){
        if(!strcmp(argv[i],"--gain") && i+1<argc)
            cfg.gain = atof(argv[++i]);
        else if(!strcmp(argv[i],"--rate") && i+1<argc)
            cfg.target_rate = atof(argv[++i]);
        else if(!strcmp(argv[i],"--test-tone"))
            tm.test_tone=true;
        else if(!strcmp(argv[i],"--tone-freq") && i+1<argc){
            tm.test_tone=true;
            tm.test_freq=atof(argv[++i]);
        }
        else if(!strcmp(argv[i],"--test-ramp"))
            tm.test_ramp=true;
        else
            usage();
    }

    if(tm.test_tone && tm.test_ramp){
        fprintf(stderr,"Can't use both test modes\n");
        exit(1);
    }

    // Ringbuffer
    ringbuf_t rb;
    if(ringbuf_init(&rb,RB_SIZE)<0){
        perror("ringbuf");
        exit(1);
    }

    ui.cfg = &cfg;
    ui.cfg_lock = &cfg_lock;
    ui.preset_count = preset_count();
    ui.preset_index = 0;
    ui.preset_name = preset_get(0)->name;

    preset_apply(0,&cfg);

    // Thread args
    audio_args_t aa = { .rb=&rb, .cfg=cfg, .test=tm };
    spi_args_t   sa = { .rb=&rb, .target_rate=cfg.target_rate };

    pthread_t th_audio, th_spi, th_ui, th_gpio;

    ui_init(&ui);
    ui_thread_create(&th_ui,&ui);
    audio_thread_create(&th_audio,&aa);
    spi_thread_create(&th_spi,&sa);

    // GPIO activity monitor
    static gpio_monitor_args_t ga = { .gpio_pin = 5 };
    gpio_monitor_thread_create(&th_gpio, &ga);

    for(;;) pause();
    return 0;
}
