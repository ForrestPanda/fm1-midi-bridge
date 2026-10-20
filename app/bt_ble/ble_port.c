/* SPDX-License-Identifier: MIT */
#include "system/includes.h"
#include "app_config.h"
#include "ble_port.h"
#include "ble_midi.h"
#include "ble_host.h"
#include "settings.h"
#include "health.h"
#include "fm1_log.h"

extern u16 bt_ble_is_connected(void);

void ble_port_set_rx(void *priv, void (*cb)(void *, const u8 *, u16))
{
    ble_midi_set_rx(priv, cb);
#if FM1_BLE_HOST_EN
    ble_host_set_rx(priv, cb);
#endif
}

int ble_port_ready(void)
{
#if FM1_BLE_HOST_EN
    if (ble_host_active()) return ble_host_ready();
#endif
    return ble_midi_ready();
}

u16 ble_port_max_packet(void)
{
#if FM1_BLE_HOST_EN
    if (ble_host_active()) return ble_host_max_packet();
#endif
    return ble_midi_max_packet();
}

int ble_port_send(const u8 *pkt, u16 len)
{
#if FM1_BLE_HOST_EN
    if (ble_host_active()) return ble_host_send(pkt, len);
#endif
    return ble_midi_send(pkt, len);
}

void ble_port_set_mode(int mode)
{
#if FM1_BLE_HOST_EN
    if (mode == BLE_MODE_HOST) ble_host_enter(); else ble_host_exit();
#else
    (void)mode;
#endif
}

void ble_port_set_link_mode(int mode)
{
    ble_midi_set_link_mode(mode);
#if FM1_BLE_HOST_EN
    ble_host_set_link_mode(mode);
#endif
}

void ble_port_boot(void)
{
    ble_midi_set_link_mode(settings_get()->ble_link_mode);
    if (settings_get()->ble_mode != BLE_MODE_HOST) return;
    /* a stored host mode that crashes at boot must not loop: after a boot that never ran
     * clean (20 s up, health.c) the FM-1 comes up as the peripheral and says so */
    if (health_get()->unclean >= 2) { fm1_log("host mode not entered: the previous boot did not run clean; turn it on by hand"); return; }
    ble_port_set_mode(BLE_MODE_HOST);
}

const char *ble_port_state_text(void)
{
#if FM1_BLE_HOST_EN
    if (ble_host_active()) return ble_host_state_text();
#endif
    if (ble_midi_ready()) return "connected";
    if (bt_ble_is_connected()) return "connecting";
    return "advertising";
}
