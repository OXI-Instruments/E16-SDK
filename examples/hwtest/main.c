/*
 * Hardware test for the SDK drivers. See README.md next to this file for what
 * each step should look like.
 */
#include <stdbool.h>
#include <stdint.h>

#include "enc.h"
#include "leds.h"
#include "system.h"

#define LEVEL               40
#define BOOTLOADER_HOLD_MS  2000

static void show(void)
{
    while (!leds_show()) {
    }
}

static void fill(uint8_t r, uint8_t g, uint8_t b)
{
    for (int ring = 0; ring < LEDS_RINGS; ring++) {
        leds_set_ring(ring, r, g, b);
    }
    show();
}

static void self_test(void)
{
    fill(LEVEL, 0, 0);
    system_delay_ms(500);
    fill(0, LEVEL, 0);
    system_delay_ms(500);
    fill(0, 0, LEVEL);
    system_delay_ms(500);

    leds_clear();
    for (int ring = 0; ring < LEDS_RINGS; ring++) {
        leds_set(ring, 0, LEVEL, LEVEL, LEVEL);
        show();
        system_delay_ms(150);
    }
    system_delay_ms(500);
}

int main(void)
{
    system_init();
    leds_init();
    enc_init();

    self_test();

    int position[ENC_COUNT] = {0};
    uint32_t combo_since = 0;

    for (;;) {
        uint32_t now = system_millis();
        bool shift = enc_shift_pressed();
        bool blink_on = (now / 500) % 2 == 0;

        if (shift && enc_pressed(0)) {
            if (combo_since == 0) {
                combo_since = now;
            } else if (now - combo_since >= BOOTLOADER_HOLD_MS) {
                fill(LEVEL, 0, 0);
                system_delay_ms(300);
                system_enter_bootloader();
            }
        } else {
            combo_since = 0;
        }

        leds_clear();
        for (int i = 0; i < ENC_COUNT; i++) {
            position[i] = (position[i] + enc_read(i)) & (LEDS_PER_RING - 1);

            if (enc_pressed(i)) {
                leds_set_ring(i, LEVEL / 4, LEVEL / 4, LEVEL / 4);
            }
            if (shift) {
                if (blink_on) {
                    leds_set(i, position[i], LEVEL, LEVEL / 2, 0);
                }
            } else {
                leds_set(i, position[i], 0, LEVEL, LEVEL);
            }
        }
        if (combo_since != 0) {
            int lit = (int)((now - combo_since) * LEDS_PER_RING / BOOTLOADER_HOLD_MS);
            for (int led = 0; led < lit && led < LEDS_PER_RING; led++) {
                leds_set(0, led, LEVEL, 0, 0);
            }
        }
        leds_show();
        system_delay_ms(5);
    }
}
