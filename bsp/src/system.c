#include "system.h"

#include "board.h"

static volatile uint32_t boot_flag __attribute__((section(".boot_flag")));

static void clock_init(void)
{
    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

    RCC_OscInitTypeDef osc = {0};
    osc.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    osc.HSEState = RCC_HSE_ON;
    osc.PLL.PLLState = RCC_PLL_ON;
    osc.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    osc.PLL.PLLM = 4;
    osc.PLL.PLLN = 168;
    osc.PLL.PLLP = RCC_PLLP_DIV2;
    osc.PLL.PLLQ = 7;
    HAL_RCC_OscConfig(&osc);

    /*
     * APB1 runs at 84 MHz so USART2 can reach the 7.95 Mbaud the LED chain needs.
     * Zero flash wait states rely on the GD32's fast first 512 KB of flash.
     */
    RCC_ClkInitTypeDef clk = {0};
    clk.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    clk.AHBCLKDivider = RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider = RCC_HCLK_DIV2;
    clk.APB2CLKDivider = RCC_HCLK_DIV2;
    HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_0);
}

static void power_init(void)
{
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOH_CLK_ENABLE();

    HAL_GPIO_WritePin(PWR_OFF_PORT, PWR_OFF_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(BLE_RESET_PORT, BLE_RESET_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(BLE_BOOT_PORT, BLE_BOOT_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(MOTOR_PORT, MOTOR_PIN, GPIO_PIN_RESET);

    GPIO_InitTypeDef gpio = {0};
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;

    gpio.Pin = PWR_OFF_PIN | BLE_BOOT_PIN | MOTOR_PIN;
    HAL_GPIO_Init(GPIOC, &gpio);
    gpio.Pin = PWR_10V_EN_PIN | BLE_RESET_PIN;
    HAL_GPIO_Init(GPIOB, &gpio);

    HAL_GPIO_WritePin(PWR_10V_EN_PORT, PWR_10V_EN_PIN, GPIO_PIN_SET);
}

void system_init(void)
{
    /*
     * The bootloader jumps here with interrupts masked and possibly some still
     * enabled or pending, and the vector table may still point at its own.
     */
    SCB->VTOR = APP_FLASH_BASE;
    for (uint32_t i = 0; i < 8; i++) {
        NVIC->ICER[i] = 0xFFFFFFFF;
        NVIC->ICPR[i] = 0xFFFFFFFF;
    }
    boot_flag = 0;

    HAL_Init();
    clock_init();
    power_init();
    __enable_irq();
}

uint32_t system_millis(void)
{
    return HAL_GetTick();
}

void system_delay_ms(uint32_t ms)
{
    HAL_Delay(ms);
}

void system_enter_bootloader(void)
{
    __disable_irq();
    boot_flag = BOOTLOADER_MAGIC;
    NVIC_SystemReset();
}

void system_power_off(void)
{
    HAL_GPIO_WritePin(PWR_OFF_PORT, PWR_OFF_PIN, GPIO_PIN_SET);
    __disable_irq();
    for (;;) {
    }
}

void SysTick_Handler(void)
{
    HAL_IncTick();
}
