/*
 * USB disk example: the E16 shows up as a USB MIDI device and a small USB
 * drive, "E16DISK", holding HELLO.TXT. The drive is a 32 KB RAM disk (64
 * blocks of 512 bytes, FAT12): files written to it are lost at power-off.
 *
 * The drive is in RAM on purpose. Don't put it on the E16's flash: the
 * computer writes to a mounted drive on its own (file table, metadata),
 * always to the same few sectors, which wears them out within days, and the
 * official firmware's presets live in that flash too.
 *
 * Shows how to:
 *   - add the mass-storage interface with usb_msc_start() (usb_msc.c, a USB
 *     function of the SDK's usb.h);
 *   - finish writes in the main loop (USB_MSC_ASYNC), the way a slow medium
 *     would, instead of in the USB interrupt;
 *   - take the medium away and give it back: after the computer ejects the
 *     drive, it comes back 5 seconds later and the computer mounts it again.
 *
 * USB MIDI keeps working: channel messages arriving over USB are echoed back.
 *
 * By Charles Vestal.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "display.h"
#include "font.h"
#include "midi.h"
#include "system.h"
#include "usb_msc.h"

#define BLOCK_SIZE      512
#define BLOCK_COUNT     64
#define ROOT_ENTRIES    32
#define FAT_BLOCK       1
#define ROOT_BLOCK      2
#define DATA_BLOCK      (ROOT_BLOCK + ROOT_ENTRIES * 32 / BLOCK_SIZE)  /* cluster 2 */
#define RETURN_MS       5000
#define LINE_CHARS      (DISPLAY_WIDTH / FONT_WIDTH)

static const char hello[] = "Hello from the E16\r\n";

static uint8_t disk[BLOCK_COUNT][BLOCK_SIZE];

/* ---- The RAM disk, formatted at startup ---- */

static void put16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void put32(uint8_t *p, uint32_t v)
{
    put16(p, (uint16_t)v);
    put16(p + 2, (uint16_t)(v >> 16));
}

static void format(void)
{
    uint8_t *boot = disk[0];
    memcpy(boot, "\xEB\x3C\x90" "MSDOS5.0", 11);
    put16(boot + 11, BLOCK_SIZE);           /* bytes per sector */
    boot[13] = 1;                           /* sectors per cluster */
    put16(boot + 14, 1);                    /* reserved sectors: the boot sector */
    boot[16] = 1;                           /* FATs */
    put16(boot + 17, ROOT_ENTRIES);
    put16(boot + 19, BLOCK_COUNT);          /* total sectors */
    boot[21] = 0xF8;                        /* media: fixed disk */
    put16(boot + 22, 1);                    /* sectors per FAT */
    put16(boot + 24, 1);                    /* sectors per track */
    put16(boot + 26, 1);                    /* heads */
    boot[36] = 0x80;                        /* drive number */
    boot[38] = 0x29;                        /* extended boot signature */
    put32(boot + 39, 0x0E160E16);           /* volume serial */
    memcpy(boot + 43, "E16DISK    ", 11);
    memcpy(boot + 54, "FAT12   ", 8);
    boot[510] = 0x55;
    boot[511] = 0xAA;

    /* FAT12: entries 0 and 1 reserved, cluster 2 (HELLO.TXT) ends its chain. */
    static const uint8_t fat[] = {0xF8, 0xFF, 0xFF, 0xFF, 0x0F};
    memcpy(disk[FAT_BLOCK], fat, sizeof(fat));

    uint8_t *label = disk[ROOT_BLOCK];
    memcpy(label, "E16DISK    ", 11);
    label[11] = 0x08;                       /* volume label */

    uint8_t *file = disk[ROOT_BLOCK] + 32;
    memcpy(file, "HELLO   TXT", 11);
    file[11] = 0x20;                        /* archive */
    put16(file + 24, (45 << 9) | (1 << 5) | 1); /* 2025-01-01 */
    put16(file + 18, (45 << 9) | (1 << 5) | 1);
    put16(file + 16, (45 << 9) | (1 << 5) | 1);
    put16(file + 26, 2);                    /* first cluster */
    put32(file + 28, sizeof(hello) - 1);

    memcpy(disk[DATA_BLOCK], hello, sizeof(hello) - 1);
}

/*
 * Everything below marked "USB interrupt" runs in the USB service interrupt,
 * not the main loop; share state with volatile variables.
 */

static volatile bool present = true;
static volatile uint32_t ejected_at;
static volatile uint32_t blocks_written;

/* A write handed to the main loop (USB_MSC_ASYNC), at most one at a time. */
static volatile bool write_pending;
static uint32_t pending_lba, pending_offset, pending_len;
static const void *pending_buf;

static bool disk_ready(void) /* USB interrupt */
{
    return present;
}

static int32_t disk_read(uint32_t lba, uint32_t offset, void *buf, uint32_t len) /* USB interrupt */
{
    if (offset + len > BLOCK_SIZE) {
        len = BLOCK_SIZE - offset;
    }
    memcpy(buf, &disk[lba][offset], len);
    return (int32_t)len;
}

static int32_t disk_write(uint32_t lba, uint32_t offset, const void *buf, uint32_t len) /* USB interrupt */
{
    if (offset + len > BLOCK_SIZE) {
        len = BLOCK_SIZE - offset;
    }
    pending_lba = lba;
    pending_offset = offset;
    pending_len = len;
    pending_buf = buf;
    write_pending = true;
    return USB_MSC_ASYNC;
}

static void disk_eject(void) /* USB interrupt */
{
    present = false;
    ejected_at = system_millis();
}

static const usb_msc_ops_t ram_disk = {
    .block_count = BLOCK_COUNT,
    .block_size = BLOCK_SIZE,
    .ready = disk_ready,
    .read = disk_read,
    .write = disk_write,
    .eject = disk_eject,
};

/* ---- Main loop ---- */

static void print(int page, const char *s)
{
    uint8_t row[DISPLAY_WIDTH] = {0};
    for (int i = 0; s[i] && i < LINE_CHARS; i++) {
        char c = s[i] >= FONT_FIRST && s[i] <= FONT_LAST ? s[i] : '?';
        memcpy(&row[i * FONT_WIDTH], font[c - FONT_FIRST], FONT_WIDTH);
    }
    display_write(page, 0, row, sizeof(row));
}

int main(void)
{
    system_init();
    display_init();
    for (int page = 0; page < DISPLAY_PAGES; page++) {
        print(page, "");
    }
    format();
    usb_msc_start(&ram_disk); /* adds the drive; without it the E16 is MIDI-only */
    midi_init();
    print(0, "USB DISK");

    bool shown_present = false;
    uint32_t shown_written = UINT32_MAX;

    for (;;) {
        if (write_pending) {
            /* A slow medium would do its work here; RAM just copies. */
            memcpy(&disk[pending_lba][pending_offset], pending_buf, pending_len);
            if (pending_offset + pending_len == BLOCK_SIZE) {
                blocks_written++;
            }
            write_pending = false;
            usb_msc_io_done((int32_t)pending_len);
        }

        if (!present && system_millis() - ejected_at >= RETURN_MS) {
            present = true;
        }

        if (present != shown_present || blocks_written != shown_written) {
            shown_present = present;
            shown_written = blocks_written;
            char line[LINE_CHARS + 1];
            print(2, present ? "PRESENT" : "EJECTED, BACK IN 5S");
            snprintf(line, sizeof(line), "BLOCKS WRITTEN %lu", (unsigned long)blocks_written);
            print(3, line);
        }

        midi_msg_t msg;
        while (midi_read(&msg)) {
            if (msg.port != MIDI_USB) {
                continue;
            }
            if (msg.data[0] >= 0x80 && msg.data[0] < 0xF0) {
                midi_write(MIDI_USB, msg.data, msg.len); /* echo */
            }
        }
    }
}
