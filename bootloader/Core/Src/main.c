#include "main.h"

#define APP_ADDR 0x08008800


COM_InitTypeDef BspCOMInit;

void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_GPIO_DeInit(void);
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
    BspCOMInit.Parity     = COM_PARITY_NONE;
    BspCOMInit.HwFlowCtl  = COM_HWCONTROL_NONE;

    if (BSP_COM_Init(COM1, &BspCOMInit) != BSP_ERROR_NONE)
    {
        Error_Handler();
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
			JumpToApp(APP_ADDR);
			//Doesn't return anything if success
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

static void JumpToApp(uint32_t addr)
{
    uint32_t sp  = *(volatile uint32_t*)addr;
    uint32_t rst = *(volatile uint32_t*)(addr + 4);

    if (sp < 0x20000000 || sp > 0x20020000) return;   // not in SRAM
    if (rst < APP_ADDR || rst >= 0x08044800) return;  // reset handler not in slot A

    printf("Jumping to APP now!\r\n");

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
