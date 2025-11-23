#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/spi/spidev.h>
#include <time.h>

#include "spi.h"
#include "ringbuf.h"

#define SPI_DEV "/dev/spidev0.0"
#define SPI_SPEED 8000000

// Convert ns
static inline void ts_from_ns(struct timespec *ts, uint64_t ns)
{
    ts->tv_sec  = ns / 1000000000ULL;
    ts->tv_nsec = ns % 1000000000ULL;
}

static void *spi_thread(void *arg)
{
    spi_args_t *sa = arg;
    ringbuf_t *rb = sa->rb;

    int fd = open(SPI_DEV, O_RDWR);
    if (fd < 0) {
        perror("open SPI");
        return NULL;
    }

    uint8_t mode = SPI_MODE_0;
    uint8_t bits = 8;
    uint32_t speed = SPI_SPEED;

    ioctl(fd, SPI_IOC_WR_MODE, &mode);
    ioctl(fd, SPI_IOC_WR_BITS_PER_WORD, &bits);
    ioctl(fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed);

    fprintf(stderr, "[SPI] thread started (efficient mode)\n");

    #define BURST_SIZE 32  // Small bursts for low latency
    uint8_t burst_buf[BURST_SIZE];
    
    while (1) {
        int count = 0;
        
        // Collect real samples from ringbuffer
        while (count < BURST_SIZE) {
            uint8_t sample;
            if (ringbuf_pop(rb, &sample)) {
                burst_buf[count++] = sample;
            } else {
                // No more samples available
                break;
            }
        }
        
        // Send if we have any samples
        if (count > 0) {
            write(fd, burst_buf, count);
//    struct timespec ts = { .tv_sec = 0, .tv_nsec = 5000 }; // 5 µs
//    nanosleep(&ts, NULL);

        } else {
            // No data available - wait briefly
            // This prevents spinning when ringbuf is empty
            //usleep(500);  // 0.5ms
        }
    }

    return NULL;
}

int spi_thread_create(pthread_t *th, spi_args_t *sa)
{
    return pthread_create(th, NULL, spi_thread, sa);
}
