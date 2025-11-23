#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/util/queue.h"  // Add this
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
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

// Use hardware queue instead of manual ring buffer
#define AUDIO_QUEUE_SIZE 8192
static queue_t audio_queue;
static uint8_t last_sample = 0x80;

// SPI buffers
#define SPI_BUF_SIZE 1024
static uint8_t spi_buf_a[SPI_BUF_SIZE] __attribute__((aligned(8)));
static uint8_t spi_buf_b[SPI_BUF_SIZE] __attribute__((aligned(8)));

static uint dma_chan_a, dma_chan_b;
static PIO spi_pio;
static uint spi_sm;

// Stats
static volatile uint64_t strobe_count = 0;
static volatile uint64_t underruns = 0;
static volatile uint64_t spi_bytes_received = 0;
static volatile uint64_t buffers_processed = 0;
static volatile uint32_t max_buffer_fill = 0;
static absolute_time_t start_time;

// ------------------------------
// DMA IRQ - Process data using queue
// ------------------------------
void dma_irq_handler() {
    if (dma_channel_get_irq0_status(dma_chan_a)) {
        dma_channel_acknowledge_irq0(dma_chan_a);

        // Process buffer A using queue
        for (int i = 0; i < SPI_BUF_SIZE; i++) {
            // Try to add to queue, drop if full
            if (!queue_try_add(&audio_queue, &spi_buf_a[i])) {
                // Queue full - this is an overrun condition
                // Could count these separately if desired
            }
        }
        spi_bytes_received += SPI_BUF_SIZE;
        buffers_processed++;

        // Start DMA to buffer B
        dma_channel_set_write_addr(dma_chan_b, spi_buf_b, true);
    }
    else if (dma_channel_get_irq0_status(dma_chan_b)) {
        dma_channel_acknowledge_irq0(dma_chan_b);

        // Process buffer B using queue
        for (int i = 0; i < SPI_BUF_SIZE; i++) {
            if (!queue_try_add(&audio_queue, &spi_buf_b[i])) {
                // Queue full - overrun
            }
        }
        spi_bytes_received += SPI_BUF_SIZE;
        buffers_processed++;

        // Start DMA to buffer A
        dma_channel_set_write_addr(dma_chan_a, spi_buf_a, true);
    }
}

// ------------------------------
// STROBE IRQ - now thread-safe!
// ------------------------------
void strobe_irq(uint gpio, uint32_t events) {
    static uint64_t last_time = 0;
    uint64_t now = time_us_64();
    
    // Debounce
    if (now - last_time < 10) return;
    
    if (gpio == STROBE_PIN && (events & GPIO_IRQ_EDGE_FALL)) {  // Change to RISE for 74LVX14
        last_time = now;
        strobe_count++;
        
        uint8_t sample;
        // Thread-safe queue operation
        if (!queue_try_remove(&audio_queue, &sample)) {
            // Queue empty - underrun
            underruns++;
            sample = last_sample;  // Use last sample
        } else {
            last_sample = sample;  // Update last sample
        }
        
        pio_sm_put(pio0, 0, sample);
    }
}

// ------------------------------
// MAIN
// ------------------------------
int main() {
    stdio_init_all();
    sleep_ms(500);

    printf("\nPico Sampler - Queue Version\n");

    start_time = get_absolute_time();

    // Initialize the thread-safe queue
    queue_init(&audio_queue, sizeof(uint8_t), AUDIO_QUEUE_SIZE);

    // 245 OE high (disabled)
    gpio_init(OE_PIN);
    gpio_set_dir(OE_PIN, GPIO_OUT);
    gpio_put(OE_PIN, 1);

    // STROBE input
    gpio_init(STROBE_PIN);
    gpio_set_dir(STROBE_PIN, GPIO_IN);
    gpio_set_input_hysteresis_enabled(STROBE_PIN, true);
    // No pull-up since using 74LVX14
    gpio_set_irq_enabled_with_callback(
        STROBE_PIN, GPIO_IRQ_EDGE_FALL, true, &strobe_irq
    );

    // ------------------------------
    // PIO SPI SLAVE (unchanged)
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
    // DMA SETUP (unchanged)
    // ------------------------------
    dma_chan_a = dma_claim_unused_channel(true);
    dma_channel_config cfg_a = dma_channel_get_default_config(dma_chan_a);
    channel_config_set_transfer_data_size(&cfg_a, DMA_SIZE_8);
    channel_config_set_read_increment(&cfg_a, false);
    channel_config_set_write_increment(&cfg_a, true);
    channel_config_set_dreq(&cfg_a, pio_get_dreq(spi_pio, spi_sm, false));

    dma_channel_configure(
        dma_chan_a, &cfg_a,
        spi_buf_a,
        &spi_pio->rxf[spi_sm],
        SPI_BUF_SIZE,
        false
    );

    dma_chan_b = dma_claim_unused_channel(true);
    dma_channel_config cfg_b = dma_channel_get_default_config(dma_chan_b);
    channel_config_set_transfer_data_size(&cfg_b, DMA_SIZE_8);
    channel_config_set_read_increment(&cfg_b, false);
    channel_config_set_write_increment(&cfg_b, true);
    channel_config_set_dreq(&cfg_b, pio_get_dreq(spi_pio, spi_sm, false));

    dma_channel_configure(
        dma_chan_b, &cfg_b,
        spi_buf_b,
        &spi_pio->rxf[spi_sm],
        SPI_BUF_SIZE,
        false
    );

    dma_channel_set_irq0_enabled(dma_chan_a, true);
    dma_channel_set_irq0_enabled(dma_chan_b, true);
    irq_set_exclusive_handler(DMA_IRQ_0, dma_irq_handler);
    irq_set_enabled(DMA_IRQ_0, true);

    // ------------------------------
    // PIO SAMPLE OUTPUT (unchanged)
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
    dma_channel_start(dma_chan_a);

    printf("Running... (queue-based version)\n\n");

    // ------------------------------
    // MAIN LOOP - Status display
    // ------------------------------
    uint32_t status_counter = 0;
    absolute_time_t last_status = get_absolute_time();

    while (1) {
        if (++status_counter >= 1000000) {
            status_counter = 0;
            absolute_time_t now = get_absolute_time();
            uint64_t elapsed = absolute_time_diff_us(last_status, now);

            if (elapsed >= 1000000) {  // 1 second
                // Get current queue level
                uint32_t queue_level = queue_get_level(&audio_queue);
                if (queue_level > max_buffer_fill) {
                    max_buffer_fill = queue_level;
                }
                
                float strobe_rate = (float)strobe_count * 1000000.0f / elapsed;
                float spi_rate = (float)spi_bytes_received * 1000000.0f / elapsed;
                
                printf("Audio: %d/%d (max:%d), STROBE: %.1f Hz, SPI: %.1f B/s, Bufs: %llu, Under: %llu\n",
                       queue_level, AUDIO_QUEUE_SIZE, max_buffer_fill,
                       strobe_rate, spi_rate, buffers_processed, underruns);

                strobe_count = 0;
                underruns = 0;
                spi_bytes_received = 0;
                buffers_processed = 0;
                last_status = now;
            }
        }

        __asm volatile ("nop");
    }
}
