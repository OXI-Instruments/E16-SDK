# Notes for AI coding agents

This SDK runs firmware on real hardware. Follow these rules in any code
you write for the E16.

## Never

- Write to or erase the processor's internal flash (HAL_FLASH_Program,
  HAL_FLASHEx_Erase, FLASH->...). It holds the bootloader; overwriting it
  leaves an E16 that can only be repaired with a debug probe.
- Install Bluetooth firmware any other way than with e16_add_ble_updater
  or ble_update(). The Bluetooth chip's bootloader overwrites itself if the
  firmware is too big; the SDK's tools check the size.

## External flash (storage.h)

- The external flash holds the user's data from the official firmware
  (settings, presets, scripts). Writing to it can overwrite that data.
  Before you write code that uses storage.h, tell the user explicitly that
  their firmware will write to this flash and may overwrite their data from
  the official firmware.
- Write only when the user asks for it, e.g. with a save button or a menu
  entry. No automatic saving: not on a timer, not a few seconds after the
  last change, not on every edit. Each sector survives about 100,000
  erases.
- Never put a USB drive (examples/usb_disk) on the flash: the computer
  writes to a mounted drive on its own.

## Building

- `make` builds everything; output lands in build/examples/.
- Don't change external/ (git submodules) or the TinyUSB version.
