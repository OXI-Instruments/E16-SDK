#!/usr/bin/env python3
"""Turn a Bluetooth chip application into the image ble_update() writes.

  e16_ble_image.py app.hex -o image.bin

Accepts the nRF52832 application as Intel HEX (what the nRF5 SDK build
produces), as a raw .bin that starts at 0x26000, or as an OXI .syx Bluetooth
update file. The application must be linked for S132 7.0.1 at 0x26000.
"""

import argparse
import struct
import sys
from pathlib import Path

APP_ADDR = 0x26000
BOOTLOADER_ADDR = 0x64000
MAX_SIZE = BOOTLOADER_ADDR - APP_ADDR
RAM_START = 0x20000000
RAM_END = 0x20010000

SYX_PREFIX = bytes([0xF0, 0x00, 0x21, 0x5B])  # then a 2-byte product ID and 00
SYX_BLOCK = 0x7E


def from_hex(text: str) -> bytes:
    data = {}
    base = 0
    for n, line in enumerate(text.splitlines(), 1):
        line = line.strip()
        if not line:
            continue
        if not line.startswith(":"):
            raise ValueError(f"line {n}: not Intel HEX")
        rec = bytes.fromhex(line[1:])
        if sum(rec) & 0xFF:
            raise ValueError(f"line {n}: bad checksum")
        count, addr, kind = rec[0], (rec[1] << 8) | rec[2], rec[3]
        payload = rec[4:4 + count]
        if kind == 0x00:
            for i, b in enumerate(payload):
                data[base + addr + i] = b
        elif kind == 0x01:
            break
        elif kind == 0x02:
            base = ((payload[0] << 8) | payload[1]) << 4
        elif kind == 0x04:
            base = ((payload[0] << 8) | payload[1]) << 16
    if not data:
        raise ValueError("the file contains no data")
    low, high = min(data), max(data)
    if low < APP_ADDR:
        raise ValueError(
            f"the file has data at 0x{low:05X}, below the application area (0x{APP_ADDR:05X}). "
            "Pass the application's .hex only, not a merged image with the SoftDevice."
        )
    if high >= BOOTLOADER_ADDR:
        raise ValueError(
            f"the file has data at 0x{high:05X}, inside the nRF bootloader (0x{BOOTLOADER_ADDR:05X}). "
            "Pass the application's .hex only, not a merged image with the bootloader."
        )
    image = bytearray(b"\xFF" * (high + 1 - APP_ADDR))
    for addr, b in data.items():
        image[addr - APP_ADDR] = b
    return bytes(image)


def from_syx(data: bytes) -> bytes:
    image = bytearray()
    start = None
    for i, b in enumerate(data):
        if b == 0xF0:
            start = i
        elif b == 0xF7 and start is not None:
            msg = data[start:i + 1]
            start = None
            if not msg.startswith(SYX_PREFIX) or len(msg) < 9 or msg[6] != 0x00:
                raise ValueError("not an OXI Bluetooth update file")
            if msg[7] != SYX_BLOCK:
                continue
            nibbles = msg[8:-1]
            block = bytes((nibbles[j] << 4) | nibbles[j + 1] for j in range(0, len(nibbles) - 1, 2))
            if len(block) != 129 or sum(block[:128]) & 0xFF != block[128]:
                raise ValueError("damaged Bluetooth update file")
            image += block[:128]
    if not image:
        raise ValueError("no firmware data found")
    return bytes(image)


def check(image: bytes) -> None:
    if len(image) > MAX_SIZE:
        raise ValueError(
            f"the application is {len(image)} bytes; the limit is {MAX_SIZE} bytes (248 KB).\n"
            f"Anything larger would overwrite the nRF bootloader at 0x{BOOTLOADER_ADDR:05X}, and the\n"
            "Bluetooth chip could then only be recovered with a debug probe."
        )
    if len(image) < 8:
        raise ValueError("the file is too small to be an application")
    stack, reset = struct.unpack_from("<II", image)
    if not RAM_START < stack <= RAM_END:
        raise ValueError(f"initial stack pointer 0x{stack:08X} is not in the nRF's RAM")
    if not (reset & 1) or not APP_ADDR <= (reset & ~1) < BOOTLOADER_ADDR:
        raise ValueError(
            f"reset handler 0x{reset:08X} is outside the application area; "
            f"the application must be linked at 0x{APP_ADDR:05X} for S132 7.0.1"
        )


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("app")
    parser.add_argument("-o", "--output", required=True)
    args = parser.parse_args()

    source = Path(args.app)
    try:
        raw = source.read_bytes()
        suffix = source.suffix.lower()
        if suffix == ".hex":
            image = from_hex(raw.decode("ascii"))
        elif suffix == ".syx":
            image = from_syx(raw)
        else:
            image = raw
        check(image)
    except (OSError, ValueError, UnicodeDecodeError) as error:
        sys.exit(f"{source}: {error}")

    Path(args.output).write_bytes(image)
    print(f"{source.name}: {len(image)} bytes of {MAX_SIZE} for the Bluetooth chip")


if __name__ == "__main__":
    main()
