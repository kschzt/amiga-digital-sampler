#ifndef GPIO_MONITOR_H
#define GPIO_MONITOR_H

#include <pthread.h>
#include <stdbool.h>

typedef struct {
    int gpio_pin;           // GPIO pin to monitor (BCM numbering)
    bool *active_target;    // Optional: set to true/false on edges
} gpio_monitor_args_t;

// Create and start GPIO monitor thread
// Runs ./sampler_active.sh on rising edge, ./sampler_inactive.sh on falling
int gpio_monitor_thread_create(pthread_t *thread, gpio_monitor_args_t *args);

#endif
