#include <stdint.h>

#include "leds.h"
#include "system.h"

static void wheel(uint8_t pos, uint8_t *r, uint8_t *g, uint8_t *b)
{
    const uint8_t level = 40;
    uint8_t third = pos / 86;
    uint8_t step = (uint8_t)((pos % 86) * level / 85);
    switch (third) {
    case 0:  *r = level - step; *g = step;         *b = 0;            break;
    case 1:  *r = 0;            *g = level - step; *b = step;         break;
    default: *r = step;         *g = 0;            *b = level - step; break;
    }
}

int main(void)
{
    system_init();
    leds_init();

    uint8_t offset = 0;
    for (;;) {
        for (int ring = 0; ring < LEDS_RINGS; ring++) {
            for (int led = 0; led < LEDS_PER_RING; led++) {
                uint8_t r, g, b;
                wheel((uint8_t)(offset + ring * 16 + led), &r, &g, &b);
                leds_set(ring, led, r, g, b);
            }
        }
        while (!leds_show()) {
        }
        offset++;
        system_delay_ms(10);
    }
}
