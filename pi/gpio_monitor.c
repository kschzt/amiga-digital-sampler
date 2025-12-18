#define _GNU_SOURCE
#include "gpio_monitor.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <gpiod.h>

static const char *SCRIPT_ACTIVE   = "./sampler_active.sh";
static const char *SCRIPT_INACTIVE = "./sampler_inactive.sh";

// Fork and exec a script if it exists (non-blocking)
static void run_script(const char *path) {
    if (access(path, X_OK) != 0) return;

    pid_t pid = fork();
    if (pid == 0) {
        execl(path, path, NULL);
        _exit(127);
    }
    // Parent continues immediately
}

static void *gpio_monitor_thread(void *arg) {
    gpio_monitor_args_t *args = (gpio_monitor_args_t *)arg;

    struct gpiod_chip *chip = gpiod_chip_open_by_name("gpiochip0");
    if (!chip) {
        perror("gpio_monitor: gpiod_chip_open");
        return NULL;
    }

    struct gpiod_line *line = gpiod_chip_get_line(chip, args->gpio_pin);
    if (!line) {
        perror("gpio_monitor: gpiod_chip_get_line");
        gpiod_chip_close(chip);
        return NULL;
    }

    if (gpiod_line_request_both_edges_events(line, "sampler") < 0) {
        perror("gpio_monitor: gpiod_line_request_both_edges_events");
        gpiod_chip_close(chip);
        return NULL;
    }

    struct timespec timeout = { .tv_sec = 1, .tv_nsec = 0 };
    struct gpiod_line_event event;

    while (1) {
        int ret = gpiod_line_event_wait(line, &timeout);
        if (ret < 0) {
            perror("gpio_monitor: event_wait");
            break;
        }
        if (ret == 0) {
            // Timeout - reap zombies
            while (waitpid(-1, NULL, WNOHANG) > 0);
            continue;
        }

        if (gpiod_line_event_read(line, &event) < 0) {
            perror("gpio_monitor: event_read");
            break;
        }

        if (event.event_type == GPIOD_LINE_EVENT_RISING_EDGE) {
            run_script(SCRIPT_ACTIVE);
        } else if (event.event_type == GPIOD_LINE_EVENT_FALLING_EDGE) {
            run_script(SCRIPT_INACTIVE);
        }

        while (waitpid(-1, NULL, WNOHANG) > 0);
    }

    gpiod_chip_close(chip);
    return NULL;
}

int gpio_monitor_thread_create(pthread_t *thread, gpio_monitor_args_t *args) {
    return pthread_create(thread, NULL, gpio_monitor_thread, args);
}