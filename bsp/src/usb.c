/*
 * USB MIDI with TinyUSB, plus the functions a firmware adds (usb.h). TinyUSB
 * runs in the USB interrupts: the USB core's, and the TIM7 service interrupt,
 * which moves bytes between TinyUSB and midi.c's buffers. So the rest of the
 * SDK never calls TinyUSB. The service runs every millisecond and right after
 * each USB interrupt, so TinyUSB's events never pile up.
 *
 * usb_reconnect() changes the set of interfaces at run time: the service
 * interrupt detaches from the computer (the D+ pull-up off), lets TinyUSB's
 * class drivers reset as if unplugged, waits RECONNECT_DETACH_MS for the
 * computer to notice, rebuilds the descriptors and attaches again. TinyUSB's
 * DWC2 driver lays out the packet memory afresh on the bus reset that
 * follows, so each set of functions only has to fit on its own.
 */
#include "usb.h"

#include <string.h>

#include "board.h"
#include "device/dcd.h" /* dcd_event_bus_signal(), for the reconnect */
#include "midi_ring.h"

#define USB_VID             0x28E9  /* the official firmware's IDs; the name tells them apart */
#define USB_PID             0x1112
#define USB_MANUFACTURER    "OXI Instruments"

/* Per-firmware product string; see e16_add_firmware(... USB_NAME "...") in cmake/e16.cmake. */
extern const char *const e16_usb_product;

#define SERVICE_HZ          1000
/*
 * TinyUSB's DWC2 driver writes the IN FIFO both from the USB interrupt and from
 * tud_task(). Running both at the same priority keeps them from interrupting
 * each other halfway through.
 */
#define USB_IRQ_PRIORITY    6
#define SERVICE_PRIORITY    USB_IRQ_PRIORITY

/* How long the E16 stays detached when it re-enumerates: long enough for any computer to notice. */
#define RECONNECT_DETACH_MS 500

static midi_ring_t *tx_ring;
static midi_ring_t *rx_ring;
static volatile bool connected;
static TIM_HandleTypeDef tim;

static const usb_function_t *functions[USB_FUNCTIONS_MAX];
static usbd_class_driver_t drivers[USB_FUNCTIONS_MAX];
static uint8_t function_count;

static volatile bool reconnect_requested;   /* set by usb_reconnect(), cleared once attached again */
static bool detached;                       /* the service interrupt's side of a reconnect */
static uint32_t detached_at;

/* ---- Descriptors ---- */

/* Not const: functions grouped by interface association descriptors change the device class. */
static tusb_desc_device_t device_desc = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = USB_VID,
    .idProduct = USB_PID,
    .bcdDevice = 0x0100,
    .iManufacturer = 1,
    .iProduct = 2,
    .iSerialNumber = 3,
    .bNumConfigurations = 1,
};

enum { ITF_MIDI, ITF_MIDI_STREAMING, ITF_MIDI_END };

#define EP_MIDI_OUT     0x01
#define EP_MIDI_IN      0x81

static uint8_t config_desc[512];

/* MIDI, then each added function that has descriptors this time. At start-up and while detached. */
static void build_config_desc(void)
{
    uint16_t len = TUD_CONFIG_DESC_LEN;
    uint8_t itf = ITF_MIDI_END;
    bool iad = false;

    const uint8_t midi[] = {
        TUD_MIDI_DESCRIPTOR(ITF_MIDI, 0, EP_MIDI_OUT, EP_MIDI_IN, 64),
    };
    memcpy(&config_desc[len], midi, sizeof(midi));
    len += sizeof(midi);

    for (uint8_t i = 0; i < function_count; i++) {
        const usb_function_t *f = functions[i];
        uint16_t n = f->descriptor(NULL, itf);
        if (n == 0 || len + n > sizeof(config_desc)) {
            continue;
        }
        f->descriptor(&config_desc[len], itf);
        len += n;
        itf += f->interfaces;
        iad |= f->iad;
    }

    /* Interface association descriptors need the device to say it uses them. */
    device_desc.bDeviceClass = iad ? TUSB_CLASS_MISC : 0;
    device_desc.bDeviceSubClass = iad ? MISC_SUBCLASS_COMMON : 0;
    device_desc.bDeviceProtocol = iad ? MISC_PROTOCOL_IAD : 0;

    const uint8_t header[] = {
        TUD_CONFIG_DESCRIPTOR(1, itf, 0, len, 0x00, 100),
    };
    memcpy(config_desc, header, sizeof(header));
}

bool usb_add_function(const usb_function_t *function)
{
    if (function_count >= USB_FUNCTIONS_MAX || !function || !function->descriptor || !function->driver) {
        return false;
    }
    functions[function_count] = function;
    drivers[function_count] = *function->driver;
    function_count++;
    return true;
}

/* TinyUSB asks for class drivers beyond its built-in ones once, when it starts. */
usbd_class_driver_t const *usbd_app_driver_get_cb(uint8_t *driver_count)
{
    *driver_count = function_count;
    return drivers;
}

const uint8_t *tud_descriptor_device_cb(void)
{
    return (const uint8_t *)&device_desc;
}

const uint8_t *tud_descriptor_configuration_cb(uint8_t index)
{
    (void)index;
    return config_desc;
}

const uint16_t *tud_descriptor_string_cb(uint8_t index, uint16_t langid)
{
    (void)langid;
    static uint16_t desc[33];
    char serial[25];
    const char *s;

    switch (index) {
    case 0:
        desc[1] = 0x0409; /* English */
        desc[0] = (uint16_t)((TUSB_DESC_STRING << 8) | 4);
        return desc;
    case 1:
        s = USB_MANUFACTURER;
        break;
    case 2:
        s = e16_usb_product;
        break;
    case 3: {
        /* The chip's unique ID, as hex. */
        const uint8_t *uid = (const uint8_t *)UID_BASE;
        static const char hex[] = "0123456789ABCDEF";
        for (int i = 0; i < 12; i++) {
            serial[i * 2] = hex[uid[i] >> 4];
            serial[i * 2 + 1] = hex[uid[i] & 0x0F];
        }
        serial[24] = '\0';
        s = serial;
        break;
    }
    default:
        return NULL;
    }

    size_t len = strlen(s);
    if (len > 32) {
        len = 32;
    }
    for (size_t i = 0; i < len; i++) {
        desc[1 + i] = (uint8_t)s[i];
    }
    desc[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * len + 2));
    return desc;
}

/* ---- Connection state ---- */

static void notify_mounted(bool mounted)
{
    for (uint8_t i = 0; i < function_count; i++) {
        if (functions[i]->mounted) {
            functions[i]->mounted(mounted);
        }
    }
}

void tud_mount_cb(void)
{
    notify_mounted(true);
    connected = true;
}

void tud_umount_cb(void)
{
    notify_mounted(false);
    connected = false;
}

void tud_suspend_cb(bool remote_wakeup_en)
{
    (void)remote_wakeup_en;
    connected = false;
}

void tud_resume_cb(void)
{
    connected = tud_mounted();
}

bool usb_midi_connected(void)
{
    return connected;
}

/* ---- Re-enumeration ---- */

void usb_reconnect(void)
{
    reconnect_requested = true;
}

bool usb_reconnecting(void)
{
    return reconnect_requested;
}

/* Every service tick: detach, wait, rebuild the descriptors, attach. */
static void reconnect_poll(void)
{
    if (!detached) {
        if (reconnect_requested) {
            tud_disconnect();
            /*
             * The class drivers reset and tud_umount_cb() runs, as on an
             * unplug, in the next tud_task(). dcd_event_bus_signal() is
             * TinyUSB's driver-side API (device/dcd.h), not its public one:
             * checked against 0.21.0, recheck when moving TinyUSB.
             */
            dcd_event_bus_signal(0, DCD_EVENT_UNPLUGGED, true);
            detached = true;
            detached_at = HAL_GetTick();
        }
        return;
    }
    if (HAL_GetTick() - detached_at < RECONNECT_DETACH_MS) {
        return;
    }
    build_config_desc();
    detached = false;
    reconnect_requested = false;
    tud_connect();
}

void usb_lock(void)
{
    NVIC_DisableIRQ(TIM7_IRQn);
    NVIC_DisableIRQ(OTG_FS_IRQn);
    __DSB();
    __ISB();
}

void usb_unlock(void)
{
    NVIC_EnableIRQ(OTG_FS_IRQn);
    NVIC_EnableIRQ(TIM7_IRQn);
    NVIC_SetPendingIRQ(TIM7_IRQn); /* handle what the main loop queued now, not at the next tick */
}

/* ---- Service ---- */

static void service(void)
{
    tud_task();
    reconnect_poll();
    for (uint8_t i = 0; i < function_count; i++) {
        if (functions[i]->service) {
            functions[i]->service();
        }
    }

    if (!tud_midi_mounted()) {
        tx_ring->tail = tx_ring->head; /* nobody to send to */
        return;
    }

    /* Only take what fits; the rest waits in TinyUSB, which makes the computer wait. */
    uint8_t buf[64];
    uint32_t room;
    while ((room = MIDI_RING_SIZE - midi_ring_count(rx_ring)) > 0) {
        uint32_t n = tud_midi_stream_read(buf, room < sizeof(buf) ? room : sizeof(buf));
        if (n == 0) {
            break;
        }
        for (uint32_t i = 0; i < n; i++) {
            midi_ring_put(rx_ring, buf[i]);
        }
    }

    /* Hand over what's queued; whatever TinyUSB can't take yet stays queued. */
    uint32_t count = midi_ring_count(tx_ring);
    if (count > sizeof(buf)) {
        count = sizeof(buf);
    }
    uint32_t tail = tx_ring->tail;
    for (uint32_t i = 0; i < count; i++) {
        buf[i] = tx_ring->data[(tail + i) & (MIDI_RING_SIZE - 1)];
    }
    if (count > 0) {
        tx_ring->tail = tail + tud_midi_stream_write(0, buf, count);
    }
}

void TIM7_IRQHandler(void)
{
    __HAL_TIM_CLEAR_FLAG(&tim, TIM_FLAG_UPDATE);
    service();
}

void OTG_FS_IRQHandler(void)
{
    tud_int_handler(0);
    NVIC_SetPendingIRQ(TIM7_IRQn);
}

void usb_midi_init(midi_ring_t *tx, midi_ring_t *rx)
{
    tx_ring = tx;
    rx_ring = rx;

    __HAL_RCC_GPIOA_CLK_ENABLE();
    GPIO_InitTypeDef gpio = {0};
    gpio.Pin = USB_DM_PIN | USB_DP_PIN;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = GPIO_AF10_OTG_FS;
    HAL_GPIO_Init(USB_PORT, &gpio);

    __HAL_RCC_USB_OTG_FS_CLK_ENABLE();
    HAL_NVIC_SetPriority(OTG_FS_IRQn, USB_IRQ_PRIORITY, 0);

    reconnect_requested = false;
    build_config_desc();

    tusb_rhport_init_t init = {.role = TUSB_ROLE_DEVICE, .speed = TUSB_SPEED_FULL};
    tusb_init(0, &init);

    /*
     * GD32 only: with VBUS ignored, the D+ pull-up still needs the B-device
     * VBUS comparator on, or the computer never sees the E16. TinyUSB turns it off.
     */
    USB_OTG_FS->GCCFG |= USB_OTG_GCCFG_VBUSBSEN;

    /* APB1 timers run at twice PCLK1 because APB1 is divided. */
    __HAL_RCC_TIM7_CLK_ENABLE();
    uint32_t timer_hz = HAL_RCC_GetPCLK1Freq() * 2;
    tim.Instance = TIM7;
    tim.Init.Prescaler = timer_hz / 1000000 - 1;
    tim.Init.Period = 1000000 / SERVICE_HZ - 1;
    tim.Init.CounterMode = TIM_COUNTERMODE_UP;
    tim.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
    HAL_TIM_Base_Init(&tim);
    HAL_NVIC_SetPriority(TIM7_IRQn, SERVICE_PRIORITY, 0);
    HAL_NVIC_EnableIRQ(TIM7_IRQn);
    HAL_TIM_Base_Start_IT(&tim);
}

/* TinyUSB's millisecond clock. */
uint32_t tusb_time_millis_api(void)
{
    return HAL_GetTick();
}
