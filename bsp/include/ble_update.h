#ifndef BLE_UPDATE_H
#define BLE_UPDATE_H

#include <stddef.h>
#include <stdint.h>

/*
 * Install an application on the E16's Bluetooth chip, an nRF52832, through its
 * bootloader. The chip is only reachable through the main processor, so
 * firmware on the main processor has to do this. e16_add_ble_updater() in
 * CMake builds a ready-made updater around this function.
 *
 * The nRF's flash: Nordic's MBR and S132 7.0.1 SoftDevice up to 0x26000, the
 * application from 0x26000, OXI's bootloader from 0x64000. Only the
 * application can be replaced; the SoftDevice and bootloader stay as they are.
 *
 * The nRF bootloader writes wherever the image takes it and does not protect
 * itself. An image larger than BLE_APP_MAX_SIZE would overwrite it, after which
 * the chip can only be recovered with a debug probe. ble_update() refuses such
 * images.
 *
 * An interrupted update leaves the nRF without a working application, but its
 * bootloader intact: run the update again.
 */
#define BLE_APP_ADDR        0x26000u
#define BLE_APP_MAX_SIZE    (0x64000u - BLE_APP_ADDR)   /* 248 KB */

typedef enum {
    BLE_UPDATE_OK,
    BLE_UPDATE_TOO_LARGE,       /* image larger than BLE_APP_MAX_SIZE, nothing was written */
    BLE_UPDATE_NO_BOOTLOADER,   /* the nRF bootloader didn't answer, nothing was written */
    BLE_UPDATE_FAILED,          /* the bootloader kept refusing data; run the update again */
} ble_update_result_t;

/* Called after each 128-byte block with the bytes sent so far. */
typedef void (*ble_update_progress_t)(size_t done, size_t total);

/*
 * Write image (the application, starting at BLE_APP_ADDR) to the nRF and
 * start it. Blocks until done: about 0.5 s per KB. progress may be NULL.
 */
ble_update_result_t ble_update(const uint8_t *image, size_t len, ble_update_progress_t progress);

#endif
