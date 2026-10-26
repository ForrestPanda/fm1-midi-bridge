/* SPDX-License-Identifier: Apache-2.0 */
/* Derived from the JieLi AC79 SDK apps/common/ble/le_net_central.c (Apache-2.0): the same
 * stack calls and callback structure, rewritten and reduced to one job for the M-VAVE FM-1
 * (ForrestPanda, 2026-10). The SDK file's notice follows. See ble_host.h.
 */
/*********************************************************************************************
    *   Filename        : le_server_module.c

    *   Description     :

    *   Author          :

    *   Email           : zh-jieli.com

    *   Last modifiled  : 2017-01-17 11:14

    *   Copyright:(c)JIELI  2011-2016  @ , All Rights Reserved.
*********************************************************************************************/

/*
 *
 * Who runs what: the stack's task calls host_packet_handler and the user_client_report_*
 * hooks; ble_host_enter / ble_host_exit run in an app task (the UI, the bridge for the
 * editor, the BT event handler at boot) and sleep while a link drops, the way the SDK's
 * demo rests between the roles. The device list has a mutex; every other field has one
 * writer. */
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
#include "fm1_log.h"
#include <stdio.h>

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
#define LINK_DROP_TICKS     200         /* 10 ms ticks to wait for a disconnect event: 2 s */
#define ROLE_REST_TICKS     20          /* the SDK's demo rests 2 s between the roles; 200 ms here */
#define DISCOVER_TIMEOUT_MS 15000       /* connected but never subscribed (pairing refused, no MIDI service) */
#define CONNECT_TIMEOUT_MS  10000       /* a connection attempt that never completes is cancelled */
#define PAIR_WAIT_MS        4000        /* Pair on connect, a keyboard that never answers: discover anyway */
/* as the central we decide the parameters; two profiles (settings ble_link_mode) */
static const struct conn_update_param_t host_param_fast = {6, 6, 0, 400};      /* 7.5 ms */
static const struct conn_update_param_t host_param_eco  = {24, 40, 4, 600};    /* 30..50 ms, latency 4 */
static const struct conn_update_param_t *host_param(void)
{
    return settings_get()->ble_link_mode == BLE_LINK_POWER_SAVING ? &host_param_eco : &host_param_fast;
}

/* 7772E5DB-3868-4112-A1A9-F2669D106BF3. The stack reports 128-bit UUIDs big-endian, as
 * written (the SDK's search tables are written that way; the first unit confirmed it against
 * BlueZ: the little-endian form matched nothing). Both orders are accepted anyway. */
static const u8 midi_char_uuid_be[16] = {
    0x77, 0x72, 0xE5, 0xDB, 0x38, 0x68, 0x41, 0x12, 0xA1, 0xA9, 0xF2, 0x66, 0x9D, 0x10, 0x6B, 0xF3};
static const u8 midi_char_uuid_le[16] = {
    0xF3, 0x6B, 0x10, 0x9D, 0x66, 0xF2, 0xA9, 0xA1, 0x12, 0x41, 0x68, 0x38, 0xDB, 0xE5, 0x72, 0x77};

static u8 att_ram[HOST_ATT_RAM] __attribute__((aligned(4)));
static u8 search_ram[HOST_SEARCH_RAM] __attribute__((aligned(4)));
static volatile int state = HOST_OFF;
static volatile hci_con_handle_t con_handle;    /* set by the stack's task, polled by exit() */
static u16 midi_value_handle;
static u16 name_handle;                     /* GAP Device Name (0x2A00), read when the scan gave no name */
static u16 mtu = 20;
static ble_host_device devices[BLE_HOST_MAX_DEVICES];
static ble_host_device peer;
static OS_MUTEX mutex;
static u8 want_scan;                        /* scan again once the stack is idle */
static void (*rx_cb)(void *, const u8 *, u16);
static void *rx_priv;
static u32 reconnect_due;                   /* ms; 0 = none */
static u8 user_disconnected;                /* a manual disconnect: do not reconnect */
static u8 search_started;                   /* the service discovery of this link has begun */
static u32 discover_since;                  /* ms; when this link was made */
static u8 want_peer;                        /* the scan is after `peer` (the remembered device): connect on sight */
static u32 connect_since;                   /* ms; when the connection attempt began */
static u8 unbond_retry;                     /* the peer refused our stored keys: reconnect to it unbonded */
static u8 link_encrypted;                   /* this link reached encryption */
static u8 key_drops;                        /* links hung up by the peer before encryption, in a row */
static u8 key_drops_addr[6];                /* ... by this peer */

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

const char *ble_host_state_text(void)        /* the Scan page strips the "host: " prefix */
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
    } else {
        want_peer = 0;
        if (state == HOST_SCANNING) set_state(HOST_IDLE);
    }
    ble_op_scan_enable2(en, 0);
}

void ble_host_scan(int on)
{
    if (state == HOST_OFF) return;
    if (on && state == HOST_CONNECTING) {       /* a pending (re)connection: its cancel event starts the scan */
        want_scan = 1;
        ble_op_create_connection_cancel();
        return;
    }
    if (on && state != HOST_IDLE && state != HOST_SCANNING) { want_scan = 1; return; }
    want_scan = 0;
    scan_enable(on);
}

/* An advertiser is listed once a packet of its carries the MIDI service UUID, whether
 * the advertisement or the scan response (keyboards differ); a scan response without
 * the UUID only refreshes a listed device (its name is often only there). */
static void create_connection(void);
static void log_ad(const char *what, const adv_report_t *r)
{
    char hex[3 * 31 + 1];
    int i, n = r->length > 31 ? 31 : r->length;
    for (i = 0; i < n; i++) sprintf(hex + 3 * i, "%02X ", r->data[i]);
    hex[3 * n] = 0;
    fm1_log("host: %s %02X:%02X:%02X:%02X:%02X:%02X type %d rssi %d: %s", what, r->address[5], r->address[4],
            r->address[3], r->address[2], r->address[1], r->address[0], r->address_type, r->rssi, hex);
}

static void adv_report(adv_report_t *r)
{
    int i, slot = -1, is_peer;
    int has_midi = adv_has_midi_service(r->data, r->length);
    char name[20];
    is_peer = want_peer && r->address_type == peer.addr_type && memcmp(r->address, peer.addr, 6) == 0;
    if (!has_midi && r->event_type != 4 /* scan response */ && !is_peer) return;
    os_mutex_pend(&mutex, 0);
    for (i = 0; i < BLE_HOST_MAX_DEVICES; i++) {
        if (devices[i].used && devices[i].addr_type == r->address_type && memcmp(devices[i].addr, r->address, 6) == 0) { slot = i; break; }
        if (!devices[i].used && slot < 0) slot = i;
    }
    if (slot >= 0) {
        ble_host_device *d = &devices[slot];
        if (!d->used) {
            if (!has_midi && !is_peer) { os_mutex_post(&mutex); return; }
            memset(d, 0, sizeof *d);
            d->used = 1;
            d->addr_type = r->address_type;
            memcpy(d->addr, r->address, 6);
            log_ad("found, adv", r);                /* the raw data once per device: what keyboards put where */
        }
        d->rssi = r->rssi;
        if (r->event_type == 4 && !d->rsp_seen) { d->rsp_seen = 1; log_ad("its scan response", r); }
        if (adv_get_name(r->data, r->length, name, sizeof name) > 0) strcpy(d->name, name);
        if (is_peer && d->name[0]) strcpy(peer.name, d->name);
    }
    os_mutex_post(&mutex);
    if (is_peer) {
        /* the remembered device is back in range: the connection is made the way the SDK's
         * demo makes every one of its own, from its advertisement (a blind create connection
         * at boot, with no scan before it, sat in "connecting" for good on the first unit) */
        want_peer = 0;
        create_connection();
    }
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

static void create_connection(void)          /* to `peer` */
{
    struct create_conn_param_t p;
    if (state == HOST_SCANNING) scan_enable(0);
    else ble_op_set_scan_param(1, SCAN_INTERVAL, SCAN_WINDOW);    /* the initiator scans with these */
    memset(&p, 0, sizeof p);
    p.conn_interval = host_param()->interval_min;
    p.conn_latency = host_param()->latency;
    p.supervision_timeout = host_param()->timeout;
    p.peer_address_type = peer.addr_type;
    memcpy(p.peer_address, peer.addr, 6);
    sm_set_master_request_pair(settings_get()->ble_host_pair);
    connect_since = timer_get_ms();
    set_state(HOST_CONNECTING);
    fm1_log("host: connecting to %s %02X:%02X:%02X:%02X:%02X:%02X", peer.name, peer.addr[5], peer.addr[4],
            peer.addr[3], peer.addr[2], peer.addr[1], peer.addr[0]);
    ble_op_create_connection(&p);
}

/* connect to an address (the remembered device): scan for it, connect when it is seen */
int ble_host_connect_addr(u8 addr_type, const u8 *addr_in)
{
    u8 addr[6];
    if (state != HOST_SCANNING && state != HOST_IDLE) return -1;
    memcpy(addr, addr_in, 6);                     /* may be peer.addr itself (the unbond retry), cleared below */
    memset(&peer, 0, sizeof peer);
    peer.addr_type = addr_type; memcpy(peer.addr, addr, 6); peer.used = 1;
    fm1_log("host: looking for %02X:%02X:%02X:%02X:%02X:%02X", addr[5], addr[4], addr[3], addr[2], addr[1], addr[0]);
    if (state != HOST_SCANNING) scan_enable(1);
    want_peer = 1;
    return 0;
}

int ble_host_connect(int index)
{
    ble_host_device list[BLE_HOST_MAX_DEVICES];
    int n = ble_host_devices(list, BLE_HOST_MAX_DEVICES);
    if (state != HOST_SCANNING && state != HOST_IDLE) return -1;
    if (index < 0 || index >= n) return -1;
    peer = list[index];
    user_disconnected = 0;
    want_peer = 0;
    create_connection();
    return 0;
}

void ble_host_disconnect(void)
{
    user_disconnected = 1;
    reconnect_due = 0;
    if (state == HOST_CONNECTING) { ble_op_create_connection_cancel(); set_state(HOST_IDLE); return; }
    if (con_handle) { set_state(HOST_DISCONNECTING); ble_op_disconnect(con_handle); }
}

static void start_search(void);

/* called every ~100 ms by the UI task: the reconnection timer, the discovery watchdog */
void ble_host_tick(void)
{
    u32 now = timer_get_ms();
    if (state == HOST_OFF) return;
    /* Pair on connect waits for the encryption before discovering. A keyboard that does not pair at
     * all (the Casio WU-BT10 adapter, 2026-10-20) never answers the request, and the link was dropped 15 s
     * later on every attempt: go on without encryption. A pairing that completes later finds the
     * discovery already running. */
    if (state == HOST_DISCOVERING && !search_started && settings_get()->ble_host_pair &&
        (s32)(now - discover_since) > PAIR_WAIT_MS) {
        fm1_log("host: no pairing in %d s, going on without", PAIR_WAIT_MS / 1000);
        start_search();
    }
    if (state == HOST_DISCOVERING && (s32)(now - discover_since) > DISCOVER_TIMEOUT_MS) {
        fm1_log("host: no MIDI subscription after %d s, dropping the link", DISCOVER_TIMEOUT_MS / 1000);
        discover_since = now;
        if (con_handle) { set_state(HOST_DISCONNECTING); ble_op_disconnect(con_handle); }
        return;
    }
    if (state == HOST_CONNECTING && (s32)(now - connect_since) > CONNECT_TIMEOUT_MS) {
        fm1_log("host: the connection attempt timed out, cancelled");
        connect_since = now;
        ble_op_create_connection_cancel();
        set_state(HOST_IDLE);                 /* its event, if any, finds the host idle again */
        return;
    }
    /* a remembered device, auto reconnect on and no manual disconnect: look for it. Idle:
     * after a short rest. Scanning already (the Scan page is open, or the scan follows a drop
     * seen from that page on the first unit): the sighting connects, the page shows it. */
    if (!settings_get()->ble_host_auto || !has_stored_peer() || user_disconnected) return;
    if (state == HOST_SCANNING) {
        if (!want_peer) {
            memset(&peer, 0, sizeof peer);
            peer.addr_type = settings_get()->host_peer[0]; memcpy(peer.addr, settings_get()->host_peer + 1, 6); peer.used = 1;
            want_peer = 1;
        }
        return;
    }
    if (state != HOST_IDLE) return;
    if (!reconnect_due) { reconnect_due = now + 2000; return; }
    if ((s32)(now - reconnect_due) < 0) return;
    reconnect_due = 0;
    ble_host_connect_addr(settings_get()->host_peer[0], settings_get()->host_peer + 1);
}

/* ---------------------------------------------------------------- discovery hooks (called by the stack) */
static void start_search(void)
{
    if (search_started) return;
    search_started = 1;
    user_client_init(con_handle, search_ram, HOST_SEARCH_RAM);
    ble_op_search_profile_all();
}

void user_client_report_search_result(search_result_t *r)
{
    if (!con_handle) return;                      /* the link went while the search ran (a weak WU-BT10 on the first unit) */
    if (r == (void *)-1) {
        if (midi_value_handle) {
            u8 ccc[2] = {0x01, 0x00};
            fm1_log("host: MIDI characteristic at %04x, subscribing", midi_value_handle);
            ble_op_att_send_data((u16)(midi_value_handle + 1), ccc, 2, ATT_OP_WRITE);
            set_state(HOST_READY);
            user_disconnected = 0;
            memcpy(settings_get()->host_peer, &peer.addr_type, 7);
            settings_mark_dirty();
            if (!peer.name[0] && name_handle) {     /* connected from its address, before any scan response */
                u16 tag = 0x55A1;                   /* the SDK's demo passes a marker as the read's data */
                ble_op_att_send_data(name_handle, (u8 *)&tag, 2, ATT_OP_READ);
            }
        } else {
            fm1_log("host: no MIDI characteristic on this device, dropping the link");
            ble_host_disconnect();
        }
        return;
    }
    if (r->characteristic.uuid16 == 0x2A00) name_handle = r->characteristic.value_handle;
    if (r->characteristic.uuid16) return;
    fm1_log("host: characteristic %02X%02X%02X%02X.. value handle %04x props %02x", r->characteristic.uuid128[0],
            r->characteristic.uuid128[1], r->characteristic.uuid128[2], r->characteristic.uuid128[3],
            r->characteristic.value_handle, r->characteristic.properties);
    if (memcmp(r->characteristic.uuid128, midi_char_uuid_be, 16) == 0 || memcmp(r->characteristic.uuid128, midi_char_uuid_le, 16) == 0)
        midi_value_handle = r->characteristic.value_handle;
}

void user_client_report_data_callback(att_data_report_t *d)
{
    if (d->packet_type == GATT_EVENT_NOTIFICATION && d->value_handle == midi_value_handle && rx_cb)
        rx_cb(rx_priv, d->blob, d->blob_length);
    else if (d->packet_type == GATT_EVENT_CHARACTERISTIC_VALUE_QUERY_RESULT && d->value_handle == name_handle && name_handle) {
        int i, n = d->blob_length < (int)sizeof peer.name - 1 ? d->blob_length : (int)sizeof peer.name - 1;
        memcpy(peer.name, d->blob, (u32)n);
        peer.name[n] = 0;
        fm1_log("host: peer name \"%s\"", peer.name);
        os_mutex_pend(&mutex, 0);                   /* the Scan page's list shows it too */
        for (i = 0; i < BLE_HOST_MAX_DEVICES; i++)
            if (devices[i].used && devices[i].addr_type == peer.addr_type && memcmp(devices[i].addr, peer.addr, 6) == 0) strcpy(devices[i].name, peer.name);
        os_mutex_post(&mutex);
    }
}

void user_client_report_descriptor_result(charact_descriptor_t *r) { (void)r; }

/* the peripheral asks for other link parameters (called by the stack; 0 accepts, 1 rejects):
 * a keyboard that wants a slow link gets it only in power saving, else it stays on ours */
int l2cap_connection_update_request_just(u8 *packet, hci_con_handle_t handle)
{
    u16 imin = little_endian_read_16(packet, 0), imax = little_endian_read_16(packet, 2);
    int ok = settings_get()->ble_link_mode == BLE_LINK_POWER_SAVING || imin <= 12;
    (void)handle;
    fm1_log("host: peer asks interval %d..%d latency %d timeout %d: %s", imin, imax,
            little_endian_read_16(packet, 4), little_endian_read_16(packet, 6), ok ? "accepted" : "refused");
    return ok ? 0 : 1;
}

/* ---------------------------------------------------------------- stack events */
/* The bond list keys a device by its address in the other byte order from the one the advertising
 * reports give (seen 2026-10-20: the PC's bond found only reversed). Both, in case a stack differs. */
static void peer_addr_reversed(u8 *rev) { int k; for (k = 0; k < 6; k++) rev[k] = peer.addr[5 - k]; }
static int peer_bonded(void)
{
    u8 rev[6];
    peer_addr_reversed(rev);
    return ble_list_check_addr_is_exist(rev, peer.addr_type) || ble_list_check_addr_is_exist(peer.addr, peer.addr_type);
}
static void peer_forget_bond(void)
{
    u8 rev[6];
    peer_addr_reversed(rev);
    ble_list_delete_device(rev, peer.addr_type);
    ble_list_delete_device(peer.addr, peer.addr_type);
    fm1_log("host: bond still there after forgetting: %d", peer_bonded());
}

static void link_up(hci_con_handle_t h, u16 interval, u16 latency)
{
    con_handle = h;
    if (!peer.name[0]) {                          /* connected from its address: the name is in the scan list */
        ble_host_device list[BLE_HOST_MAX_DEVICES];
        int n = ble_host_devices(list, BLE_HOST_MAX_DEVICES), i;
        for (i = 0; i < n; i++)
            if (list[i].addr_type == peer.addr_type && memcmp(list[i].addr, peer.addr, 6) == 0) { strcpy(peer.name, list[i].name); break; }
    }
    ble_op_att_send_init(h, att_ram, HOST_ATT_RAM, HOST_MTU);
    midi_value_handle = 0;
    name_handle = 0;
    search_started = 0;
    link_encrypted = 0;
    discover_since = timer_get_ms();
    set_state(HOST_DISCOVERING);
    fm1_log("host: connected to %s, handle %x, interval %d latency %d%s", peer.name, h, interval, latency,
            settings_get()->ble_host_pair ? ", pairing first" : "");
    /* with pairing on, the SDK's demo discovers only once the link is encrypted */
    if (!settings_get()->ble_host_pair) start_search();
}

static void link_failed(u8 status)
{
    fm1_log("host: connect failed %02x", status);
    set_state(HOST_IDLE);
    if (want_scan) ble_host_scan(1);
    else if (!user_disconnected) reconnect_due = timer_get_ms() + 5000;
}

static void host_packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size)
{
    (void)channel; (void)size;
    if (packet_type != HCI_EVENT_PACKET) return;
    switch (hci_event_packet_get_type(packet)) {
    case HCI_EVENT_LE_META:
        switch (hci_event_le_meta_get_subevent_code(packet)) {
        case HCI_SUBEVENT_LE_CONNECTION_COMPLETE:
            if (packet[3]) link_failed(packet[3]);
            else link_up(hci_subevent_le_connection_complete_get_connection_handle(packet),
                         hci_subevent_le_connection_complete_get_conn_interval(packet),
                         hci_subevent_le_connection_complete_get_conn_latency(packet));
            break;
        case HCI_SUBEVENT_LE_ENHANCED_CONNECTION_COMPLETE:
            if (hci_subevent_le_enhanced_connection_complete_get_status(packet))
                link_failed(hci_subevent_le_enhanced_connection_complete_get_status(packet));
            else link_up(hci_subevent_le_enhanced_connection_complete_get_connection_handle(packet),
                         hci_subevent_le_enhanced_connection_complete_get_conn_interval(packet),
                         hci_subevent_le_enhanced_connection_complete_get_conn_latency(packet));
            break;
        case HCI_SUBEVENT_LE_CONNECTION_UPDATE_COMPLETE:
            fm1_log("host: link update status %d interval %d latency %d",
                    hci_subevent_le_connection_update_complete_get_status(packet),
                    hci_subevent_le_connection_update_complete_get_conn_interval(packet),
                    hci_subevent_le_connection_update_complete_get_conn_latency(packet));
            break;
        }
        break;
    case HCI_EVENT_DISCONNECTION_COMPLETE:
        fm1_log("host: disconnected, reason %02x", packet[5]);
        /* The other way a peer refuses our stored keys: BlueZ told to forget the FM-1 answers the
         * encryption by hanging up (13) within ~100 ms, no 06 (2026-10-20), and the reconnect did the
         * same every 4 s. Twice in a row, unencrypted, within 2 s of the link and while we hold a
         * bond for it: forget the bond, as for 06. */
        if (state == HOST_DISCOVERING && !link_encrypted && (packet[5] == 0x13 || packet[5] == 0x05) &&
            (s32)(timer_get_ms() - discover_since) < 2000 && peer_bonded()) {
            if (memcmp(key_drops_addr, peer.addr, 6)) { memcpy(key_drops_addr, peer.addr, 6); key_drops = 0; }
            if (++key_drops >= 2) {
                fm1_log("host: %s hangs up at encryption, forgetting our stored keys", peer.name);
                peer_forget_bond();
                key_drops = 0;
                unbond_retry = 1;
            }
        }
        con_handle = 0;
        midi_value_handle = 0;
        ble_op_att_send_init(0, 0, 0, 0);
        if (state != HOST_OFF) {
            set_state(HOST_IDLE);
            if (unbond_retry) {                            /* the same device again, now without keys */
                char name[sizeof peer.name];
                unbond_retry = 0;
                memcpy(name, peer.name, sizeof name);
                if (ble_host_connect_addr(peer.addr_type, peer.addr) == 0) memcpy(peer.name, name, sizeof name);
            } else if (want_scan) {
                ble_host_scan(1);                          /* else the tick reconnects to the remembered device */
            }
        }
        break;
    case HCI_EVENT_ENCRYPTION_CHANGE:
        fm1_log("host: encryption change status %02x enabled %d", hci_event_encryption_change_get_status(packet),
                hci_event_encryption_change_get_encryption_enabled(packet));
        if (hci_event_encryption_change_get_status(packet) == 0) { link_encrypted = 1; key_drops = 0; }
        if (state == HOST_DISCOVERING && hci_event_encryption_change_get_status(packet) == 0) start_search();
        /* Our stored keys for this device and its are no longer the same (06: key missing; it was
         * reset or forgot us): the stack encrypted on its own, failed, and the discovery started at
         * connect never finished, so the link was dropped 15 s later, on every attempt, until Forget
         * bonds (the PC, 2026-10-20). Forget this one bond and connect again: no encryption then, or a
         * fresh pairing with Pair on connect. */
        else if (state == HOST_DISCOVERING && con_handle) {
            fm1_log("host: %s refused our stored keys, forgetting them and reconnecting", peer.name);
            peer_forget_bond();
            unbond_retry = 1;
            set_state(HOST_DISCONNECTING);
            ble_op_disconnect(con_handle);
        }
        break;
    case ATT_EVENT_MTU_EXCHANGE_COMPLETE:
        mtu = (u16)(att_event_mtu_exchange_complete_get_MTU(packet) - 3);
        ble_op_att_set_send_mtu(mtu);
        fm1_log("host: MTU %d", mtu + 3);
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
    /* the central changes the parameters itself (ble_op_conn_param_request is the slave's
     * L2CAP request: used here, it reset the first unit on a settings write in host mode) */
    if (con_handle) ble_op_conn_param_update(con_handle, (void *)host_param());
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
    int i;
    if (state != HOST_OFF) return;
    if (!os_mutex_valid(&mutex)) os_mutex_create(&mutex);
    set_state(HOST_STARTING);
    fm1_log("host: enter%s", bt_ble_is_connected() ? ", dropping the peripheral link" : "");
    bt_ble_adv_enable(0);
    if (bt_ble_is_connected()) {
        /* the phone's disconnect event must reach the peripheral's handler, which clears its
         * state; switching the handlers before it arrives would leave a stale link behind */
        ble_app_disconnect();
        for (i = 0; i < LINK_DROP_TICKS && bt_ble_is_connected(); i++) os_time_dly(1);
        if (bt_ble_is_connected()) fm1_log("host: the peripheral link did not drop in time");
    }
    os_time_dly(ROLE_REST_TICKS);
    if (!client_inited) { gatt_client_init(); client_inited = 1; }
    ble_stack_gatt_role(1);
    gatt_client_register_packet_handler(host_packet_handler);
    hci_event_callback_set(&host_packet_handler);
    le_l2cap_register_packet_handler(&host_packet_handler);
    sm_set_master_request_pair(settings_get()->ble_host_pair);
    mtu = 20;
    con_handle = 0;
    midi_value_handle = 0;
    set_state(HOST_IDLE);
    user_disconnected = 0;
    if (settings_get()->ble_host_auto && has_stored_peer()) reconnect_due = timer_get_ms() + 500;
    else scan_enable(1);
}

void ble_host_exit(void)
{
    int i;
    if (state == HOST_OFF) return;
    want_scan = 0;
    want_peer = 0;
    reconnect_due = 0;
    user_disconnected = 1;
    fm1_log("host: exit");
    if (state == HOST_SCANNING) scan_enable(0);
    if (state == HOST_CONNECTING) ble_op_create_connection_cancel();
    if (con_handle) {
        /* the disconnect event belongs to this handler; the peripheral's must not see it */
        set_state(HOST_DISCONNECTING);
        ble_op_disconnect(con_handle);
        for (i = 0; i < LINK_DROP_TICKS && con_handle; i++) os_time_dly(1);
        if (con_handle) fm1_log("host: the link did not drop in time");
    }
    set_state(HOST_OFF);
    con_handle = 0;
    midi_value_handle = 0;
    os_time_dly(ROLE_REST_TICKS);
    ble_midi_role_peripheral();                             /* role 0, handlers, advertising */
}

#endif /* FM1_BLE_HOST_EN */
