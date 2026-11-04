/*
 * USB audio source (USB Audio Class 2.0, device to host): the descriptors
 * and TinyUSB class driver for usb.h, the class requests the host sends about
 * the clock, and the per-frame packet feed that calls the firmware's fill().
 * This runs in the USB interrupts (OTG_FS and the TIM7 service, both
 * priority 6), see usb.h.
 */
#include "usb_audio.h"

#include <string.h>

#include "usb.h"

#define EP_IN               0x83

/* Interfaces the audio function adds: Audio Control and Audio Streaming. */
#define ITF_COUNT           2

/* Largest packet, 49 stereo 16-bit frames at 48 kHz; tusb_config_app.h sizes buffers for it. */
#define EP_SIZE_MAX         ((48 + 1) * 2 * 2)

/* Entity IDs inside the audio function. */
#define ID_INPUT_TERMINAL   1   /* where the samples come from: the E16's synth */
#define ID_OUTPUT_TERMINAL  3   /* USB streaming, towards the host */
#define ID_CLOCK            4   /* internal fixed clock at sample_rate */

/* UAC2 terminal type 0x0713: an embedded synthesizer. */
#define TERM_TYPE_SYNTH     0x0713

static volatile bool streaming;
static const usb_audio_ops_t *source;

static bool usable(const usb_audio_ops_t *ops)
{
    return ops && ops->fill && ops->channels >= 1 && ops->channels <= 2 &&
           ops->sample_rate >= 8000 && ops->sample_rate <= 48000;
}

static const usb_audio_ops_t *usb_audio_ops(void)
{
    return source;
}

/* Frames in the largest packet: a whole millisecond's worth rounded up, plus one for drift. */
static uint16_t max_frames(const usb_audio_ops_t *ops)
{
    return (uint16_t)((ops->sample_rate + 999) / 1000 + 1);
}

static uint16_t descriptor(uint8_t *out, uint8_t itf)
{
    const uint8_t ep_in = EP_IN;
    const usb_audio_ops_t *ops = usb_audio_ops();
    if (!ops) {
        return 0;
    }
    const uint8_t ch = ops->channels;
    const uint32_t ch_cfg = ch == 2 ? AUDIO20_CHANNEL_CONFIG_FRONT_LEFT | AUDIO20_CHANNEL_CONFIG_FRONT_RIGHT
                                    : AUDIO20_CHANNEL_CONFIG_NON_PREDEFINED;
    const uint16_t ep_size = (uint16_t)(max_frames(ops) * 2 * ch);

    const uint8_t desc[] = {
        TUD_AUDIO20_DESC_IAD(itf, ITF_COUNT, 0),
        TUD_AUDIO20_DESC_STD_AC(itf, 0, 0),
        TUD_AUDIO20_DESC_CS_AC(0x0200, AUDIO20_FUNC_IO_BOX,
            TUD_AUDIO20_DESC_CLK_SRC_LEN + TUD_AUDIO20_DESC_INPUT_TERM_LEN + TUD_AUDIO20_DESC_OUTPUT_TERM_LEN, 0),
        /* The sample rate can be read, not changed. */
        TUD_AUDIO20_DESC_CLK_SRC(ID_CLOCK, AUDIO20_CLOCK_SOURCE_ATT_INT_FIX_CLK,
            AUDIO20_CTRL_R << AUDIO20_CLOCK_SOURCE_CTRL_CLK_FRQ_POS, 0, 0),
        TUD_AUDIO20_DESC_INPUT_TERM(ID_INPUT_TERMINAL, TERM_TYPE_SYNTH, 0, ID_CLOCK, ch, ch_cfg, 0, 0, 0),
        TUD_AUDIO20_DESC_OUTPUT_TERM(ID_OUTPUT_TERMINAL, AUDIO_TERM_TYPE_USB_STREAMING, 0, ID_INPUT_TERMINAL,
            ID_CLOCK, 0, 0),
        /* Alternate setting 0: no endpoint, no bandwidth; the host picks 1 to record. */
        TUD_AUDIO20_DESC_STD_AS_INT((uint8_t)(itf + 1), 0, 0, 0),
        TUD_AUDIO20_DESC_STD_AS_INT((uint8_t)(itf + 1), 1, 1, 0),
        TUD_AUDIO20_DESC_CS_AS_INT(ID_OUTPUT_TERMINAL, AUDIO20_CTRL_NONE, AUDIO20_FORMAT_TYPE_I,
            AUDIO20_DATA_FORMAT_TYPE_I_PCM, ch, ch_cfg, 0),
        TUD_AUDIO20_DESC_TYPE_I_FORMAT(2, 16),
        TUD_AUDIO20_DESC_STD_AS_ISO_EP(ep_in,
            (uint8_t)(TUSB_XFER_ISOCHRONOUS | TUSB_ISO_EP_ATT_ASYNCHRONOUS | TUSB_ISO_EP_ATT_DATA), ep_size, 1),
        TUD_AUDIO20_DESC_CS_AS_ISO_EP(AUDIO20_CS_AS_ISO_DATA_EP_ATT_NON_MAX_PACKETS_OK, AUDIO20_CTRL_NONE,
            AUDIO20_CS_AS_ISO_DATA_EP_LOCK_DELAY_UNIT_UNDEFINED, 0),
    };
    if (out) {
        memcpy(out, desc, sizeof(desc));
    }
    return sizeof(desc);
}

bool usb_audio_streaming(void)
{
    return streaming && tud_audio_mounted() && !tud_suspended();
}

/* ---- Streaming ---- */

bool tud_audio_set_itf_cb(uint8_t rhport, tusb_control_request_t const *p_request)
{
    (void)rhport;
    streaming = TU_U16_LOW(p_request->wValue) != 0; /* alternate setting */
    return true;
}

/*
 * A new configuration starts every interface at alternate setting 0, with no
 * SET_INTERFACE to say so: after a re-enumeration (usb_reconnect()) the host
 * may never close what was streaming before.
 */
static void mounted(bool is_mounted)
{
    (void)is_mounted;
    streaming = false;
}

bool tud_audio_set_itf_close_ep_cb(uint8_t rhport, tusb_control_request_t const *p_request)
{
    (void)rhport;
    (void)p_request;
    streaming = false;
    return true;
}

/*
 * A packet went out (or the first, empty one): TinyUSB has already handed the
 * next packet, whatever we queued last time, to the endpoint. Queue the one
 * after it. With CFG_TUD_AUDIO_EP_IN_FLOW_CONTROL off, TinyUSB sends exactly
 * what is queued, so each fill() becomes one packet.
 */
bool tud_audio_tx_done_isr(uint8_t rhport, uint16_t n_bytes_sent, uint8_t func_id, uint8_t ep_in,
                           uint8_t cur_alt_setting)
{
    (void)rhport;
    (void)n_bytes_sent;
    (void)func_id;
    (void)ep_in;
    (void)cur_alt_setting;

    static int16_t buf[EP_SIZE_MAX / 2];
    const usb_audio_ops_t *ops = usb_audio_ops();
    if (!ops) {
        return true;
    }
    const uint16_t max = max_frames(ops);
    uint16_t frames = ops->fill(buf, max);
    if (frames > max) {
        frames = max;
    }
    if (frames == 0) {
        frames = (uint16_t)(ops->sample_rate / 1000);
        memset(buf, 0, (size_t)frames * ops->channels * sizeof(int16_t));
    }
    tud_audio_write(buf, (uint16_t)(frames * ops->channels * sizeof(int16_t)));
    return true;
}

/* ---- Class requests: only the clock's are needed ---- */

bool tud_audio_get_req_entity_cb(uint8_t rhport, tusb_control_request_t const *p_request)
{
    const usb_audio_ops_t *ops = usb_audio_ops();
    const uint8_t entity = TU_U16_HIGH(p_request->wIndex);
    const uint8_t control = TU_U16_HIGH(p_request->wValue);

    if (!ops || entity != ID_CLOCK) {
        return false;
    }
    if (control == AUDIO20_CS_CTRL_SAM_FREQ) {
        if (p_request->bRequest == AUDIO20_CS_REQ_CUR) {
            audio20_control_cur_4_t cur = {.bCur = (int32_t)ops->sample_rate};
            return tud_audio_buffer_and_schedule_control_xfer(rhport, p_request, &cur, sizeof(cur));
        }
        if (p_request->bRequest == AUDIO20_CS_REQ_RANGE) {
            audio20_control_range_4_n_t(1) range = {
                .wNumSubRanges = 1,
                .subrange[0] = {.bMin = (int32_t)ops->sample_rate, .bMax = (int32_t)ops->sample_rate, .bRes = 0},
            };
            return tud_audio_buffer_and_schedule_control_xfer(rhport, p_request, &range, sizeof(range));
        }
    } else if (control == AUDIO20_CS_CTRL_CLK_VALID && p_request->bRequest == AUDIO20_CS_REQ_CUR) {
        audio20_control_cur_1_t valid = {.bCur = 1};
        return tud_audio_buffer_and_schedule_control_xfer(rhport, p_request, &valid, sizeof(valid));
    }
    return false;
}

/* Some hosts set the rate even though it is read-only: accept the one we run at. */
bool tud_audio_set_req_entity_cb(uint8_t rhport, tusb_control_request_t const *p_request, uint8_t *buf)
{
    (void)rhport;
    const usb_audio_ops_t *ops = usb_audio_ops();
    return ops && TU_U16_HIGH(p_request->wIndex) == ID_CLOCK &&
           TU_U16_HIGH(p_request->wValue) == AUDIO20_CS_CTRL_SAM_FREQ &&
           p_request->bRequest == AUDIO20_CS_REQ_CUR && p_request->wLength == 4 &&
           tu_unaligned_read32(buf) == ops->sample_rate;
}

/* ---- Hooking into usb.h ---- */

static const usbd_class_driver_t driver = {
    .name = "AUDIO",
    .init = audiod_init,
    .deinit = audiod_deinit,
    .reset = audiod_reset,
    .open = audiod_open,
    .control_xfer_cb = audiod_control_xfer_cb,
    .xfer_cb = audiod_xfer_cb,
    .xfer_isr = audiod_xfer_isr,
    .sof = audiod_sof_isr,
};

static const usb_function_t function = {
    .descriptor = descriptor,
    .interfaces = ITF_COUNT,
    .iad = true,
    .driver = &driver,
    .mounted = mounted,
};

bool usb_audio_start(const usb_audio_ops_t *ops)
{
    if (!usable(ops)) {
        return false;
    }
    source = ops;
    return usb_add_function(&function);
}
