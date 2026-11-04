#include "ble_midi.h"

#include "board.h"
#include "system.h"

#define BAUD            115200
#define RING_SIZE       256     /* a power of two */
#define IRQ_PRIORITY    5

/* head and tail count bytes written and read and wrap around on their own. */
typedef struct {
    uint8_t data[RING_SIZE];
    volatile uint32_t head;
    volatile uint32_t tail;
} ring_t;

static ring_t tx;
static ring_t rx;

void ble_midi_init(void)
{
    __HAL_RCC_UART5_CLK_ENABLE();

    GPIO_InitTypeDef gpio = {0};
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    gpio.Alternate = GPIO_AF8_UART5;
    gpio.Pin = BLE_TX_PIN;
    HAL_GPIO_Init(BLE_TX_PORT, &gpio);
    gpio.Pin = BLE_RX_PIN;
    HAL_GPIO_Init(BLE_RX_PORT, &gpio);

    UART_HandleTypeDef uart = {0};
    uart.Instance = BLE_UART;
    uart.Init.BaudRate = BAUD;
    uart.Init.WordLength = UART_WORDLENGTH_8B;
    uart.Init.StopBits = UART_STOPBITS_1;
    uart.Init.Parity = UART_PARITY_NONE;
    uart.Init.Mode = UART_MODE_TX_RX;
    uart.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    uart.Init.OverSampling = UART_OVERSAMPLING_16;
    HAL_UART_Init(&uart);

    BLE_UART->CR1 |= USART_CR1_RXNEIE;
    HAL_NVIC_SetPriority(UART5_IRQn, IRQ_PRIORITY, 0);
    HAL_NVIC_EnableIRQ(UART5_IRQn);

    /* Start the Bluetooth chip's application: boot pin low, then release reset. */
    HAL_GPIO_WritePin(BLE_BOOT_PORT, BLE_BOOT_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(BLE_RESET_PORT, BLE_RESET_PIN, GPIO_PIN_RESET);
    system_delay_ms(1);
    HAL_GPIO_WritePin(BLE_RESET_PORT, BLE_RESET_PIN, GPIO_PIN_SET);
}

bool ble_midi_write(const uint8_t *bytes, size_t len)
{
    uint32_t head = tx.head;
    if (len > RING_SIZE - (head - tx.tail)) {
        return false;
    }
    for (size_t i = 0; i < len; i++) {
        tx.data[head++ & (RING_SIZE - 1)] = bytes[i];
    }
    tx.head = head;
    BLE_UART->CR1 |= USART_CR1_TXEIE;
    return true;
}

bool ble_midi_read_byte(uint8_t *byte)
{
    uint32_t tail = rx.tail;
    if (tail == rx.head) {
        return false;
    }
    *byte = rx.data[tail & (RING_SIZE - 1)];
    rx.tail = tail + 1;
    return true;
}

void UART5_IRQHandler(void)
{
    /* Reading SR then DR also clears overrun and framing errors. */
    uint32_t sr = BLE_UART->SR;
    if (sr & (USART_SR_RXNE | USART_SR_ORE)) {
        uint8_t b = (uint8_t)BLE_UART->DR;
        uint32_t head = rx.head;
        if (!(sr & USART_SR_FE) && head - rx.tail < RING_SIZE) {
            rx.data[head & (RING_SIZE - 1)] = b;
            rx.head = head + 1;
        }
    }
    if ((BLE_UART->CR1 & USART_CR1_TXEIE) && (sr & USART_SR_TXE)) {
        if (tx.tail == tx.head) {
            BLE_UART->CR1 &= ~USART_CR1_TXEIE;
        } else {
            BLE_UART->DR = tx.data[tx.tail & (RING_SIZE - 1)];
            tx.tail++;
        }
    }
}
