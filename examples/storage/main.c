/*
 * Storage example: each encoder moves a dot around its LED ring, and the dot
 * positions survive switching off. Press Shift to save them; the dots flash
 * green when saved. Save only when the user asks for it, never automatically
 * or on a timer: each flash sector survives a limited number of erases.
 *
 * Uses the last 4 KB sector of the flash. The official firmware also keeps
 * its data in this flash.
 */
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "enc.h"
#include "leds.h"
#include "storage.h"
#include "system.h"

#define LEVEL           40
#define SAVED_FLASH_MS  300
#define MAGIC           0x45313653u

typedef struct {
    uint32_t magic;     /* tells saved data apart from erased or foreign data */
    uint8_t position[ENC_COUNT];
} saved_t;

static uint32_t save_addr;

static void load(saved_t *s)
{
    storage_read(save_addr, s, sizeof(*s));
    if (s->magic != MAGIC) {
        memset(s, 0, sizeof(*s));
        s->magic = MAGIC;
    }
}

static void save(const saved_t *s)
{
    storage_erase(save_addr, sizeof(*s));
    storage_write(save_addr, s, sizeof(*s));
}

int main(void)
{
    system_init();
    leds_init();
    enc_init();

    if (!storage_init()) {
        leds_set_ring(0, LEVEL, 0, 0);
        while (!leds_show()) {
        }
        for (;;) {
        }
    }
    save_addr = storage_size() - STORAGE_SECTOR_SIZE;

    saved_t state;
    load(&state);

    uint32_t saved_at = 0;
    bool unsaved = false;
    bool shift_was_pressed = false;

    for (;;) {
        uint32_t now = system_millis();

        for (int i = 0; i < ENC_COUNT; i++) {
            int steps = enc_read(i);
            if (steps != 0) {
                state.position[i] = (uint8_t)((state.position[i] + steps) & (LEDS_PER_RING - 1));
                unsaved = true;
            }
        }

        bool shift = enc_shift_pressed();
        if (shift && !shift_was_pressed && unsaved) {
            save(&state);
            unsaved = false;
            saved_at = now;
        }
        shift_was_pressed = shift;

        bool flash = saved_at != 0 && now - saved_at < SAVED_FLASH_MS;
        leds_clear();
        for (int i = 0; i < ENC_COUNT; i++) {
            if (flash) {
                leds_set(i, state.position[i], 0, LEVEL, 0);
            } else {
                leds_set(i, state.position[i], 0, LEVEL, LEVEL);
            }
        }
        leds_show();
        system_delay_ms(5);
    }
}
