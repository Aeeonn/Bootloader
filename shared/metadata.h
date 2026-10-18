/*
 * metadata.h
 *
 *  Created on: Oct 17, 2026
 *      Author: Justin Dubreuil
 *
 * Shared between the bootloader and the application. Both projects link this
 * folder, so the layout below can never differ between them.
 */

#ifndef SHARED_METADATA_H_
#define SHARED_METADATA_H_

#include <stdint.h>

/*
 * Flash layout (STM32G474RE in dual-bank mode: DBANK = 1, 2 KB pages)
 *
 *  Bank 1  0x08000000  Bootloader    32 KB
 *          0x08008000  Slot A       224 KB
 *  Bank 2  0x08040000  Metadata 0     2 KB
 *          0x08040800  Metadata 1     2 KB
 *          0x08041000  Spare         28 KB
 *          0x08048000  Slot B       224 KB
 *
 * Both slots sit at offset 0x8000 inside their bank.
 */
#define FLASH_BANK2_ADDR        0x08040000U
#define SLOT_SIZE               0x00038000U
#define SLOT_A_ADDR             0x08008000U
#define SLOT_B_ADDR             0x08048000U
#define META_PAGE0_ADDR         0x08040000U
#define META_PAGE1_ADDR         0x08040800U

#define META_MAGIC              0xB00710ADU
#define META_MAX_BOOT_ATTEMPTS  3U

enum { SLOT_A = 0, SLOT_B = 1, SLOT_COUNT = 2 };

typedef enum {
	SLOT_STATE_EMPTY = 0,   // no managed image (or flashed with the debugger)
	SLOT_STATE_PENDING,     // new image on trial, the app has to confirm it
	SLOT_STATE_CONFIRMED,   // image booted and confirmed itself
	SLOT_STATE_BAD,         // image failed, never boot it
} SlotState_t;

// Must stay a multiple of 8 bytes (flash is programmed in double-words)
typedef struct {
	uint32_t magic;          // written last, so a half-written page is never valid
	uint32_t seq;            // incremented on every save, the newest valid page wins
	uint32_t active_slot;    // SLOT_A or SLOT_B
	uint32_t boot_attempts;  // only counted while the active slot is PENDING
	uint32_t state[SLOT_COUNT];    // SlotState_t
	uint32_t version[SLOT_COUNT];  // image version, 0 = unknown
	uint32_t size[SLOT_COUNT];     // image length in bytes, 0 = unknown (CRC check skipped)
	uint32_t crc[SLOT_COUNT];      // CRC32 over the first size[] bytes of the slot
	uint32_t reserved[3];
	uint32_t meta_crc;       // CRC32 over everything above
} Metadata_t;

_Static_assert(sizeof(Metadata_t) % 8U == 0U, "Metadata_t must be a multiple of 8 bytes");

static inline uint32_t slot_addr(uint32_t slot)
{
	return (slot == SLOT_B) ? SLOT_B_ADDR : SLOT_A_ADDR;
}

void meta_init_default(Metadata_t *m);
int meta_load(Metadata_t *m);
int meta_save(Metadata_t *m);
int meta_confirm_slot(uint32_t slot);
uint32_t meta_crc32(const void *p, uint32_t length);

#endif /* SHARED_METADATA_H_ */
