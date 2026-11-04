#!/usr/bin/env python3
"""Package and upload E16 firmware over USB MIDI.

  e16_upload.py syx firmware.bin [-o firmware.syx]   build a .syx update file
  e16_upload.py upload firmware.bin|firmware.syx     send it to the E16
  e16_upload.py ports                                list MIDI ports

Uploading needs python-rtmidi (pip install -r tools/requirements.txt) and the
E16 in bootloader mode: switch it off, hold encoders 15 and 16, press Shift.
"""

import argparse
import queue
import struct
import sys
import time
from pathlib import Path

HEADER = bytes([0xF0, 0x00, 0x21, 0x5B, 0x02, 0x01])
CMD_PAGE = bytes([0x00, 0x7E])
CMD_RESET = bytes([0x00, 0x7F])
END = 0xF7

RESTART = HEADER + bytes([0x00, 0x55, END])
EXIT = HEADER + bytes([0x00, 0x52, END])
ACK = HEADER + bytes([0x00, 0x53, END])
NACK = HEADER + bytes([0x00, 0x54, END])

PAGE_SIZE = 2048
APP_BASE = 0x08004000
APP_MAX_SIZE = 512 * 1024 - 16 * 1024
RAM_START = 0x20000000
RAM_END = 0x20020000

BOOTLOADER_PORT = "E16 FW Update"
SETTLE_S = 0.2
ACK_TIMEOUT_S = 4.0


def check_firmware(image: bytes) -> None:
    if len(image) < 8:
        raise ValueError("file is too small to be firmware")
    if len(image) > APP_MAX_SIZE:
        raise ValueError(f"firmware is {len(image)} bytes, the limit is {APP_MAX_SIZE}")
    stack, reset = struct.unpack_from("<II", image)
    if not RAM_START < stack <= RAM_END:
        raise ValueError(f"initial stack pointer 0x{stack:08X} is not in RAM; is this an E16 .bin?")
    if not (reset & 1) or not APP_BASE <= (reset & ~1) < APP_BASE + APP_MAX_SIZE:
        raise ValueError(
            f"reset handler 0x{reset:08X} is outside the app area; "
            f"the firmware must be linked at 0x{APP_BASE:08X} (use e16_add_firmware)"
        )


def nibbles(data: bytes) -> bytes:
    out = bytearray()
    for byte in data:
        out.append(byte >> 4)
        out.append(byte & 0x0F)
    return bytes(out)


def build_messages(image: bytes) -> list:
    check_firmware(image)
    messages = []
    for offset in range(0, len(image), PAGE_SIZE):
        page = image[offset:offset + PAGE_SIZE].ljust(PAGE_SIZE, b"\x00")
        checksum = sum(page) & 0xFF
        messages.append(HEADER + CMD_PAGE + nibbles(page + bytes([checksum])) + bytes([END]))
    messages.append(HEADER + CMD_RESET + bytes([END]))
    return messages


def split_syx(data: bytes) -> list:
    messages = []
    start = None
    for i, byte in enumerate(data):
        if byte == 0xF0:
            start = i
        elif byte == END and start is not None:
            messages.append(data[start:i + 1])
            start = None
    if not messages:
        raise ValueError("no SysEx messages found")
    return messages


def load_messages(path: Path) -> list:
    data = path.read_bytes()
    if path.suffix.lower() == ".syx":
        return split_syx(data)
    return build_messages(data)


def cmd_syx(args) -> None:
    source = Path(args.firmware)
    output = Path(args.output) if args.output else source.with_suffix(".syx")
    messages = build_messages(source.read_bytes())
    output.write_bytes(b"".join(messages))
    print(f"{output}: {len(messages) - 1} pages")


def import_rtmidi():
    try:
        import rtmidi
    except ImportError:
        sys.exit("python-rtmidi is missing: pip install -r tools/requirements.txt")
    return rtmidi


def cmd_ports(_args) -> None:
    rtmidi = import_rtmidi()
    for kind, midi in (("out", rtmidi.MidiOut()), ("in", rtmidi.MidiIn())):
        for i, name in enumerate(midi.get_ports()):
            print(f"{kind} {i}: {name}")


def open_port(midi, wanted: str, kind: str) -> str:
    ports = midi.get_ports()
    matches = [i for i, name in enumerate(ports) if wanted.lower() in name.lower()]
    if not matches:
        raise LookupError(f'no MIDI {kind} matching "{wanted}"')
    midi.open_port(matches[0])
    return ports[matches[0]]


class UploadError(Exception):
    pass


def cmd_upload(args) -> None:
    messages = load_messages(Path(args.firmware))
    pages, final = messages[:-1], messages[-1]
    rtmidi = import_rtmidi()
    midi_out = rtmidi.MidiOut()
    midi_in = rtmidi.MidiIn()
    midi_in.ignore_types(sysex=False)
    replies = queue.Queue()
    midi_in.set_callback(lambda event, _data: replies.put(bytes(event[0])))

    wanted = args.port or BOOTLOADER_PORT
    try:
        name = open_port(midi_out, wanted, "output")
        open_port(midi_in, wanted, "input")
    except LookupError as error:
        hint = "" if args.port else (
            "\nPut the E16 in bootloader mode: switch it off, hold encoders 15 and 16, press Shift."
        )
        sys.exit(f"{error}.{hint}")
    print(f"Uploading to {name}")

    def send(message: bytes) -> None:
        midi_out.send_message(list(message))

    def wait_for_ack(page: int) -> None:
        deadline = time.monotonic() + ACK_TIMEOUT_S
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise UploadError(f"no answer to page {page} within {ACK_TIMEOUT_S:.0f} s")
            try:
                reply = replies.get(timeout=remaining)
            except queue.Empty:
                continue
            if reply == ACK:
                return
            if reply == NACK:
                raise UploadError(f"the bootloader refused page {page}")

    try:
        time.sleep(SETTLE_S)
        # Restart the page counter, in case an earlier upload stopped part-way.
        send(RESTART)
        time.sleep(SETTLE_S)
        while not replies.empty():
            replies.get_nowait()

        for n, page in enumerate(pages, 1):
            send(page)
            wait_for_ack(n)
            print(f"\rpage {n}/{len(pages)}", end="", flush=True)

        send(final)
        send(EXIT)
    except UploadError as error:
        send(EXIT)
        sys.exit(f"\nerror: {error}. Put the E16 back in bootloader mode and run the upload again.")
    finally:
        midi_in.close_port()
        midi_out.close_port()
    print("\nDone. The E16 restarts with the new firmware.")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)

    p = sub.add_parser("syx", help="build a .syx update file from a .bin")
    p.add_argument("firmware")
    p.add_argument("-o", "--output")
    p.set_defaults(func=cmd_syx)

    p = sub.add_parser("upload", help="send a .bin or .syx to the E16 bootloader")
    p.add_argument("firmware")
    p.add_argument("--port", help=f'MIDI port name to match (default "{BOOTLOADER_PORT}")')
    p.set_defaults(func=cmd_upload)

    p = sub.add_parser("ports", help="list MIDI ports")
    p.set_defaults(func=cmd_ports)

    args = parser.parse_args()
    try:
        args.func(args)
    except (OSError, ValueError) as error:
        sys.exit(f"error: {error}")


if __name__ == "__main__":
    main()
