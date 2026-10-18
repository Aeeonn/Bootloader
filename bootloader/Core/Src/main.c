/*
 * main.c
 *
 *  Created on: Oct 12, 2026
 *      Author: Justin Dubreuil
 */

#include "main.h"

#define SRAM_START 0x20000000U
#define SRAM_END   0x20020000U

COM_InitTypeDef BspCOMInit;

static void MX_GPIO_Init(void);
static void MX_GPIO_DeInit(void);
static int  slot_vectors_ok(uint32_t slot);
static int  slot_image_ok(const Metadata_t *m, uint32_t slot);
static int  select_boot_slot(Metadata_t *m, int *trial);
static void watchdog_start(void);
static void JumpToApp(uint32_t addr);

int main(void)
{
	HAL_Init();
	SystemClock_Config();
	MX_GPIO_Init();

	BSP_LED_Init(LED_GREEN);
	BSP_PB_Init(BUTTON_USER, BUTTON_MODE_EXTI);

	BspCOMInit.BaudRate   = 115200;
	BspCOMInit.WordLength = COM_WORDLENGTH_8B;
	BspCOMInit.StopBits   = COM_STOPBITS_1;
	BspCOMInit.Parity	 = COM_PARITY_NONE;
	BspCOMInit.HwFlowCtl  = COM_HWCONTROL_NONE;

	if (BSP_COM_Init(COM1, &BspCOMInit) != BSP_ERROR_NONE)
	{
		Error_Handler();
	}

	if ((FLASH->OPTR & FLASH_OPTR_DBANK) == 0U)
	{
		printf("WARNING: flash is in single-bank mode, metadata cannot be saved (set DBANK = 1)\r\n");
	}

	Metadata_t meta;
	int loaded = meta_load(&meta);

	// Holding the user button during reset wipes the metadata (e.g. to clear a BAD slot)
	if (!loaded || BSP_PB_GetState(BUTTON_USER) == GPIO_PIN_SET)
	{
		uint32_t seq = loaded ? meta.seq : 0U;  // keep counting so the new copy is the newest

		printf("Metadata reset to defaults\r\n");
		meta_init_default(&meta);
		meta.seq = seq;
		if (!meta_save(&meta))
		{
			printf("WARNING: failed to save metadata\r\n");
		}
	}

	static int count = 0;

	while (1)
	{
		BSP_LED_Toggle(LED_GREEN);
		HAL_Delay(500);
		if(count++ % 2 == 0)
		{
			printf("Hello from STM32 as the BOOTLOADER!\r\n");
		}

		else if(count % 16 == 0)
		{
			int trial;
			int slot = select_boot_slot(&meta, &trial);

			if (slot < 0)
			{
				printf("No bootable slot, staying in the bootloader\r\n");
				continue;
			}

			if (trial)
			{
				printf("Slot %c is on trial (attempt %lu of %u)\r\n", slot == SLOT_B ? 'B' : 'A',
					   (unsigned long)meta.boot_attempts, META_MAX_BOOT_ATTEMPTS);
				watchdog_start();
			}

			JumpToApp(slot_addr((uint32_t)slot));
			//Doesn't return
		}

	}
}

void SystemClock_Config(void)
{
	RCC_OscInitTypeDef RCC_OscInitStruct = {0};
	RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

	HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1_BOOST);

	RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
	RCC_OscInitStruct.HSIState = RCC_HSI_ON;
	RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
	RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
	RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
	RCC_OscInitStruct.PLL.PLLM = RCC_PLLM_DIV4;
	RCC_OscInitStruct.PLL.PLLN = 85;
	RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
	RCC_OscInitStruct.PLL.PLLQ = RCC_PLLQ_DIV2;
	RCC_OscInitStruct.PLL.PLLR = RCC_PLLR_DIV2;

	if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
	{
		Error_Handler();
	}

	RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK |
								   RCC_CLOCKTYPE_SYSCLK |
								   RCC_CLOCKTYPE_PCLK1 |
								   RCC_CLOCKTYPE_PCLK2;
	RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
	RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
	RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
	RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

	if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK)
	{
		Error_Handler();
	}
}

static void MX_GPIO_Init(void)
{
	__HAL_RCC_GPIOC_CLK_ENABLE();
	__HAL_RCC_GPIOF_CLK_ENABLE();
	__HAL_RCC_GPIOA_CLK_ENABLE();
	__HAL_RCC_GPIOB_CLK_ENABLE();
}

static void MX_GPIO_DeInit(void)
{
	__HAL_RCC_GPIOB_CLK_DISABLE();
	__HAL_RCC_GPIOA_CLK_DISABLE();
	__HAL_RCC_GPIOF_CLK_DISABLE();
	__HAL_RCC_GPIOC_CLK_DISABLE();
}

// Initial stack pointer must be in SRAM and the reset handler inside the slot
static int slot_vectors_ok(uint32_t slot)
{
	uint32_t base = slot_addr(slot);
	uint32_t sp   = *(volatile uint32_t*)base;
	uint32_t rst  = *(volatile uint32_t*)(base + 4);

	if (sp < SRAM_START || sp > SRAM_END) return 0;
	if ((rst & 1U) == 0U) return 0;  // must be a Thumb address
	if (rst < base || rst >= base + SLOT_SIZE) return 0;
	return 1;
}

static int slot_image_ok(const Metadata_t *m, uint32_t slot)
{
	if (!slot_vectors_ok(slot)) return 0;
	if (m->size[slot] == 0U) return 1;  // size unknown (e.g. flashed by the debugger): vectors only
	if (m->size[slot] > SLOT_SIZE) return 0;
	return meta_crc32((const void *)slot_addr(slot), m->size[slot]) == m->crc[slot];
}

/*
 * Picks the slot to boot, starting with the active one and falling back to the
 * other. Metadata is only saved when something changes (slot switched, slot
 * marked BAD, or a trial boot counted), so normal boots don't wear the flash.
 * Returns the slot, or -1 if neither slot is bootable. *trial is set when the
 * chosen slot is PENDING and the watchdog has to be started.
 */
static int select_boot_slot(Metadata_t *m, int *trial)
{
	uint32_t slot = (m->active_slot == SLOT_B) ? SLOT_B : SLOT_A;
	int dirty = 0;

	*trial = 0;

	for (int i = 0; i < SLOT_COUNT; i++, slot ^= 1U)
	{
		if (m->state[slot] == SLOT_STATE_BAD) continue;

		if (!slot_image_ok(m, slot))
		{
			// An EMPTY slot just has nothing in it, anything else is a broken image
			if (m->state[slot] != SLOT_STATE_EMPTY)
			{
				m->state[slot] = SLOT_STATE_BAD;
				dirty = 1;
			}
			continue;
		}

		if (m->active_slot != slot)
		{
			m->active_slot = slot;
			m->boot_attempts = 0;
			dirty = 1;
		}

		if (m->state[slot] == SLOT_STATE_PENDING)
		{
			if (m->boot_attempts >= META_MAX_BOOT_ATTEMPTS)
			{
				// Never confirmed itself: give up on it
				m->state[slot] = SLOT_STATE_BAD;
				m->boot_attempts = 0;
				dirty = 1;
				continue;
			}
			m->boot_attempts++;
			*trial = 1;
			dirty = 1;
		}

		if (dirty && !meta_save(m))
		{
			printf("WARNING: failed to save metadata\r\n");
		}
		return (int)slot;
	}

	if (dirty && !meta_save(m))
	{
		printf("WARNING: failed to save metadata\r\n");
	}
	return -1;
}

// Independent watchdog, ~4 s timeout. Once started it can't be stopped, so the
// application must keep refreshing it (IWDG->KR = 0xAAAA).
static void watchdog_start(void)
{
	DBGMCU->APB1FZR1 |= DBGMCU_APB1FZR1_DBG_IWDG_STOP;  // pause it while halted in the debugger

	IWDG->KR  = 0xCCCCU;    // start (also turns on the LSI)
	IWDG->KR  = 0x5555U;    // unlock PR and RLR
	IWDG->PR  = 4U;         // LSI 32 kHz / 64 = 500 Hz
	IWDG->RLR = 2000U - 1U; // 2000 ticks = 4 s
	while (IWDG->SR != 0U) {}
	IWDG->KR  = 0xAAAAU;    // refresh
}

static void JumpToApp(uint32_t addr)
{
	uint32_t sp  = *(volatile uint32_t*)addr;
	uint32_t rst = *(volatile uint32_t*)(addr + 4);

	if (addr == SLOT_A_ADDR) {
		printf("Jumping to APP A now!\r\n");
	} else {
		printf("Jumping to APP B now!\r\n");
	}


	SysTick->CTRL = 0;
	SysTick->VAL  = 0;
	SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk;

	BSP_COM_DeInit(COM1);
	BSP_PB_DeInit(BUTTON_USER);
	BSP_LED_DeInit(LED_GREEN);

	MX_GPIO_DeInit();
	HAL_RCC_DeInit();
	HAL_DeInit();

	__disable_irq();

	for (int i = 0; i < 8; i++) {
		NVIC->ICER[i] = 0xFFFFFFFF;
		NVIC->ICPR[i] = 0xFFFFFFFF;
	}

	SCB->VTOR = addr;
	__DSB();
	__ISB();

	__asm volatile ("msr msp, %0 \n bx %1" :: "r"(sp), "r"(rst));
}

void Error_Handler(void)
{
	__disable_irq();

	while (1)
	{
	}
}

void BSP_PB_Callback(Button_TypeDef Button)
{
	static uint16_t count = 0;
	if (Button == BUTTON_USER)
	{
		printf("Button pressed! #%d\r\n", ++count);
	}
}
