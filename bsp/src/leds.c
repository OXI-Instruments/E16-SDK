#include "leds.h"

#include <stddef.h>
#include <string.h>

#include "board.h"

#define CHAIN_COUNT         2
#define RINGS_PER_CHAIN     (LEDS_RINGS / CHAIN_COUNT)
#define LEDS_PER_CHAIN      (RINGS_PER_CHAIN * LEDS_PER_RING)
#define UART_BYTES_PER_LED  24

/*
 * Each WS2812 bit is one UART frame at 7.95 Mbaud with TX inverted: the start
 * bit plus the data pattern give a ~880 ns high pulse for a 1 and ~380 ns for a 0.
 */
#define WS2812_ONE          0xC0
#define WS2812_ZERO         0xFC

/* The LEDs latch after >50 us of idle line; 2 ticks guarantees at least 1 ms. */
#define LATCH_TICKS         2

typedef struct {
    UART_HandleTypeDef uart;
    DMA_HandleTypeDef dma;
    volatile bool busy;
    volatile uint32_t done_tick;
    uint8_t rgb[LEDS_PER_CHAIN][3];
    uint8_t wire[LEDS_PER_CHAIN * UART_BYTES_PER_LED];
} chain_t;

static chain_t chains[CHAIN_COUNT];

static void on_tx_done(UART_HandleTypeDef *uart)
{
    chain_t *chain = (chain_t *)((uint8_t *)uart - offsetof(chain_t, uart));
    chain->done_tick = HAL_GetTick();
    chain->busy = false;
}

static void chain_init(chain_t *chain, USART_TypeDef *instance, GPIO_TypeDef *port, uint32_t pin,
                       uint8_t gpio_af, DMA_Stream_TypeDef *stream, IRQn_Type dma_irq, IRQn_Type uart_irq)
{
    GPIO_InitTypeDef gpio = {0};
    gpio.Pin = pin;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = gpio_af;
    HAL_GPIO_Init(port, &gpio);

    GD32_USART_CTL3(instance) |= GD32_USART_CTL3_TINV;

    chain->uart.Instance = instance;
    chain->uart.Init.BaudRate = 7950000;
    chain->uart.Init.WordLength = UART_WORDLENGTH_8B;
    chain->uart.Init.StopBits = UART_STOPBITS_1;
    chain->uart.Init.Parity = UART_PARITY_NONE;
    chain->uart.Init.Mode = UART_MODE_TX;
    chain->uart.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    chain->uart.Init.OverSampling = UART_OVERSAMPLING_8;
    HAL_UART_Init(&chain->uart);
    HAL_UART_RegisterCallback(&chain->uart, HAL_UART_TX_COMPLETE_CB_ID, on_tx_done);

    chain->dma.Instance = stream;
    chain->dma.Init.Channel = DMA_CHANNEL_4;
    chain->dma.Init.Direction = DMA_MEMORY_TO_PERIPH;
    chain->dma.Init.PeriphInc = DMA_PINC_DISABLE;
    chain->dma.Init.MemInc = DMA_MINC_ENABLE;
    chain->dma.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    chain->dma.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
    chain->dma.Init.Mode = DMA_NORMAL;
    chain->dma.Init.Priority = DMA_PRIORITY_LOW;
    chain->dma.Init.FIFOMode = DMA_FIFOMODE_DISABLE;
    HAL_DMA_Init(&chain->dma);
    __HAL_LINKDMA(&chain->uart, hdmatx, chain->dma);

    HAL_NVIC_SetPriority(dma_irq, 6, 1);
    HAL_NVIC_EnableIRQ(dma_irq);
    HAL_NVIC_SetPriority(uart_irq, 5, 0);
    HAL_NVIC_EnableIRQ(uart_irq);
}

void leds_init(void)
{
    __HAL_RCC_DMA1_CLK_ENABLE();
    __HAL_RCC_DMA2_CLK_ENABLE();
    __HAL_RCC_USART1_CLK_ENABLE();
    __HAL_RCC_USART2_CLK_ENABLE();

    chain_init(&chains[0], LEDS_A_UART, LEDS_A_PORT, LEDS_A_PIN, GPIO_AF7_USART2,
               LEDS_A_DMA_STREAM, LEDS_A_DMA_IRQn, USART2_IRQn);
    chain_init(&chains[1], LEDS_B_UART, LEDS_B_PORT, LEDS_B_PIN, GPIO_AF7_USART1,
               LEDS_B_DMA_STREAM, LEDS_B_DMA_IRQn, USART1_IRQn);

    leds_clear();
}

void leds_set(int ring, int led, uint8_t r, uint8_t g, uint8_t b)
{
    if (ring < 0 || ring >= LEDS_RINGS || led < 0 || led >= LEDS_PER_RING) {
        return;
    }
    uint8_t *px = chains[ring / RINGS_PER_CHAIN].rgb[(ring % RINGS_PER_CHAIN) * LEDS_PER_RING + led];
    px[0] = r;
    px[1] = g;
    px[2] = b;
}

void leds_set_ring(int ring, uint8_t r, uint8_t g, uint8_t b)
{
    for (int led = 0; led < LEDS_PER_RING; led++) {
        leds_set(ring, led, r, g, b);
    }
}

void leds_clear(void)
{
    for (int i = 0; i < CHAIN_COUNT; i++) {
        memset(chains[i].rgb, 0, sizeof(chains[i].rgb));
    }
}

static void encode_byte(uint8_t value, uint8_t *out)
{
    for (int bit = 0; bit < 8; bit++) {
        out[bit] = (value & (0x80 >> bit)) ? WS2812_ONE : WS2812_ZERO;
    }
}

bool leds_busy(void)
{
    for (int i = 0; i < CHAIN_COUNT; i++) {
        if (chains[i].busy || HAL_GetTick() - chains[i].done_tick < LATCH_TICKS) {
            return true;
        }
    }
    return false;
}

bool leds_show(void)
{
    if (leds_busy()) {
        return false;
    }
    for (int i = 0; i < CHAIN_COUNT; i++) {
        chain_t *chain = &chains[i];
        for (int led = 0; led < LEDS_PER_CHAIN; led++) {
            uint8_t *out = &chain->wire[led * UART_BYTES_PER_LED];
            encode_byte(chain->rgb[led][1], out);
            encode_byte(chain->rgb[led][0], out + 8);
            encode_byte(chain->rgb[led][2], out + 16);
        }
        chain->busy = true;
        HAL_UART_Transmit_DMA(&chain->uart, chain->wire, sizeof(chain->wire));
    }
    return true;
}

void DMA1_Stream6_IRQHandler(void)
{
    HAL_DMA_IRQHandler(&chains[0].dma);
}

void USART2_IRQHandler(void)
{
    HAL_UART_IRQHandler(&chains[0].uart);
}

void DMA2_Stream7_IRQHandler(void)
{
    HAL_DMA_IRQHandler(&chains[1].dma);
}

void USART1_IRQHandler(void)
{
    HAL_UART_IRQHandler(&chains[1].uart);
}
