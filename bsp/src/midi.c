#include "midi.h"

#include "board.h"
#include "midi_ring.h"

#define BAUD            31250
#define IRQ_PRIORITY    5

/* Outgoing bytes per output and incoming bytes per input, indexed by port. */
static midi_ring_t tx[MIDI_PORTS];
static midi_ring_t rx[MIDI_PORTS];

static void uart_init(USART_TypeDef *instance, uint32_t mode, GPIO_TypeDef *port, uint32_t pin,
                      uint8_t gpio_af, IRQn_Type irq)
{
    /* The output circuits invert, so the outputs send inverted to come out right. */
    if (mode == UART_MODE_TX) {
        GD32_USART_CTL3(instance) |= GD32_USART_CTL3_TINV;
    }

    GPIO_InitTypeDef gpio = {0};
    gpio.Pin = pin;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    gpio.Alternate = gpio_af;
    HAL_GPIO_Init(port, &gpio);

    UART_HandleTypeDef uart = {0};
    uart.Instance = instance;
    uart.Init.BaudRate = BAUD;
    uart.Init.WordLength = UART_WORDLENGTH_8B;
    uart.Init.StopBits = UART_STOPBITS_1;
    uart.Init.Parity = UART_PARITY_NONE;
    uart.Init.Mode = mode;
    uart.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    uart.Init.OverSampling = UART_OVERSAMPLING_16;
    HAL_UART_Init(&uart);

    HAL_NVIC_SetPriority(irq, IRQ_PRIORITY, 0);
    HAL_NVIC_EnableIRQ(irq);
}

void midi_init(void)
{
    __HAL_RCC_USART3_CLK_ENABLE();
    __HAL_RCC_USART6_CLK_ENABLE();
    __HAL_RCC_UART4_CLK_ENABLE();

    uart_init(MIDI_OUT1_UART, UART_MODE_TX, MIDI_OUT1_PORT, MIDI_OUT1_PIN, GPIO_AF7_USART3, USART3_IRQn);
    uart_init(MIDI_OUT2_UART, UART_MODE_TX, MIDI_OUT2_PORT, MIDI_OUT2_PIN, GPIO_AF8_USART6, USART6_IRQn);
    uart_init(MIDI_IN_UART, UART_MODE_RX, MIDI_IN_PORT, MIDI_IN_PIN, GPIO_AF8_UART4, UART4_IRQn);

    MIDI_IN_UART->CR1 |= USART_CR1_RXNEIE;

    usb_midi_init(&tx[MIDI_USB], &rx[MIDI_USB]);
}

bool midi_usb_connected(void)
{
    return usb_midi_connected();
}

bool midi_write(midi_port_t port, const uint8_t *bytes, size_t len)
{
    if (port != MIDI_TRS_1 && port != MIDI_TRS_2 && port != MIDI_USB) {
        return false;
    }
    if (port == MIDI_USB && !usb_midi_connected()) {
        return false;
    }
    midi_ring_t *ring = &tx[port];
    if (len > MIDI_RING_SIZE - midi_ring_count(ring)) {
        return false;
    }
    for (size_t i = 0; i < len; i++) {
        midi_ring_put(ring, bytes[i]);
    }
    if (port == MIDI_TRS_1) {
        MIDI_OUT1_UART->CR1 |= USART_CR1_TXEIE;
    } else if (port == MIDI_TRS_2) {
        MIDI_OUT2_UART->CR1 |= USART_CR1_TXEIE;
    }
    return true;
}

bool midi_read_byte(midi_port_t port, uint8_t *byte)
{
    if (port != MIDI_TRS_IN && port != MIDI_USB) {
        return false;
    }
    return midi_ring_get(&rx[port], byte);
}

/* ---- Message parsing ---- */

typedef struct {
    uint8_t status;         /* running status, 0 if none */
    uint8_t needed;         /* data bytes the status takes */
    uint8_t msg[3];
    uint8_t count;          /* data bytes received so far */
    uint8_t realtime;
    bool in_sysex;
    bool sysex_overflow;
    size_t sysex_len;
    uint8_t sysex[MIDI_SYSEX_MAX];
} parser_t;

static parser_t parsers[MIDI_PORTS];

static uint8_t data_bytes(uint8_t status)
{
    switch (status & 0xF0) {
    case 0xC0:
    case 0xD0:
        return 1;
    case 0xF0:
        return status == 0xF2 ? 2 : (status == 0xF1 || status == 0xF3) ? 1 : 0;
    default:
        return 2;
    }
}

static bool parse(midi_port_t port, midi_msg_t *msg)
{
    parser_t *p = &parsers[port];
    uint8_t b;
    while (midi_read_byte(port, &b)) {
        if (b >= 0xF8) {
            p->realtime = b;
            msg->data = &p->realtime;
            msg->len = 1;
            return true;
        }

        if (b == 0xF0) {
            p->status = 0;
            p->in_sysex = true;
            p->sysex_overflow = false;
            p->sysex[0] = b;
            p->sysex_len = 1;
            continue;
        }

        if (p->in_sysex) {
            if (b < 0x80 || b == 0xF7) {
                if (p->sysex_len < MIDI_SYSEX_MAX) {
                    p->sysex[p->sysex_len++] = b;
                } else {
                    p->sysex_overflow = true;
                }
                if (b == 0xF7) {
                    p->in_sysex = false;
                    if (!p->sysex_overflow) {
                        msg->data = p->sysex;
                        msg->len = p->sysex_len;
                        return true;
                    }
                }
                continue;
            }
            /* Any other status byte ends an unfinished SysEx; it is dropped. */
            p->in_sysex = false;
        }

        if (b >= 0x80) {
            p->count = 0;
            p->needed = data_bytes(b);
            p->msg[0] = b;
            /* System common messages cancel running status. */
            p->status = b < 0xF0 || p->needed > 0 ? b : 0;
            if (b >= 0xF0 && p->needed == 0) {
                if (b == 0xF6) {
                    msg->data = p->msg;
                    msg->len = 1;
                    return true;
                }
                continue; /* stray F7 or undefined F4/F5 */
            }
            continue;
        }

        if (p->status == 0) {
            continue; /* data byte without a status */
        }
        p->msg[0] = p->status;
        p->msg[1 + p->count++] = b;
        if (p->count == p->needed) {
            p->count = 0;
            if (p->status >= 0xF0) {
                p->status = 0;
            }
            msg->data = p->msg;
            msg->len = 1 + p->needed;
            return true;
        }
    }
    return false;
}

bool midi_read(midi_msg_t *msg)
{
    static const midi_port_t inputs[] = {MIDI_TRS_IN, MIDI_USB};
    for (size_t i = 0; i < sizeof(inputs) / sizeof(inputs[0]); i++) {
        if (parse(inputs[i], msg)) {
            msg->port = inputs[i];
            return true;
        }
    }
    return false;
}

/* ---- Interrupts ---- */

static void tx_irq(USART_TypeDef *uart, midi_ring_t *ring)
{
    if (!(uart->SR & USART_SR_TXE)) {
        return;
    }
    uint8_t b;
    if (!midi_ring_get(ring, &b)) {
        uart->CR1 &= ~USART_CR1_TXEIE;
        return;
    }
    uart->DR = b;
}

void USART3_IRQHandler(void)
{
    tx_irq(MIDI_OUT1_UART, &tx[MIDI_TRS_1]);
}

void USART6_IRQHandler(void)
{
    tx_irq(MIDI_OUT2_UART, &tx[MIDI_TRS_2]);
}

void UART4_IRQHandler(void)
{
    /* Reading SR then DR also clears overrun and framing errors. */
    uint32_t sr = MIDI_IN_UART->SR;
    if (sr & (USART_SR_RXNE | USART_SR_ORE)) {
        uint8_t b = (uint8_t)MIDI_IN_UART->DR;
        if (!(sr & USART_SR_FE)) {
            midi_ring_put(&rx[MIDI_TRS_IN], b);
        }
    }
}
