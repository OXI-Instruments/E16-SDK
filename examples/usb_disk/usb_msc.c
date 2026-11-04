/*
 * USB mass storage: the interface's descriptor and TinyUSB class driver for
 * usb.h, and TinyUSB's MSC callbacks, forwarded to the firmware's block
 * device. They run in the USB service interrupt (TIM7), see usb.h.
 */
#include "usb_msc.h"

#include <string.h>

#include "board.h"
#include "usb.h"

#define EP_OUT  0x02
#define EP_IN   0x82

static const usb_msc_ops_t *disk;

/*
 * Whether the host last saw the medium present. When ready() turns true after
 * being false, the next TEST UNIT READY reports UNIT ATTENTION once, which
 * makes the host reread the medium.
 */
static bool host_saw_ready = true;

static bool medium_ready(void)
{
    const usb_msc_ops_t *ops = disk;
    return ops && (!ops->ready || ops->ready());
}

static void not_present(uint8_t lun)
{
    host_saw_ready = false;
    tud_msc_set_sense(lun, SCSI_SENSE_NOT_READY, 0x3A, 0x00); /* MEDIUM NOT PRESENT */
}

void tud_msc_inquiry_cb(uint8_t lun, uint8_t vendor_id[8], uint8_t product_id[16], uint8_t product_rev[4])
{
    (void)lun;
    /* Space padded, no terminator. */
    memcpy(vendor_id, "OXI     ", 8);
    memcpy(product_id, "E16 Disk        ", 16);
    memcpy(product_rev, "1.0 ", 4);
}

bool tud_msc_test_unit_ready_cb(uint8_t lun)
{
    if (!medium_ready()) {
        not_present(lun);
        return false;
    }
    if (!host_saw_ready) {
        host_saw_ready = true;
        tud_msc_set_sense(lun, SCSI_SENSE_UNIT_ATTENTION, 0x28, 0x00); /* MEDIUM MAY HAVE CHANGED */
        return false;
    }
    return true;
}

void tud_msc_capacity_cb(uint8_t lun, uint32_t *block_count, uint16_t *block_size)
{
    if (!medium_ready()) {
        /* Zero makes TinyUSB fail the command; set the sense ourselves. */
        not_present(lun);
        *block_count = 0;
        *block_size = 0;
        return;
    }
    *block_count = disk->block_count;
    *block_size = disk->block_size;
}

bool tud_msc_is_writable_cb(uint8_t lun)
{
    (void)lun;
    return disk && disk->write;
}

int32_t tud_msc_read10_cb(uint8_t lun, uint32_t lba, uint32_t offset, void *buffer, uint32_t bufsize)
{
    if (!medium_ready() || lba >= disk->block_count) {
        not_present(lun);
        return USB_MSC_ERROR;
    }
    return disk->read(lba, offset, buffer, bufsize);
}

int32_t tud_msc_write10_cb(uint8_t lun, uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t bufsize)
{
    if (!medium_ready() || lba >= disk->block_count || !disk->write) {
        not_present(lun);
        return USB_MSC_ERROR;
    }
    return disk->write(lba, offset, buffer, bufsize);
}

/*
 * PREVENT ALLOW MEDIUM REMOVAL. macOS asks to lock the medium right after
 * mounting. A host that believes the medium is locked stops polling for its
 * removal, so when the firmware later makes ready() return false, macOS
 * keeps a stale mount (and `diskutil eject` can hang on it). Refusing the
 * lock, as a card reader without one does, keeps the host polling TEST UNIT
 * READY, so it unmounts within about a second of the medium going away.
 * Allowing removal is always fine.
 *
 * Side effect: macOS then treats the drive as manual-eject and ejects it
 * without sending START STOP UNIT, so ops->eject isn't called on a macOS
 * eject. A firmware that wants to know when the host let go can watch for
 * the FAT clean-shutdown bit being written, or offer its own "done" control.
 */
bool tud_msc_prevent_allow_medium_removal_cb(uint8_t lun, uint8_t prohibit_removal, uint8_t control)
{
    (void)control;
    if (prohibit_removal) {
        tud_msc_set_sense(lun, SCSI_SENSE_ILLEGAL_REQUEST, 0x24, 0x00); /* INVALID FIELD IN CDB */
        return false;
    }
    return true;
}

bool tud_msc_start_stop_cb(uint8_t lun, uint8_t power_condition, bool start, bool load_eject)
{
    (void)lun;
    (void)power_condition;
    if (load_eject && !start && disk && disk->eject) {
        disk->eject();
    }
    return true;
}

int32_t tud_msc_scsi_cb(uint8_t lun, uint8_t const scsi_cmd[16], void *buffer, uint16_t bufsize)
{
    (void)buffer;
    (void)bufsize;
    switch (scsi_cmd[0]) {
    case 0x35: /* SYNCHRONIZE CACHE (10): accepted; a firmware with a write cache flushes it on eject */
        return 0;
    default:
        tud_msc_set_sense(lun, SCSI_SENSE_ILLEGAL_REQUEST, 0x20, 0x00); /* INVALID COMMAND OPERATION CODE */
        return -1;
    }
}

void usb_msc_io_done(int32_t result)
{
    /* TinyUSB's event queue is also written from the USB interrupts; keep them out meanwhile. */
    usb_lock();
    tud_msc_async_io_done(result, true);
    usb_unlock();
}

/* ---- Hooking into usb.h ---- */

static uint16_t descriptor(uint8_t *desc, uint8_t first_itf)
{
    const uint8_t d[] = {
        TUD_MSC_DESCRIPTOR(first_itf, 0, EP_OUT, EP_IN, 64),
    };
    if (desc) {
        memcpy(desc, d, sizeof(d));
    }
    return sizeof(d);
}

static const usbd_class_driver_t driver = {
    .name = "MSC",
    .init = mscd_init,
    .deinit = mscd_deinit,
    .reset = mscd_reset,
    .open = mscd_open,
    .control_xfer_cb = mscd_control_xfer_cb,
    .xfer_cb = mscd_xfer_cb,
};

static const usb_function_t function = {
    .descriptor = descriptor,
    .interfaces = 1,
    .driver = &driver,
};

bool usb_msc_start(const usb_msc_ops_t *ops)
{
    disk = ops;
    return usb_add_function(&function);
}
