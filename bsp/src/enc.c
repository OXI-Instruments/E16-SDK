#include "enc.h"

#include <stdint.h>

#include "board.h"

#define SCAN_BYTES      6
#define SCAN_HZ         500

/* Where each encoder's A and B contacts (B = A bit + 1) and switch sit in the scan. */
static const struct {
    uint8_t ab_byte, ab_bit, sw_byte, sw_bit;
} wiring[ENC_COUNT] = {
    {0, 0, 1, 0}, {0, 4, 1, 2}, {3, 0, 1, 4}, {3, 4, 1, 6},
    {0, 2, 1, 1}, {0, 6, 1, 3}, {3, 2, 1, 5}, {3, 6, 1, 7},
    {2, 0, 5, 0}, {2, 4, 5, 2}, {4, 0, 5, 4}, {4, 4, 5, 6},
    {2, 2, 5, 1}, {2, 6, 5, 3}, {4, 2, 5, 5}, {4, 6, 5, 7},
};

static SPI_HandleTypeDef spi;
static TIM_HandleTypeDef tim;

static uint8_t hist_a[ENC_COUNT];
static uint8_t hist_b[ENC_COUNT];
static uint8_t half_up[ENC_COUNT];
static uint8_t half_down[ENC_COUNT];
static volatile int16_t steps[ENC_COUNT];

static uint8_t sw_hist[ENC_COUNT + 1];
static volatile uint32_t pressed_mask;

#define SHIFT_BIT ENC_COUNT

/*
 * Same quadrature decoding as the stock firmware. It counts the pattern it
 * calls "left" as +1 and "right" as -1.
 */
static bool step_up(int i)
{
    uint8_t a = hist_a[i] & 3, b = hist_b[i] & 3;
    if (half_up[i]) {
        if (a == 2 && b == 0) {
            half_up[i] = 0;
            return true;
        }
    } else if (a == 1 && b == 3) {
        half_up[i] = 1;
        return true;
    }
    return false;
}

static bool step_down(int i)
{
    uint8_t a = hist_a[i] & 3, b = hist_b[i] & 3;
    if (half_down[i]) {
        if (b == 2 && a == 0) {
            half_down[i] = 0;
            return true;
        }
    } else if (b == 1 && a == 3) {
        half_down[i] = 1;
        return true;
    }
    return false;
}

static void debounce(int bit, bool down)
{
    sw_hist[bit] = (uint8_t)((sw_hist[bit] << 1) | down);
    if (sw_hist[bit] == 0xFF) {
        pressed_mask |= 1u << bit;
    } else if (sw_hist[bit] == 0x00) {
        pressed_mask &= ~(1u << bit);
    }
}

static void scan(void)
{
    uint8_t rx[SCAN_BYTES] = {0};
    uint8_t tx[SCAN_BYTES] = {0};

    HAL_GPIO_WritePin(ENC_LATCH_PORT, ENC_LATCH_PIN, GPIO_PIN_SET);
    HAL_SPI_TransmitReceive(&spi, tx, rx, SCAN_BYTES, 5);
    HAL_GPIO_WritePin(ENC_LATCH_PORT, ENC_LATCH_PIN, GPIO_PIN_RESET);

    for (int i = 0; i < ENC_COUNT; i++) {
        uint8_t ab = rx[wiring[i].ab_byte] >> wiring[i].ab_bit;
        hist_a[i] = (uint8_t)((hist_a[i] << 1) | (ab & 1));
        hist_b[i] = (uint8_t)((hist_b[i] << 1) | ((ab >> 1) & 1));
        if (step_down(i)) {
            steps[i]--;
        } else if (step_up(i)) {
            steps[i]++;
        }
        /* Encoder switches read 0 when pressed. */
        debounce(i, !((rx[wiring[i].sw_byte] >> wiring[i].sw_bit) & 1));
    }
    debounce(SHIFT_BIT, HAL_GPIO_ReadPin(SHIFT_BTN_PORT, SHIFT_BTN_PIN) == GPIO_PIN_SET);
}

void enc_init(void)
{
    for (int i = 0; i < ENC_COUNT; i++) {
        hist_a[i] = hist_b[i] = 0xFF;
    }

    __HAL_RCC_SPI1_CLK_ENABLE();
    __HAL_RCC_TIM3_CLK_ENABLE();

    GPIO_InitTypeDef gpio = {0};
    gpio.Pin = ENC_SCK_PIN | ENC_MISO_PIN;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = GPIO_AF5_SPI1;
    HAL_GPIO_Init(ENC_SCK_PORT, &gpio);

    HAL_GPIO_WritePin(ENC_LATCH_PORT, ENC_LATCH_PIN, GPIO_PIN_RESET);
    gpio.Pin = ENC_LATCH_PIN;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    gpio.Alternate = 0;
    HAL_GPIO_Init(ENC_LATCH_PORT, &gpio);

    gpio.Pin = SHIFT_BTN_PIN;
    gpio.Mode = GPIO_MODE_INPUT;
    HAL_GPIO_Init(SHIFT_BTN_PORT, &gpio);

    spi.Instance = ENC_SPI;
    spi.Init.Mode = SPI_MODE_MASTER;
    spi.Init.Direction = SPI_DIRECTION_2LINES;
    spi.Init.DataSize = SPI_DATASIZE_8BIT;
    spi.Init.CLKPolarity = SPI_POLARITY_LOW;
    spi.Init.CLKPhase = SPI_PHASE_1EDGE;
    spi.Init.NSS = SPI_NSS_SOFT;
    spi.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_64;
    spi.Init.FirstBit = SPI_FIRSTBIT_MSB;
    spi.Init.TIMode = SPI_TIMODE_DISABLE;
    spi.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
    HAL_SPI_Init(&spi);

    /* APB1 timers run at twice PCLK1 because APB1 is divided. */
    uint32_t timer_hz = HAL_RCC_GetPCLK1Freq() * 2;
    tim.Instance = TIM3;
    tim.Init.Prescaler = timer_hz / 1000000 - 1;
    tim.Init.Period = 1000000 / SCAN_HZ - 1;
    tim.Init.CounterMode = TIM_COUNTERMODE_UP;
    tim.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    tim.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
    HAL_TIM_Base_Init(&tim);

    HAL_NVIC_SetPriority(TIM3_IRQn, 7, 0);
    HAL_NVIC_EnableIRQ(TIM3_IRQn);
    HAL_TIM_Base_Start_IT(&tim);
}

int enc_read(int index)
{
    if (index < 0 || index >= ENC_COUNT) {
        return 0;
    }
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    int value = steps[index];
    steps[index] = 0;
    __set_PRIMASK(primask);
    return value;
}

bool enc_pressed(int index)
{
    if (index < 0 || index >= ENC_COUNT) {
        return false;
    }
    return pressed_mask & (1u << index);
}

bool enc_shift_pressed(void)
{
    return pressed_mask & (1u << SHIFT_BIT);
}

void TIM3_IRQHandler(void)
{
    if (__HAL_TIM_GET_FLAG(&tim, TIM_FLAG_UPDATE)) {
        __HAL_TIM_CLEAR_FLAG(&tim, TIM_FLAG_UPDATE);
        scan();
    }
}
