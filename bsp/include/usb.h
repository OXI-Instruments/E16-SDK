#ifndef USB_H
#define USB_H

#include <stdbool.h>
#include <stdint.h>

#include "tusb.h"
#include "device/usbd_pvt.h" /* usbd_class_driver_t */

/*
 * USB functions of your own next to MIDI, e.g. a drive, an audio input or a
 * webcam (examples/usb_disk, usb_tone and usb_cam). midi_init() starts USB;
 * functions added before it appear next to the MIDI interface.
 *
 * A function brings its own TinyUSB class driver: compile TinyUSB's class
 * source (in ${E16_TINYUSB_DIR}/class) into your firmware and switch the
 * class on in a tusb_config_app.h on your firmware's include path, which the
 * SDK's tusb_config.h includes if it exists. See the examples' CMakeLists.
 *
 * The E16's USB core has endpoint 0 plus three endpoints in each direction.
 * MIDI uses EP1 (0x01 and 0x81); EP2 and EP3 are free for your functions.
 * Its packet memory (FIFO RAM) is 320 words of 4 bytes. TinyUSB lays it out
 * when the computer configures the E16, in words:
 *     RX FIFO, shared by all OUT endpoints     56
 *     EP0 IN                                   16
 *     MIDI IN                                  16
 *     each further IN endpoint                 its packet size / 4
 * so IN endpoints of your own have 232 words (928 bytes) between them. An
 * endpoint that doesn't fit fails to open, and the function with it.
 *
 * Everything here runs in the USB interrupts: the USB core's (OTG_FS) and
 * the USB service interrupt (TIM7), both at NVIC priority 6, which also run
 * TinyUSB's class driver callbacks. While they run, USB MIDI, the LED DMA
 * (also priority 6) and the encoders (priority 7) wait, so keep callbacks
 * short. From the main loop, call TinyUSB only between usb_lock() and
 * usb_unlock().
 */

typedef struct {
    /*
     * Write the function's interface descriptors to desc, numbering its
     * interfaces from first_itf, and return their length. desc is NULL when
     * only the length is asked for. Returning 0 leaves the function out of
     * this enumeration. Called when USB starts and on each usb_reconnect(),
     * while the E16 is detached from the computer.
     */
    uint16_t (*descriptor)(uint8_t *desc, uint8_t first_itf);
    uint8_t interfaces;     /* how many interfaces the descriptor numbers */
    bool iad;               /* the descriptor groups them with an interface association descriptor */

    const usbd_class_driver_t *driver;  /* TinyUSB class driver for the interfaces */

    void (*service)(void);              /* every millisecond in the service interrupt, or NULL */
    void (*mounted)(bool mounted);      /* the computer configured or dropped the E16, or NULL */
} usb_function_t;

#define USB_FUNCTIONS_MAX 2

/* Add a function. Call before midi_init(). False if there are already USB_FUNCTIONS_MAX. */
bool usb_add_function(const usb_function_t *function);

/*
 * Re-enumerate: detach from the computer (as if unplugged, for about half a
 * second; USB MIDI stops too and anything sent meanwhile is dropped), ask
 * each function for its descriptors again, and attach. Lets a firmware
 * change its set of functions at run time. Returns at once.
 */
void usb_reconnect(void);

/* True from usb_reconnect() until the E16 is attached again. */
bool usb_reconnecting(void);

/* Keep the USB interrupts out while the main loop calls TinyUSB. */
void usb_lock(void);
void usb_unlock(void);

#endif
