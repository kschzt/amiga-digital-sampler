#ifndef SPI_H
#define SPI_H

#include <pthread.h>
#include "ringbuf.h"

typedef struct {
    ringbuf_t *rb;
    int target_rate;
} spi_args_t;

int spi_thread_create(pthread_t *th, spi_args_t *sa);

#endif
