/*
 * USB camera example: a video synth. The E16 shows up on the computer as a
 * USB MIDI device and a webcam, "E16 Cam", streaming 160x120 at 20 fps.
 * Open it in any video app (Photo Booth, QuickTime's New Movie Recording,
 * OBS, a browser) or with ffmpeg:
 *
 *     ffmpeg -f avfoundation -framerate 20 -video_size 160x120 -i "E16 Cam" out.mp4
 *
 * Encoders:
 *   1  pattern: plasma, tunnel, rings, bars
 *   2  colour (palette hue)
 *   3  speed
 *   4  zoom
 *   press 1  OLED: status text <-> a dithered mirror of the picture
 *   Shift    camera off <-> on: the E16 re-enumerates as MIDI only, and
 *            back (usb_video_select())
 * The LED rings show the picture's colour at their spot on a 4x4 grid,
 * plus each of the first four rings' setting as a white dot.
 *
 * Shows how to:
 *   - add the webcam interfaces with usb_video_start() (usb_video.c, a USB
 *     function of the SDK's usb.h);
 *   - render the picture a line at a time, in the USB interrupt, without a
 *     frame buffer: tables are prepared once per frame (frame()) and line()
 *     is lookups and adds (it times itself, see the 0B reply below);
 *   - take a consistent copy of the main loop's settings at each frame;
 *   - switch the camera off and on at run time.
 *
 * USB MIDI keeps working: channel messages arriving over USB are echoed back.
 * F0 7D 45 0B F7 answers F0 7D 45 4B with five 32-bit numbers, each as five
 * 7-bit bytes, least significant first: frames sent, CPU cycles line()
 * took for the last frame, the most cycles one line() call took, lines
 * rendered in the last frame, and frames per second x 100.
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
#include "enc.h"
#include "font.h"
#include "leds.h"
#include "midi.h"
#include "system.h"
#include "usb_video.h"

#define W           160
#define H           120
#define FPS         20
#define QW          (W / 2)
#define QH          (H / 2)
#define LINE_CHARS  (DISPLAY_WIDTH / FONT_WIDTH)

enum { PLASMA, TUNNEL, RINGS, BARS, PATTERNS };
static const char *const pattern_names[PATTERNS] = {"PLASMA", "TUNNEL", "RINGS", "BARS"};

/* ---- Tables, built once at startup ---- */

static int8_t sin64[256];               /* 63 * sin, one turn in 256 steps */
static uint8_t qdist[QH][QW];           /* distance from the centre, one quarter of the picture */
static uint8_t qangle[QH][QW];          /* angle from the centre, 0-64 for the quarter turn */
static uint8_t depth[256];              /* tunnel depth for each distance */

/* A palette: 256 colours as YUV (for the video) and RGB (for the LEDs). */
typedef struct {
    uint8_t y[256], u[256], v[256];
    uint8_t r[256], g[256], b[256];
} palette_t;

static palette_t palettes[2];
static volatile uint8_t palette_ready;  /* which of the two the main loop finished last */

static void build_tables(void)
{
    for (int i = 0; i < 256; i++) {
        sin64[i] = (int8_t)lrintf(63.0f * sinf((float)i * 6.2831853f / 256.0f));
    }
    for (int y = 0; y < QH; y++) {
        for (int x = 0; x < QW; x++) {
            /* Pixel centres sit half a pixel off the picture's centre. */
            float dx = (float)x + 0.5f, dy = (float)y + 0.5f;
            float d = sqrtf(dx * dx + dy * dy);
            qdist[y][x] = (uint8_t)d;
            qangle[y][x] = (uint8_t)lrintf(atan2f(dy, dx) * 256.0f / 6.2831853f);
        }
    }
    for (int d = 0; d < 256; d++) {
        depth[d] = (uint8_t)(2048 / (d + 4));
    }
}

static uint8_t clamp8(int v)
{
    return (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v);
}

/* A smooth, looping rainbow, turned by hue. */
static void build_palette(palette_t *p, uint8_t hue)
{
    for (int i = 0; i < 256; i++) {
        uint8_t a = (uint8_t)(i + hue);
        int r = 128 + 2 * sin64[a];
        int g = 128 + 2 * sin64[(uint8_t)(a + 85)];
        int b = 128 + 2 * sin64[(uint8_t)(a + 171)];
        r = clamp8(r), g = clamp8(g), b = clamp8(b);
        p->r[i] = (uint8_t)r, p->g[i] = (uint8_t)g, p->b[i] = (uint8_t)b;
        /* BT.601, video range */
        p->y[i] = (uint8_t)(16 + ((66 * r + 129 * g + 25 * b + 128) >> 8));
        p->u[i] = (uint8_t)(128 + ((-38 * r - 74 * g + 112 * b + 128) >> 8));
        p->v[i] = (uint8_t)(128 + ((112 * r - 94 * g - 18 * b + 128) >> 8));
    }
}

/* ---- Settings, changed by the main loop ---- */

static volatile uint8_t pattern = PLASMA;
static volatile uint8_t hue;
static volatile uint8_t speed = 4;      /* 0-32 */
static volatile uint8_t zoom = 2;       /* 1-8 */
static volatile uint32_t anim_clock;         /* animation time: speed added every millisecond */

/* ---- The picture ---- */

/* Everything one frame needs: a copy of the settings and the tables made from them. */
typedef struct {
    uint8_t pattern, zoom;
    uint8_t t;                          /* animation phase */
    const palette_t *pal;
    int8_t xwave[W];                    /* plasma: the column term */
    int8_t dwave[W + H];                /* plasma: the diagonal term */
} view_t;

static void view_prepare(view_t *v)
{
    v->pattern = pattern;
    v->zoom = zoom;
    v->t = (uint8_t)(anim_clock >> 5);
    v->pal = &palettes[palette_ready];
    if (v->pattern == PLASMA) {
        for (int x = 0; x < W; x++) {
            v->xwave[x] = sin64[(uint8_t)(x * v->zoom + v->t)];
        }
        for (int i = 0; i < W + H; i++) {
            v->dwave[i] = sin64[(uint8_t)((i * v->zoom >> 1) - 2 * v->t)];
        }
    }
}

/* Palette index and brightness (0-255) of pixel x, y. qx, qy, up and left say where it is in which quarter. */
static inline uint8_t pixel(const view_t *v, int x, int y, int qx, int qy, bool left, bool up, uint8_t *shade)
{
    uint8_t t = v->t;
    uint8_t d = qdist[qy][qx];
    *shade = 255;
    switch (v->pattern) {
    case PLASMA: {
        int s = v->xwave[x] + v->dwave[x + y] + sin64[(uint8_t)(y * v->zoom + 3 * t)] +
                sin64[(uint8_t)(d * v->zoom * 2 - 2 * t)];
        return (uint8_t)(s + t);
    }
    case TUNNEL: {
        uint8_t a = qangle[qy][qx];
        uint8_t angle = left ? (up ? 128 + a : 128 - a) : (up ? 256 - a : a);
        uint8_t z = depth[d];
        *shade = (uint8_t)(d >= 50 ? 255 : d * 5);
        return (uint8_t)((uint8_t)(z * v->zoom / 2 + 2 * t) ^ (uint8_t)(angle + t));
    }
    case RINGS:
        return (uint8_t)(d * v->zoom * 3 - 3 * t);
    default: { /* BARS: eight bars, wobbling */
        int wobble = sin64[(uint8_t)(y * 2 + t)] >> 2;
        return (uint8_t)(((x + wobble) * v->zoom + 2 * t) & 0xE0);
    }
    }
}

static uint8_t shaded(uint8_t y, uint8_t shade)
{
    return (uint8_t)(16 + ((y - 16) * shade >> 8));
}

/* What the USB interrupt renders from; only touched there. */
static view_t video;

/* Cycle counts, for the 0B stats reply. */
static volatile uint32_t cycles_frame, cycles_line_max, lines_frame;
static uint32_t cycles_acc, lines_acc;

static void cam_frame(uint32_t number)
{
    (void)number;
    cycles_frame = cycles_acc;
    lines_frame = lines_acc;
    cycles_acc = 0;
    lines_acc = 0;
    view_prepare(&video);
}

static void cam_line(uint16_t y, uint8_t *yuy2)
{
    uint32_t start = DWT->CYCCNT;
    const view_t *v = &video;
    const palette_t *p = v->pal;
    uint32_t *out = (uint32_t *)yuy2;
    bool up = y < QH;
    int qy = up ? QH - 1 - y : y - QH;

    for (int x = 0; x < W; x += 2) {
        bool left = x < QW;
        int qx0 = left ? QW - 1 - x : x - QW;
        int qx1 = left ? qx0 - 1 : qx0 + 1;
        uint8_t s0, s1;
        uint8_t i0 = pixel(v, x, y, qx0, qy, left, up, &s0);
        uint8_t i1 = pixel(v, x + 1, y, qx1, qy, left, up, &s1);
        uint8_t y0 = p->y[i0], y1 = p->y[i1];
        if (v->pattern == TUNNEL) {
            y0 = shaded(y0, s0);
            y1 = shaded(y1, s1);
        }
        *out++ = y0 | (uint32_t)p->u[i0] << 8 | (uint32_t)y1 << 16 | (uint32_t)p->v[i0] << 24;
    }

    uint32_t took = DWT->CYCCNT - start;
    cycles_acc += took;
    lines_acc++;
    if (took > cycles_line_max) {
        cycles_line_max = took;
    }
}

static const usb_video_ops_t cam = {
    .width = W,
    .height = H,
    .fps = FPS,
    .frame = cam_frame,
    .line = cam_line,
};

/* ---- Main loop: preview on the OLED and LEDs ---- */

static view_t preview;

static uint8_t sample(const view_t *v, int x, int y, uint8_t *shade)
{
    bool left = x < QW, up = y < QH;
    int qx = left ? QW - 1 - x : x - QW;
    int qy = up ? QH - 1 - y : y - QH;
    return pixel(v, x, y, qx, qy, left, up, shade);
}

static void print(int page, const char *s)
{
    uint8_t row[DISPLAY_WIDTH] = {0};
    for (int i = 0; s[i] && i < LINE_CHARS; i++) {
        char c = s[i] >= FONT_FIRST && s[i] <= FONT_LAST ? s[i] : '?';
        memcpy(&row[i * FONT_WIDTH], font[c - FONT_FIRST], FONT_WIDTH);
    }
    display_write(page, 0, row, sizeof(row));
}

/* The picture, scaled to 128x64 and dithered to black and white. */
static void mirror(const view_t *v)
{
    static const uint8_t bayer[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
    for (int page = 0; page < DISPLAY_PAGES; page++) {
        uint8_t row[DISPLAY_WIDTH];
        for (int col = 0; col < DISPLAY_WIDTH; col++) {
            uint8_t bits = 0;
            for (int b = 0; b < 8; b++) {
                int oy = page * 8 + b;
                uint8_t shade;
                uint8_t i = sample(v, col * W / DISPLAY_WIDTH, oy * H / DISPLAY_HEIGHT, &shade);
                int luma = (shaded(v->pal->y[i], shade) - 16) * 255 / 219;
                if (luma > bayer[oy & 3][col & 3] * 16 + 8) {
                    bits |= (uint8_t)(1 << b);
                }
            }
            row[col] = bits;
        }
        display_write(page, 0, row, sizeof(row));
    }
}

static void status(void)
{
    char line[LINE_CHARS + 1];
    print(0, "USB CAM 160X120");
    snprintf(line, sizeof(line), "PATTERN %s", pattern_names[pattern]);
    print(2, line);
    snprintf(line, sizeof(line), "HUE %3u  SPEED %2u", hue, speed);
    print(3, line);
    snprintf(line, sizeof(line), "ZOOM %u", zoom);
    print(4, line);
    print(5, usb_video_switching()  ? "RECONNECTING"
             : !usb_video_active()  ? "CAMERA OFF (SHIFT)"
             : usb_video_streaming() ? "STREAMING"
                                         : "WAITING FOR AN APP");
}

static void update_leds(const view_t *v)
{
    for (int ring = 0; ring < LEDS_RINGS; ring++) {
        int x = (ring % 4) * (W / 4) + W / 8;
        int y = (ring / 4) * (H / 4) + H / 8;
        uint8_t shade;
        uint8_t i = sample(v, x, y, &shade);
        uint8_t r = (uint8_t)(v->pal->r[i] * shade >> 11);
        uint8_t g = (uint8_t)(v->pal->g[i] * shade >> 11);
        uint8_t b = (uint8_t)(v->pal->b[i] * shade >> 11);
        leds_set_ring(ring, r, g, b);
    }
    /* The settings, as a dot on rings 1-4. */
    leds_set(0, pattern * LEDS_PER_RING / PATTERNS, 40, 40, 40);
    leds_set(1, hue / 16, 40, 40, 40);
    leds_set(2, speed / 2 < LEDS_PER_RING ? speed / 2 : LEDS_PER_RING - 1, 40, 40, 40);
    leds_set(3, (zoom - 1) * 2, 40, 40, 40);
    leds_show();
}

static void put_u32(uint8_t *p, uint32_t v)
{
    for (int i = 0; i < 5; i++) {
        p[i] = (uint8_t)((v >> (7 * i)) & 0x7F);
    }
}

static uint32_t fps_x100;

static void send_stats(void)
{
    uint8_t m[4 + 5 * 5 + 1] = {0xF0, 0x7D, 0x45, 0x4B};
    put_u32(&m[4], usb_video_frames());
    put_u32(&m[9], cycles_frame);
    put_u32(&m[14], cycles_line_max);
    put_u32(&m[19], lines_frame);
    put_u32(&m[24], fps_x100);
    m[29] = 0xF7;
    midi_write(MIDI_USB, m, sizeof(m));
}

static int wrap(int v, int n)
{
    return ((v % n) + n) % n;
}

static int limit(int v, int lo, int hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

int main(void)
{
    system_init();
    /* The cycle counter, for timing line(). */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    build_tables();
    build_palette(&palettes[0], 0);
    palette_ready = 0;
    display_init();
    leds_init();
    enc_init();
    usb_video_start(&cam); /* adds the webcam; without it the E16 is MIDI-only */
    midi_init();

    bool show_mirror = false, was_pressed = false, was_shift = false, camera_on = true;
    bool redraw = true;
    uint8_t built_hue = 0;
    uint32_t last_ms = system_millis(), last_screen = 0, last_leds = 0;
    uint32_t fps_since = last_ms, fps_frames = usb_video_frames();
    uint8_t was_usb_state = 0;

    for (;;) {
        uint32_t now = system_millis();
        anim_clock += speed * (now - last_ms);
        last_ms = now;

        int d;
        if ((d = enc_read(0)) != 0) {
            pattern = (uint8_t)wrap(pattern + d, PATTERNS);
            redraw = true;
        }
        if ((d = enc_read(1)) != 0) {
            hue = (uint8_t)(hue + d * 4);
            redraw = true;
        }
        if ((d = enc_read(2)) != 0) {
            speed = (uint8_t)limit(speed + d, 0, 32);
            redraw = true;
        }
        if ((d = enc_read(3)) != 0) {
            zoom = (uint8_t)limit(zoom + d, 1, 8);
            redraw = true;
        }
        bool pressed = enc_pressed(0);
        if (pressed && !was_pressed) {
            show_mirror = !show_mirror;
            redraw = true;
        }
        was_pressed = pressed;
        bool shift = enc_shift_pressed();
        if (shift && !was_shift) {
            camera_on = !camera_on;
            usb_video_select(camera_on ? &cam : NULL);
            redraw = true;
        }
        was_shift = shift;

        /* A new hue: fill the palette the video isn't using, then switch to it. */
        if (hue != built_hue) {
            built_hue = hue;
            uint8_t next = palette_ready ^ 1;
            build_palette(&palettes[next], built_hue);
            palette_ready = next;
        }

        if (now - fps_since >= 2000) {
            uint32_t frames = usb_video_frames();
            fps_x100 = (frames - fps_frames) * 100000 / (now - fps_since);
            fps_frames = frames;
            fps_since = now;
            redraw = true;
        }
        uint8_t usb_state = (uint8_t)(usb_video_streaming() | usb_video_switching() << 1 |
                                      usb_video_active() << 2);
        if (usb_state != was_usb_state) {
            was_usb_state = usb_state;
            redraw = true;
        }

        if (now - last_leds >= 30) {
            last_leds = now;
            view_prepare(&preview);
            update_leds(&preview);
        }

        if (show_mirror && now - last_screen >= 60) {
            last_screen = now;
            view_prepare(&preview);
            mirror(&preview);
        } else if (!show_mirror && redraw) {
            redraw = false;
            for (int page = 0; page < DISPLAY_PAGES; page++) {
                print(page, "");
            }
            status();
            char line[LINE_CHARS + 1];
            snprintf(line, sizeof(line), "%lu.%02lu FPS", (unsigned long)(fps_x100 / 100),
                     (unsigned long)(fps_x100 % 100));
            print(7, line);
        }

        midi_msg_t msg;
        while (midi_read(&msg)) {
            if (msg.port != MIDI_USB) {
                continue;
            }
            static const uint8_t stats[] = {0xF0, 0x7D, 0x45, 0x0B, 0xF7};
            if (msg.len == sizeof(stats) && memcmp(msg.data, stats, sizeof(stats)) == 0) {
                send_stats();
            }
            if (msg.data[0] >= 0x80 && msg.data[0] < 0xF0) {
                midi_write(MIDI_USB, msg.data, msg.len); /* echo */
            }
        }
    }
}
