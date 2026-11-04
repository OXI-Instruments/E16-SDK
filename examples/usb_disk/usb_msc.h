#ifndef USB_MSC_H
#define USB_MSC_H

#include <stdbool.h>
#include <stdint.h>

/*
 * USB mass storage next to USB MIDI, as a USB function of the SDK's usb.h
 * (by Charles Vestal). The E16 appears to the computer as a USB MIDI device
 * and a USB drive. Copy this file, usb_msc.c and tusb_config_app.h into your
 * firmware, and compile TinyUSB's class/msc/msc_device.c with it (see
 * examples/CMakeLists.txt).
 *
 *     static const usb_msc_ops_t disk = {
 *         .block_count = 64, .block_size = 512,
 *         .ready = disk_ready, .read = disk_read, .write = disk_write,
 *         .eject = disk_eject,
 *     };
 *     usb_msc_start(&disk);   // before midi_init()
 *
 * Endpoints: EP2 OUT and IN.
 *
 * Thread context: every callback runs in the USB service interrupt (TIM7,
 * NVIC priority 6, see usb.h), not in the main loop. While a callback
 * runs, USB MIDI, the LED DMA (also priority 6) and the encoders (priority 7)
 * wait; SysTick (priority 0) keeps counting, so system_millis() and
 * timeouts based on it work. Share state with the main loop the way you would
 * with any interrupt handler (volatile flags, or briefly masking TIM7_IRQn).
 *
 * A callback should return within a few milliseconds. Slow media should
 * either return USB_MSC_BUSY until the medium can take the request, or return
 * USB_MSC_ASYNC and finish the work in the main loop, then call
 * usb_msc_io_done() (see below).
 */

/* Special return values for read and write. */
#define USB_MSC_BUSY    0    /* not now: called again with the same arguments, every millisecond */
#define USB_MSC_ERROR   (-1) /* fails the command; the host sees "medium not present" */
#define USB_MSC_ASYNC   (-2) /* the main loop finishes it, then calls usb_msc_io_done() */

typedef struct {
    uint32_t block_count;   /* blocks on the medium */
    uint16_t block_size;    /* bytes per block, normally 512 */

    /*
     * True while the host may use the medium. While false, the host is told
     * "medium not present" and shows no volume (macOS keeps polling). When it
     * turns true again, the host is told the medium may have changed and
     * rereads it. NULL means always ready.
     */
    bool (*ready)(void);

    /*
     * Read or write len bytes at byte lba * block_size + offset. len is at most
     * 512: one whole block with 512-byte blocks, unless an earlier call
     * handled fewer bytes than it was given. Return
     * the number of bytes handled (fewer is fine: the rest comes in another
     * call), or USB_MSC_BUSY, USB_MSC_ERROR or USB_MSC_ASYNC. With ASYNC, buf
     * stays valid until usb_msc_io_done(). write NULL makes the medium
     * read-only.
     */
    int32_t (*read)(uint32_t lba, uint32_t offset, void *buf, uint32_t len);
    int32_t (*write)(uint32_t lba, uint32_t offset, const void *buf, uint32_t len);

    /*
     * The host sent START STOP UNIT with eject. Typically makes ready()
     * return false until the firmware presents the medium again. NULL to
     * ignore. macOS doesn't send it: usb_msc.c refuses the medium lock, so macOS ejects without telling the drive. Watch for the
     * FAT clean-shutdown bit, or give the user a "done" control.
     */
    void (*eject)(void);
} usb_msc_ops_t;

/* Add the drive. Call before midi_init(); ops must stay valid. False if USB has no room for it. */
bool usb_msc_start(const usb_msc_ops_t *ops);

/*
 * Finish a read or write that returned USB_MSC_ASYNC: result is the number
 * of bytes handled (the len it was called with) or USB_MSC_ERROR. Call it from
 * the main loop, once per ASYNC return.
 */
void usb_msc_io_done(int32_t result);

#endif
