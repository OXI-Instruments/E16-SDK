#ifndef MIDI_H
#define MIDI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Ports: write to the outputs and USB, read from the TRS input and USB. */
typedef enum {
    MIDI_TRS_1,     /* TRS output 1 */
    MIDI_TRS_2,     /* TRS output 2 */
    MIDI_TRS_IN,    /* TRS input */
    MIDI_USB,       /* USB MIDI, both directions */
    MIDI_PORTS,
} midi_port_t;

/* Longest SysEx message midi_read() returns, F0 and F7 included. */
#define MIDI_SYSEX_MAX  256

/*
 * TRS MIDI (31250 baud) and USB MIDI. Bytes are sent and received in the
 * background, so nothing is lost while the main loop is busy, as long as it
 * reads before the TRS input's 256-byte buffer fills, about 80 ms. Over USB
 * the computer waits instead.
 *
 * Over USB the E16 appears as a USB MIDI device named "E16 Custom".
 */
void midi_init(void);

/*
 * Queue bytes for an output. Either all of them are queued or, if there isn't
 * room, none and it returns false. Also false for USB while no computer is
 * connected. Each output buffers 256 bytes.
 */
bool midi_write(midi_port_t port, const uint8_t *bytes, size_t len);

/* Received bytes, unprocessed. For a port, use either this or midi_read(), not both. */
bool midi_read_byte(midi_port_t port, uint8_t *byte);

typedef struct {
    midi_port_t port;       /* where it arrived */
    const uint8_t *data;    /* status byte first; valid until the next midi_read() */
    size_t len;             /* 1-3, or the length of a SysEx message */
} midi_msg_t;

/*
 * Next complete message from any input, with running status filled in.
 * Real-time messages (clock, start, stop, ...) are returned as soon as they
 * arrive, even in the middle of another message. SysEx longer than
 * MIDI_SYSEX_MAX is dropped.
 */
bool midi_read(midi_msg_t *msg);

/* True while a computer has the E16 connected as a USB MIDI device. */
bool midi_usb_connected(void);

#endif
