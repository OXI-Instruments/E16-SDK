#ifndef TUSB_CONFIG_APP_H
#define TUSB_CONFIG_APP_H

/*
 * TinyUSB's video class, for usb_video.c. Included by the SDK's tusb_config.h.
 * Isochronous; one packet per millisecond of at most 928 bytes, what fits the
 * USB core's packet memory next to MIDI (see usb.h).
 */
#define CFG_TUD_VIDEO                       1
#define CFG_TUD_VIDEO_STREAMING             1
#define CFG_TUD_VIDEO_STREAMING_BULK        0
#define CFG_TUD_VIDEO_STREAMING_EP_BUFSIZE  928

#endif
