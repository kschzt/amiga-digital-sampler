#include "pico/stdlib.h"

#define DATA_BASE 2
#define DATA_MASK (0xFFu << DATA_BASE)
#define OE_PIN    11   // /OE on 74HCT245 (active low)

int main() {
    stdio_init_all();

    // Data pins as outputs
    gpio_init_mask(DATA_MASK);
    gpio_set_dir_out_masked(DATA_MASK);

    // Enable 245 outputs
    gpio_init(OE_PIN);
    gpio_set_dir(OE_PIN, true);
    gpio_put(OE_PIN, 0);   // /OE low = enabled

    uint8_t v = 1;

    while (true) {
        gpio_put_masked(DATA_MASK, (uint32_t)v << DATA_BASE);
        sleep_ms(200);
        v = (v == 0x80) ? 1 : (v << 1);   // 1,2,4,8,16,32,64,128, repeat
    }
}

