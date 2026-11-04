/*
 * MIDI example: turning an encoder sends a control change on both TRS
 * outputs and USB, and the display lists the messages arriving at the TRS
 * input and over USB.
 *
 * Encoder 1 sends CC 20, encoder 2 CC 21 and so on, on channel 1. With a
 * cable from an output to the input, the E16 shows its own messages.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "display.h"
#include "enc.h"
#include "font.h"
#include "midi.h"
#include "system.h"

#define CC_FIRST        20
#define CHANNEL         0   /* MIDI channel 1 */
#define LINE_CHARS      (DISPLAY_WIDTH / FONT_WIDTH)
#define HISTORY         (DISPLAY_PAGES - 1)

static char history[HISTORY][LINE_CHARS + 1];

/* One line of text per display page: the font's columns are already page bytes. */
static void print(int page, const char *s)
{
    uint8_t row[DISPLAY_WIDTH] = {0};
    for (int i = 0; s[i] && i < LINE_CHARS; i++) {
        char c = s[i] >= FONT_FIRST && s[i] <= FONT_LAST ? s[i] : '?';
        memcpy(&row[i * FONT_WIDTH], font[c - FONT_FIRST], FONT_WIDTH);
    }
    display_write(page, 0, row, sizeof(row));
}

static void add_line(const char *s)
{
    memmove(history[0], history[1], sizeof(history[0]) * (HISTORY - 1));
    snprintf(history[HISTORY - 1], sizeof(history[0]), "%s", s);
    for (int i = 0; i < HISTORY; i++) {
        print(1 + i, history[i]);
    }
}

static void describe(const midi_msg_t *msg, char *out, size_t size)
{
    const char *from = msg->port == MIDI_USB ? "USB" : "TRS";
    if (msg->data[0] == 0xF0) {
        snprintf(out, size, "%s SYSEX %u BYTES", from, (unsigned)msg->len);
        return;
    }
    size_t n = (size_t)snprintf(out, size, "%s ", from);
    for (size_t i = 0; i < msg->len && n < size; i++) {
        n += (size_t)snprintf(out + n, size - n, "%02X ", msg->data[i]);
    }
}

int main(void)
{
    system_init();
    display_init();
    enc_init();
    midi_init();

    print(0, "MIDI IN");

    int value[ENC_COUNT] = {0};

    for (;;) {
        for (int i = 0; i < ENC_COUNT; i++) {
            int steps = enc_read(i);
            if (steps == 0) {
                continue;
            }
            int v = value[i] + steps;
            v = v < 0 ? 0 : v > 127 ? 127 : v;
            if (v != value[i]) {
                value[i] = v;
                const uint8_t cc[] = {0xB0 | CHANNEL, CC_FIRST + i, (uint8_t)v};
                midi_write(MIDI_TRS_1, cc, sizeof(cc));
                midi_write(MIDI_TRS_2, cc, sizeof(cc));
                midi_write(MIDI_USB, cc, sizeof(cc));
            }
        }

        midi_msg_t msg;
        while (midi_read(&msg)) {
            /* Clock and active sensing arrive many times a second; leave them out. */
            if (msg.data[0] == 0xF8 || msg.data[0] == 0xFE) {
                continue;
            }
            char line[LINE_CHARS + 1];
            describe(&msg, line, sizeof(line));
            add_line(line);
        }
    }
}
