/* SPDX-License-Identifier: MIT */
/* Written after the SDK's apps/common/ble/le_net_central.c (Apache-2.0): the same
 * stack calls, reduced to one job. See ble_host.h. */
#include "system/includes.h"
#include "app_config.h"
#include "os/os_api.h"
#include "btstack/btstack_task.h"
#include "btstack/bluetooth.h"
#include "btcontroller_modules.h"
#include "bt_common.h"
#include "le_common.h"
#include "third_party/common/ble_user.h"
#include "ble_host.h"
#include "adv_parse.h"
#include "settings.h"

#define LOG_TAG_CONST       APP
#define LOG_TAG             "[BLE-HOST]"
#define LOG_ERROR_ENABLE
#define LOG_INFO_ENABLE
#include "debug.h"

#if FM1_BLE_HOST_EN

#define HOST_MTU            100
#define HOST_SEND_CBUF      512
#define HOST_ATT_RAM        (ATT_CTRL_BLOCK_SIZE + HOST_MTU + HOST_SEND_CBUF)
#define HOST_SEARCH_RAM     512
#define SCAN_INTERVAL       0x30        /* x 0.625 ms = 30 ms */
#define SCAN_WINDOW         0x20
/* as the central we decide the parameters; two profiles (settings ble_link_mode) */
static const struct conn_update_param_t host_param_fast = {6, 6, 0, 400};      /* 7.5 ms */
static const struct conn_update_param_t host_param_eco  = {24, 40, 4, 600};    /* 30..50 ms, latency 4 */
static const struct conn_update_param_t *host_param(void)
{
    return settings_get()->ble_link_mode == BLE_LINK_POWER_SAVING ? &host_param_eco : &host_param_fast;
}

static const u8 midi_char_uuid_le[16] = {   /* 7772E5DB-3868-4112-A1A9-F2669D106BF3 */
    0xF3, 0x6B, 0x10, 0x9D, 0x66, 0xF2, 0xA9, 0xA1, 0x12, 0x41, 0x68, 0x38, 0xDB, 0xE5, 0x72, 0x77};

static u8 att_ram[HOST_ATT_RAM] __attribute__((aligned(4)));
static u8 search_ram[HOST_SEARCH_RAM] __attribute__((aligned(4)));
static volatile int state = HOST_OFF;
static hci_con_handle_t con_handle;
static u16 midi_value_handle;
static u16 mtu = 20;
static ble_host_device devices[BLE_HOST_MAX_DEVICES];
static ble_host_device peer;
static OS_MUTEX mutex;
static u8 want_scan;                        /* scan again once the stack is idle */
static void (*rx_cb)(void *, const u8 *, u16);
static void *rx_priv;
static u32 reconnect_due;                   /* ms; 0 = none */
static u8 user_disconnected;                /* a manual disconnect: do not reconnect */

extern void gatt_client_init(void);
extern void gatt_client_register_packet_handler(btstack_packet_handler_t handler);
extern u8 get_ble_gatt_role(void);
extern void sm_set_master_request_pair(int enable);
extern void ble_midi_role_peripheral(void);
extern void ble_app_disconnect(void);
extern u16 bt_ble_is_connected(void);
extern void bt_ble_adv_enable(u8 enable);

static void set_state(int s) { if (state != s) { log_info("host state %d -> %d", state, s); state = s; } }
int ble_host_state(void) { return state; }
int ble_host_active(void) { return state != HOST_OFF; }
int ble_host_ready(void) { return state == HOST_READY; }
u16 ble_host_max_packet(void) { return mtu; }
const ble_host_device *ble_host_peer(void) { return &peer; }

const char *ble_host_state_text(void)
{
    switch (state) {
    case HOST_OFF: return "off";
    case HOST_STARTING: return "host: starting";
    case HOST_IDLE: return "host: idle";
    case HOST_SCANNING: return "host: scanning";
    case HOST_CONNECTING: return "host: connecting";
    case HOST_DISCOVERING: return "host: discovering";
    case HOST_READY: return "host: connected";
    case HOST_DISCONNECTING: return "host: disconnecting";
    default: return "host";
    }
}

void ble_host_set_rx(void *priv, void (*cb)(void *, const u8 *, u16)) { rx_priv = priv; rx_cb = cb; }

/* ---------------------------------------------------------------- scanning */
static void scan_enable(int en)
{
    if (en) {
        memset(devices, 0, sizeof devices);
        ble_op_set_scan_param(1 /* active */, SCAN_INTERVAL, SCAN_WINDOW);
        set_state(HOST_SCANNING);
    } else if (state == HOST_SCANNING) {
        set_state(HOST_IDLE);
    }
    ble_op_scan_enable2(en, 0);
}

void ble_host_scan(int on)
{
    if (state == HOST_OFF) return;
    if (on && state != HOST_IDLE && state != HOST_SCANNING) { want_scan = 1; return; }
    want_scan = 0;
    scan_enable(on);
}

static void adv_report(adv_report_t *r)
{
    int i, slot = -1;
    char name[20];
    if (!adv_has_midi_service(r->data, r->length) && !(r->event_type == 4 /* scan response */)) return;
    os_mutex_pend(&mutex, 0);
    for (i = 0; i < BLE_HOST_MAX_DEVICES; i++) {
        if (devices[i].used && devices[i].addr_type == r->address_type && memcmp(devices[i].addr, r->address, 6) == 0) { slot = i; break; }
        if (!devices[i].used && slot < 0) slot = i;
    }
    if (slot >= 0) {
        ble_host_device *d = &devices[slot];
        if (!d->used) {
            if (r->event_type == 4) { os_mutex_post(&mutex); return; }   /* a scan response alone: not ours */
            memset(d, 0, sizeof *d);
            d->used = 1;
            d->addr_type = r->address_type;
            memcpy(d->addr, r->address, 6);
        }
        d->rssi = r->rssi;
        if (adv_get_name(r->data, r->length, name, sizeof name) > 0) strcpy(d->name, name);
    }
    os_mutex_post(&mutex);
}

int ble_host_devices(ble_host_device *out, int max)
{
    int i, n = 0;
    os_mutex_pend(&mutex, 0);
    for (i = 0; i < BLE_HOST_MAX_DEVICES && n < max; i++)
        if (devices[i].used) out[n++] = devices[i];
    os_mutex_post(&mutex);
    return n;
}

/* ---------------------------------------------------------------- connecting */
static int has_stored_peer(void)
{
    const u8 *p = settings_get()->host_peer;
    return (p[1] | p[2] | p[3] | p[4] | p[5] | p[6]) != 0;
}

/* connect straight to an address (the remembered device) */
int ble_host_connect_addr(u8 addr_type, const u8 *addr)
{
    struct create_conn_param_t p;
    if (state != HOST_SCANNING && state != HOST_IDLE) return -1;
    if (state == HOST_SCANNING) scan_enable(0);
    memset(&peer, 0, sizeof peer);
    peer.addr_type = addr_type; memcpy(peer.addr, addr, 6); peer.used = 1;
    memset(&p, 0, sizeof p);
    p.conn_interval = host_param()->interval_min;
    p.conn_latency = host_param()->latency;
    p.supervision_timeout = host_param()->timeout;
    p.peer_address_type = addr_type;
    memcpy(p.peer_address, addr, 6);
    set_state(HOST_CONNECTING);
    ble_op_create_connection(&p);
    return 0;
}

/* called every ~100 ms by the UI task: reconnection timer */
void ble_host_tick(void)
{
    if (state == HOST_OFF || !reconnect_due) return;
    if ((s32)(timer_get_ms() - reconnect_due) < 0) return;
    reconnect_due = 0;
    if (state == HOST_IDLE && settings_get()->ble_host_auto && has_stored_peer() && !user_disconnected)
        ble_host_connect_addr(settings_get()->host_peer[0], settings_get()->host_peer + 1);
}

int ble_host_connect(int index)
{
    ble_host_device list[BLE_HOST_MAX_DEVICES];
    struct create_conn_param_t p;
    int n = ble_host_devices(list, BLE_HOST_MAX_DEVICES);
    if (state != HOST_SCANNING && state != HOST_IDLE) return -1;
    if (index < 0 || index >= n) return -1;
    peer = list[index];
    user_disconnected = 0;
    if (state == HOST_SCANNING) scan_enable(0);
    memset(&p, 0, sizeof p);
    p.conn_interval = host_param()->interval_min;
    p.conn_latency = host_param()->latency;
    p.supervision_timeout = host_param()->timeout;
    p.peer_address_type = peer.addr_type;
    memcpy(p.peer_address, peer.addr, 6);
    set_state(HOST_CONNECTING);
    log_info("connect to %s", peer.name);
    ble_op_create_connection(&p);
    return 0;
}

void ble_host_disconnect(void)
{
    user_disconnected = 1;
    reconnect_due = 0;
    if (state == HOST_CONNECTING) { ble_op_create_connection_cancel(); set_state(HOST_IDLE); return; }
    if (con_handle) { set_state(HOST_DISCONNECTING); ble_op_disconnect(con_handle); }
}

/* ---------------------------------------------------------------- discovery hooks (called by the stack) */
void user_client_report_search_result(search_result_t *r)
{
    if (r == (void *)-1) {
        if (midi_value_handle) {
            u8 ccc[2] = {0x01, 0x00};
            log_info("subscribe handle %04x", midi_value_handle + 1);
            ble_op_att_send_data((u16)(midi_value_handle + 1), ccc, 2, ATT_OP_WRITE);
            set_state(HOST_READY);
            user_disconnected = 0;
            memcpy(settings_get()->host_peer, &peer.addr_type, 7);
            settings_mark_dirty();
        } else {
            log_error("no MIDI characteristic on this device");
            ble_host_disconnect();
        }
        return;
    }
    if (!r->characteristic.uuid16 && memcmp(r->characteristic.uuid128, midi_char_uuid_le, 16) == 0)
        midi_value_handle = r->characteristic.value_handle;
}

void user_client_report_data_callback(att_data_report_t *d)
{
    if (d->packet_type == GATT_EVENT_NOTIFICATION && d->value_handle == midi_value_handle && rx_cb)
        rx_cb(rx_priv, d->blob, d->blob_length);
}

void user_client_report_descriptor_result(charact_descriptor_t *r) { (void)r; }

/* ---------------------------------------------------------------- stack events */
static void host_packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size)
{
    (void)channel; (void)size;
    if (packet_type != HCI_EVENT_PACKET) return;
    switch (hci_event_packet_get_type(packet)) {
    case HCI_EVENT_LE_META:
        switch (hci_event_le_meta_get_subevent_code(packet)) {
        case HCI_SUBEVENT_LE_CONNECTION_COMPLETE:
            if (packet[3]) { log_error("connect failed %02x", packet[3]); set_state(HOST_IDLE); if (want_scan) ble_host_scan(1); else reconnect_due = timer_get_ms() + 5000; break; }
            con_handle = hci_subevent_le_connection_complete_get_connection_handle(packet);
            ble_op_att_send_init(con_handle, att_ram, HOST_ATT_RAM, HOST_MTU);
            midi_value_handle = 0;
            set_state(HOST_DISCOVERING);
            user_client_init(con_handle, search_ram, HOST_SEARCH_RAM);
            ble_op_search_profile_all();
            break;
        case HCI_SUBEVENT_LE_ENHANCED_CONNECTION_COMPLETE:
            if (hci_subevent_le_enhanced_connection_complete_get_status(packet)) { set_state(HOST_IDLE); ble_host_scan(1); break; }
            con_handle = hci_subevent_le_enhanced_connection_complete_get_connection_handle(packet);
            ble_op_att_send_init(con_handle, att_ram, HOST_ATT_RAM, HOST_MTU);
            midi_value_handle = 0;
            set_state(HOST_DISCOVERING);
            user_client_init(con_handle, search_ram, HOST_SEARCH_RAM);
            ble_op_search_profile_all();
            break;
        }
        break;
    case HCI_EVENT_DISCONNECTION_COMPLETE:
        con_handle = 0;
        midi_value_handle = 0;
        ble_op_att_send_init(0, 0, 0, 0);
        if (state != HOST_OFF) {
            set_state(HOST_IDLE);
            if (want_scan) ble_host_scan(1);
            else if (!user_disconnected && settings_get()->ble_host_auto && has_stored_peer()) reconnect_due = timer_get_ms() + 2000;
        }
        break;
    case ATT_EVENT_MTU_EXCHANGE_COMPLETE:
        mtu = (u16)(att_event_mtu_exchange_complete_get_MTU(packet) - 3);
        ble_op_att_set_send_mtu(mtu);
        break;
    case GAP_EVENT_ADVERTISING_REPORT:
        if (state == HOST_SCANNING) adv_report((void *)&packet[2]);
        break;
    default:
        break;
    }
}

void ble_host_set_link_mode(int mode)
{
    (void)mode;                                   /* read from settings when needed */
    if (con_handle) ble_op_conn_param_request(con_handle, (void *)host_param());
}

int ble_host_send(const u8 *pkt, u16 len)
{
    u32 room = 0;
    if (state != HOST_READY || !midi_value_handle) return -1;
    ble_op_att_get_remain(&room);
    if (room < len) return -2;
    return ble_op_att_send_data(midi_value_handle, pkt, len, ATT_OP_WRITE_WITHOUT_RESPOND);
}

/* ---------------------------------------------------------------- mode switch */
static u8 client_inited;

void ble_host_enter(void)
{
    if (state != HOST_OFF) return;
    if (!os_mutex_valid(&mutex)) os_mutex_create(&mutex);
    set_state(HOST_STARTING);
    bt_ble_adv_enable(0);
    if (bt_ble_is_connected()) ble_app_disconnect();        /* the disconnect event ends the peripheral link */
    if (!client_inited) { gatt_client_init(); client_inited = 1; }
    ble_stack_gatt_role(1);
    gatt_client_register_packet_handler(host_packet_handler);
    hci_event_callback_set(&host_packet_handler);
    le_l2cap_register_packet_handler(&host_packet_handler);
    sm_set_master_request_pair(settings_get()->ble_host_pair);
    mtu = 20;
    set_state(HOST_IDLE);
    user_disconnected = 0;
    if (settings_get()->ble_host_auto && has_stored_peer()) reconnect_due = timer_get_ms() + 500;
    else scan_enable(1);
}

void ble_host_exit(void)
{
    if (state == HOST_OFF) return;
    want_scan = 0;
    if (state == HOST_SCANNING) scan_enable(0);
    if (state == HOST_CONNECTING) ble_op_create_connection_cancel();
    if (con_handle) ble_op_disconnect(con_handle);
    set_state(HOST_OFF);
    ble_midi_role_peripheral();                             /* role 0, handlers, advertising */
}

#endif /* FM1_BLE_HOST_EN */
