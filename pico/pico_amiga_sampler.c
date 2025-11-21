#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/spi.h"
#include "hardware/irq.h"
#include "sampleout.pio.h"

// ------------------------------
// CONFIG
// ------------------------------
#define STROBE_PIN 10     // Amiga STROBE (5V → divider → GP10)
#define DATA_BASE  2       // GP2..GP9  (8 bits)
#define OE_PIN     11      // 74HCT245 /OE (active LOW)

#define BUF_SIZE   256     // ultra low latency
static volatile uint8_t buf[BUF_SIZE];
static volatile uint32_t head = 0;
static volatile uint32_t tail = 0;

// SPI pins for SPI0 (SLAVE)
#define PIN_RX   16        // Pi MOSI → Pico RX
#define PIN_CS   17        // Pi CE0  → Pico CSn
#define PIN_SCK  18        // Pi SCLK → Pico SCK

// ------------------------------
// RING BUFFER
// ------------------------------
static inline bool buf_empty() {
    return head == tail;
}

static inline bool buf_full() {
    return ((head + 1) & (BUF_SIZE - 1)) == tail;
}

static inline void buf_push(uint8_t v) {
    if (!buf_full()) {
        buf[head] = v;
        head = (head + 1) & (BUF_SIZE - 1);
    }
    // If full → drop (better to drop than block)
}

static inline uint8_t buf_pop() {
    if (buf_empty()) return 0;      // strict underrun → silence
    uint8_t v = buf[tail];
    tail = (tail + 1) & (BUF_SIZE - 1);
    return v;
}

// ------------------------------
// STROBE IRQ: Amiga clock → next sample out
// ------------------------------
void strobe_irq(uint gpio, uint32_t events) {
    if (gpio == STROBE_PIN && (events & GPIO_IRQ_EDGE_RISE)) {
        uint8_t s = buf_pop();
        pio_sm_put(pio0, 0, s);
    }
}

// ------------------------------
// SPI IRQ: receive bytes from Pi
// ------------------------------
void spi0_irq_handler() {
    // RX FIFO ready
    while (spi_is_readable(spi0)) {
        uint8_t v = (uint8_t) spi_get_hw(spi0)->dr;
        buf_push(v);
    }
}

// ------------------------------
// MAIN
// ------------------------------
int main() {
    // Init SDK
    stdio_init_all();
    sleep_ms(200);

    // Hi-Z 245 at boot
    gpio_init(OE_PIN);
    gpio_set_dir(OE_PIN, GPIO_OUT);
    gpio_put(OE_PIN, 1);

    // -------------------
    // STROBE input setup
    // -------------------
    gpio_init(STROBE_PIN);
    gpio_set_dir(STROBE_PIN, GPIO_IN);
    gpio_pull_down(STROBE_PIN);

    gpio_set_irq_enabled_with_callback(
        STROBE_PIN,
        GPIO_IRQ_EDGE_RISE,
        true,
        &strobe_irq
    );

    // -------------------
    // SPI SLAVE SETUP
    // -------------------
    spi_init(spi0, 8 * 1000 * 1000);  // ignored in slave mode but required

    gpio_set_function(PIN_RX,  GPIO_FUNC_SPI);
    gpio_set_function(PIN_SCK, GPIO_FUNC_SPI);
    gpio_set_function(PIN_CS,  GPIO_FUNC_SPI);

    // enable RX interrupt
    irq_set_exclusive_handler(SPI0_IRQ, spi0_irq_handler);
    irq_set_enabled(SPI0_IRQ, true);

    // enable RX FIFO interrupt
    spi_get_hw(spi0)->imsc = SPI_SSPIMSC_RXIM_BITS;

    // -------------------
    // PIO sample output
    // -------------------
    PIO pio = pio0;
    uint off = pio_add_program(pio, &sampleout_program);
    uint sm  = pio_claim_unused_sm(pio, true);

    for (int i = 0; i < 8; i++)
        pio_gpio_init(pio, DATA_BASE + i);

    pio_sm_config c = sampleout_program_get_default_config(off);
    sm_config_set_out_pins(&c, DATA_BASE, 8);
    pio_sm_set_consecutive_pindirs(pio, sm, DATA_BASE, 8, true);

    pio_sm_init(pio, sm, off, &c);
    pio_sm_set_enabled(pio, sm, true);

    // 74HCT245 bus-enable
    gpio_put(OE_PIN, 0);

    // -------------------
    // MAIN LOOP
    // -------------------
    while (1) tight_loop_contents();
}
