/*
 * Bluetooth MIDI example: a bridge between Bluetooth and the E16's other MIDI
 * ports. Messages from Bluetooth go out over USB and both TRS outputs;
 * messages from USB and the TRS input go out over Bluetooth. The display
 * counts the messages in each direction.
 *
 * Needs this example's firmware on the Bluetooth chip, see README.md.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ble_midi.h"
#include "display.h"
#include "font.h"
#include "midi.h"
#include "system.h"

static void print(int page, const char *s)
{
    uint8_t row[DISPLAY_WIDTH] = {0};
    for (int i = 0; s[i] && (i + 1) * FONT_WIDTH <= DISPLAY_WIDTH; i++) {
        char c = s[i] >= FONT_FIRST && s[i] <= FONT_LAST ? s[i] : '?';
        memcpy(&row[i * FONT_WIDTH], font[c - FONT_FIRST], FONT_WIDTH);
    }
    display_write(page, 0, row, sizeof(row));
}

int main(void)
{
    system_init();
    display_init();
    midi_init();
    ble_midi_init();

    print(0, "BLUETOOTH MIDI");
    print(1, "E16 CUSTOM");

    uint32_t from_ble = 0, to_ble = 0, shown_from = 1, shown_to = 1;

    for (;;) {
        /*
         * Bluetooth arrives as complete messages; pass the bytes straight on.
         * USB waits for room, TRS drops what doesn't fit: it is slower than
         * Bluetooth.
         */
        uint8_t b;
        while (ble_midi_read_byte(&b)) {
            while (!midi_write(MIDI_USB, &b, 1) && midi_usb_connected()) {
            }
            midi_write(MIDI_TRS_1, &b, 1);
            midi_write(MIDI_TRS_2, &b, 1);
            if (b & 0x80) {
                from_ble++;
            }
        }

        /* Waiting for room here makes USB wait too, so nothing is lost. */
        midi_msg_t msg;
        while (midi_read(&msg)) {
            while (!ble_midi_write(msg.data, msg.len)) {
            }
            to_ble++;
        }

        if (from_ble != shown_from || to_ble != shown_to) {
            char line[32];
            snprintf(line, sizeof(line), "FROM BLE %lu", (unsigned long)from_ble);
            print(3, line);
            snprintf(line, sizeof(line), "TO BLE   %lu", (unsigned long)to_ble);
            print(4, line);
            shown_from = from_ble;
            shown_to = to_ble;
        }
    }
}
