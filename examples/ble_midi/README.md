# Bluetooth MIDI

Makes the E16 a standard Bluetooth LE MIDI device named "E16 Custom". It works
with anything that speaks BLE-MIDI, for example macOS (Audio MIDI Setup >
Bluetooth Configuration), iOS and Android apps, and Windows apps.

The example is a bridge: MIDI from Bluetooth goes out over USB and both TRS
outputs, and MIDI from USB and the TRS input goes out over Bluetooth.

It has two parts, one for each chip:

| Part | Runs on | What it does |
|---|---|---|
| `nrf/` | Bluetooth chip (nRF52832) | BLE-MIDI peripheral; passes MIDI to and from the serial port |
| `main.c`, `ble_midi.c` | Main processor | Starts the Bluetooth firmware and bridges it to USB and TRS |

The two chips exchange plain MIDI bytes over their serial link (115200 baud,
8N1, no flow control), nothing else. What arrives over Bluetooth reaches the
main processor as complete MIDI messages; MIDI bytes sent to the Bluetooth chip
go out over Bluetooth. Use `ble_midi.c` in your own firmware to do something
else with them.

## Building

The Bluetooth firmware needs Nordic's nRF5 SDK 16.0.0 (the version the E16's
SoftDevice, S132 7.0.1, belongs to) and GNU make. Download it once:

```sh
curl -L -o nRF5_SDK_16.0.0.zip \
  https://files.nordicsemi.com/artifactory/nRF5-SDK/external/nRF5_SDK_v16.x.x/nRF5_SDK_16.0.0_98a08e2.zip
mkdir nRF5_SDK_16.0.0 && unzip -q nRF5_SDK_16.0.0.zip -d nRF5_SDK_16.0.0
```

Then tell the build where it is:

```sh
cmake -S . -B build -DNRF5_SDK_ROOT=/path/to/nRF5_SDK_16.0.0
cmake --build build
```

This builds `ble_midi` (the bridge) and `ble_midi_nrf`, an updater firmware
that contains the Bluetooth firmware (see "Bluetooth chip" in the main README).
Without `NRF5_SDK_ROOT`, only `ble_midi` is built.

## Installing

1. Install `ble_midi_nrf` like any firmware. It writes the Bluetooth chip and
   shows "DONE"; press Shift to return to bootloader mode.
2. Install `ble_midi`.

The display shows how many messages went each way.

Installing your own Bluetooth firmware replaces OXI's. To go back, install
OXI's Bluetooth firmware the same way: `e16_add_ble_updater()` also accepts its
`.syx` update file.

## Limits

- Peripheral only: the E16 waits for a computer, phone or tablet to connect.
  It doesn't connect to other Bluetooth MIDI devices by itself.
- No pairing. Anyone in range can connect while the E16 isn't connected.
- The serial link carries about 11 KB/s in each direction, more than
  Bluetooth MIDI usually needs. The TRS outputs are slower (3 KB/s); what
  doesn't fit is dropped there.

`nrf/sdk_config.h` and `nrf/ble_midi_nrf.ld` are based on Nordic's
`ble_app_uart` example and keep Nordic's licence, which allows use with Nordic
chips only.
