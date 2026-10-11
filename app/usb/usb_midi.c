/* SPDX-License-Identifier: Apache-2.0 */
/* USB-MIDI 1.0 class on the SDK USB device stack: see usb_midi.h. */
#include "usb/device/usb_stack.h"
#include "usb/usb_config.h"
#include "app_config.h"
#include "os/os_api.h"
#include "usb_midi.h"

#define LOG_TAG_CONST       USB
#define LOG_TAG             "[USB-MIDI]"
#define LOG_ERROR_ENABLE
#define LOG_INFO_ENABLE
#include "debug.h"

#if TCFG_USB_SLAVE_MIDI_ENABLE

struct usb_midi_gadget {
    u8 *rx_buf;                 /* copy of the last OUT packet (bulk is double buffered in the SIE) */
    u8 *ep_in_buf;              /* DMA buffers */
    u8 *ep_out_buf;
    void *priv;
    void (*rx)(void *priv, const u8 *pkts, u32 len);
    OS_MUTEX tx_mutex;
    u8 configured;
};

static struct usb_midi_gadget *midi_hdl[USB_MAX_HW_NUM];
static void (*pending_rx[USB_MAX_HW_NUM])(void *, const u8 *, u32);   /* set before register() */
static void *pending_priv[USB_MAX_HW_NUM];

/* USB Device Class Definition for MIDI Devices 1.0, Appendix B, trimmed to one
 * cable. Interface numbers and endpoint addresses are patched in at run time. */
static const u8 midi_desc[] = {
    /* Standard AC interface descriptor */
    0x09, 0x04, 0x00, 0x00, 0x00, 0x01, 0x01, 0x00, 0x00,
    /* Class-specific AC interface header: bcdADC 1.00, wTotalLength 9, 1 MS interface */
    0x09, 0x24, 0x01, 0x00, 0x01, 0x09, 0x00, 0x01, 0x01,
    /* Standard MS interface descriptor: 2 endpoints, class 1 subclass 3 */
    0x09, 0x04, 0x01, 0x00, 0x02, 0x01, 0x03, 0x00, 0x00,
    /* Class-specific MS header: bcdMSC 1.00, wTotalLength = 65 */
    0x07, 0x24, 0x01, 0x00, 0x01, 0x41, 0x00,
    /* MIDI IN jack, embedded, id 1 */
    0x06, 0x24, 0x02, 0x01, 0x01, 0x00,
    /* MIDI IN jack, external, id 2 */
    0x06, 0x24, 0x02, 0x02, 0x02, 0x00,
    /* MIDI OUT jack, embedded, id 3, source = jack 2 pin 1 */
    0x09, 0x24, 0x03, 0x01, 0x03, 0x01, 0x02, 0x01, 0x00,
    /* MIDI OUT jack, external, id 4, source = jack 1 pin 1 */
    0x09, 0x24, 0x03, 0x02, 0x04, 0x01, 0x01, 0x01, 0x00,
    /* Bulk OUT endpoint + class-specific: 1 embedded IN jack (1) */
    0x09, 0x05, 0x01, 0x02, 0x40, 0x00, 0x00, 0x00, 0x00,
    0x05, 0x25, 0x01, 0x01, 0x01,
    /* Bulk IN endpoint + class-specific: 1 embedded OUT jack (3) */
    0x09, 0x05, 0x81, 0x02, 0x40, 0x00, 0x00, 0x00, 0x00,
    0x05, 0x25, 0x01, 0x01, 0x03,
};
#define D_AC_ITF        0
#define D_AC_HDR        (D_AC_ITF + 9)
#define D_MS_ITF        (D_AC_HDR + 9)
#define D_EP_OUT        (D_MS_ITF + 9 + 7 + 6 + 6 + 9 + 9)
#define D_EP_IN         (D_EP_OUT + 9 + 5)

static void midi_rx_isr(struct usb_device_t *usb_device, u32 ep)
{
    const usb_dev usb_id = usb_device2id(usb_device);
    struct usb_midi_gadget *g = midi_hdl[usb_id];
    u32 len;
    (void)ep;
    if (!g) return;
    len = usb_g_bulk_read(usb_id, MIDI_EP_OUT, g->rx_buf, MAXP_SIZE_MIDI_BULK, 0);
    len &= ~3u;
    if (len && g->rx) g->rx(g->priv, g->rx_buf, len);
}

static void midi_endpoint_init(struct usb_device_t *usb_device, u32 itf)
{
    const usb_dev usb_id = usb_device2id(usb_device);
    struct usb_midi_gadget *g = midi_hdl[usb_id];
    (void)itf;
    ASSERT(g, "usb midi not registered");
    usb_g_ep_config(usb_id, MIDI_EP_IN | USB_DIR_IN, USB_ENDPOINT_XFER_BULK, 0, g->ep_in_buf, MAXP_SIZE_MIDI_BULK);
    usb_g_ep_config(usb_id, MIDI_EP_OUT | USB_DIR_OUT, USB_ENDPOINT_XFER_BULK, 1, g->ep_out_buf, MAXP_SIZE_MIDI_BULK);
    usb_g_set_intr_hander(usb_id, MIDI_EP_OUT | USB_DIR_OUT, midi_rx_isr);
    usb_enable_ep(usb_id, MIDI_EP_IN);
    g->configured = 1;
}

static void midi_reset(struct usb_device_t *usb_device, u32 itf)
{
    const usb_dev usb_id = usb_device2id(usb_device);
    log_info("%s itf %d", __func__, itf);
#if USB_ROOT2
    if (midi_hdl[usb_id]) midi_hdl[usb_id]->configured = 0;
    usb_disable_ep(usb_id, MIDI_EP_IN);
#else
    midi_endpoint_init(usb_device, itf);
#endif
}

/* Standard interface requests, as hid.c answers them; MIDI has no class requests. */
static u32 midi_setup(struct usb_device_t *usb_device, struct usb_ctrlrequest *req)
{
    u8 *tx_payload = usb_get_setup_buffer(usb_device);
    switch (req->bRequestType & USB_TYPE_MASK) {
    case USB_TYPE_STANDARD:
        switch (req->bRequest) {
        case USB_REQ_SET_INTERFACE:
            if (usb_device->bDeviceStates == USB_CONFIGURED && req->wValue == 0) {
                usb_set_setup_phase(usb_device, USB_EP0_STAGE_SETUP);
#if USB_ROOT2
                midi_endpoint_init(usb_device, req->wIndex & 0xFF);
#endif
            } else {
                usb_set_setup_phase(usb_device, USB_EP0_SET_STALL);
            }
            break;
        case USB_REQ_GET_INTERFACE:
            if (usb_device->bDeviceStates == USB_CONFIGURED && req->wLength == 1) {
                tx_payload[0] = 0;
                usb_set_data_payload(usb_device, req, tx_payload, 1);
            } else {
                usb_set_setup_phase(usb_device, USB_EP0_SET_STALL);
            }
            break;
        default:
            usb_set_setup_phase(usb_device, USB_EP0_SET_STALL);
            break;
        }
        break;
    default:
        usb_set_setup_phase(usb_device, USB_EP0_SET_STALL);
        break;
    }
    return 0;
}

u32 usb_midi_desc_config(const usb_dev usb_id, u8 *ptr, u32 *itf)
{
    memcpy(ptr, midi_desc, sizeof(midi_desc));
    ptr[D_AC_ITF + 2] = *itf;                          /* AC interface number */
    ptr[D_AC_HDR + 8] = *itf + 1;                      /* baInterfaceNr: the MS interface */
    ptr[D_MS_ITF + 2] = *itf + 1;                      /* MS interface number */
    ptr[D_EP_OUT + 2] = MIDI_EP_OUT;
    ptr[D_EP_OUT + 4] = MAXP_SIZE_MIDI_BULK & 0xff;
    ptr[D_EP_OUT + 5] = MAXP_SIZE_MIDI_BULK >> 8;
    ptr[D_EP_IN + 2] = USB_DIR_IN | MIDI_EP_IN;
    ptr[D_EP_IN + 4] = MAXP_SIZE_MIDI_BULK & 0xff;
    ptr[D_EP_IN + 5] = MAXP_SIZE_MIDI_BULK >> 8;
    if (usb_set_interface_hander(usb_id, *itf, midi_setup) != *itf) {
        ASSERT(0, "midi set interface_hander fail");
    }
    if (usb_set_interface_hander(usb_id, *itf + 1, midi_setup) != *itf + 1) {
        ASSERT(0, "midi set interface_hander fail");
    }
    usb_set_reset_hander(usb_id, *itf + 1, midi_reset);
    *itf += 2;
    return sizeof(midi_desc);
}

void usb_midi_set_rx_handler(const usb_dev usb_id, void *priv, void (*rx)(void *, const u8 *, u32))
{
    pending_rx[usb_id] = rx;
    pending_priv[usb_id] = priv;
    if (midi_hdl[usb_id]) {
        midi_hdl[usb_id]->priv = priv;
        midi_hdl[usb_id]->rx = rx;
    }
}

int usb_midi_configured(const usb_dev usb_id)
{
    return midi_hdl[usb_id] && midi_hdl[usb_id]->configured;
}

u32 usb_midi_write(const usb_dev usb_id, const u8 *pkts, u32 len)
{
    struct usb_midi_gadget *g = midi_hdl[usb_id];
    u32 off = 0, n;
    if (!g || !g->configured) return 0;
    len &= ~3u;
    os_mutex_pend(&g->tx_mutex, 0);
    while (off < len) {
        n = len - off > MAXP_SIZE_MIDI_BULK ? MAXP_SIZE_MIDI_BULK : len - off;
        n = usb_g_bulk_write(usb_id, MIDI_EP_IN, pkts + off, n);
        if (n == 0) break;
        off += n;
    }
    os_mutex_post(&g->tx_mutex);
    return off;
}

void usb_midi_register(const usb_dev usb_id)
{
    struct usb_midi_gadget *g = midi_hdl[usb_id];
    if (g) return;
    g = zalloc(sizeof *g);
    if (!g) { log_error("usb midi register: no memory"); return; }
    g->rx_buf = malloc(MAXP_SIZE_MIDI_BULK);
    g->ep_in_buf = usb_alloc_ep_dmabuffer(usb_id, MIDI_EP_IN | USB_DIR_IN, MAXP_SIZE_MIDI_BULK);
    g->ep_out_buf = usb_alloc_ep_dmabuffer(usb_id, MIDI_EP_OUT | USB_DIR_OUT, MAXP_SIZE_MIDI_BULK);
    if (!g->rx_buf || !g->ep_in_buf || !g->ep_out_buf) {
        log_error("usb midi register: no buffers");
        midi_hdl[usb_id] = g;
        usb_midi_release(usb_id);
        return;
    }
    os_mutex_create(&g->tx_mutex);
    g->rx = pending_rx[usb_id];
    g->priv = pending_priv[usb_id];
    midi_hdl[usb_id] = g;
}

void usb_midi_release(const usb_dev usb_id)
{
    struct usb_midi_gadget *g = midi_hdl[usb_id];
    if (!g) return;
    if (g->ep_in_buf) usb_free_ep_dmabuffer(usb_id, g->ep_in_buf);
    if (g->ep_out_buf) usb_free_ep_dmabuffer(usb_id, g->ep_out_buf);
    if (g->rx_buf) free(g->rx_buf);
    free(g);
    midi_hdl[usb_id] = NULL;
}

#endif /* TCFG_USB_SLAVE_MIDI_ENABLE */
