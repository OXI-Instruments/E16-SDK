#include "storage.h"

#include "board.h"
#include "system.h"

#define CMD_PAGE_PROGRAM    0x02
#define CMD_READ            0x03
#define CMD_READ_STATUS     0x05
#define CMD_WRITE_ENABLE    0x06
#define CMD_SECTOR_ERASE    0x20
#define CMD_JEDEC_ID        0x9F
#define CMD_WAKE            0xAB

#define STATUS_BUSY         0x01

#define SPI_TIMEOUT_MS      100
#define MAX_SIZE            (16u * 1024 * 1024)  /* 3-byte addresses */

static SPI_HandleTypeDef spi;
static uint32_t size;

static void cs_low(void)
{
    HAL_GPIO_WritePin(STORAGE_CS_PORT, STORAGE_CS_PIN, GPIO_PIN_RESET);
}

static void cs_high(void)
{
    HAL_GPIO_WritePin(STORAGE_CS_PORT, STORAGE_CS_PIN, GPIO_PIN_SET);
}

static void tx(const void *bytes, size_t len)
{
    HAL_SPI_Transmit(&spi, (uint8_t *)bytes, (uint16_t)len, SPI_TIMEOUT_MS);
}

static void rx(void *buf, size_t len)
{
    uint8_t *p = buf;
    while (len > 0) {
        uint16_t chunk = len > 0xFFFF ? 0xFFFF : (uint16_t)len;
        HAL_SPI_Receive(&spi, p, chunk, SPI_TIMEOUT_MS);
        p += chunk;
        len -= chunk;
    }
}

static void command(uint8_t cmd)
{
    cs_low();
    tx(&cmd, 1);
    cs_high();
}

static void command_addr(uint8_t cmd, uint32_t addr)
{
    const uint8_t bytes[] = {cmd, (uint8_t)(addr >> 16), (uint8_t)(addr >> 8), (uint8_t)addr};
    tx(bytes, sizeof(bytes));
}

static void wait_ready(void)
{
    const uint8_t cmd = CMD_READ_STATUS;
    uint8_t status;
    cs_low();
    tx(&cmd, 1);
    do {
        rx(&status, 1);
    } while (status & STATUS_BUSY);
    cs_high();
}

bool storage_init(void)
{
    __HAL_RCC_SPI2_CLK_ENABLE();

    GPIO_InitTypeDef gpio = {0};
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = GPIO_AF5_SPI2;
    gpio.Pin = STORAGE_SCK_PIN;
    HAL_GPIO_Init(STORAGE_SCK_PORT, &gpio);
    gpio.Pin = STORAGE_MISO_PIN | STORAGE_MOSI_PIN;
    HAL_GPIO_Init(STORAGE_MISO_PORT, &gpio);

    cs_high();
    gpio.Pin = STORAGE_CS_PIN;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    gpio.Alternate = 0;
    HAL_GPIO_Init(STORAGE_CS_PORT, &gpio);

    /* 21 MHz, mode 0. */
    spi.Instance = STORAGE_SPI;
    spi.Init.Mode = SPI_MODE_MASTER;
    spi.Init.Direction = SPI_DIRECTION_2LINES;
    spi.Init.DataSize = SPI_DATASIZE_8BIT;
    spi.Init.CLKPolarity = SPI_POLARITY_LOW;
    spi.Init.CLKPhase = SPI_PHASE_1EDGE;
    spi.Init.NSS = SPI_NSS_SOFT;
    spi.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_4;
    spi.Init.FirstBit = SPI_FIRSTBIT_MSB;
    spi.Init.TIMode = SPI_TIMODE_DISABLE;
    spi.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
    HAL_SPI_Init(&spi);

    /* In case the chip was left in deep power-down; it needs a moment to wake. */
    command(CMD_WAKE);
    system_delay_ms(1);
    wait_ready();

    const uint8_t cmd = CMD_JEDEC_ID;
    uint8_t id[3];
    cs_low();
    tx(&cmd, 1);
    rx(id, sizeof(id));
    cs_high();

    /* The third ID byte is the size as a power of two on common chips. */
    if (id[0] == 0x00 || id[0] == 0xFF || id[2] < 16 || id[2] > 24) {
        size = 0;
        return false;
    }
    size = 1u << id[2];
    if (size > MAX_SIZE) {
        size = MAX_SIZE;
    }
    return true;
}

uint32_t storage_size(void)
{
    return size;
}

void storage_read(uint32_t addr, void *buf, size_t len)
{
    if (len == 0) {
        return;
    }
    cs_low();
    command_addr(CMD_READ, addr);
    rx(buf, len);
    cs_high();
}

void storage_write(uint32_t addr, const void *data, size_t len)
{
    const uint8_t *p = data;
    while (len > 0) {
        /* A page program must not cross a 256-byte page boundary. */
        size_t room = STORAGE_PAGE_SIZE - (addr % STORAGE_PAGE_SIZE);
        size_t chunk = len < room ? len : room;

        command(CMD_WRITE_ENABLE);
        cs_low();
        command_addr(CMD_PAGE_PROGRAM, addr);
        tx(p, chunk);
        cs_high();
        wait_ready();

        addr += chunk;
        p += chunk;
        len -= chunk;
    }
}

void storage_erase(uint32_t addr, size_t len)
{
    if (len == 0) {
        return;
    }
    uint32_t first = addr / STORAGE_SECTOR_SIZE;
    uint32_t last = (uint32_t)((addr + len - 1) / STORAGE_SECTOR_SIZE);
    for (uint32_t sector = first; sector <= last; sector++) {
        command(CMD_WRITE_ENABLE);
        cs_low();
        command_addr(CMD_SECTOR_ERASE, sector * STORAGE_SECTOR_SIZE);
        cs_high();
        wait_ready();
    }
}
