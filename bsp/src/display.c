#include "display.h"

#include "board.h"
#include "system.h"

#define SPI_TIMEOUT_MS      10
#define DEFAULT_CONTRAST    0x8F

/* Sent once after reset. */
static const uint8_t init_commands[] = {
    0xAE,       /* display off */
    0xD5, 0xF0, /* fastest display clock: slower settings whine audibly */
    0xA8, 0x3F, /* 64 rows */
    0xD3, 0x00, /* no vertical offset */
    0x40,       /* first row is RAM row 0 */
    0xA1,       /* mirror columns and */
    0xC8,       /* scan rows in reverse, which puts page 0, column 0 top left */
    0xDA, 0x12, /* COM pin layout of this panel */
    0x81, DEFAULT_CONTRAST,
    0xD9, 0x22, /* pre-charge period */
    0xDB, 0x35, /* VCOMH level */
    0xA6,       /* not inverted */
};

static SPI_HandleTypeDef spi;

static void send(GPIO_PinState dc, const uint8_t *bytes, size_t len)
{
    /* DC low = command, high = data. HAL_SPI_Transmit waits until the last bit is out. */
    HAL_GPIO_WritePin(OLED_DC_PORT, OLED_DC_PIN, dc);
    HAL_SPI_Transmit(&spi, (uint8_t *)bytes, (uint16_t)len, SPI_TIMEOUT_MS);
}

void display_command(const uint8_t *bytes, size_t len)
{
    send(GPIO_PIN_RESET, bytes, len);
}

void display_write(int page, int column, const uint8_t *data, size_t len)
{
    if (page < 0 || page >= DISPLAY_PAGES || column < 0 || column >= DISPLAY_WIDTH) {
        return;
    }
    if (len > (size_t)(DISPLAY_WIDTH - column)) {
        len = (size_t)(DISPLAY_WIDTH - column);
    }
    const uint8_t address[] = {
        (uint8_t)(0xB0 | page),
        (uint8_t)(0x00 | (column & 0x0F)),
        (uint8_t)(0x10 | (column >> 4)),
    };
    send(GPIO_PIN_RESET, address, sizeof(address));
    send(GPIO_PIN_SET, data, len);
}

void display_contrast(uint8_t level)
{
    const uint8_t cmd[] = {0x81, level};
    display_command(cmd, sizeof(cmd));
}

void display_init(void)
{
    __HAL_RCC_SPI3_CLK_ENABLE();

    GPIO_InitTypeDef gpio = {0};
    gpio.Pin = OLED_SCK_PIN | OLED_MOSI_PIN;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = GPIO_AF6_SPI3;
    HAL_GPIO_Init(OLED_SCK_PORT, &gpio);

    HAL_GPIO_WritePin(OLED_RES_PORT, OLED_RES_PIN, GPIO_PIN_RESET);
    gpio.Pin = OLED_DC_PIN | OLED_RES_PIN;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    gpio.Alternate = 0;
    HAL_GPIO_Init(OLED_DC_PORT, &gpio);

    /* 21 MHz, transmit only. The panel has no chip select. */
    spi.Instance = OLED_SPI;
    spi.Init.Mode = SPI_MODE_MASTER;
    spi.Init.Direction = SPI_DIRECTION_1LINE;
    spi.Init.DataSize = SPI_DATASIZE_8BIT;
    spi.Init.CLKPolarity = SPI_POLARITY_LOW;
    spi.Init.CLKPhase = SPI_PHASE_1EDGE;
    spi.Init.NSS = SPI_NSS_SOFT;
    spi.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_4;
    spi.Init.FirstBit = SPI_FIRSTBIT_MSB;
    spi.Init.TIMode = SPI_TIMODE_DISABLE;
    spi.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
    HAL_SPI_Init(&spi);

    system_delay_ms(1);
    HAL_GPIO_WritePin(OLED_RES_PORT, OLED_RES_PIN, GPIO_PIN_SET);
    system_delay_ms(1);

    display_command(init_commands, sizeof(init_commands));

    /* Clear the display memory before switching on, so no noise shows. */
    static const uint8_t blank[DISPLAY_WIDTH];
    for (int page = 0; page < DISPLAY_PAGES; page++) {
        display_write(page, 0, blank, sizeof(blank));
    }

    const uint8_t on = 0xAF;
    display_command(&on, 1);
}
