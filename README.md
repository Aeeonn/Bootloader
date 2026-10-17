# bootloader

An A/B bootloader to run on a Nucleo-G474RE mcu from STM. A lot more features than just that will be added such as: Write protection, CAN updates for the app, and eventually, making the bootloader/switching process brickproof (unbrickable and recoverable no matter the state)

> **Status:** work in progress.

## Repository layout

| Folder | Description |
| --- | --- |
| [`bootloader/`](bootloader) | STM32CubeIDE project for the bootloader. Runs from the start of flash. |
| [`application/`](application) | STM32CubeIDE project for the application loaded by the bootloader. Can be linked for slot A or slot B. |

Both are standard STM32CubeIDE projects. The repository root is the IDE workspace folder, so both projects can be imported from it.

## How the application connects to the bootloader

The two projects are built separately, and the bootloader hands control to the application at a fixed flash address.

Flash memory map (STM32G474RE, flash starts at `0x08000000`):

| Region | Start address | Size | Defined in |
| --- | --- | --- | --- |
| Bootloader | `0x08000000` | 32 KB | `bootloader/STM32G474RETX_FLASH.ld` |
| (unassigned) | `0x08008000` | 2 KB | — |
| Application slot A | `0x08008800` | 240 KB | `application/STM32G474RETX_FLASH_A.ld` |
| Application slot B | `0x08044800` | 240 KB | `application/STM32G474RETX_FLASH_B.ld` |

The application has two build configurations, **SlotA** and **SlotB**, and each one uses the matching linker script. That way the same application source can be built to run from either slot.

When the bootloader starts the application (`JumpToApp()` in `bootloader/Core/Src/main.c`), it:

1. Reads the application's initial stack pointer and reset handler from the start of the slot's vector table.
2. Checks that the stack pointer is in SRAM and the reset handler is inside slot A. If either check fails, it stays in the bootloader.
3. Stops SysTick, deinitializes the BSP, GPIO, RCC and HAL, disables interrupts, and clears all pending NVIC interrupts.
4. Points `SCB->VTOR` at the application's vector table, loads the main stack pointer, and branches to the application's reset handler.

Currently the bootloader always jumps to slot A. Slot B is laid out and buildable, but nothing selects it yet.

## Building and running

1. Open this folder as the workspace in STM32CubeIDE, then import `bootloader` and `application` (*File → Import → Existing Projects into Workspace*).
2. Build `bootloader` and flash it to the board.
3. Build `application` with the **SlotA** configuration and flash it. Its image is linked at `0x08008800`, so it doesn't overwrite the bootloader.
4. Open the ST-LINK virtual COM port. The bootloader blinks the green LED, prints a message, and after a few seconds jumps to the application in slot A.

## License

[GPL-3.0](LICENSE)
