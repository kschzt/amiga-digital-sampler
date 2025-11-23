#include "pico/stdlib.h"
#include "hardware/spi.h"

#define PIN_RX   16  // Pi MOSI -> Pico RX
#define PIN_CS   17  // Pi CE0  -> Pico CSn
#define PIN_SCK  18  // Pi SCLK -> Pico SCK

int main() {
    stdio_init_all();
    sleep_ms(2000); // give USB time to come up

    // Configure pins for SPI0
    gpio_set_function(PIN_RX,  GPIO_FUNC_SPI);
    gpio_set_function(PIN_SCK, GPIO_FUNC_SPI);
    gpio_set_function(PIN_CS,  GPIO_FUNC_SPI);
    // (We don't use MISO, so don't set it)

    // Init SPI and put it into SLAVE mode
    spi_init(spi0, 1000 * 1000);  // required even for slave
    spi_set_slave(spi0, true);
    spi_set_format(spi0,
                   8,             // bits
                   SPI_CPOL_0,
                   SPI_CPHA_0,    // mode 0: same as Linux spidev default
                   SPI_MSB_FIRST);

    printf("SPI slave test ready\n");

    while (true) {
        if (spi_is_readable(spi0)) {
            uint8_t v;
            spi_read_blocking(spi0, 0, &v, 1);
            printf("RX: 0x%02x\n", v);
        }
    }
}
