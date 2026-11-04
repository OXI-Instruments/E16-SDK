#ifndef USB_AUDIO_H
#define USB_AUDIO_H

#include <stdbool.h>
#include <stdint.h>

/*
 * USB audio source next to USB MIDI, as a USB function of the SDK's usb.h
 * (by Charles Vestal): the E16 shows up on the computer as an audio input
 * ("USB sound card" with one mono or stereo input), e.g. as "E16 Custom" in
 * macOS's Audio MIDI Setup. 16-bit PCM, USB Audio Class 2.0, isochronous IN
 * endpoint, asynchronous. Copy this file, usb_audio.c and tusb_config_app.h
 * into your firmware, and compile TinyUSB's class/audio/audio_device.c with
 * it (see examples/CMakeLists.txt).
 *
 *     static uint16_t fill(int16_t *buf, uint16_t max_frames) { ... }
 *     static const usb_audio_ops_t audio = {
 *         .sample_rate = 48000, .channels = 1, .fill = fill,
 *     };
 *     usb_audio_start(&audio);   // before midi_init()
 *
 * How samples move: while the computer records (it selects the streaming
 * interface's alternate setting 1), the SDK calls fill() once per USB frame,
 * i.e. every millisecond, from the USB interrupt (OTG_FS, NVIC priority 6,
 * same as the USB service interrupt, see usb.h). fill() copies up to
 * max_frames frames (one int16_t per channel, interleaved L R L R for
 * stereo) out of the firmware's own buffer and returns how many it wrote.
 * Those frames go out in the next USB frame. Return 0 when nothing is ready:
 * the SDK then sends a packet of silence (sample_rate / 1000 frames) instead.
 *
 * Clock: the E16 is asynchronous, i.e. its samples come from its own crystal
 * (typically a timer interrupt), not from the USB frame clock. The two drift
 * apart by a few samples per second. The host copes with that by measuring
 * how many frames arrive per packet and resampling, which is why fill() may
 * return a different count each time. A firmware producing samples at
 * sample_rate keeps a ring buffer about half full (a few milliseconds) and
 * returns what it has, up to max_frames: normally 48 at 48 kHz, 49 when the
 * ring is running full, 47 when it is running low. max_frames is
 * sample_rate / 1000 rounded up, plus 1. An asynchronous IN endpoint needs no
 * feedback endpoint: the packet sizes themselves tell the host the rate
 * (tested with macOS's UAC2 driver).
 *
 * Thread context: fill() runs in an interrupt and must be short (copying from
 * a ring is fine; rendering 48 frames of a heavy synth is not). While it runs,
 * USB MIDI, the LED DMA (also priority 6) and the encoders (priority 7) wait.
 * Fill the ring from a higher-priority sample timer (lower number, e.g. 2) or
 * from the main loop, with a lock-free single-producer/single-consumer ring.
 *
 * Endpoints: EP3 IN, so it fits next to MIDI (EP1) and a drive (EP2, see
 * examples/usb_disk).
 */

typedef struct {
    uint32_t sample_rate;   /* Hz; 48000 (8000-48000 work, only 48000 is tested) */
    uint8_t channels;       /* 1 (mono) or 2 (stereo) */

    /*
     * Copy up to max_frames frames (channels samples each, interleaved) into
     * buf and return how many. Called every millisecond from the USB
     * interrupt while the host records; 0 sends silence.
     */
    uint16_t (*fill)(int16_t *buf, uint16_t max_frames);
} usb_audio_ops_t;

/* Add the audio input. Call before midi_init(); ops must stay valid. False if unusable or no room. */
bool usb_audio_start(const usb_audio_ops_t *ops);

/* True while the computer records, i.e. while fill() is being called. */
bool usb_audio_streaming(void);

#endif
