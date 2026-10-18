/*
 * metadata.c
 *
 *  Created on: Oct 16, 2026
 *      Author: Justin Dubreuil
 */

#include "stm32g4xx_hal.h"
#include <stddef.h>
#include <string.h>
#include "metadata.h"

static const uint32_t meta_page_addr[2] = { META_PAGE0_ADDR, META_PAGE1_ADDR };

// Page holding the newest valid copy, -1 if neither page is valid
static int meta_current_page = -1;

void meta_init_default(Metadata_t *m)
{
	// Slot A active, both slots EMPTY, versions/sizes/CRCs unknown, no boot attempts
	memset(m, 0, sizeof *m);
}

static int meta_read_page(uint32_t addr, Metadata_t *m)
{
	memcpy(m, (const void *)addr, sizeof *m);
	return m->magic == META_MAGIC &&
		   m->meta_crc == meta_crc32(m, offsetof(Metadata_t, meta_crc));
}

// Load the newest valid copy of the metadata. Returns 0 if neither page is valid.
int meta_load(Metadata_t *m)
{
	Metadata_t p0, p1;
	int ok0 = meta_read_page(META_PAGE0_ADDR, &p0);
	int ok1 = meta_read_page(META_PAGE1_ADDR, &p1);

	if (ok0 && ok1) {
		// Signed difference keeps working when seq wraps around
		meta_current_page = ((int32_t)(p1.seq - p0.seq) > 0) ? 1 : 0;
	} else if (ok0) {
		meta_current_page = 0;
	} else if (ok1) {
		meta_current_page = 1;
	} else {
		meta_current_page = -1;
		return 0;
	}

	*m = (meta_current_page == 1) ? p1 : p0;
	return 1;
}

// Save the metadata to the page that does NOT hold the current copy, so a
// power cut during the write leaves the previous copy intact.
int meta_save(Metadata_t *m)
{
	int target = (meta_current_page == 0) ? 1 : 0;
	uint32_t addr = meta_page_addr[target];

	// The layout above assumes dual-bank mode (2 KB pages, bank 2 at 0x08040000)
	if ((FLASH->OPTR & FLASH_OPTR_DBANK) == 0U) return 0;

	m->magic = META_MAGIC;
	m->seq++;
	m->meta_crc = meta_crc32(m, offsetof(Metadata_t, meta_crc));

	FLASH_EraseInitTypeDef e = {
		.TypeErase = FLASH_TYPEERASE_PAGES,
		.Banks = FLASH_BANK_2,
		.Page = (addr - FLASH_BANK2_ADDR) / FLASH_PAGE_SIZE,
		.NbPages = 1
	};
	uint32_t err;
	int ok = 1;

	HAL_FLASH_Unlock();
	__HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_ALL_ERRORS);
	if (HAL_FLASHEx_Erase(&e, &err) != HAL_OK) ok = 0;

	// Program everything except the first double-word (magic + seq)
	for (uint32_t i = 8; ok && i < sizeof *m; i += 8) {
		uint64_t dw;
		memcpy(&dw, (const uint8_t *)m + i, 8);
		if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, addr + i, dw) != HAL_OK) ok = 0;
	}

	// Magic last: the page only becomes valid once everything else is written
	if (ok) {
		uint64_t dw;
		memcpy(&dw, m, 8);
		if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, addr, dw) != HAL_OK) ok = 0;
	}

	HAL_FLASH_Lock();

	if (ok && memcmp((const void *)addr, m, sizeof *m) != 0) ok = 0;
	if (ok) meta_current_page = target;
	return ok;
}

// Called by the application once it is running correctly. Only writes flash
// when the slot is on trial (PENDING), so normal boots cause no flash wear.
int meta_confirm_slot(uint32_t slot)
{
	Metadata_t m;

	if (slot >= SLOT_COUNT || !meta_load(&m)) return 0;
	if (m.state[slot] != SLOT_STATE_PENDING) return 1;

	m.state[slot] = SLOT_STATE_CONFIRMED;
	m.boot_attempts = 0;
	return meta_save(&m);
}

uint32_t meta_crc32(const void *p, uint32_t length)
{
	const uint8_t *b = p;
	uint32_t crc = 0xFFFFFFFFU;

	// Loop once per byte of input
	while (length--) {
		crc ^= *b++;
		for (int i = 0; i < 8; i++)
			crc = (crc >> 1) ^ (0xEDB88320U & -(crc & 1U));
	}
	return ~crc;
}
