/*
 * Bluetooth MIDI firmware for the E16's Bluetooth chip (nRF52832, S132 7.0.1).
 *
 * The E16 appears as a standard Bluetooth LE MIDI device named "E16 Custom".
 * MIDI passes as plain bytes over the serial port to the main processor
 * (115200 baud, 8N1, no flow control): what arrives over Bluetooth comes out of
 * the serial port as complete MIDI messages, and MIDI sent to the serial port
 * goes out over Bluetooth. There is no other protocol on the serial port.
 */
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "app_error.h"
#include "app_timer.h"
#include "app_uart.h"
#include "ble.h"
#include "ble_advdata.h"
#include "ble_advertising.h"
#include "ble_conn_params.h"
#include "ble_srv_common.h"
#include "nrf_ble_gatt.h"
#include "nrf_pwr_mgmt.h"
#include "nrf_sdh.h"
#include "nrf_sdh_ble.h"
#include "nrf_uarte.h"

#define DEVICE_NAME             "E16 Custom"

/* Serial port to the main processor. */
#define UART_RX_PIN             8
#define UART_TX_PIN             6
#define UART_BUFFER_SIZE        1024    /* takes bursts while Bluetooth or the serial port catches up */

#define CONN_CFG_TAG            1
#define NOTIFY_QUEUE_SIZE       8       /* notifications the SoftDevice can hold per connection */
#define BLE_OBSERVER_PRIO       3
#define ADV_INTERVAL            64      /* 40 ms, in 0.625 ms units */
#define MIN_CONN_INTERVAL       MSEC_TO_UNITS(7.5, UNIT_1_25_MS)
#define MAX_CONN_INTERVAL       MSEC_TO_UNITS(15, UNIT_1_25_MS)
#define SUPERVISION_TIMEOUT     MSEC_TO_UNITS(4000, UNIT_10_MS)
#define FIRST_PARAMS_UPDATE     APP_TIMER_TICKS(5000)
#define NEXT_PARAMS_UPDATE      APP_TIMER_TICKS(30000)
#define MAX_PARAMS_UPDATES      3

/* Bytes from the serial port wait at most this long before they go out. */
#define FLUSH_INTERVAL          APP_TIMER_TICKS(2)

/* The standard BLE-MIDI service and characteristic UUIDs, little-endian. */
static const ble_uuid128_t midi_service_base = {{
    0x00, 0xC7, 0xC4, 0x4E, 0xE3, 0x6C, 0x51, 0xA7, 0x33, 0x4B, 0xE8, 0xED, 0x5A, 0x0E, 0xB8, 0x03}};
static const ble_uuid128_t midi_char_base = {{
    0xF3, 0x6B, 0x10, 0x9D, 0x66, 0xF2, 0xA9, 0xA1, 0x12, 0x41, 0x68, 0x38, 0xDB, 0xE5, 0x72, 0x77}};
#define MIDI_SERVICE_UUID       0x0E5A
#define MIDI_CHAR_UUID          0xE5DB

NRF_BLE_GATT_DEF(m_gatt);
BLE_ADVERTISING_DEF(m_advertising);
APP_TIMER_DEF(m_flush_timer);

static uint16_t m_conn = BLE_CONN_HANDLE_INVALID;
static uint8_t m_service_uuid_type;
static ble_gatts_char_handles_t m_midi;
static bool m_notify;                       /* the peer subscribed to notifications */
static uint16_t m_max_packet = 20;          /* ATT MTU - 3 */
static volatile bool m_flush_due;

static uint8_t data_bytes(uint8_t status)
{
    switch (status & 0xF0) {
    case 0xC0:
    case 0xD0:
        return 1;
    case 0xF0:
        return status == 0xF2 ? 2 : (status == 0xF1 || status == 0xF3) ? 1 : 0;
    default:
        return 2;
    }
}

/* ---- Bluetooth to serial ---- */

static struct {
    uint8_t running;    /* status for running status, 0 if none */
    uint8_t need;       /* data bytes the status takes */
    uint8_t count;      /* data bytes since the last status */
    bool sysex;
} rx;

/*
 * Decoded bytes wait here for the main loop, which passes them to the serial
 * driver. app_uart isn't safe to call from the Bluetooth event interrupt.
 */
static uint8_t m_to_uart[UART_BUFFER_SIZE];
static volatile uint32_t m_to_uart_head;   /* written by the Bluetooth event handler */
static volatile uint32_t m_to_uart_tail;   /* written by the main loop */

static void uart_out(uint8_t b)
{
    uint32_t head = m_to_uart_head;
    if (head - m_to_uart_tail < UART_BUFFER_SIZE) { /* dropped if full */
        m_to_uart[head % UART_BUFFER_SIZE] = b;
        m_to_uart_head = head + 1;
    }
}

static void ble_to_uart(void)
{
    uint32_t tail = m_to_uart_tail;
    while (tail != m_to_uart_head && app_uart_put(m_to_uart[tail % UART_BUFFER_SIZE]) == NRF_SUCCESS) {
        tail++;
    }
    m_to_uart_tail = tail;
}

static void rx_status(uint8_t b)
{
    if (b >= 0xF8) {
        uart_out(b); /* real-time, allowed anywhere */
        return;
    }
    if (b == 0xF7) {
        if (rx.sysex) {
            uart_out(b);
            rx.sysex = false;
        }
        return;
    }
    rx.sysex = b == 0xF0;
    rx.running = b == 0xF0 ? 0 : b;
    rx.need = data_bytes(b);
    rx.count = 0;
    uart_out(b);
    if (b >= 0xF0 && rx.need == 0) {
        rx.running = 0;
    }
}

static void rx_data(uint8_t b)
{
    if (rx.sysex) {
        uart_out(b);
        return;
    }
    if (rx.running == 0) {
        return; /* data without a status */
    }
    if (rx.count == rx.need) {
        uart_out(rx.running); /* running status: send complete messages */
        rx.count = 0;
    }
    uart_out(b);
    if (++rx.count == rx.need && rx.running >= 0xF0) {
        rx.running = 0; /* system common messages have no running status */
    }
}

/*
 * A BLE-MIDI packet: a header byte, then messages each preceded by a timestamp
 * byte. Both have bit 7 set; the byte after a timestamp is a status byte, or a
 * data byte under running status. SysEx continues across packets without
 * timestamps.
 */
static void rx_packet(const uint8_t *p, uint16_t len)
{
    if (len < 2 || !(p[0] & 0x80)) {
        return;
    }
    for (uint16_t i = 1; i < len; i++) {
        uint8_t b = p[i];
        if (b & 0x80) {
            if (++i >= len) {
                break;
            }
            b = p[i];
            if (b & 0x80) {
                rx_status(b);
                continue;
            }
        }
        rx_data(b);
    }
}

/* ---- Serial to Bluetooth ---- */

static uint8_t m_packet[NRF_SDH_BLE_GATT_MAX_MTU_SIZE - 3];
static uint16_t m_packet_len;
static uint8_t m_timestamp;     /* low timestamp byte for this packet's messages */
static bool m_packet_blocked;   /* the SoftDevice's queue was full; retry later. The
                                   packet keeps filling meanwhile. */

static struct {
    uint8_t msg[3];
    uint8_t len;
    uint8_t need;
    uint8_t running;
    bool sysex;
} tx;

static bool can_send(void)
{
    return m_conn != BLE_CONN_HANDLE_INVALID && m_notify;
}

/* Returns false while the SoftDevice can't take another notification. */
static bool send_packet(void)
{
    if (m_packet_len == 0) {
        return true;
    }
    ble_gatts_hvx_params_t hvx = {0};
    uint16_t len = m_packet_len;
    hvx.handle = m_midi.value_handle;
    hvx.type = BLE_GATT_HVX_NOTIFICATION;
    hvx.p_len = &len;
    hvx.p_data = m_packet;
    uint32_t err = sd_ble_gatts_hvx(m_conn, &hvx);
    if (err == NRF_ERROR_RESOURCES) {
        m_packet_blocked = true;
        return false;
    }
    /* Sent, or the link is gone and the packet with it. */
    m_packet_len = 0;
    m_packet_blocked = false;
    return true;
}

/* Make room for n bytes in the packet, starting a new one if needed. */
static bool reserve(uint16_t n)
{
    if (m_packet_len + n > m_max_packet && !send_packet()) {
        return false;
    }
    if (m_packet_len == 0) {
        uint32_t ms = (uint32_t)((uint64_t)app_timer_cnt_get() * 1000 * (APP_TIMER_CONFIG_RTC_FREQUENCY + 1) /
                                 APP_TIMER_CLOCK_FREQ);
        m_packet[0] = (uint8_t)(0x80 | ((ms >> 7) & 0x3F));
        m_timestamp = (uint8_t)(0x80 | (ms & 0x7F));
        m_packet_len = 1;
    }
    return true;
}

/* Append a message with its timestamp. */
static bool put_message(const uint8_t *bytes, uint8_t n)
{
    if (!reserve(n + 1u)) {
        return false;
    }
    m_packet[m_packet_len++] = m_timestamp;
    memcpy(&m_packet[m_packet_len], bytes, n);
    m_packet_len += n;
    return true;
}

/* Append a SysEx data byte, which needs no timestamp. */
static bool put_sysex_data(uint8_t b)
{
    if (!reserve(1)) {
        return false;
    }
    m_packet[m_packet_len++] = b;
    return true;
}

/* Returns false, changing nothing, if b can't be taken yet. */
static bool tx_byte(uint8_t b)
{
    if (b >= 0xF8) {
        return put_message(&b, 1);
    }
    if (b == 0xF0) {
        if (!put_message(&b, 1)) {
            return false;
        }
        tx.sysex = true;
        tx.running = 0;
        tx.len = 0;
        return true;
    }
    if (tx.sysex) {
        if (b == 0xF7) {
            if (!put_message(&b, 1)) {
                return false;
            }
            tx.sysex = false;
            return true;
        }
        if (b < 0x80) {
            return put_sysex_data(b);
        }
        tx.sysex = false; /* unterminated SysEx; continue with this status */
    }

    if (b & 0x80) {
        if (b == 0xF7) {
            return true; /* stray end of SysEx */
        }
        tx.msg[0] = b;
        tx.len = 1;
        tx.need = data_bytes(b);
        tx.running = b < 0xF0 ? b : 0;
    } else {
        if (tx.len == 0) {
            if (tx.running == 0) {
                return true; /* data without a status */
            }
            tx.msg[0] = tx.running;
            tx.len = 1;
            tx.need = data_bytes(tx.running);
        }
        tx.msg[tx.len] = b;
        if (tx.len + 1 == 1 + tx.need) {
            if (!put_message(tx.msg, tx.len + 1)) {
                return false;
            }
            tx.len = 0;
            return true;
        }
        tx.len++;
        return true;
    }

    if (tx.need == 0) {
        if (!put_message(tx.msg, 1)) {
            return false; /* retried with the same byte */
        }
        tx.len = 0;
    }
    return true;
}

static void uart_to_ble(void)
{
    static bool held;
    static uint8_t b;

    for (;;) {
        if (!held) {
            if (app_uart_get(&b) != NRF_SUCCESS) {
                break;
            }
            held = true;
        }
        if (!can_send()) {
            held = false; /* nobody listening */
            continue;
        }
        if (!tx_byte(b)) {
            break;
        }
        held = false;
    }
}

/* ---- BLE setup ---- */

static void ble_evt_handler(ble_evt_t const *evt, void *context)
{
    (void)context;
    switch (evt->header.evt_id) {
    case BLE_GAP_EVT_CONNECTED:
        m_conn = evt->evt.gap_evt.conn_handle;
        m_notify = false;
        m_max_packet = 20;
        memset(&rx, 0, sizeof(rx));
        break;

    case BLE_GAP_EVT_DISCONNECTED:
        m_conn = BLE_CONN_HANDLE_INVALID;
        m_notify = false;
        m_packet_len = 0;
        m_packet_blocked = false;
        memset(&tx, 0, sizeof(tx));
        break;

    case BLE_GAP_EVT_PHY_UPDATE_REQUEST: {
        ble_gap_phys_t const phys = {.rx_phys = BLE_GAP_PHY_AUTO, .tx_phys = BLE_GAP_PHY_AUTO};
        APP_ERROR_CHECK(sd_ble_gap_phy_update(evt->evt.gap_evt.conn_handle, &phys));
        break;
    }

    case BLE_GAP_EVT_SEC_PARAMS_REQUEST:
        APP_ERROR_CHECK(sd_ble_gap_sec_params_reply(m_conn, BLE_GAP_SEC_STATUS_PAIRING_NOT_SUPP, NULL, NULL));
        break;

    case BLE_GATTS_EVT_SYS_ATTR_MISSING:
        APP_ERROR_CHECK(sd_ble_gatts_sys_attr_set(m_conn, NULL, 0, 0));
        break;

    case BLE_GATTC_EVT_TIMEOUT:
    case BLE_GATTS_EVT_TIMEOUT:
        (void)sd_ble_gap_disconnect(m_conn, BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION);
        break;

    case BLE_GATTS_EVT_WRITE: {
        ble_gatts_evt_write_t const *write = &evt->evt.gatts_evt.params.write;
        if (write->handle == m_midi.cccd_handle && write->len == 2) {
            m_notify = ble_srv_is_notification_enabled(write->data);
        } else if (write->handle == m_midi.value_handle) {
            rx_packet(write->data, write->len);
        }
        break;
    }

    case BLE_GATTS_EVT_HVN_TX_COMPLETE:
        m_packet_blocked = false; /* the main loop retries */
        break;

    default:
        break;
    }
}

NRF_SDH_BLE_OBSERVER(m_ble_observer, BLE_OBSERVER_PRIO, ble_evt_handler, NULL);

static void gatt_evt_handler(nrf_ble_gatt_t *gatt, nrf_ble_gatt_evt_t const *evt)
{
    (void)gatt;
    if (evt->evt_id == NRF_BLE_GATT_EVT_ATT_MTU_UPDATED && evt->conn_handle == m_conn) {
        uint16_t max = evt->params.att_mtu_effective - 3;
        m_max_packet = max < sizeof(m_packet) ? max : sizeof(m_packet);
    }
}

static void ble_init(void)
{
    APP_ERROR_CHECK(nrf_sdh_enable_request());
    uint32_t ram_start = 0;
    APP_ERROR_CHECK(nrf_sdh_ble_default_cfg_set(CONN_CFG_TAG, &ram_start));
    ble_cfg_t cfg = {0};
    cfg.conn_cfg.conn_cfg_tag = CONN_CFG_TAG;
    cfg.conn_cfg.params.gatts_conn_cfg.hvn_tx_queue_size = NOTIFY_QUEUE_SIZE;
    APP_ERROR_CHECK(sd_ble_cfg_set(BLE_CONN_CFG_GATTS, &cfg, ram_start));
    APP_ERROR_CHECK(nrf_sdh_ble_enable(&ram_start));

    ble_gap_conn_sec_mode_t open;
    BLE_GAP_CONN_SEC_MODE_SET_OPEN(&open);
    APP_ERROR_CHECK(sd_ble_gap_device_name_set(&open, (const uint8_t *)DEVICE_NAME, strlen(DEVICE_NAME)));
    ble_gap_conn_params_t params = {
        .min_conn_interval = MIN_CONN_INTERVAL,
        .max_conn_interval = MAX_CONN_INTERVAL,
        .slave_latency = 0,
        .conn_sup_timeout = SUPERVISION_TIMEOUT,
    };
    APP_ERROR_CHECK(sd_ble_gap_ppcp_set(&params));

    APP_ERROR_CHECK(nrf_ble_gatt_init(&m_gatt, gatt_evt_handler));
    APP_ERROR_CHECK(nrf_ble_gatt_att_mtu_periph_set(&m_gatt, NRF_SDH_BLE_GATT_MAX_MTU_SIZE));

    /* MIDI service with one characteristic: read (empty), write without response, notify. */
    APP_ERROR_CHECK(sd_ble_uuid_vs_add(&midi_service_base, &m_service_uuid_type));
    ble_uuid_t service = {.uuid = MIDI_SERVICE_UUID, .type = m_service_uuid_type};
    uint16_t service_handle;
    APP_ERROR_CHECK(sd_ble_gatts_service_add(BLE_GATTS_SRVC_TYPE_PRIMARY, &service, &service_handle));

    uint8_t char_uuid_type;
    APP_ERROR_CHECK(sd_ble_uuid_vs_add(&midi_char_base, &char_uuid_type));
    ble_add_char_params_t c = {0};
    c.uuid = MIDI_CHAR_UUID;
    c.uuid_type = char_uuid_type;
    c.max_len = sizeof(m_packet);
    c.init_len = 0;
    c.is_var_len = true;
    c.char_props.read = 1;
    c.char_props.write_wo_resp = 1;
    c.char_props.notify = 1;
    c.read_access = SEC_OPEN;
    c.write_access = SEC_OPEN;
    c.cccd_write_access = SEC_OPEN;
    APP_ERROR_CHECK(characteristic_add(service_handle, &c, &m_midi));

    /* Advertise the MIDI service; the name goes in the scan response. */
    ble_advertising_init_t adv = {0};
    ble_uuid_t adv_uuid = service;
    adv.advdata.flags = BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE;
    adv.advdata.uuids_complete.uuid_cnt = 1;
    adv.advdata.uuids_complete.p_uuids = &adv_uuid;
    adv.srdata.name_type = BLE_ADVDATA_FULL_NAME;
    adv.config.ble_adv_fast_enabled = true;
    adv.config.ble_adv_fast_interval = ADV_INTERVAL;
    adv.config.ble_adv_fast_timeout = 0; /* advertise until connected */
    APP_ERROR_CHECK(ble_advertising_init(&m_advertising, &adv));
    ble_advertising_conn_cfg_tag_set(&m_advertising, CONN_CFG_TAG);

    ble_conn_params_init_t cp = {0};
    cp.first_conn_params_update_delay = FIRST_PARAMS_UPDATE;
    cp.next_conn_params_update_delay = NEXT_PARAMS_UPDATE;
    cp.max_conn_params_update_count = MAX_PARAMS_UPDATES;
    cp.start_on_notify_cccd_handle = BLE_GATT_HANDLE_INVALID;
    cp.disconnect_on_fail = false;
    APP_ERROR_CHECK(ble_conn_params_init(&cp));
}

/* ---- Serial port and main loop ---- */

static void uart_evt_handler(app_uart_evt_t *evt)
{
    (void)evt; /* data is read in the main loop; errors are ignored */
}

static void flush_timer_handler(void *context)
{
    (void)context;
    m_flush_due = true;
}

int main(void)
{
    APP_ERROR_CHECK(app_timer_init());
    APP_ERROR_CHECK(nrf_pwr_mgmt_init());
    ble_init();

    const app_uart_comm_params_t uart = {
        .rx_pin_no = UART_RX_PIN,
        .tx_pin_no = UART_TX_PIN,
        .rts_pin_no = UART_PIN_DISCONNECTED,
        .cts_pin_no = UART_PIN_DISCONNECTED,
        .flow_control = APP_UART_FLOW_CONTROL_DISABLED,
        .use_parity = false,
        .baud_rate = NRF_UARTE_BAUDRATE_115200,
    };
    uint32_t err;
    APP_UART_FIFO_INIT(&uart, UART_BUFFER_SIZE, UART_BUFFER_SIZE, uart_evt_handler, APP_IRQ_PRIORITY_LOWEST, err);
    APP_ERROR_CHECK(err);

    APP_ERROR_CHECK(app_timer_create(&m_flush_timer, APP_TIMER_MODE_REPEATED, flush_timer_handler));
    APP_ERROR_CHECK(app_timer_start(m_flush_timer, FLUSH_INTERVAL, NULL));
    APP_ERROR_CHECK(ble_advertising_start(&m_advertising, BLE_ADV_MODE_FAST));

    for (;;) {
        ble_to_uart();
        uart_to_ble();
        if (m_flush_due) {
            m_flush_due = false;
            if (!m_packet_blocked) {
                (void)send_packet();
            }
        }
        if (m_packet_blocked) {
            (void)send_packet();
        }
        nrf_pwr_mgmt_run();
    }
}
