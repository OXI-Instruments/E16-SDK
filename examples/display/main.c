/*
 * Display example: a frame buffer with pixels, rectangles and text on top of
 * the SDK's display_write().
 *
 * Shows the value of each encoder in a 4x4 grid. Pressing an encoder
 * highlights its cell, holding Shift inverts the whole screen.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "display.h"
#include "enc.h"
#include "font.h"
#include "system.h"

#define FRAME_MS    20
#define CELL_W      (DISPLAY_WIDTH / 4)
#define CELL_H      13
#define GRID_TOP    (DISPLAY_HEIGHT - 4 * CELL_H)

/* Same layout as the display's memory: frame[page][column], bit 0 at the top. */
static uint8_t frame[DISPLAY_PAGES][DISPLAY_WIDTH];

static void pixel(int x, int y, bool on)
{
    if (x < 0 || x >= DISPLAY_WIDTH || y < 0 || y >= DISPLAY_HEIGHT) {
        return;
    }
    uint8_t bit = (uint8_t)(1u << (y % 8));
    if (on) {
        frame[y / 8][x] |= bit;
    } else {
        frame[y / 8][x] &= (uint8_t)~bit;
    }
}

static void rect(int x, int y, int w, int h, bool on)
{
    for (int i = 0; i < w; i++) {
        for (int j = 0; j < h; j++) {
            pixel(x + i, y + j, on);
        }
    }
}

/* Draws only the lit pixels of each character, in the given colour. */
static void text(int x, int y, const char *s, bool on)
{
    for (; *s; s++, x += FONT_WIDTH) {
        char c = *s;
        if (c < FONT_FIRST || c > FONT_LAST) {
            c = '?';
        }
        for (int col = 0; col < FONT_WIDTH; col++) {
            uint8_t bits = font[c - FONT_FIRST][col];
            for (int row = 0; row < FONT_HEIGHT; row++) {
                if (bits & (1u << row)) {
                    pixel(x + col, y + row, on);
                }
            }
        }
    }
}

static void show(void)
{
    for (int page = 0; page < DISPLAY_PAGES; page++) {
        display_write(page, 0, frame[page], DISPLAY_WIDTH);
    }
}

int main(void)
{
    system_init();
    enc_init();
    display_init();

    int value[ENC_COUNT] = {0};
    bool inverted = false;

    for (;;) {
        for (int i = 0; i < ENC_COUNT; i++) {
            value[i] += enc_read(i);
        }

        /* Inverting is a controller command, so it costs no redrawing. */
        bool shift = enc_shift_pressed();
        if (shift != inverted) {
            const uint8_t cmd = shift ? 0xA7 : 0xA6;
            display_command(&cmd, 1);
            inverted = shift;
        }

        memset(frame, 0, sizeof(frame));
        text(0, 1, "E16 SDK", true);
        rect(0, GRID_TOP - 3, DISPLAY_WIDTH, 1, true);

        for (int i = 0; i < ENC_COUNT; i++) {
            int x = (i % 4) * CELL_W;
            int y = GRID_TOP + (i / 4) * CELL_H;
            bool pressed = enc_pressed(i);
            char label[8];
            snprintf(label, sizeof(label), "%d", value[i]);
            if (pressed) {
                rect(x, y, CELL_W - 1, CELL_H - 1, true);
            }
            text(x + 3, y + 3, label, !pressed);
        }

        show();
        system_delay_ms(FRAME_MS);
    }
}
