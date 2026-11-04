#ifndef MIDI_RING_H
#define MIDI_RING_H

/* Internal to the SDK: the byte buffers between midi.c and the port drivers. */

#include <stdbool.h>
#include <stdint.h>

#define MIDI_RING_SIZE  256     /* a power of two */

/*
 * One writer and one reader, each in its own context. head and tail count
 * bytes written and read and wrap around on their own; head - tail is the
 * fill level, masked to index the data.
 */
typedef struct {
    uint8_t data[MIDI_RING_SIZE];
    volatile uint32_t head;
    volatile uint32_t tail;
} midi_ring_t;

static inline uint32_t midi_ring_count(const midi_ring_t *r)
{
    return r->head - r->tail;
}

static inline bool midi_ring_put(midi_ring_t *r, uint8_t b)
{
    uint32_t head = r->head;
    if (head - r->tail >= MIDI_RING_SIZE) {
        return false;
    }
    r->data[head & (MIDI_RING_SIZE - 1)] = b;
    r->head = head + 1;
    return true;
}

static inline bool midi_ring_get(midi_ring_t *r, uint8_t *b)
{
    uint32_t tail = r->tail;
    if (tail == r->head) {
        return false;
    }
    *b = r->data[tail & (MIDI_RING_SIZE - 1)];
    r->tail = tail + 1;
    return true;
}

/* usb.c */
void usb_midi_init(midi_ring_t *tx, midi_ring_t *rx);
bool usb_midi_connected(void);

#endif
