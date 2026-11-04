#ifndef ENC_H
#define ENC_H

#include <stdbool.h>

#define ENC_COUNT 16

/*
 * The 16 push encoders and the Shift button. Encoders are numbered 0-15 like
 * the LED rings. They are scanned every 2 ms in the background once enc_init()
 * has been called; buttons are debounced over 16 ms.
 */
void enc_init(void);

/* Steps turned since the last call for this encoder; positive is clockwise. */
int enc_read(int index);

bool enc_pressed(int index);
bool enc_shift_pressed(void);

#endif
