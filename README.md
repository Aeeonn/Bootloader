# bootloader

An A/B bootloader to run on a Nucleo-G474RE MCU from ST. Eventually, a lot more features than just that will be added.

> **Status:** work in progress.

## Repository layout

| Folder | Description |
| --- | --- |
| [`bootloader/`](bootloader) | STM32CubeIDE project for the bootloader. Runs from the start of flash. |
| [`application/`](application) | STM32CubeIDE project for the application loaded by the bootloader. Can be linked for slot A or slot B. |
| [`shared/`](shared) | Code used by both projects (the boot metadata). Linked into each project as the `Shared` folder. |

Both are standard STM32CubeIDE projects. The repository root is the IDE workspace folder, so both projects can be imported from it.

## Flash memory map

The layout assumes the STM32G474RE's default dual-bank mode (option byte `DBANK = 1`): two 256 KB banks with 2 KB pages. The bootloader prints a warning at startup if the chip is in single-bank mode.

| Bank | Region | Start address | Size | Defined in |
| --- | --- | --- | --- | --- |
| 1 | Bootloader | `0x08000000` | 32 KB | `bootloader/STM32G474RETX_FLASH.ld` |
| 1 | Application slot A | `0x08008000` | 224 KB | `application/STM32G474RETX_FLASH_A.ld` |
| 2 | Metadata page 0 | `0x08040000` | 2 KB | `shared/metadata.h` |
| 2 | Metadata page 1 | `0x08040800` | 2 KB | `shared/metadata.h` |
| 2 | Spare | `0x08041000` | 28 KB | — |
| 2 | Application slot B | `0x08048000` | 224 KB | `application/STM32G474RETX_FLASH_B.ld` |

Both slots start at offset `0x8000` inside their bank.

The application has two build configurations, **SlotA** and **SlotB**. Each uses the matching linker script and `VECT_TAB_OFFSET`, so the same source can run from either slot. SlotB also defines `APP_SLOT_B`.

## Boot metadata

The bootloader keeps its state in a `Metadata_t` record (`shared/metadata.h`):

| Field | Purpose |
| --- | --- |
| `seq` | Incremented on every save. The valid page with the highest `seq` is the current one. |
| `active_slot` | Slot to boot first (`SLOT_A` or `SLOT_B`). |
| `boot_attempts` | Trial boots of a `PENDING` slot that haven't been confirmed yet. |
| `state[]` | Per slot: `EMPTY`, `PENDING`, `CONFIRMED` or `BAD`. |
| `version[]` | Image version per slot (0 = unknown). |
| `size[]`, `crc[]` | Image length and CRC32. When `size` is 0 (for example when flashed with the debugger), only the vector table is checked. |
| `meta_crc` | CRC32 of the record itself. |

Saves alternate between the two metadata pages, and the magic word is written last. If power is lost during a save, the previous copy is still valid. On first boot (both pages erased) the bootloader writes defaults: slot A active, both slots `EMPTY`. Holding the user button during reset also resets the metadata to defaults.

## Boot flow

1. Start with `active_slot`. Skip a slot that is `BAD`. A slot whose vector table (or CRC, if `size` is set) is invalid is marked `BAD`, unless it is `EMPTY`. Then try the other slot.
2. A `CONFIRMED` or `EMPTY` slot with a valid image boots straight away, with no flash write.
3. A `PENDING` slot is a trial boot. The bootloader increments `boot_attempts`, saves, starts the independent watchdog (about 4 s) and jumps. After `META_MAX_BOOT_ATTEMPTS` (3) unconfirmed attempts, the slot is marked `BAD` and the other slot is used.
4. The application refreshes the watchdog in its main loop. After running for 5 s, it calls `meta_confirm_slot()`, which changes a `PENDING` slot to `CONFIRMED` and clears `boot_attempts`. If the application hangs, the watchdog resets the chip and the attempt counts against the slot.

Metadata is only written when something changes, so normal boots cause no flash wear.

`JumpToApp()` stops SysTick, deinitializes the BSP, GPIO, RCC and HAL, disables and clears all NVIC interrupts, points `SCB->VTOR` at the slot, loads the main stack pointer and branches to the reset handler. The application re-enables interrupts at the start of `main()`.

## Building and running

1. Open this folder as the workspace in STM32CubeIDE, then import `bootloader` and `application` (*File → Import → Existing Projects into Workspace*).
2. Build `bootloader` and flash it to the board.
3. Build `application` with the **SlotA** and/or **SlotB** configuration and flash it. Each image is linked to its own slot, so it doesn't overwrite the bootloader or the other slot.
4. Open the ST-LINK virtual COM port. The bootloader blinks the green LED, prints a message, and after a few seconds jumps to the active slot.

## License

[GPL-3.0](LICENSE)
[GPL-3.0](LICENSE)
