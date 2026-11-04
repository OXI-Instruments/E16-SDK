#ifndef LEDS_H
#define LEDS_H

#include <stdbool.h>
#include <stdint.h>

#define LEDS_RINGS          16
#define LEDS_PER_RING       16

/*
 * RGB LED rings, one per encoder. Rings are numbered 0-15 like the encoders,
 * LEDs 0-15 within a ring. Changes are buffered until leds_show().
 *
 * Power: the stock firmware never drives a channel above ~85. All 256 LEDs at
 * full white has not been tested against the E16's supply, so keep levels low.
 */
void leds_init(void);

void leds_set(int ring, int led, uint8_t r, uint8_t g, uint8_t b);
void leds_set_ring(int ring, uint8_t r, uint8_t g, uint8_t b);
void leds_clear(void);

/*
 * Send the buffer to the LEDs in the background (~1.5 ms).
 * Returns false and does nothing if the previous frame is still being sent.
 */
bool leds_show(void);
bool leds_busy(void);

#endif
