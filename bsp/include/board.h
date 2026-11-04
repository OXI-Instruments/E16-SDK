#ifndef BOARD_H
#define BOARD_H

#include "stm32f4xx_hal.h"

/*
 * E16 hardware map.
 *
 * MCU: GigaDevice GD32F405RG (STM32F405RG compatible), 8 MHz crystal, 168 MHz.
 * Pins marked "unverified" are taken from the firmware but not yet confirmed
 * against a schematic.
 */

/* Encoders and buttons: shift registers read over SPI1 (receive only). */
#define ENC_SPI                 SPI1
#define ENC_SCK_PORT            GPIOA
#define ENC_SCK_PIN             GPIO_PIN_5
#define ENC_MISO_PORT           GPIOA
#define ENC_MISO_PIN            GPIO_PIN_6
#define ENC_LATCH_PORT          GPIOA
#define ENC_LATCH_PIN           GPIO_PIN_0

#define SHIFT_BTN_PORT          GPIOC
#define SHIFT_BTN_PIN           GPIO_PIN_5

/* LED rings: two WS2812 chains, each driven by an inverted UART TX at 7.95 Mbaud. */
#define LEDS_A_UART             USART2  /* rings 0-7 */
#define LEDS_A_PORT             GPIOA
#define LEDS_A_PIN              GPIO_PIN_2
#define LEDS_A_DMA_STREAM       DMA1_Stream6
#define LEDS_A_DMA_IRQn         DMA1_Stream6_IRQn

#define LEDS_B_UART             USART1  /* rings 8-15 */
#define LEDS_B_PORT             GPIOA
#define LEDS_B_PIN              GPIO_PIN_9
#define LEDS_B_DMA_STREAM       DMA2_Stream7
#define LEDS_B_DMA_IRQn         DMA2_Stream7_IRQn

/* OLED (SSD1306 compatible, 128x64) on SPI3, transmit only, no chip select. */
#define OLED_SPI                SPI3
#define OLED_SCK_PORT           GPIOB
#define OLED_SCK_PIN            GPIO_PIN_3
#define OLED_MOSI_PORT          GPIOB
#define OLED_MOSI_PIN           GPIO_PIN_5
#define OLED_DC_PORT            GPIOB
#define OLED_DC_PIN             GPIO_PIN_6
#define OLED_RES_PORT           GPIOB
#define OLED_RES_PIN            GPIO_PIN_8

/* SPI NOR flash for user data (256 B pages, 4 KB sectors) on SPI2. */
#define STORAGE_SPI             SPI2
#define STORAGE_SCK_PORT        GPIOB
#define STORAGE_SCK_PIN         GPIO_PIN_13
#define STORAGE_MISO_PORT       GPIOC
#define STORAGE_MISO_PIN        GPIO_PIN_2
#define STORAGE_MOSI_PORT       GPIOC
#define STORAGE_MOSI_PIN        GPIO_PIN_3
#define STORAGE_CS_PORT         GPIOC
#define STORAGE_CS_PIN          GPIO_PIN_4

/* MIDI over TRS, 31250 baud. The outputs need TX inverted, see below. */
#define MIDI_OUT1_UART          USART3
#define MIDI_OUT1_PORT          GPIOB
#define MIDI_OUT1_PIN           GPIO_PIN_10
#define MIDI_OUT2_UART          USART6
#define MIDI_OUT2_PORT          GPIOC
#define MIDI_OUT2_PIN           GPIO_PIN_6
#define MIDI_IN_UART            UART4
#define MIDI_IN_PORT            GPIOA
#define MIDI_IN_PIN             GPIO_PIN_1

/* USB OTG FS. VBUS is not connected to the USB core. */
#define USB_PORT                GPIOA
#define USB_DM_PIN              GPIO_PIN_11
#define USB_DP_PIN              GPIO_PIN_12

/* Bluetooth chip (nRF52832) on UART5. Held in reset by the SDK until used. */
#define BLE_UART                UART5
#define BLE_TX_PORT             GPIOC
#define BLE_TX_PIN              GPIO_PIN_12
#define BLE_RX_PORT             GPIOD
#define BLE_RX_PIN              GPIO_PIN_2
#define BLE_RESET_PORT          GPIOB
#define BLE_RESET_PIN           GPIO_PIN_4
#define BLE_BOOT_PORT           GPIOC
#define BLE_BOOT_PIN            GPIO_PIN_9

/* Power. */
#define PWR_10V_EN_PORT         GPIOB   /* 10 V DC-DC enable, high = on */
#define PWR_10V_EN_PIN          GPIO_PIN_12
#define PWR_OFF_PORT            GPIOC   /* high = power off (unverified) */
#define PWR_OFF_PIN             GPIO_PIN_0

/* Haptic motor (unused by the stock firmware, unverified). */
#define MOTOR_PORT              GPIOC
#define MOTOR_PIN               GPIO_PIN_15

/*
 * GD32 only: USART_CTL3, bit 9 inverts the TX pin. Not present on STM32F405.
 * The LED chains and both MIDI outputs need it.
 */
#define GD32_USART_CTL3(uart)   (*(volatile uint32_t *)((uint32_t)(uart) + 0x80U))
#define GD32_USART_CTL3_TINV    (1U << 9)

/* Bootloader handshake. */
#define APP_FLASH_BASE          0x08004000UL
#define BOOTLOADER_MAGIC        0xDEADBEEFUL

#endif
