#ifndef DISPLAY_H
#define DISPLAY_H

#include <stddef.h>
#include <stdint.h>

#define DISPLAY_WIDTH   128
#define DISPLAY_HEIGHT  64
#define DISPLAY_PAGES   (DISPLAY_HEIGHT / 8)

/*
 * The 128x64 monochrome OLED, SSD1306 compatible.
 *
 * Its memory is split into 8 pages, each a strip 8 pixels high across the full
 * width. One byte sets one column of 8 pixels within a page: bit 0 is the top
 * pixel, 1 is lit. Page 0, column 0 is the top-left corner.
 *
 * Writes block until sent. A full screen (8 pages of 128 bytes) takes about
 * 0.5 ms. examples/display builds a frame buffer and text on top of this.
 */
void display_init(void);

/*
 * Write len bytes to one page, starting at column. Bytes past the right edge
 * are dropped.
 */
void display_write(int page, int column, const uint8_t *data, size_t len);

/* Brightness, 0-255. display_init() sets 143. */
void display_contrast(uint8_t level);

/* Send raw controller commands, see the SSD1306 datasheet. */
void display_command(const uint8_t *bytes, size_t len);

#endif
