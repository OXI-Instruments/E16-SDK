#ifndef STORAGE_H
#define STORAGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define STORAGE_PAGE_SIZE   256
#define STORAGE_SECTOR_SIZE 4096

/*
 * The E16's external SPI NOR flash, for presets and settings. Addresses run
 * from 0 to storage_size() - 1. All calls block until done.
 *
 * Like all NOR flash, writing can only turn 1 bits into 0 bits: erase a sector
 * (all bytes become 0xFF) before writing to it again. Each sector survives
 * about 100,000 erases, so only write when the user asks for it, e.g. by
 * saving, never automatically or on a timer. That includes delayed saves
 * ("save a few seconds after the last change"): every change then costs an
 * erase, without the user ever asking for one.
 *
 * To protect the flash from code that erases it in a loop, storage_erase()
 * erases at most STORAGE_ERASE_BURST sectors in a row, then one more every
 * STORAGE_ERASE_INTERVAL_MS. A firmware that saves when the user asks never
 * notices; one that erases too often gets false back.
 *
 * The official firmware keeps its own data in this flash. Writing here can
 * overwrite it.
 */

#define STORAGE_ERASE_BURST         32
#define STORAGE_ERASE_INTERVAL_MS   15000u

/* Returns false if no flash chip answers. */
bool storage_init(void);

/* Size in bytes, read from the chip. */
uint32_t storage_size(void);

void storage_read(uint32_t addr, void *buf, size_t len);

/* Write to erased flash. Any length and alignment, about 1.5 ms per KB. */
void storage_write(uint32_t addr, const void *data, size_t len);

/*
 * Erase every 4 KB sector that addr .. addr + len - 1 touches, including the
 * parts of those sectors outside the range. About 15 ms per sector. Returns
 * false, erasing nothing, if the range goes past the end of the flash or
 * would exceed the erase limit; try again later.
 */
bool storage_erase(uint32_t addr, size_t len);

#endif
