/*
 * Bluetooth updater, built by e16_add_ble_updater(): installs the application
 * linked in as ble_image on the Bluetooth chip, shows the result, and on a
 * press of Shift restarts the E16 in bootloader mode for the next firmware.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ble_update.h"
#include "display.h"
#include "enc.h"
#include "font.h"
#include "leds.h"
#include "system.h"

#define LEVEL   30

extern const uint8_t ble_image[];
extern const uint8_t ble_image_end[];

/* For a debug probe: state 1 = updating, 2 = finished; result is a ble_update_result_t. */
volatile struct {
    uint32_t state;
    uint32_t result;
    uint32_t done;
    uint32_t total;
    uint32_t ms;
} updater;

static void print(int page, const char *s)
{
    uint8_t row[DISPLAY_WIDTH] = {0};
    for (int i = 0; s[i] && (i + 1) * FONT_WIDTH <= DISPLAY_WIDTH; i++) {
        char c = s[i] >= FONT_FIRST && s[i] <= FONT_LAST ? s[i] : '?';
        memcpy(&row[i * FONT_WIDTH], font[c - FONT_FIRST], FONT_WIDTH);
    }
    display_write(page, 0, row, sizeof(row));
}

static void fill_rings(int rings, uint8_t r, uint8_t g, uint8_t b)
{
    leds_clear();
    for (int ring = 0; ring < rings; ring++) {
        leds_set_ring(ring, r, g, b);
    }
    while (!leds_show()) {
    }
}

static void on_progress(size_t done, size_t total)
{
    updater.done = done;

    uint8_t bar[DISPLAY_WIDTH];
    int filled = (int)(done * DISPLAY_WIDTH / total);
    for (int x = 0; x < DISPLAY_WIDTH; x++) {
        bar[x] = x < filled ? 0x7E : (x == 0 || x == DISPLAY_WIDTH - 1) ? 0x7E : 0x42;
    }
    display_write(3, 0, bar, sizeof(bar));

    if (!leds_busy()) {
        fill_rings((int)(done * LEDS_RINGS / total), 0, 0, LEVEL);
    }
}

int main(void)
{
    system_init();
    display_init();
    leds_init();
    enc_init();

    size_t len = (size_t)(ble_image_end - ble_image);
    char text[32];

    updater.state = 1;
    updater.total = len;
    print(0, "BLUETOOTH UPDATE");
    snprintf(text, sizeof(text), "%u OF %u KB", (unsigned)((len + 1023) / 1024),
             (unsigned)(BLE_APP_MAX_SIZE / 1024));
    print(1, text);

    uint32_t start = system_millis();
    ble_update_result_t result = ble_update(ble_image, len, on_progress);
    updater.ms = system_millis() - start;
    updater.result = result;

    switch (result) {
    case BLE_UPDATE_OK:
        print(5, "DONE");
        fill_rings(LEDS_RINGS, 0, LEVEL, 0);
        break;
    case BLE_UPDATE_TOO_LARGE:
        print(5, "IMAGE TOO LARGE");
        fill_rings(LEDS_RINGS, LEVEL, 0, 0);
        break;
    case BLE_UPDATE_NO_BOOTLOADER:
        print(5, "NO ANSWER FROM BLE CHIP");
        fill_rings(LEDS_RINGS, LEVEL, 0, 0);
        break;
    default:
        print(5, "FAILED, TRY AGAIN");
        fill_rings(LEDS_RINGS, LEVEL, 0, 0);
        break;
    }
    print(7, "PRESS SHIFT TO CONTINUE");
    updater.state = 2;

    while (!enc_shift_pressed()) {
    }
    system_enter_bootloader();
}
