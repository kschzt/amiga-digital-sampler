#define _GNU_SOURCE
#include "gpio_monitor.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <gpiod.h>

static const char *SCRIPT_ACTIVE   = "./sampler_active.sh";
static const char *SCRIPT_INACTIVE = "./sampler_inactive.sh";

static void run_script(const char *path) {
    if (access(path, X_OK) != 0) return;

    pid_t pid = fork();
    if (pid == 0) {
        execl(path, path, NULL);
        _exit(127);
    }
}

static void *gpio_monitor_thread(void *arg) {
    gpio_monitor_args_t *args = (gpio_monitor_args_t *)arg;

    struct gpiod_chip *chip = gpiod_chip_open("/dev/gpiochip0");
    if (!chip) {
        perror("gpio_monitor: gpiod_chip_open");
        return NULL;
    }

    struct gpiod_line_settings *settings = gpiod_line_settings_new();
    gpiod_line_settings_set_direction(settings, GPIOD_LINE_DIRECTION_INPUT);
    gpiod_line_settings_set_edge_detection(settings, GPIOD_LINE_EDGE_BOTH);

    struct gpiod_line_config *line_cfg = gpiod_line_config_new();
    unsigned int offset = args->gpio_pin;
    gpiod_line_config_add_line_settings(line_cfg, &offset, 1, settings);

    struct gpiod_request_config *req_cfg = gpiod_request_config_new();
    gpiod_request_config_set_consumer(req_cfg, "sampler");

    struct gpiod_line_request *request = gpiod_chip_request_lines(chip, req_cfg, line_cfg);

    gpiod_request_config_free(req_cfg);
    gpiod_line_config_free(line_cfg);
    gpiod_line_settings_free(settings);

    if (!request) {
        perror("gpio_monitor: gpiod_chip_request_lines");
        gpiod_chip_close(chip);
        return NULL;
    }

    struct gpiod_edge_event_buffer *event_buffer = gpiod_edge_event_buffer_new(1);

    while (1) {
        int ret = gpiod_line_request_wait_edge_events(request, 1000000000LL); // 1 sec
        if (ret < 0) {
            perror("gpio_monitor: wait_edge_events");
            break;
        }
        if (ret == 0) {
            while (waitpid(-1, NULL, WNOHANG) > 0);
            continue;
        }

        int num = gpiod_line_request_read_edge_events(request, event_buffer, 1);
        if (num < 0) {
            perror("gpio_monitor: read_edge_events");
            break;
        }

        struct gpiod_edge_event *event = gpiod_edge_event_buffer_get_event(event_buffer, 0);
        enum gpiod_edge_event_type type = gpiod_edge_event_get_event_type(event);

        if (type == GPIOD_EDGE_EVENT_RISING_EDGE) {
            run_script(SCRIPT_ACTIVE);
        } else if (type == GPIOD_EDGE_EVENT_FALLING_EDGE) {
            run_script(SCRIPT_INACTIVE);
        }

        while (waitpid(-1, NULL, WNOHANG) > 0);
    }

    gpiod_edge_event_buffer_free(event_buffer);
    gpiod_line_request_release(request);
    gpiod_chip_close(chip);
    return NULL;
}

int gpio_monitor_thread_create(pthread_t *thread, gpio_monitor_args_t *args) {
    return pthread_create(thread, NULL, gpio_monitor_thread, args);
}