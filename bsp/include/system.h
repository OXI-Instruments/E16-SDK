#ifndef SYSTEM_H
#define SYSTEM_H

#include <stdint.h>

/* Clocks, power rails, SysTick and interrupts. Call once, first thing in main(). */
void system_init(void);

/* Milliseconds since system_init(). Wraps after ~49 days. */
uint32_t system_millis(void);

void system_delay_ms(uint32_t ms);

/* Reboot into the OXI bootloader so new firmware can be sent over USB MIDI. */
void system_enter_bootloader(void) __attribute__((noreturn));

/* Cut power (unverified on hardware). */
void system_power_off(void) __attribute__((noreturn));

#endif
