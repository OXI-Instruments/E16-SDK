#include "ble_update.h"

#include <stdbool.h>
#include <string.h>

#include "board.h"
#include "system.h"

#define BAUD                57600
#define BLOCK_SIZE          128     /* image bytes per message */
#define PAGE_BLOCKS         32      /* the bootloader writes flash in 4 KB pages */
#define START_TIMEOUT_MS    1000
#define REPLY_TIMEOUT_MS    1000
#define QUIET_MS            20
#define MAX_RETRIES         10

/* Messages between the main processor and the nRF bootloader. */
static const uint8_t header[] = {0xF0, 0x00, 0x21, 0x5B, 0x01, 0x01, 0x00};
#define CMD_BLOCK           0x7E    /* + 128 data bytes and their checksum, as nibbles */
#define CMD_FINISH          0x7F    /* write what's left and start the application */
#define CMD_RESTART         0x55    /* forget everything received so far */
#define REPLY_STARTED       0x51
#define REPLY_ACK           0x53
#define REPLY_NACK          0x54

static USART_TypeDef *const uart = BLE_UART;

/* The last bytes received, to spot the 9-byte replies. */
static uint8_t window[sizeof(header) + 2];
static int reply;
static uint32_t received;   /* bytes received so far */

static void receive(void)
{
    /* Reading SR then DR also clears overrun and framing errors. */
    uint32_t sr = uart->SR;
    if (!(sr & (USART_SR_RXNE | USART_SR_ORE))) {
        return;
    }
    uint8_t b = (uint8_t)uart->DR;
    received++;
    memmove(window, window + 1, sizeof(window) - 1);
    window[sizeof(window) - 1] = b;
    if (b == 0xF7 && memcmp(window, header, sizeof(header)) == 0) {
        reply = window[sizeof(header)];
    }
}

static void send_byte(uint8_t b)
{
    while (!(uart->SR & USART_SR_TXE)) {
        receive();
    }
    uart->DR = b;
}

static void send_command(uint8_t cmd)
{
    for (size_t i = 0; i < sizeof(header); i++) {
        send_byte(header[i]);
    }
    send_byte(cmd);
}

static void send_end(void)
{
    send_byte(0xF7);
    while (!(uart->SR & USART_SR_TC)) {
        receive();
    }
}

static int wait_reply(uint32_t timeout_ms)
{
    uint32_t start = system_millis();
    while (reply < 0 && system_millis() - start < timeout_ms) {
        receive();
    }
    return reply;
}

/* Let any burst of replies (the bootloader NACKs each stray byte) die down. */
static void wait_quiet(void)
{
    uint32_t last = system_millis();
    while (system_millis() - last < QUIET_MS) {
        if (uart->SR & (USART_SR_RXNE | USART_SR_ORE)) {
            receive();
            last = system_millis();
        }
    }
    reply = -1;
}

static void send_block(const uint8_t *image, size_t len, size_t offset)
{
    uint8_t block[BLOCK_SIZE];
    size_t n = len - offset < BLOCK_SIZE ? len - offset : BLOCK_SIZE;
    memcpy(block, image + offset, n);
    memset(block + n, 0xFF, BLOCK_SIZE - n);

    uint8_t checksum = 0;
    send_command(CMD_BLOCK);
    for (size_t i = 0; i <= BLOCK_SIZE; i++) {
        uint8_t b = i < BLOCK_SIZE ? block[i] : checksum;
        checksum += b;
        send_byte(b >> 4);
        send_byte(b & 0x0F);
    }
    send_end();
}

static void uart_init(void)
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

    UART_HandleTypeDef h = {0};
    h.Instance = uart;
    h.Init.BaudRate = BAUD;
    h.Init.WordLength = UART_WORDLENGTH_8B;
    h.Init.StopBits = UART_STOPBITS_1;
    h.Init.Parity = UART_PARITY_NONE;
    h.Init.Mode = UART_MODE_TX_RX;
    h.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    h.Init.OverSampling = UART_OVERSAMPLING_16;
    HAL_UART_Init(&h);
}

/* Reset the nRF; with boot high its bootloader stays in update mode. */
static void reset_nrf(bool boot)
{
    HAL_GPIO_WritePin(BLE_BOOT_PORT, BLE_BOOT_PIN, boot ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(BLE_RESET_PORT, BLE_RESET_PIN, GPIO_PIN_RESET);
    system_delay_ms(1);
    HAL_GPIO_WritePin(BLE_RESET_PORT, BLE_RESET_PIN, GPIO_PIN_SET);
}

ble_update_result_t ble_update(const uint8_t *image, size_t len, ble_update_progress_t progress)
{
    if (len > BLE_APP_MAX_SIZE) {
        return BLE_UPDATE_TOO_LARGE;
    }

    uart_init();
    reply = -1;
    reset_nrf(true);
    if (wait_reply(START_TIMEOUT_MS) != REPLY_STARTED) {
        reset_nrf(false);
        return BLE_UPDATE_NO_BOOTLOADER;
    }
    wait_quiet();

    int retries = 0;
    size_t offset = 0;
    while (offset < len) {
        send_block(image, len, offset);
        int r = wait_reply(REPLY_TIMEOUT_MS);
        if (r == REPLY_ACK) {
            offset += BLOCK_SIZE;
            reply = -1;
            if (progress) {
                progress(offset < len ? offset : len, len);
            }
            continue;
        }

        if (++retries > MAX_RETRIES) {
            reset_nrf(true);
            return BLE_UPDATE_FAILED;
        }
        wait_quiet();
        if (r == REPLY_NACK) {
            /* A refused block also drops the rest of its 4 KB page: resend the page. */
            offset -= offset % (PAGE_BLOCKS * BLOCK_SIZE);
        } else {
            /* No answer: the bootloader's position is unknown, so start over. */
            send_command(CMD_RESTART);
            send_end();
            wait_quiet();
            offset = 0;
        }
    }

    /*
     * The bootloader writes the last page, then starts its ACK and jumps to the
     * application before the ACK is fully out. So any answer means done.
     */
    uint32_t before = received;
    send_command(CMD_FINISH);
    send_end();
    uint32_t start = system_millis();
    while (received == before && system_millis() - start < REPLY_TIMEOUT_MS) {
        receive();
    }
    if (received == before) {
        reset_nrf(true);
        return BLE_UPDATE_FAILED;
    }

    /* The bootloader starts the application itself; reset so it starts the normal way. */
    reset_nrf(false);
    return BLE_UPDATE_OK;
}
