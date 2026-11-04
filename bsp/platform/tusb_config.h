#ifndef TUSB_CONFIG_H
#define TUSB_CONFIG_H

/* TinyUSB settings for the E16: full-speed device on the USB OTG FS core, MIDI only. */

#define CFG_TUSB_MCU                OPT_MCU_STM32F4
#define CFG_TUSB_OS                 OPT_OS_NONE
#define CFG_TUSB_DEBUG              0
#define CFG_TUSB_MEM_ALIGN          __attribute__((aligned(4)))

#define CFG_TUD_ENABLED             1
#define CFG_TUD_MAX_SPEED           OPT_MODE_FULL_SPEED
#define CFG_TUD_ENDPOINT0_SIZE      64
#define CFG_TUD_TASK_QUEUE_SZ       32

/* VBUS isn't wired to the USB core; PA9 drives the LEDs instead. */
#define CFG_TUD_VBUS_DETECT_HW      0

#define CFG_TUD_MIDI                1
#define CFG_TUD_MIDI_RX_BUFSIZE     64
#define CFG_TUD_MIDI_TX_BUFSIZE     64

/*
 * A firmware that adds USB functions of its own (usb.h) switches their
 * TinyUSB classes on in a tusb_config_app.h on its include path. The SDK's
 * own files are built without it.
 */
#if __has_include("tusb_config_app.h")
#include "tusb_config_app.h"
#endif

#endif
