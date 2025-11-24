#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "spi_slave_rx.pio.h"
#include "sampleout.pio.h"

// ------------------------------
// CONFIG
// ------------------------------
#define STROBE_PIN 10
#define DATA_BASE  2
#define OE_PIN     11

#define PIN_MOSI 16
#define PIN_CS   17
#define PIN_SCK  18

// Ring buffer - must be power of 2 and aligned to its size
#define RING_BITS 13  // 8KB
#define RING_SIZE (1 << RING_BITS)
#define RING_MASK (RING_SIZE - 1)

static uint8_t spi_ring[RING_SIZE] __attribute__((aligned(RING_SIZE)));
static volatile uint32_t read_ptr = 0;
static uint8_t last_sample = 0x80;

static uint dma_chan;
static PIO spi_pio;
static uint spi_sm;

// Stats
static volatile uint64_t strobe_count = 0;
static volatile uint64_t underruns = 0;

// ------------------------------
// STROBE IRQ - reads directly from DMA ring buffer
// ------------------------------
void strobe_irq(uint gpio, uint32_t events) {
    if (gpio == STROBE_PIN && (events & GPIO_IRQ_EDGE_FALL)) {
        // Get DMA's current write position
        uint32_t write_ptr = ((uint32_t)dma_channel_hw_addr(dma_chan)->write_addr
                             - (uint32_t)spi_ring) & RING_MASK;

        // Check for underrun
        if (read_ptr == write_ptr) {
            underruns++;
            pio_sm_put(pio0, 0, last_sample);
            return;
        }

        uint8_t sample = spi_ring[read_ptr];
        read_ptr = (read_ptr + 1) & RING_MASK;
        last_sample = sample;
        strobe_count++;

        pio_sm_put(pio0, 0, sample);
    }
}

// ------------------------------
// MAIN
// ------------------------------
int main() {
    stdio_init_all();
    sleep_ms(500);

    printf("\nPico Sampler - Ring Buffer Version\n");

    // Pre-fill ring with silence
    for (int i = 0; i < RING_SIZE; i++) {
        spi_ring[i] = 0x80;
    }

    // 245 OE high (disabled)
    gpio_init(OE_PIN);
    gpio_set_dir(OE_PIN, GPIO_OUT);
    gpio_put(OE_PIN, 1);

    // STROBE input
    gpio_init(STROBE_PIN);
    gpio_set_dir(STROBE_PIN, GPIO_IN);
    gpio_set_input_hysteresis_enabled(STROBE_PIN, true);
    gpio_set_irq_enabled_with_callback(
        STROBE_PIN, GPIO_IRQ_EDGE_FALL, true, &strobe_irq
    );

    // ------------------------------
    // PIO SPI SLAVE
    // ------------------------------
    spi_pio = pio1;
    spi_sm = 0;
    uint offset = pio_add_program(spi_pio, &spi_slave_rx_program);

    pio_gpio_init(spi_pio, PIN_MOSI);
    pio_gpio_init(spi_pio, PIN_SCK);
    pio_gpio_init(spi_pio, PIN_CS);

    pio_sm_config c = spi_slave_rx_program_get_default_config(offset);
    sm_config_set_in_pins(&c, PIN_MOSI);
    sm_config_set_jmp_pin(&c, PIN_CS);
    sm_config_set_in_shift(&c, false, true, 8);

    pio_sm_init(spi_pio, spi_sm, offset, &c);

    // ------------------------------
    // DMA SETUP - Ring buffer mode, runs forever
    // ------------------------------
    dma_chan = dma_claim_unused_channel(true);
    dma_channel_config cfg = dma_channel_get_default_config(dma_chan);
    channel_config_set_transfer_data_size(&cfg, DMA_SIZE_8);
    channel_config_set_read_increment(&cfg, false);
    channel_config_set_write_increment(&cfg, true);
    channel_config_set_dreq(&cfg, pio_get_dreq(spi_pio, spi_sm, false));
    channel_config_set_ring(&cfg, true, RING_BITS);  // Wrap write address

    dma_channel_configure(
        dma_chan, &cfg,
        spi_ring,                 // Write to ring buffer
        &spi_pio->rxf[spi_sm],    // Read from PIO RX FIFO
        0xFFFFFFFF,               // Transfer "forever"
        false                     // Don't start yet
    );

    // ------------------------------
    // PIO SAMPLE OUTPUT
    // ------------------------------
    PIO out_pio = pio0;
    uint out_offset = pio_add_program(out_pio, &sampleout_program);
    uint out_sm = pio_claim_unused_sm(out_pio, true);

    for (int i = 0; i < 8; i++)
        pio_gpio_init(out_pio, DATA_BASE + i);

    pio_sm_config out_c = sampleout_program_get_default_config(out_offset);
    sm_config_set_out_pins(&out_c, DATA_BASE, 8);
    pio_sm_set_consecutive_pindirs(out_pio, out_sm, DATA_BASE, 8, true);

    pio_sm_init(out_pio, out_sm, out_offset, &out_c);
    pio_sm_set_enabled(out_pio, out_sm, true);

    // Enable outputs
    gpio_put(OE_PIN, 0);

    // Start everything
    pio_sm_set_enabled(spi_pio, spi_sm, true);
    dma_channel_start(dma_chan);

    printf("Running... (ring buffer version)\n\n");

    // ------------------------------
    // MAIN LOOP - Just stats, nothing timing-critical
    // ------------------------------
    absolute_time_t last_status = get_absolute_time();

    while (1) {
        sleep_ms(100);

        absolute_time_t now = get_absolute_time();
        uint64_t elapsed = absolute_time_diff_us(last_status, now);

        if (elapsed >= 1000000) {
            // Calculate buffer fill level
            uint32_t write_ptr = ((uint32_t)dma_channel_hw_addr(dma_chan)->write_addr
                                 - (uint32_t)spi_ring) & RING_MASK;
            uint32_t fill = (write_ptr - read_ptr) & RING_MASK;

            float strobe_rate = (float)strobe_count * 1000000.0f / elapsed;

            printf("Ring: %lu/%d, STROBE: %.1f Hz, Under: %llu\n",
                   fill, RING_SIZE, strobe_rate, underruns);

            strobe_count = 0;
            underruns = 0;
            last_status = now;
        }
    }
}
