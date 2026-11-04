#ifndef BLE_MIDI_H
#define BLE_MIDI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Driver for this example's Bluetooth firmware (nrf/). That firmware passes
 * MIDI as plain bytes over UART5 at 115200 baud, so this is a byte pipe:
 * whatever is written goes out over Bluetooth, and complete MIDI messages
 * received over Bluetooth can be read here. Without a Bluetooth connection,
 * written bytes are dropped by the Bluetooth chip.
 */
void ble_midi_init(void);

/* Queue bytes to send; all or nothing, false if the 256-byte buffer is too full. */
bool ble_midi_write(const uint8_t *bytes, size_t len);

bool ble_midi_read_byte(uint8_t *byte);

#endif
