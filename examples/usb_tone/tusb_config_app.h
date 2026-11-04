#ifndef TUSB_CONFIG_APP_H
#define TUSB_CONFIG_APP_H

/*
 * TinyUSB's audio class, for usb_audio.c. Included by the SDK's tusb_config.h.
 * One UAC2 function with an isochronous IN endpoint of up to 49 stereo 16-bit
 * frames (196 bytes), fed by tud_audio_tx_done_isr(). Flow control is off: the
 * firmware's fill() decides each packet's size.
 */
#define CFG_TUD_AUDIO                           1
#define CFG_TUD_AUDIO_ENABLE_EP_IN              1
#define CFG_TUD_AUDIO_EP_IN_FLOW_CONTROL        0
#define CFG_TUD_AUDIO_FUNC_1_EP_IN_SZ_MAX       196
#define CFG_TUD_AUDIO_FUNC_1_EP_IN_SW_BUF_SZ    (2 * 196)

#endif
