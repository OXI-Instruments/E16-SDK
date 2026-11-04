/*
 * USB tone example: the E16 shows up on the computer as a USB audio input
 * (next to its MIDI port) playing a 440 Hz sine at 48 kHz, -6 dBFS. Record it
 * in any audio app, or e.g.:
 *
 *     ffmpeg -f avfoundation -list_devices true -i ""     # find the E16's index
 *     ffmpeg -f avfoundation -i ":<index>" -t 10 tone.wav
 *
 * Shows how to:
 *   - add the audio interfaces with usb_audio_start() (usb_audio.c, a USB
 *     function of the SDK's usb.h);
 *   - produce samples from a timer interrupt (TIM6, 48 kHz) on the E16's own
 *     clock, into a lock-free ring that fill() empties every millisecond;
 *   - keep the ring about 2 ms full by sending 47, 48 or 49 frames per packet,
 *     which absorbs the drift between the E16's crystal and the computer's.
 *
 * Build with -DUSB_TONE_CHANNELS=2 for stereo (440 Hz left, 660 Hz right).
 *
 * USB MIDI keeps working: channel messages arriving over USB are echoed back,
 * and F0 7D 45 10 F7 answers F0 7D 45 50 <counters> F7 with the packet
 * statistics (see send_stats()).
 *
 * By Charles Vestal.
 */
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "board.h"
#include "display.h"
#include "font.h"
#include "midi.h"
#include "system.h"
#include "usb_audio.h"

#ifndef USB_TONE_CHANNELS
#define USB_TONE_CHANNELS   1
#endif

#define SAMPLE_RATE     48000
#define CHANNELS        USB_TONE_CHANNELS
#define TABLE_BITS      10
#define TABLE_SIZE      (1 << TABLE_BITS)
#define RING_FRAMES     512                 /* power of two */
#define TARGET_FRAMES   96                  /* 2 ms: how full fill() keeps the ring */
#define SLACK_FRAMES    16                  /* within +-this of the target, send 48 */
#define LINE_CHARS      (DISPLAY_WIDTH / FONT_WIDTH)

static int16_t sine[TABLE_SIZE + 1];        /* one extra for interpolation */

/* ---- The ring: TIM6 writes, the USB interrupt reads ---- */

static int16_t ring[RING_FRAMES][CHANNELS];
static volatile uint32_t ring_head;         /* written by TIM6 only */
static volatile uint32_t ring_tail;         /* written by fill() only */

/* Packet statistics, by frames per packet. */
static volatile uint32_t sent_47, sent_48, sent_49, sent_short, sent_empty, resyncs;

static uint32_t timer_clock(void)
{
    /* APB1 timers run at twice PCLK1 when APB1 is divided. */
    return HAL_RCC_GetPCLK1Freq() * 2;
}

static int16_t oscillator(uint32_t *phase, uint32_t inc)
{
    uint32_t p = *phase;
    *phase = p + inc;
    uint32_t i = p >> (32 - TABLE_BITS);
    int32_t frac = (int32_t)((p >> (16 - TABLE_BITS)) & 0xFFFF);
    int32_t a = sine[i];
    int32_t b = sine[i + 1];
    return (int16_t)(a + (((b - a) * frac) >> 16));
}

void TIM6_DAC_IRQHandler(void)
{
    static uint32_t phase[2];
    static const uint32_t inc[2] = {
        (uint32_t)(440.0 / SAMPLE_RATE * 4294967296.0),
        (uint32_t)(660.0 / SAMPLE_RATE * 4294967296.0),
    };

    TIM6->SR = 0;
    int16_t frame[CHANNELS];
    for (int c = 0; c < CHANNELS; c++) {
        frame[c] = oscillator(&phase[c], inc[c]);
    }
    uint32_t head = ring_head;
    if (head - ring_tail < RING_FRAMES) {   /* full: nobody is recording, drop it */
        memcpy(ring[head & (RING_FRAMES - 1)], frame, sizeof(frame));
        ring_head = head + 1;
    }
}

/* USB interrupt, every millisecond while the computer records. */
static uint16_t fill(int16_t *buf, uint16_t max_frames)
{
    uint32_t tail = ring_tail;
    uint32_t avail = ring_head - tail;

    /* Recording just started on a ring that filled up meanwhile: keep only the target. */
    if (avail > 2 * TARGET_FRAMES) {
        tail += avail - TARGET_FRAMES;
        avail = TARGET_FRAMES;
        resyncs++;
    }

    uint16_t n = SAMPLE_RATE / 1000;
    if (avail > TARGET_FRAMES + SLACK_FRAMES) {
        n++;
    } else if (avail < TARGET_FRAMES - SLACK_FRAMES) {
        n--;
    }
    if (n > max_frames) {
        n = max_frames;
    }
    if (n > avail) {
        n = (uint16_t)avail;
    }

    for (uint16_t i = 0; i < n; i++) {
        memcpy(&buf[i * CHANNELS], ring[(tail + i) & (RING_FRAMES - 1)], CHANNELS * sizeof(int16_t));
    }
    ring_tail = tail + n;

    if (n == 47) {
        sent_47++;
    } else if (n == 48) {
        sent_48++;
    } else if (n == 49) {
        sent_49++;
    } else if (n == 0) {
        sent_empty++;
    } else {
        sent_short++;
    }
    return n;
}

static const usb_audio_ops_t tone = {
    .sample_rate = SAMPLE_RATE,
    .channels = CHANNELS,
    .fill = fill,
};

static void tone_init(void)
{
    for (int i = 0; i <= TABLE_SIZE; i++) {
        sine[i] = (int16_t)lrintf(16384.0f * sinf(2.0f * (float)M_PI * (float)i / TABLE_SIZE)); /* -6 dBFS */
    }

    /* TIM6: the 48 kHz sample clock, from the E16's crystal. */
    __HAL_RCC_TIM6_CLK_ENABLE();
    TIM6->PSC = 0;
    TIM6->ARR = timer_clock() / SAMPLE_RATE - 1;
    TIM6->DIER = TIM_DIER_UIE;
    TIM6->EGR = TIM_EGR_UG;
    TIM6->SR = 0;
    HAL_NVIC_SetPriority(TIM6_DAC_IRQn, 2, 0);
    HAL_NVIC_EnableIRQ(TIM6_DAC_IRQn);
    TIM6->CR1 = TIM_CR1_CEN;
}

/* ---- Main loop ---- */

static void print(int page, const char *s)
{
    uint8_t row[DISPLAY_WIDTH] = {0};
    for (int i = 0; s[i] && i < LINE_CHARS; i++) {
        char c = s[i] >= FONT_FIRST && s[i] <= FONT_LAST ? s[i] : '?';
        memcpy(&row[i * FONT_WIDTH], font[c - FONT_FIRST], FONT_WIDTH);
    }
    display_write(page, 0, row, sizeof(row));
}

/*
 * F0 7D 45 50, then six counters (packets of 47, 48 and 49 frames, shorter
 * ones, empty ones (silence), ring resyncs), each 5 x 7 bits, low first, F7.
 */
static void send_stats(void)
{
    const uint32_t counts[] = {sent_47, sent_48, sent_49, sent_short, sent_empty, resyncs};
    uint8_t out[4 + 6 * 5 + 1] = {0xF0, 0x7D, 0x45, 0x50};
    size_t len = 4;
    for (size_t i = 0; i < 6; i++) {
        for (int b = 0; b < 5; b++) {
            out[len++] = (uint8_t)((counts[i] >> (7 * b)) & 0x7F);
        }
    }
    out[len++] = 0xF7;
    uint32_t start = system_millis();
    while (!midi_write(MIDI_USB, out, len) && system_millis() - start < 100) {
    }
}

int main(void)
{
    system_init();
    display_init();
    for (int page = 0; page < DISPLAY_PAGES; page++) {
        print(page, "");
    }
    tone_init();
    usb_audio_start(&tone); /* adds the audio input; without it the E16 is MIDI-only */
    midi_init();
    print(0, CHANNELS == 2 ? "USB TONE 440/660 HZ" : "USB TONE 440 HZ");

    int shown_streaming = -1;
    uint32_t next_stats = 0;

    for (;;) {
        int now_streaming = usb_audio_streaming();
        if (now_streaming != shown_streaming) {
            shown_streaming = now_streaming;
            print(2, now_streaming ? "RECORDING" : "IDLE");
        }
        if (system_millis() >= next_stats) {
            next_stats = system_millis() + 500;
            char line[LINE_CHARS + 1];
            snprintf(line, sizeof(line), "47:%lu 49:%lu", (unsigned long)sent_47, (unsigned long)sent_49);
            print(4, line);
            snprintf(line, sizeof(line), "SHORT:%lu EMPTY:%lu", (unsigned long)sent_short, (unsigned long)sent_empty);
            print(5, line);
        }

        midi_msg_t msg;
        while (midi_read(&msg)) {
            if (msg.port != MIDI_USB) {
                continue;
            }
            static const uint8_t stats[] = {0xF0, 0x7D, 0x45, 0x10, 0xF7};
            if (msg.len == sizeof(stats) && memcmp(msg.data, stats, sizeof(stats)) == 0) {
                send_stats();
            }
            if (msg.data[0] >= 0x80 && msg.data[0] < 0xF0) {
                midi_write(MIDI_USB, msg.data, msg.len); /* echo */
            }
        }
    }
}
