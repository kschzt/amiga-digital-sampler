#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <string.h>
#include <linux/spi/spidev.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define SPI_DEV "/dev/spidev0.0"
#define TOTAL_SIZE 1024  // Send 1KB at a time

int main() {
    int fd = open(SPI_DEV, O_RDWR);
    uint8_t mode = 0;
    uint8_t bits = 8;
    //uint32_t speed = 100000;
    uint32_t speed = 300000;
    
    ioctl(fd, SPI_IOC_WR_MODE, &mode);
    ioctl(fd, SPI_IOC_WR_BITS_PER_WORD, &bits);
    ioctl(fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed);
    
    uint8_t buf[TOTAL_SIZE];
    uint8_t rv = 0;
    
    // Fill entire buffer
    for (int i = 0; i < TOTAL_SIZE; i++)
        buf[i] = rv++;
    
    while (1) {
        // Send entire 1KB buffer in one write
        write(fd, buf, TOTAL_SIZE);
        
        // Update buffer for next iteration
        for (int i = 0; i < TOTAL_SIZE; i++)
            buf[i] = rv++;
            
        //usleep(10000);  // 10ms between transfers
    }
}
