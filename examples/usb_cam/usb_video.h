#ifndef USB_VIDEO_H
#define USB_VIDEO_H

#include <stdbool.h>
#include <stdint.h>

/*
 * USB webcam (UVC) next to USB MIDI, as a USB function of the SDK's usb.h
 * (by Charles Vestal). The E16 appears to the computer as a USB MIDI device
 * and a USB camera streaming uncompressed YUY2 video. Copy this file,
 * usb_video.c and tusb_config_app.h into your firmware, and compile TinyUSB's
 * class/video/video_device.c with it (see examples/CMakeLists.txt).
 *
 *     static const usb_video_ops_t cam = {
 *         .width = 160, .height = 120, .fps = 15,
 *         .frame = cam_frame, .line = cam_line,
 *     };
 *     usb_video_start(&cam);   // before midi_init()
 *
 * There's no frame buffer: the SDK asks for the picture one line at a time,
 * just before that line goes out over USB, so a 160x120 stream needs only a
 * 320-byte line buffer.
 *
 * Video needs all the packet memory left next to MIDI (see usb.h), so it
 * can't share the USB connection with another function of usb.h, e.g. the
 * drive of examples/usb_disk (which would also want EP2). A firmware that
 * wants both can switch the camera off and on at run time, see
 * usb_video_select() below, and make its other function's descriptor()
 * return 0 while the camera is on.
 *
 * Bandwidth: video goes out as one isochronous packet per millisecond, at
 * most USB_VIDEO_MAX_PACKET bytes (2 of them header), so at most 926 000
 * bytes of picture per second. YUY2 takes 2 bytes per pixel:
 *     width * height * 2 * fps <= 926 000
 * e.g. 160x120 at 15 fps (576 KB/s), 128x96 at 30 fps (737 KB/s), or
 * 160x120 at 20 fps (768 KB/s). Asking for more just runs slower than fps:
 * measured on a Mac, 160x120 at 24 fps delivers 23.5.
 *
 * Endpoint: EP2 IN.
 *
 * Thread context: frame and line run in the USB service interrupt (TIM7,
 * NVIC priority 6, see usb.h).
 * Every millisecond while the computer is capturing, line is called for
 * about one packet's worth of lines (2-4 lines at 160x120), so it must be
 * fast: a few hundred cycles per pixel pair at most. Precompute tables in
 * the main loop and keep line to lookups and adds. While it runs, USB MIDI,
 * the LED DMA (also priority 6) and the encoders (priority 7) wait.
 * Share state with the main loop the way you would with any interrupt
 * handler; frame() is the natural place to take a consistent copy of the
 * parameters the main loop changes.
 */

#define USB_VIDEO_MAX_PACKET    928  /* bytes; what's left of the USB core's packet memory, see usb.h */
#define USB_VIDEO_MAX_WIDTH     320

typedef struct {
    uint16_t width;     /* pixels, even, at most USB_VIDEO_MAX_WIDTH */
    uint16_t height;    /* pixels */
    uint8_t fps;        /* frames per second the computer is offered, 1-30 */

    /*
     * A new frame starts; number counts frames since power-on. Called before
     * the frame's first line. NULL if not needed.
     */
    void (*frame)(uint32_t number);

    /*
     * Fill line y (0 is the top) of the current frame: width pixels as YUY2,
     * width * 2 bytes, 4-byte aligned. Each pair of pixels is 4 bytes:
     * Y0 U Y1 V, where Y0 and Y1 are the two pixels' brightness (16-235)
     * and U and V the colour they share (16-240, 128 is grey). Lines are
     * asked for in order, each once per frame.
     */
    void (*line)(uint16_t y, uint8_t *yuy2);
} usb_video_ops_t;

/*
 * Add the webcam with ops as its source, or with none (NULL) to start with
 * the camera off. Call before midi_init(); ops must stay valid.
 * usb_video_select() replaces the source at run time. False if USB has no
 * room for another function.
 */
bool usb_video_start(const usb_video_ops_t *ops);

/*
 * Choosing at run time, for a firmware that is a webcam only some of the
 * time (a "webcam mode"): makes ops the video source, or none with NULL,
 * and re-enumerates. The E16 detaches from the computer (as if unplugged,
 * for about half a second: USB MIDI stops too, and anything sent meanwhile
 * is dropped), then attaches again with the new set of interfaces: MIDI +
 * video with a source, or MIDI plus whatever other optional functions the
 * firmware defines without one. ops must stay valid until another call
 * replaces it.
 *
 * To the computer a switch is an unplug: if it has the E16's mass storage
 * mounted, it sees a surprise removal (macOS warns "Disk Not Ejected
 * Properly"; unwritten data may be lost). Switch only with the medium
 * not in use, e.g. not while a firmware's disk mode is active.
 *
 * Returns at once; usb_video_switching() is true until the E16 is
 * attached again. From the moment it returns, the previous source's
 * frame() and line() are never called again, so its memory may be reused
 * straight away (the rest of a frame under way goes out black).
 *
 * Main loop only. An unusable ops (odd or too wide a width, no line(), fps
 * out of 1-30) counts as NULL.
 */
void usb_video_select(const usb_video_ops_t *ops);

/*
 * True from usb_video_select() until the E16 attaches to the computer
 * again (its D+ pull-up back on). The computer still has to enumerate and
 * configure it after that, typically a few hundred milliseconds more:
 * midi_usb_connected() (midi.h) says when it's configured, and
 * usb_video_streaming() when an app opens the camera.
 */
bool usb_video_switching(void);

/*
 * True while the E16's USB configuration is MIDI + video (and so has no
 * other function of usb.h), not switching.
 */
bool usb_video_active(void);

/* True while the computer is capturing (an app has the camera open). */
bool usb_video_streaming(void);

/* Frames sent completely since power-on. */
uint32_t usb_video_frames(void);

#endif
