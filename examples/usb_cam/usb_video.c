/*
 * USB webcam: a UVC function of usb.h streaming the firmware's video source
 * as uncompressed YUY2 over an isochronous IN endpoint. It runs in the USB
 * service interrupt (TIM7), see usb.h. Without a video source its descriptor
 * is empty, so the interfaces aren't in the configuration and the host never
 * talks to this code.
 *
 * The source is the one given to usb_video_start(), until the firmware picks
 * another (or none) at run time with usb_video_select(): the old one is
 * dropped at once, and usb.c detaches from the computer, rebuilds the
 * descriptors around the new choice and attaches again (usb_reconnect()).
 *
 * No frame buffer: TinyUSB asks for each packet's bytes as it goes
 * (tud_video_prepare_payload_cb), and they're copied out of one line,
 * rendered by the firmware's line() when the packet reaches it.
 *
 * Isochronous, not bulk: UVC cameras are isochronous almost without
 * exception and every host's UVC driver handles that; an isochronous
 * endpoint gets its bandwidth reserved, and one packet per millisecond fits
 * the USB core's packet memory in one piece, so the interrupt writes it
 * straight into the FIFO without refills. Bulk on this core would be limited
 * to 64-byte packets, each needing its own FIFO refill, and gets only the
 * bandwidth left over by everything else on the bus.
 *
 * Packet memory (FIFO RAM) on the GD32F405's full-speed core: 320 words of
 * 4 bytes. TinyUSB's DWC2 driver lays it out as
 *     RX FIFO (all OUT endpoints)  56 words  13 + 1 + 2 * (64 / 4 + 1) + 2 * 4
 *     EP0 IN                       16 words  64 bytes
 *     MIDI IN (EP1)                16 words  64 bytes
 *     video IN (EP2)              <= 232 words, the rest: 928 bytes
 * so the video packet, header included, is at most 928 bytes
 * (USB_VIDEO_MAX_PACKET). Its size is what TinyUSB will ask the host for,
 * frame bytes / frame interval in ms, + 2 bytes of header; at 160x120, 15 fps
 * that's 584 bytes (146 words).
 */
#include "usb_video.h"

#include <string.h>

#include "board.h"
#include "usb.h"

#if CFG_TUD_VIDEO_STREAMING_EP_BUFSIZE != USB_VIDEO_MAX_PACKET
#error "tusb_config_app.h and usb_video.h disagree about the video packet size"
#endif

#define EP_IN           0x82

/* Bytes descriptor() writes: the IAD, video control and video streaming interfaces. */
#define DESC_LEN        159

#define ENTITY_CAMERA   1
#define ENTITY_OUTPUT   2

static uint8_t line_buf[USB_VIDEO_MAX_WIDTH * 2] __attribute__((aligned(4)));
static int32_t line_y = -1;           /* the line in line_buf, -1 if none */
static uint32_t frame_number;
static volatile uint32_t frames_sent;

/*
 * The video source in use, and the run-time choice (usb_video_select()).
 * Until a firmware selects one, the source is the one given to usb_video_start().
 * select() sets `selected` and `chosen` together, with the USB interrupts
 * held off, and chosen stays NULL (no picture asked for) until usb.c has
 * detached from the computer and calls usb_video_apply(), which makes the
 * pending source the one in use for the next enumeration.
 */
static const usb_video_ops_t *initial;
static const usb_video_ops_t *volatile chosen;
static const usb_video_ops_t *volatile pending;
static volatile bool selected;

static const usb_video_ops_t *source(void)
{
    return selected ? chosen : initial;
}

static bool usable(const usb_video_ops_t *v)
{
    return v && v->line && v->width >= 2 && v->width <= USB_VIDEO_MAX_WIDTH && !(v->width & 1) &&
           v->height > 0 && v->fps > 0 && v->fps <= 30;
}

static bool usb_video_enabled(void)
{
    return usable(source());
}

void usb_video_select(const usb_video_ops_t *ops)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    selected = true;
    chosen = NULL; /* from here on, the old source's frame() and line() are never called again */
    pending = usable(ops) ? ops : NULL;
    __set_PRIMASK(primask);
    usb_reconnect();
}

/* While detached for a reconnect: the pending choice becomes the source the descriptors use. */
static void apply(void)
{
    if (selected) {
        chosen = pending;
    }
}

bool usb_video_active(void)
{
    return usb_video_enabled() && !usb_reconnecting();
}

bool usb_video_switching(void)
{
    return usb_reconnecting();
}

static uint32_t frame_bytes(void)
{
    return (uint32_t)source()->width * source()->height * 2;
}

static uint32_t frame_interval(void)
{
    return 10000000u / source()->fps; /* in 100 ns units */
}

/* Mirrors what TinyUSB negotiates with the host (video_device.c, _update_streaming_parameters). */
static uint16_t packet_size(void)
{
    uint32_t interval_ms = frame_interval() / 10000;
    uint32_t size = (frame_bytes() + interval_ms - 1) / interval_ms + 2;
    return (uint16_t)(size < USB_VIDEO_MAX_PACKET ? size : USB_VIDEO_MAX_PACKET);
}

#define PUT16(v)    (uint8_t)(v), (uint8_t)((v) >> 8)
#define PUT32(v)    PUT16(v), PUT16((v) >> 16)

static uint16_t descriptor(uint8_t *buf, uint8_t itf)
{
    apply();
    if (!usb_video_enabled()) {
        return 0;
    }
    if (!buf) {
        return DESC_LEN;
    }
    const uint8_t ep_in = EP_IN;
    const usb_video_ops_t *v = source();
    const uint16_t w = v->width;
    const uint16_t h = v->height;
    const uint32_t bits = frame_bytes() * 8;
    const uint32_t interval = frame_interval();
    const uint8_t vs = (uint8_t)(itf + 1);

    const uint8_t desc[] = {
        TUD_VIDEO_DESC_IAD(itf, 2, 0),
        TUD_VIDEO_DESC_STD_VC(itf, 0, 0),
        TUD_VIDEO_DESC_CS_VC(VIDEO_BCD_1_50,
                             TUD_VIDEO_DESC_CAMERA_TERM_LEN + TUD_VIDEO_DESC_OUTPUT_TERM_LEN,
                             27000000, vs),
        TUD_VIDEO_DESC_CAMERA_TERM(ENTITY_CAMERA, 0, 0, 0, 0, 0, 0),
        TUD_VIDEO_DESC_OUTPUT_TERM(ENTITY_OUTPUT, VIDEO_TT_STREAMING, 0, ENTITY_CAMERA, 0),

        /* Alternate setting 0: no bandwidth, while nobody is capturing. */
        TUD_VIDEO_DESC_STD_VS(vs, 0, 0, 0),
        TUD_VIDEO_DESC_CS_VS_INPUT(1,
                                   TUD_VIDEO_DESC_CS_VS_FMT_UNCOMPR_LEN + TUD_VIDEO_DESC_CS_VS_FRM_UNCOMPR_DISC_LEN + 4 +
                                       TUD_VIDEO_DESC_CS_VS_COLOR_MATCHING_LEN,
                                   ep_in, 0, ENTITY_OUTPUT, 0, 0, 0, 0),
        TUD_VIDEO_DESC_CS_VS_FMT_UNCOMPR(1, 1, TUD_VIDEO_GUID_YUY2, 16, 1, 0, 0, 0, 0),
        /* One frame size, one frame interval (TinyUSB's DISC macro can't take 32-bit intervals). */
        TUD_VIDEO_DESC_CS_VS_FRM_UNCOMPR_DISC_LEN + 4, TUSB_DESC_CS_INTERFACE, VIDEO_CS_ITF_VS_FRAME_UNCOMPRESSED,
        1, 0, PUT16(w), PUT16(h),
        PUT32(bits * v->fps), PUT32(bits * v->fps), /* min, max bit rate */
        PUT32(frame_bytes()), PUT32(interval), 1, PUT32(interval),
        TUD_VIDEO_DESC_CS_VS_COLOR_MATCHING(VIDEO_COLOR_PRIMARIES_BT709, VIDEO_COLOR_XFER_CH_BT709,
                                            VIDEO_COLOR_COEF_SMPTE170M),

        /* Alternate setting 1: streaming. */
        TUD_VIDEO_DESC_STD_VS(vs, 1, 1, 0),
        TUD_VIDEO_DESC_EP_ISO(ep_in, packet_size(), 1),
    };
    _Static_assert(sizeof(desc) == DESC_LEN, "DESC_LEN is wrong");
    memcpy(buf, desc, sizeof(desc));
    return DESC_LEN;
}

/* ---- Streaming ---- */

/* Does nothing unless there's a source, the host is capturing and no frame is under way. */
static void start_frame(void)
{
    if (usb_video_enabled() && tud_video_n_streaming(0, 0)) {
        tud_video_n_frame_xfer(0, 0, NULL, frame_bytes()); /* NULL: ask for the bytes packet by packet */
    }
}

static void service(void)
{
    start_frame();
}

/* TinyUSB wants the next packet's picture bytes; offset counts from the frame's start. */
void tud_video_prepare_payload_cb(uint_fast8_t ctl_idx, uint_fast8_t stm_idx, tud_video_payload_request_t *request)
{
    (void)ctl_idx;
    (void)stm_idx;
    const usb_video_ops_t *v = source();
    uint8_t *dst = request->buf;
    uint32_t offset = request->offset;
    uint32_t len = request->length;

    if (!v) {
        /* Deselected mid-frame (usb_video_select()): black until the E16 detaches. */
        for (uint32_t i = 0; i < len; i++) {
            dst[i] = ((offset + i) & 1) ? 128 : 16;
        }
        return;
    }
    const uint32_t line_bytes = (uint32_t)v->width * 2;

    if (offset == 0) {
        line_y = -1;
        if (v->frame) {
            v->frame(frame_number);
        }
        frame_number++;
    }

    while (len > 0) {
        uint32_t y = offset / line_bytes;
        uint32_t x = offset % line_bytes;
        if ((int32_t)y != line_y) {
            v->line((uint16_t)y, line_buf);
            line_y = (int32_t)y;
        }
        uint32_t n = line_bytes - x;
        if (n > len) {
            n = len;
        }
        memcpy(dst, &line_buf[x], n);
        dst += n;
        offset += n;
        len -= n;
    }
}

void tud_video_frame_xfer_complete_cb(uint_fast8_t ctl_idx, uint_fast8_t stm_idx)
{
    (void)ctl_idx;
    (void)stm_idx;
    frames_sent++;
    start_frame(); /* straight on with the next one, so frames follow back to back */
}

bool usb_video_streaming(void)
{
    return usb_video_enabled() && tud_video_n_streaming(0, 0);
}

uint32_t usb_video_frames(void)
{
    return frames_sent;
}

/* ---- Hooking into usb.h ---- */

static const usbd_class_driver_t driver = {
    .name = "VIDEO",
    .init = videod_init,
    .deinit = videod_deinit,
    .reset = videod_reset,
    .open = videod_open,
    .control_xfer_cb = videod_control_xfer_cb,
    .xfer_cb = videod_xfer_cb,
};

static const usb_function_t function = {
    .descriptor = descriptor,
    .interfaces = 2,
    .iad = true,
    .driver = &driver,
    .service = service,
};

bool usb_video_start(const usb_video_ops_t *ops)
{
    initial = usable(ops) ? ops : NULL;
    return usb_add_function(&function);
}
