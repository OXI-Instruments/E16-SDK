# E16 SDK

## Disclaimer

This SDK is provided as is, without warranty of any kind. Firmware built with
it has full control of the E16's hardware. OXI Instruments does not provide
support for custom firmware and is not liable for damage it causes. You can
always go back to the official firmware through bootloader mode.

- **Writing to the processor's internal flash is strongly discouraged.** It
  holds the bootloader, and a firmware that overwrites it leaves an E16 that
  can only be repaired with a debug probe.
- **Excessive writing to the external flash (`storage.h`) can damage it,
  and it holds your data from the official firmware.** Only write when the
  user asks for it, e.g. by saving, never automatically or on a timer. The
  SDK refuses erases that come too often.
- **Only install Bluetooth firmware with the SDK's own tools.** The Bluetooth
  chip's bootloader overwrites itself if the firmware it receives is too big,
  and the SDK's tools refuse such files.

If you use an AI coding assistant, it finds these rules in
[AGENTS.md](AGENTS.md).

Write your own firmware for the [OXI E16](https://oxiinstruments.com) MIDI controller.

The SDK gives you small C drivers for the E16's hardware, a build system, and
startup code that works with the E16's built-in bootloader, so custom firmware
can be installed over USB like an official update and swapped back at any time.

## Requirements

- [Arm GNU Toolchain](https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads) (`arm-none-eabi-gcc` on your `PATH`, or set `ARM_TOOLCHAIN_DIR`)
- CMake 3.20+
- Git
- Python 3, plus `pip install -r tools/requirements.txt` for uploading

## Build

```sh
git clone --recursive https://github.com/OXI-Instruments/E16-SDK.git
cd E16-SDK
make
```

Output lands in `build/examples/`: `rainbow.elf`, `rainbow.bin` (the raw
image), `rainbow.syx` (the update file) and `rainbow.hex` (for SWD probes).

The submodules are pinned to the versions the SDK is tested with. Without git
(for example a ZIP download), clone them into `external/` at these versions:

| Folder | Repository | Version |
|---|---|---|
| `external/tinyusb` | hathach/tinyusb | tag `0.21.0` |
| `external/cmsis_core` | STMicroelectronics/cmsis_core | tag `v5.9.0_20250520` |
| `external/cmsis_device_f4` | STMicroelectronics/cmsis_device_f4 | tag `v2.6.9` |
| `external/stm32f4xx_hal_driver` | STMicroelectronics/stm32f4xx_hal_driver | tag `v1.8.5` |

Other TinyUSB versions are refused at configure time: the GD32 patch applies to
newer ones with offsets, but the result hangs during USB start-up.

## Your own firmware

```c
#include "leds.h"
#include "system.h"

int main(void)
{
    system_init();
    leds_init();

    leds_set_ring(0, 0, 0, 40);
    leds_show();

    for (;;) {
    }
}
```

In your `CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.20)
set(CMAKE_TOOLCHAIN_FILE path/to/E16-SDK/cmake/arm-none-eabi.cmake)
project(my_firmware C CXX ASM)

add_subdirectory(path/to/E16-SDK e16_sdk)
e16_add_firmware(my_firmware main.c)
```

Every firmware shows up to the host (e.g. in a MIDI port picker) as "E16
Custom" unless you give it its own USB product name:

```cmake
e16_add_firmware(my_firmware main.c USB_NAME "My Firmware")
```

macOS keeps showing the name it first saw for the E16's USB ID in Audio MIDI
Setup; remove the device there to see a new one.

### USB functions of your own

Next to MIDI, a firmware can add USB functions of its own, e.g. a drive, an
audio input or a webcam (`bsp/include/usb.h`). Three examples by Charles
Vestal show how, each with its function in its own files to copy into your
firmware:

| Example | The E16 shows up as | Function |
|---|---|---|
| `usb_disk` | a 32 KB USB drive | `usb_msc.c` |
| `usb_tone` | an audio input playing 440 Hz | `usb_audio.c` |
| `usb_cam` | a webcam showing a video synth | `usb_video.c` |

They aren't part of the SDK, so a firmware that doesn't use them pays
nothing for them.

## Installing firmware

1. Put the E16 in bootloader mode: with it switched off, hold **encoders 15
   and 16**, then press **Shift** to power on. It appears as the USB MIDI
   device "OXI E16 FW Update".
2. Upload:

   ```sh
   cmake --build build --target rainbow-upload
   # or directly:
   python3 tools/e16_upload.py upload build/examples/rainbow.bin
   ```

The E16 restarts with the new firmware when the upload finishes.

The bootloader's button combo works whatever firmware is installed, even a
broken one. To go back to the official firmware, install the official update
file the same way.

`tools/e16_upload.py syx` builds a `.syx` file without uploading it, e.g. to
share a release. Send it with `tools/e16_upload.py upload` too: general SysEx
tools don't wait for the E16 to confirm each page. Run
`tools/e16_upload.py --help` for all options.

## Bluetooth chip

Bluetooth runs on a separate chip, a Nordic nRF52832, connected to the main
processor by a serial port (57600 baud) and two control lines. Its flash holds:

| Address | Contents | Replaceable |
|---|---|---|
| `0x00000` | Nordic MBR and S132 7.0.1 SoftDevice | no |
| `0x26000` | Bluetooth application | yes |
| `0x64000` | OXI's bootloader | no |

Your own Bluetooth application must be built with the nRF5 SDK 16.0.0 for
S132 7.0.1 and linked at `0x26000`. The SoftDevice can't be changed: the
bootloader only writes the application.

The chip is only reachable through the main processor, so the SDK installs
Bluetooth applications with a small updater firmware. In your CMakeLists.txt:

```cmake
e16_add_ble_updater(ble_update path/to/your_app.hex)
```

This builds `ble_update`, which contains your application (`.hex`, `.bin`
from `0x26000`, or an OXI Bluetooth `.syx` update file). Install it like any
firmware. It writes the Bluetooth chip, shows the result, and on a press of
Shift restarts in bootloader mode so you can install your main firmware again.
Your own firmware can also call `ble_update()` directly.

> [!WARNING]
> **The Bluetooth chip's bootloader does not protect itself.** It writes the
> application wherever the data takes it. An application larger than 248 KB
> (`0x26000` up to `0x64000`) would overwrite the bootloader, and the chip
> could then only be repaired with a debug probe on the nRF52832 itself.
> The SDK's tools refuse such files. Don't install Bluetooth firmware any
> other way.

If an update is interrupted, the Bluetooth chip has no working application,
but its bootloader is intact: run the updater again.

## Hardware

| Part | Details |
|---|---|
| MCU | GigaDevice GD32F405RG (STM32F405 compatible), Cortex-M4F, 168 MHz |
| Memory | 496 KB flash for your app, 128 KB RAM, 64 KB CCM RAM |
| Controls | 16 push encoders, Shift button |
| LEDs | 16 rings of 16 RGB LEDs (WS2812) |
| Display | 128×64 monochrome OLED, SSD1306 compatible |
| MIDI | USB, 1 TRS in, 2 TRS out, Bluetooth (nRF52832) |
| Storage | 16 MB SPI NOR flash, shared with the official firmware's data |

The full pin map is in [`bsp/include/board.h`](bsp/include/board.h).

The E16 uses a GD32F405, which runs ST's STM32F4 HAL unchanged with two
differences the SDK already handles: the first 512 KB of flash runs with zero
wait states, and the USARTs can invert their TX pin (used for the LEDs and
MIDI outputs).

## Layout

```
bsp/include/    driver headers (system.h, leds.h, enc.h, display.h, midi.h,
                storage.h, usb.h, ble_update.h, board.h)
bsp/src/        driver implementations
bsp/platform/   linker script, HAL and TinyUSB configuration, TinyUSB patch for the GD32
cmake/          toolchain file and e16_add_firmware()
examples/       example firmware
tools/          e16_upload.py, e16_ble_image.py, the Bluetooth updater
external/       ST CMSIS and HAL, TinyUSB (git submodules)
```

## License

The SDK is released under the [MIT License](LICENSE), except for the
third-party code below.

## Third-party code

`external/` contains STMicroelectronics' CMSIS and STM32F4 HAL and the
[TinyUSB](https://github.com/hathach/tinyusb) USB stack, fetched as git
submodules under their own licenses (Apache-2.0 / BSD-3-Clause / MIT).
`bsp/platform/stm32f4xx_hal_conf.h` is derived from ST's template.
`bsp/platform/tinyusb-gd32.patch` adapts TinyUSB's USB driver to the GD32's
USB core; the build applies it to copies, so the submodule stays unchanged.
`examples/ble_midi/nrf/sdk_config.h` and `ble_midi_nrf.ld` are based on
Nordic's nRF5 SDK and keep Nordic's licence (use with Nordic chips only); the
SDK itself is downloaded separately, see `examples/ble_midi/README.md`.

## Thanks

Thanks to [Charles Vestal](https://github.com/charlesvestal) for initial
testing and the examples he provided.
