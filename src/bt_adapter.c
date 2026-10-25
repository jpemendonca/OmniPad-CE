#include "bt_adapter.h"
#include "bt_rtl.h"
#include "bt/host.h"
#include "ps5_vpad.h"
#include "usb_hotplug.h"
#include "pad_types.h"
#include "log.h"
#include "util.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/time.h>

#ifdef __PROSPERO__
#include <dev/usb/usb.h>
#include <dev/usb/usb_ioctl.h>
#include <dev/usb/usb_endian.h>
#include <dev/usb/usbdi.h>
#include "rtl8761bu_fw.h"
#include "rtl8761bu_config.h"
#endif

#define BT_SCAN_INTERVAL_MS 2000
#define BT_DB_PATH          "/data/anypad/omnipad_bt.db"  /* not PR 18's pads.db: those keys are for the internal radio */
enum { AUTO_OFF, AUTO_FULL, AUTO_PEEK };
#define AUTO_FULL_S    600        /* renewed while no pad of ours is connected */
#define AUTO_FULL_INQ  8          /* inquiry rounds of 10.24 s */
#define AUTO_PEEK_S    4          /* with a pad connected: a 3.84 s round ... */
#define AUTO_PEEK_INQ  3
#define AUTO_PEEK_MS   20000      /* ... every 20 s */

#define EVQ_LEN   32
#define ACLQ_LEN  32
#define ACL_RX    2048

typedef struct {
    uint8_t data[HCI_PKT_MAX];
    int     len;
} bt_pkt_t;

typedef struct {
    bt_pkt_t pkt[ACLQ_LEN];
    int      head, count, cap;
    unsigned dropped;
} bt_queue_t;

enum { EP_EVT = 0, EP_ACL_IN = 1, EP_ACL_OUT = 2, EP_COUNT = 3 };

typedef struct {
    int      fd;
    char     path[32];
    uint16_t vid, pid;
    uint8_t  ep_addr[EP_COUNT];
    uint16_t ep_mps[EP_COUNT];
#ifdef __PROSPERO__
    struct usb_fs_endpoint eps[EP_COUNT];
#endif
    void    *frame[EP_COUNT];
    uint32_t frame_len[EP_COUNT];
    int      pending[EP_COUNT];
    uint8_t  rx_evt[64];
    uint8_t  rx_acl[ACL_RX];
    uint8_t  tx_acl[HCI_PKT_MAX];
    int      out_done, out_status;

    uint8_t  evt_acc[512];   /* events span several interrupt packets */
    int      evt_acc_len;
    uint8_t  acl_acc[2 * ACL_RX];
    int      acl_acc_len;
    bt_queue_t evq, aclq;
    unsigned events_in, acl_in, acl_out;
    int      gone;           /* the adapter stopped answering (unplugged, rest mode) */
} bt_dev_t;

/* One pad of the host and the virtual DualSense slot it was given */
typedef struct {
    int      vslot;          /* -1: none */
    pad_info info;
    char     name[64];
} bt_pad_t;

static struct {
    pthread_t       thread;
    volatile int    stop;
    int             running;
    pthread_mutex_t lock;
    char            owned[32];
    char            done[32];      /* adapter that failed to start; retried after it is replugged */
    uint32_t        done_ids;
    uint32_t        logged_ids[6][21];

    /* Requests from the web thread, applied by the worker (the host is not thread-safe) */
    int             pair_req;      /* seconds, -1 none */
    int             forget_req;

    /* What the dashboard shows, written by the worker */
    const char     *state;
    char            adapter[96];
    char            address[18];
    int             pairing_left;
    int             paired;
    bt_pad_t        pads[HOST_MAX_PADS];
} g_bt = { .lock = PTHREAD_MUTEX_INITIALIZER, .pair_req = -1, .state = "no adapter" };

static bt_dev_t g_dev;

int bt_adapter_owns(const char *dev_path)
{
    pthread_mutex_lock(&g_bt.lock);
    int owns = g_bt.owned[0] && strcmp(g_bt.owned, dev_path) == 0;
    pthread_mutex_unlock(&g_bt.lock);
    return owns;
}

static void set_state(const char *state, const char *path)
{
    pthread_mutex_lock(&g_bt.lock);
    g_bt.state = state;
    if (path) snprintf(g_bt.owned, sizeof(g_bt.owned), "%s", path);
    pthread_mutex_unlock(&g_bt.lock);
}

static void fmt_addr(const uint8_t *a, char *out)
{
    snprintf(out, 18, "%02X:%02X:%02X:%02X:%02X:%02X", a[5], a[4], a[3], a[2], a[1], a[0]);
}

static const char *manufacturer_name(uint16_t m)
{
    switch (m) {
    case 0x0002: return "Intel";
    case 0x000A: return "CSR / Qualcomm";
    case 0x000F: return "Broadcom";
    case 0x001D: return "Qualcomm";
    case 0x0046: return "MediaTek";
    case 0x0048: return "Marvell";
    case 0x005D: return "Realtek";
    default:     return "?";
    }
}

static void queue_push(bt_queue_t *q, const uint8_t *data, int len)
{
    if (len > HCI_PKT_MAX) len = HCI_PKT_MAX;
    if (q->count == q->cap) {
        q->head = (q->head + 1) % q->cap;
        q->count--;
        q->dropped++;
    }
    bt_pkt_t *p = &q->pkt[(q->head + q->count) % q->cap];
    memcpy(p->data, data, (size_t)len);
    p->len = len;
    q->count++;
}

static int queue_pop(bt_queue_t *q, uint8_t *out, int max)
{
    if (q->count == 0) return 0;
    bt_pkt_t *p = &q->pkt[q->head];
    int len = p->len < max ? p->len : max;
    memcpy(out, p->data, (size_t)len);
    q->head = (q->head + 1) % q->cap;
    q->count--;
    return len;
}

#ifdef __PROSPERO__

/* ---- USB transport -------------------------------------------------------- */

/* The console's own radio. Only MediaTek has been seen there (0e8d:3603 on ugen0.2); external
 * MediaTek USB adapters are rare, so the whole vendor is left alone. */
static int is_internal_radio(uint16_t vid, uint16_t pid)
{
    (void)pid;
    return vid == 0x0e8d;
}

/* Finds the HCI interface (class E0/01/01): its event, ACL in and ACL out endpoints */
static int find_hci_interface(int fd, bt_dev_t *d)
{
    uint8_t raw[1024];
    struct usb_gen_descriptor desc;
    memset(&desc, 0, sizeof(desc));
    desc.ugd_data = raw;
    desc.ugd_maxlen = sizeof(raw);
    if (ioctl(fd, USB_GET_FULL_DESC, &desc) != 0) return 0;
    int len = desc.ugd_actlen;
    if (len <= 0 || len > (int)sizeof(raw)) return 0;

    int in_hci = 0;
    memset(d->ep_addr, 0, sizeof(d->ep_addr));
    for (int off = 0; off + 2 <= len;) {
        int dl = raw[off];
        if (dl < 2 || off + dl > len) break;
        if (raw[off + 1] == 0x04 && dl >= 9) {
            in_hci = raw[off + 2] == 0 && raw[off + 3] == 0 &&
                     raw[off + 5] == 0xE0 && raw[off + 6] == 0x01 && raw[off + 7] == 0x01;
        } else if (raw[off + 1] == 0x05 && dl >= 7 && in_hci) {
            uint8_t addr = raw[off + 2], type = raw[off + 3] & 0x03;
            uint16_t mps = (uint16_t)(raw[off + 4] | (raw[off + 5] & 0x07) << 8);
            int k = -1;
            if (type == 0x03 && (addr & 0x80)) k = EP_EVT;
            else if (type == 0x02 && (addr & 0x80)) k = EP_ACL_IN;
            else if (type == 0x02) k = EP_ACL_OUT;
            if (k >= 0 && !d->ep_addr[k]) {
                d->ep_addr[k] = addr;
                d->ep_mps[k] = mps;
            }
        }
        off += dl;
    }
    return d->ep_addr[EP_EVT] && d->ep_addr[EP_ACL_IN] && d->ep_addr[EP_ACL_OUT];
}

static int bt_open(bt_dev_t *d)
{
    for (int itf = 0; itf < 2; itf++) {
        int n = itf;
        if (ioctl(d->fd, USB_IFACE_DRIVER_ACTIVE, &n) == 0) {
            log_line("bt: a system driver holds interface %d of %s, detaching it", itf, d->path);
            n = itf;
            if (ioctl(d->fd, USB_IFACE_DRIVER_DETACH, &n) != 0) {
                log_line("bt: detach failed (%s)", strerror(errno));
            }
        }
    }

    int config = 0;
    if (ioctl(d->fd, USB_GET_CONFIG, &config) == 0 && (config & 0xff) == 0xff) {
        config = 0;
        ioctl(d->fd, USB_SET_CONFIG, &config);
    }

    struct usb_fs_init init;
    memset(d->eps, 0, sizeof(d->eps));
    memset(&init, 0, sizeof(init));
    init.pEndpoints = d->eps;
    init.ep_index_max = EP_COUNT;
    if (ioctl(d->fd, USB_FS_INIT, &init) != 0) {
        log_line("bt: USB_FS_INIT failed on %s (%s)", d->path, strerror(errno));
        return 0;
    }

    static const uint32_t bufsize[EP_COUNT] = { 64, ACL_RX, HCI_PKT_MAX };
    d->frame[EP_EVT] = d->rx_evt;
    d->frame[EP_ACL_IN] = d->rx_acl;
    d->frame[EP_ACL_OUT] = d->tx_acl;
    for (int k = 0; k < EP_COUNT; k++) {
        struct usb_fs_open op;
        memset(&op, 0, sizeof(op));
        op.max_bufsize = bufsize[k];
        op.max_frames = 1;
        op.ep_index = (uint8_t)k;
        op.ep_no = d->ep_addr[k];
        if (ioctl(d->fd, USB_FS_OPEN, &op) != 0) {
            log_line("bt: cannot open endpoint 0x%02x on %s (%s)", d->ep_addr[k], d->path, strerror(errno));
            struct usb_fs_uninit un = {0};
            ioctl(d->fd, USB_FS_UNINIT, &un);
            return 0;
        }
        d->eps[k].ppBuffer = &d->frame[k];
        d->eps[k].pLength = &d->frame_len[k];
        d->eps[k].nFrames = 1;
        d->eps[k].flags = USB_FS_FLAG_SINGLE_SHORT_OK;
        d->eps[k].timeout = k == EP_ACL_OUT ? 1000 : USB_FS_TIMEOUT_NONE;
    }
    if (d->ep_mps[EP_EVT] == 0 || d->ep_mps[EP_EVT] > sizeof(d->rx_evt)) d->ep_mps[EP_EVT] = sizeof(d->rx_evt);
    d->evq.cap = EVQ_LEN;
    d->aclq.cap = ACLQ_LEN;
    return 1;
}

static void bt_close(bt_dev_t *d)
{
    if (d->fd < 0) return;
    for (int k = 0; k < EP_COUNT; k++) {
        if (!d->pending[k]) continue;
        struct usb_fs_stop st = { .ep_index = (uint8_t)k };
        ioctl(d->fd, USB_FS_STOP, &st);
    }
    struct usb_fs_uninit un = {0};
    ioctl(d->fd, USB_FS_UNINIT, &un);
    close(d->fd);
    d->fd = -1;
    memset(d->pending, 0, sizeof(d->pending));
    d->evt_acc_len = d->acl_acc_len = 0;
    d->evq.count = d->aclq.count = 0;
}

static int arm_read(bt_dev_t *d, int k)
{
    if (d->pending[k]) return 1;
    /* One interrupt packet per event read; ACL reads end on the short last packet */
    d->frame_len[k] = k == EP_EVT ? d->ep_mps[EP_EVT] : ACL_RX;
    struct usb_fs_start start = { .ep_index = (uint8_t)k };
    if (ioctl(d->fd, USB_FS_START, &start) != 0 && errno != EBUSY) {
        d->gone = 1;
        return 0;
    }
    d->pending[k] = 1;
    return 1;
}

/* Connection events as the adapter sends them, before the host sees them: shows whether a pad's
 * call reached the adapter at all. Link keys are blanked. */
static int s_acl_log_left;     /* ACL packets still to log after a link comes up */

static void log_link_event(const uint8_t *ev, int len)
{
    static int budget = 300;
    switch (ev[0]) {
    case 0x01: case 0x02: case 0x22: case 0x2F:     /* inquiry: the host logs what matters */
    case 0x0E: case 0x0F: case 0x13: case 0x3E:     /* command replies, completed packets, LE */
        return;
    default: break;
    }
    if (ev[0] == 0x03) s_acl_log_left = 16;
    if (budget <= 0) return;
    budget--;
    char hex[3 * 24 + 1];
    int k;
    for (k = 0; k < len && k < 24; k++) {
        int blank = ev[0] == 0x18 && k >= 8 && k < 24;
        snprintf(hex + 3 * k, 4, blank ? "-- " : "%02x ", ev[k]);
    }
    hex[3 * k] = 0;
    log_line("bt: adapter event %s(len %d)", hex, len);
}

static void take_events(bt_dev_t *d)
{
    while (d->evt_acc_len >= 2 && d->evt_acc_len >= 2 + d->evt_acc[1]) {
        int total = 2 + d->evt_acc[1];
        log_link_event(d->evt_acc, total);
        queue_push(&d->evq, d->evt_acc, total);
        d->events_in++;
        d->evt_acc_len -= total;
        memmove(d->evt_acc, d->evt_acc + total, (size_t)d->evt_acc_len);
    }
}

static void log_acl(const char *dir, const uint8_t *pkt, int len)
{
    char hex[3 * 20 + 1];
    int k;
    for (k = 0; k < len && k < 20; k++) snprintf(hex + 3 * k, 4, "%02x ", pkt[k]);
    hex[3 * k] = 0;
    log_line("bt: ACL %s %s(len %d)", dir, hex, len);
}

static void take_acl(bt_dev_t *d)
{
    while (d->acl_acc_len >= 4) {
        int total = 4 + (int)le16(d->acl_acc + 2);
        if (total > HCI_PKT_MAX) {
            log_line("bt: ACL packet of %d bytes does not fit, stream reset", total);
            d->acl_acc_len = 0;
            return;
        }
        if (d->acl_acc_len < total) return;
        if (s_acl_log_left > 0) {
            s_acl_log_left--;
            log_acl("in", d->acl_acc, total);
        }
        queue_push(&d->aclq, d->acl_acc, total);
        d->acl_in++;
        d->acl_acc_len -= total;
        memmove(d->acl_acc, d->acl_acc + total, (size_t)d->acl_acc_len);
    }
}

/* Collects finished transfers, waiting up to timeout_ms for the first one, and keeps one read
 * queued on each IN endpoint so nothing is missed between calls. Returns -1 once the adapter is gone. */
static int service(bt_dev_t *d, int timeout_ms)
{
    if (d->gone) return -1;
    if (!arm_read(d, EP_EVT) || !arm_read(d, EP_ACL_IN)) return -1;

    long deadline = now_ms() + timeout_ms;
    int got = 0;
    for (;;) {
        struct usb_fs_complete c;
        memset(&c, 0, sizeof(c));
        if (ioctl(d->fd, USB_FS_COMPLETE, &c) == 0) {
            int k = c.ep_index;
            if (k < 0 || k >= EP_COUNT) continue;
            d->pending[k] = 0;
            int st = d->eps[k].status;
            int ok = st == USB_ERR_NORMAL_COMPLETION || st == USB_ERR_SHORT_XFER;
            got++;
            if (k == EP_ACL_OUT) {
                d->out_done = 1;
                d->out_status = ok ? 0 : st;
                continue;
            }
            if (!ok) {
                if (st == USB_ERR_CANCELLED) continue;
                log_line("bt: %s endpoint error %d", k == EP_EVT ? "event" : "ACL", st);
                d->gone = 1;
                return -1;
            }
            int n = (int)d->frame_len[k];
            if (k == EP_EVT) {
                if (n > (int)sizeof(d->evt_acc) - d->evt_acc_len) d->evt_acc_len = 0;
                memcpy(d->evt_acc + d->evt_acc_len, d->rx_evt, (size_t)n);
                d->evt_acc_len += n;
                take_events(d);
            } else {
                if (n > (int)sizeof(d->acl_acc) - d->acl_acc_len) d->acl_acc_len = 0;
                memcpy(d->acl_acc + d->acl_acc_len, d->rx_acl, (size_t)n);
                d->acl_acc_len += n;
                take_acl(d);
            }
            if (!arm_read(d, k)) return -1;
            continue;
        }
        if (errno != EBUSY && errno != EAGAIN) {
            d->gone = 1;
            return -1;
        }
        long left = deadline - now_ms();
        if (got || left <= 0 || g_bt.stop) return got;

        fd_set rset, wset;
        FD_ZERO(&rset);
        FD_ZERO(&wset);
        FD_SET(d->fd, &rset);
        FD_SET(d->fd, &wset);
        struct timeval tv = { 0, (left < 10 ? left : 10) * 1000 };
        select(d->fd + 1, &rset, &wset, NULL, &tv);
    }
}

static int hci_send(bt_dev_t *d, uint16_t opcode, const uint8_t *params, int len)
{
    uint8_t buf[3 + 255];
    if (len < 0 || len > 255) return -1;
    buf[0] = (uint8_t)opcode;
    buf[1] = (uint8_t)(opcode >> 8);
    buf[2] = (uint8_t)len;
    if (len) memcpy(buf + 3, params, (size_t)len);

    struct usb_ctl_request req;
    memset(&req, 0, sizeof(req));
    req.ucr_request.bmRequestType = 0x20; /* class request to the device: HCI command */
    req.ucr_request.bRequest = 0;
    USETW(req.ucr_request.wValue, 0);
    USETW(req.ucr_request.wIndex, 0);
    USETW(req.ucr_request.wLength, 3 + len);
    req.ucr_data = buf;
    if (ioctl(d->fd, USB_DO_REQUEST, &req) != 0) {
        log_line("bt: command 0x%04x not sent (%s)", opcode, strerror(errno));
        d->gone = 1;
        return -1;
    }
    return 0;
}

static int acl_send(bt_dev_t *d, const uint8_t *pkt, int len)
{
    if (d->gone || len <= 0 || len > HCI_PKT_MAX) return 0;
    if (s_acl_log_left > 0) {
        s_acl_log_left--;
        log_acl("out", pkt, len);
    }
    memcpy(d->tx_acl, pkt, (size_t)len);
    d->frame_len[EP_ACL_OUT] = (uint32_t)len;
    d->out_done = 0;
    struct usb_fs_start start = { .ep_index = EP_ACL_OUT };
    if (ioctl(d->fd, USB_FS_START, &start) != 0) {
        log_line("bt: ACL send failed (%s)", strerror(errno));
        d->gone = 1;
        return 0;
    }
    d->pending[EP_ACL_OUT] = 1;
    long deadline = now_ms() + 1500;
    while (!d->out_done && now_ms() < deadline) {
        if (service(d, 5) < 0) return 0;
    }
    if (!d->out_done) {
        log_line("bt: ACL send timed out");
        return 0;
    }
    d->acl_out++;
    return d->out_status == 0;
}

/* ---- hci_ops for the host (src/bt/host.c) ---- */

static int op_cmd(void *ctx, unsigned opcode, const void *params, int plen)
{
    return hci_send(ctx, (uint16_t)opcode, params, plen) == 0;
}

static int op_acl_send(void *ctx, const unsigned char *pkt, int len)
{
    return acl_send(ctx, pkt, len);
}

static int op_pump(void *ctx, int timeout_ms)
{
    bt_dev_t *d = ctx;
    if (d->evq.count || d->aclq.count) {
        service(d, 0);
        return d->gone ? -1 : 1;
    }
    if (service(d, timeout_ms) < 0) return -1;
    return d->evq.count || d->aclq.count;
}

static int op_next_event(void *ctx, unsigned char *out, int max)
{
    return queue_pop(&((bt_dev_t *)ctx)->evq, out, max);
}

static int op_next_acl(void *ctx, unsigned char *out, int max)
{
    return queue_pop(&((bt_dev_t *)ctx)->aclq, out, max);
}

static void op_close(void *ctx)
{
    (void)ctx;  /* the worker closes the device itself */
}

static void op_diag(void *ctx)
{
    bt_dev_t *d = ctx;
    log_line("bt: transport %s: %u events, %u ACL in, %u ACL out, dropped %u events / %u ACL",
             d->path, d->events_in, d->acl_in, d->acl_out, d->evq.dropped, d->aclq.dropped);
}

static int op_chip_vendor(void *ctx)
{
    return ((bt_dev_t *)ctx)->vid;
}

static const hci_ops k_usb_ops = {
    .cmd = op_cmd,
    .acl_send = op_acl_send,
    .pump = op_pump,
    .next_event = op_next_event,
    .next_acl = op_next_acl,
    .close = op_close,
    .diag = op_diag,
    .chip_vendor = op_chip_vendor,
};

/* ---- bring-up, before the host takes over ---- */

/* Sends a command and waits for its Command Complete (return parameters, status first) or
 * Command Status (one status byte). Returns the reply length, or -1. */
static int hci_cmd(bt_dev_t *d, uint16_t opcode, const uint8_t *params, int len,
                   uint8_t *reply, int max, int timeout_ms)
{
    if (hci_send(d, opcode, params, len) != 0) return -1;
    long deadline = now_ms() + timeout_ms;
    uint8_t ev[HCI_PKT_MAX];
    while (now_ms() < deadline) {
        int n;
        while ((n = queue_pop(&d->evq, ev, sizeof(ev))) > 0) {
            const uint8_t *p = ev + 2;
            int plen = n - 2;
            if (ev[0] == 0x0E && plen >= 3 && (p[1] | p[2] << 8) == opcode) {
                int r = plen - 3 < max ? plen - 3 : max;
                memcpy(reply, p + 3, (size_t)r);
                return r;
            }
            if (ev[0] == 0x0F && plen >= 4 && (p[2] | p[3] << 8) == opcode) {
                if (max > 0) reply[0] = p[0];
                return 1;
            }
        }
        if (service(d, 20) < 0) break;
    }
    if (!d->gone) log_line("bt: no reply to command 0x%04x", opcode);
    return -1;
}

typedef struct {
    uint8_t  hci_ver, lmp_ver;
    uint16_t hci_rev, manufacturer, lmp_sub;
} bt_version_t;

static int read_version(bt_dev_t *d, bt_version_t *v)
{
    uint8_t r[16];
    if (hci_cmd(d, 0x1001, NULL, 0, r, sizeof(r), 1000) < 9 || r[0] != 0) return 0;
    v->hci_ver = r[1];
    v->hci_rev = (uint16_t)(r[2] | r[3] << 8);
    v->lmp_ver = r[4];
    v->manufacturer = (uint16_t)(r[5] | r[6] << 8);
    v->lmp_sub = (uint16_t)(r[7] | r[8] << 8);
    return 1;
}

static void realtek_firmware(bt_dev_t *d, bt_version_t *v)
{
    if (v->lmp_sub != 0x8761) {
        log_line("bt: Realtek chip already running patched firmware (subversion 0x%04x)", v->lmp_sub);
        return;
    }
    if (RTL8761BU_FW_LEN == 0) {
        log_line("bt: this build carries no Realtek firmware, staying on ROM firmware");
        return;
    }
    int want = rtl_expected_project(v->lmp_sub, v->hci_rev);
    uint8_t r[8];
    if (hci_cmd(d, 0xFC6D, NULL, 0, r, sizeof(r), 1000) < 2 || r[0] != 0) {
        log_line("bt: Realtek ROM version not readable, staying on ROM firmware");
        return;
    }
    uint8_t rom = r[1];

    static uint8_t data[64 * 1024];
    rtl_fw_info_t info;
    int total = rtl_fw_build((const uint8_t *)RTL8761BU_FW, RTL8761BU_FW_LEN,
                             (const uint8_t *)RTL8761BU_CONFIG, RTL8761BU_CONFIG_LEN,
                             rom, data, sizeof(data), &info);
    if (want < 0 || info.project_id != want) {
        log_line("bt: no firmware for this Realtek chip (rev 0x%04x, ROM %u); the embedded one is for RTL8761B",
                 v->hci_rev, rom);
        return;
    }
    if (total < 0) {
        log_line("bt: RTL8761B firmware has no patch for ROM %u (error %d)", rom, total);
        return;
    }

    int frags = (total + RTL_FRAGMENT_LEN - 1) / RTL_FRAGMENT_LEN;
    log_line("bt: loading RTL8761B firmware 0x%08x: patch %d (%d bytes) + config %d bytes, %d fragments",
             info.fw_version, info.patch_index, info.patch_len, info.config_len, frags);
    long t0 = now_ms();
    for (int n = 0; n < frags; n++) {
        uint8_t frag[1 + RTL_FRAGMENT_LEN];
        int len = total - n * RTL_FRAGMENT_LEN;
        if (len > RTL_FRAGMENT_LEN) len = RTL_FRAGMENT_LEN;
        frag[0] = rtl_fragment_index(n, frags);
        memcpy(frag + 1, data + n * RTL_FRAGMENT_LEN, (size_t)len);
        if (hci_cmd(d, 0xFC20, frag, 1 + len, r, sizeof(r), 3000) < 1 || r[0] != 0) {
            log_line("bt: firmware fragment %d of %d failed", n + 1, frags);
            return;
        }
    }
    bt_version_t after;
    if (read_version(d, &after)) {
        log_line("bt: firmware loaded in %ld ms, subversion 0x%04x -> 0x%04x",
                 now_ms() - t0, v->lmp_sub, after.lmp_sub);
        *v = after;
    }
}

/* Reset, firmware and the settings a fresh controller needs; the host sets the rest */
static int bring_up(bt_dev_t *d)
{
    uint8_t r[255];
    if (hci_cmd(d, 0x0C03, NULL, 0, r, sizeof(r), 3000) < 1 || r[0] != 0) {
        log_line("bt: HCI reset failed on %s", d->path);
        return 0;
    }
    bt_version_t v;
    if (!read_version(d, &v)) {
        log_line("bt: Read_Local_Version failed");
        return 0;
    }
    log_line("bt: chip %s (0x%04x), HCI %u rev 0x%04x, LMP %u subversion 0x%04x",
             manufacturer_name(v.manufacturer), v.manufacturer, v.hci_ver, v.hci_rev, v.lmp_ver, v.lmp_sub);
    if (v.manufacturer == 0x005D) realtek_firmware(d, &v);
    hci_cmd(d, 0x0C03, NULL, 0, r, sizeof(r), 3000);

    /* Seen by pads as a console; extended inquiry results carry the pads' names */
    static const uint8_t cod[3] = { 0x14, 0x08, 0x00 };   /* toy, minor class game */
    hci_cmd(d, 0x0C24, cod, sizeof(cod), r, sizeof(r), 1000);
    uint8_t name[248] = "OmniPad";
    hci_cmd(d, 0x0C13, name, sizeof(name), r, sizeof(r), 1000);
    uint8_t mode = 0x02;
    hci_cmd(d, 0x0C45, &mode, 1, r, sizeof(r), 1000);

    char address[18] = "";
    if (hci_cmd(d, 0x1009, NULL, 0, r, sizeof(r), 1000) >= 7 && r[0] == 0) fmt_addr(r + 1, address);
    pthread_mutex_lock(&g_bt.lock);
    snprintf(g_bt.adapter, sizeof(g_bt.adapter), "%s %04x:%04x", manufacturer_name(v.manufacturer), d->vid, d->pid);
    snprintf(g_bt.address, sizeof(g_bt.address), "%s", address);
    pthread_mutex_unlock(&g_bt.lock);
    return !d->gone;
}

/* Looks for a Bluetooth adapter that is not the console's own radio */
static int find_adapter(bt_dev_t *d)
{
    for (int bus = 0; bus <= 5; bus++) {
        for (int dev = 1; dev <= 20; dev++) {
            char path[32];
            snprintf(path, sizeof(path), "/dev/ugen%d.%d", bus, dev);
            int fd = open(path, O_RDWR | O_NONBLOCK);
            if (fd < 0) continue;

            struct usb_device_descriptor dd;
            if (ioctl(fd, USB_GET_DEVICE_DESC, &dd) != 0) {
                close(fd);
                continue;
            }
            uint16_t vid = UGETW(dd.idVendor), pid = UGETW(dd.idProduct);
            uint32_t ids = (uint32_t)vid << 16 | pid;
            if (is_internal_radio(vid, pid)) {
                /* Checked before any further request reaches it */
                if (g_bt.logged_ids[bus][dev] != ids) {
                    g_bt.logged_ids[bus][dev] = ids;
                    log_line("bt: %04x:%04x on %s is the console's own radio, left alone", vid, pid, path);
                }
                close(fd);
                continue;
            }
            if (!find_hci_interface(fd, d)) {
                close(fd);
                continue;
            }
            if (g_bt.logged_ids[bus][dev] != ids) {
                g_bt.logged_ids[bus][dev] = ids;
                log_line("bt: Bluetooth adapter %04x:%04x on %s", vid, pid, path);
            }
            if (strcmp(g_bt.done, path) == 0 && g_bt.done_ids == ids) {
                close(fd);
                continue;
            }
            d->fd = fd;
            snprintf(d->path, sizeof(d->path), "%s", path);
            d->vid = vid;
            d->pid = pid;
            return 1;
        }
    }
    return 0;
}

/* Forgets the failed adapter once it has been unplugged, so plugging it in again retries */
static void check_done_unplugged(void)
{
    if (!g_bt.done[0]) return;
    int fd = open(g_bt.done, O_RDWR | O_NONBLOCK);
    struct usb_device_descriptor dd;
    int same = fd >= 0 && ioctl(fd, USB_GET_DEVICE_DESC, &dd) == 0 &&
               ((uint32_t)UGETW(dd.idVendor) << 16 | UGETW(dd.idProduct)) == g_bt.done_ids;
    if (fd >= 0) close(fd);
    if (!same) {
        int bus, dev;
        if (sscanf(g_bt.done, "/dev/ugen%d.%d", &bus, &dev) == 2 && bus >= 0 && bus < 6 && dev >= 0 && dev < 21) {
            g_bt.logged_ids[bus][dev] = 0;
        }
        g_bt.done[0] = 0;
        log_line("bt: adapter unplugged");
    }
}

/* ---- pads -> virtual DualSense slots ---------------------------------------- */

static void to_slot_state(pad_state_t *out, const pad_state *in, const pad_info *info)
{
    pad_state_neutral(out);
    out->buttons = in->buttons;
    out->lx = in->lx; out->ly = in->ly;
    out->rx = in->rx; out->ry = in->ry;
    out->l2 = in->l2; out->r2 = in->r2;
    if (in->has_motion) {
        memcpy(out->accel, in->accel, sizeof(out->accel));
        memcpy(out->gyro, in->gyro, sizeof(out->gyro));
    }
    if (in->has_touch) {
        out->touch_down = in->touch[0].down;
        out->touch_x = in->touch[0].x;
        out->touch_y = in->touch[0].y;
    }
    if (in->has_battery) {
        out->battery_level = in->battery_pct;
        out->battery_charging = in->charging;
    }
    out->connected = 1;
    out->conn_type = CONN_BLUETOOTH_CLASSIC;
    out->vid = info->vid;
    out->pid = info->pid;
    memcpy(&out->mac_or_dev_id, info->addr, 6);
}

static void on_connect(void *ud, int hs, const pad_info *info)
{
    (void)ud;
    if (hs < 0 || hs >= HOST_MAX_PADS) return;
    bt_pad_t *p = &g_bt.pads[hs];
    char a[18];
    fmt_addr(info->addr, a);
    pthread_mutex_lock(&g_bt.lock);
    p->info = *info;
    snprintf(p->name, sizeof(p->name), "%s (Bluetooth)", info->profile ? info->profile : "Bluetooth pad");
    p->vslot = -1;
    pthread_mutex_unlock(&g_bt.lock);

    /* The first slot neither a USB pad nor another pad holds; vpad_add takes it atomically */
    int vslot = -1;
    for (int i = 0; i < MAX_SLOTS && vslot < 0; i++) {
        uint16_t vid, pid;
        if (usb_hotplug_slot_ids(i, &vid, &pid)) continue;
        if (vpad_add(i, CONN_BLUETOOTH_CLASSIC, p->name)) vslot = i;
    }
    pthread_mutex_lock(&g_bt.lock);
    p->vslot = vslot;
    pthread_mutex_unlock(&g_bt.lock);
    if (vslot < 0) log_line("bt: %s %s connected but every slot is taken", p->name, a);
    else log_line("bt: %s %s (%04x:%04x) on slot %d", p->name, a, info->vid, info->pid, vslot);
}

static void on_state(void *ud, int hs, const pad_state *st)
{
    (void)ud;
    if (hs < 0 || hs >= HOST_MAX_PADS || g_bt.pads[hs].vslot < 0) return;
    pad_state_t s;
    to_slot_state(&s, st, &g_bt.pads[hs].info);
    vpad_update(g_bt.pads[hs].vslot, &s);
}

static void on_disconnect(void *ud, int hs)
{
    (void)ud;
    if (hs < 0 || hs >= HOST_MAX_PADS) return;
    pthread_mutex_lock(&g_bt.lock);
    int vslot = g_bt.pads[hs].vslot;
    g_bt.pads[hs].vslot = -1;
    pthread_mutex_unlock(&g_bt.lock);
    if (vslot >= 0) {
        vpad_remove(vslot);
        log_line("bt: %s left slot %d", g_bt.pads[hs].name, vslot);
    }
}

static void publish(host_t *h)
{
    pthread_mutex_lock(&g_bt.lock);
    g_bt.pairing_left = host_pairing_left(h);
    g_bt.paired = host_paired_count(h);
    pthread_mutex_unlock(&g_bt.lock);
}

static void run_host(bt_dev_t *d)
{
    hci_t hci = { &k_usb_ops, d };
    host_events ev = { 0 };
    ev.on_connect = on_connect;
    ev.on_state = on_state;
    ev.on_disconnect = on_disconnect;
    for (int i = 0; i < HOST_MAX_PADS; i++) g_bt.pads[i].vslot = -1;

    host_t *h = host_open(hci, BT_DB_PATH, &ev);
    if (!h) {
        log_line("bt: the Bluetooth host did not start on this adapter");
        return;
    }
    set_state("ready", NULL);
    log_line("bt: ready, %d paired pad(s). Paired pads connect when switched on.", host_paired_count(h));
    log_line("bt: searching on its own: put a pad in pairing mode (DS4: hold Share + PS) to connect it");

    long last_publish = 0, last_alive = now_ms();
    /* Searching on its own, so a pad only has to be put in pairing mode: all the time while no pad
     * of ours is connected, and a short look every AUTO_PEEK_MS once one is (a search takes radio
     * time from the pads). None when every slot is taken. */
    int auto_mode = -1, auto_window = 0;
    long next_auto = 0;
    while (!g_bt.stop && !host_transport_lost(h)) {
        int pair = -1, forget = 0;
        pthread_mutex_lock(&g_bt.lock);
        pair = g_bt.pair_req;
        forget = g_bt.forget_req;
        g_bt.pair_req = -1;
        g_bt.forget_req = 0;
        pthread_mutex_unlock(&g_bt.lock);
        if (forget) log_line("bt: forgot %d paired pad(s)", host_forget_all(h));
        if (pair >= 0) {
            host_pair(h, pair);
            auto_window = 0;      /* the page's own search is not cut short below */
        }

        host_poll(h, 10);

        long now = now_ms();
        if (now - last_publish > 250) {
            last_publish = now;
            publish(h);
        }
        int connected = 0, slot_free = 0;
        for (int i = 0; i < HOST_MAX_PADS; i++) connected += g_bt.pads[i].vslot >= 0;
        for (int i = 0; i < MAX_SLOTS; i++) {
            uint16_t vid, pid;
            if (!usb_hotplug_slot_ids(i, &vid, &pid) && vpad_slot_is_free(i)) slot_free = 1;
        }
        int mode = !slot_free ? AUTO_OFF : connected ? AUTO_PEEK : AUTO_FULL;
        if (mode != auto_mode) {
            auto_mode = mode;
            host_tune_set(h, "inq_len", mode == AUTO_PEEK ? AUTO_PEEK_INQ : AUTO_FULL_INQ);
            if (auto_window && host_pairing_left(h) > 0) host_pair(h, 0);
            auto_window = 0;
            next_auto = now + (mode == AUTO_PEEK ? AUTO_PEEK_MS : 0);
        }
        if (mode != AUTO_OFF && host_pairing_left(h) == 0 && now >= next_auto) {
            host_pair(h, mode == AUTO_PEEK ? AUTO_PEEK_S : AUTO_FULL_S);
            auto_window = 1;
            next_auto = now + (mode == AUTO_PEEK ? AUTO_PEEK_MS : 0);
        }

        /* While nothing is connected, a line now and then shows the adapter is still being heard */
        int any = 0;
        for (int i = 0; i < HOST_MAX_PADS; i++) any |= g_bt.pads[i].vslot >= 0;
        if (any) last_alive = now;
        else if (now - last_alive > 60000) {
            last_alive = now;
            log_line("bt: waiting for a paired pad (%d paired); %u events from the adapter so far",
                     host_paired_count(h), d->events_in);
        }
    }
    if (host_transport_lost(h)) log_line("bt: adapter lost (unplugged or rest mode)");
    set_state("stopping", NULL);
    host_close(h);
    for (int i = 0; i < HOST_MAX_PADS; i++) on_disconnect(NULL, i);
}

static void *bt_worker(void *arg)
{
    (void)arg;
    while (!g_bt.stop) {
        check_done_unplugged();
        bt_dev_t *d = &g_dev;
        memset(d, 0, sizeof(*d));
        d->fd = -1;
        if (!find_adapter(d)) {
            sleep_ms(BT_SCAN_INTERVAL_MS);
            continue;
        }

        set_state("starting", d->path);
        log_line("bt: using adapter %04x:%04x on %s (events 0x%02x, ACL 0x%02x/0x%02x)",
                 d->vid, d->pid, d->path, d->ep_addr[EP_EVT], d->ep_addr[EP_ACL_IN], d->ep_addr[EP_ACL_OUT]);
        int started = bt_open(d) && bring_up(d);
        /* An instance being replaced may still hold the adapter for a moment: try again before giving up */
        for (int tries = 1; !started && !d->gone && tries < 3 && !g_bt.stop; tries++) {
            log_line("bt: retrying adapter start (%d)", tries + 1);
            bt_close(d);
            sleep_ms(1500);
            d->fd = open(d->path, O_RDWR | O_NONBLOCK);
            if (d->fd < 0) break;
            started = bt_open(d) && bring_up(d);
        }
        if (started) run_host(d);
        else log_line("bt: adapter did not start; unplug and plug it again to retry");
        bt_close(d);

        pthread_mutex_lock(&g_bt.lock);
        g_bt.owned[0] = 0;
        g_bt.state = "no adapter";
        g_bt.adapter[0] = 0;
        g_bt.address[0] = 0;
        g_bt.pairing_left = 0;
        g_bt.paired = 0;
        pthread_mutex_unlock(&g_bt.lock);
        if (!started) {
            snprintf(g_bt.done, sizeof(g_bt.done), "%s", d->path);
            g_bt.done_ids = (uint32_t)d->vid << 16 | d->pid;
        } else {
            sleep_ms(BT_SCAN_INTERVAL_MS);   /* a lost adapter may come back after rest mode */
        }
    }
    return NULL;
}

int bt_adapter_init(void)
{
    g_bt.stop = 0;
    for (int i = 0; i < HOST_MAX_PADS; i++) g_bt.pads[i].vslot = -1;
    if (pthread_create(&g_bt.thread, NULL, bt_worker, NULL) != 0) {
        log_line("bt: worker thread not started");
        return 0;
    }
    g_bt.running = 1;
    log_line("bt: watching for a USB Bluetooth adapter (firmware for RTL8761B %s)",
             RTL8761BU_FW_LEN ? "embedded" : "not embedded");
    return 1;
}

void bt_adapter_cleanup(void)
{
    if (!g_bt.running) return;
    g_bt.stop = 1;
    pthread_join(g_bt.thread, NULL);
    g_bt.running = 0;
}

#else

int bt_adapter_init(void) { return 1; }
void bt_adapter_cleanup(void) {}

#endif

int bt_adapter_pair(int seconds)
{
    pthread_mutex_lock(&g_bt.lock);
    int ok = strcmp(g_bt.state, "ready") == 0;
    if (ok) g_bt.pair_req = seconds < 0 ? 0 : seconds;
    pthread_mutex_unlock(&g_bt.lock);
    return ok;
}

int bt_adapter_forget_all(void)
{
    pthread_mutex_lock(&g_bt.lock);
    int ok = strcmp(g_bt.state, "ready") == 0;
    if (ok) g_bt.forget_req = 1;
    pthread_mutex_unlock(&g_bt.lock);
    return ok;
}

int bt_adapter_status_json(char *out, int n)
{
    pthread_mutex_lock(&g_bt.lock);
    int len = snprintf(out, (size_t)n,
        "{\"state\":\"%s\",\"adapter\":\"%s\",\"address\":\"%s\",\"pairing_left\":%d,\"paired\":%d,\"pads\":[",
        g_bt.state, g_bt.adapter, g_bt.address, g_bt.pairing_left, g_bt.paired);
    int first = 1;
    for (int i = 0; i < HOST_MAX_PADS && len < n; i++) {
        if (g_bt.pads[i].vslot < 0) continue;
        char a[18];
        fmt_addr(g_bt.pads[i].info.addr, a);
        len += snprintf(out + len, (size_t)(n - len), "%s{\"slot\":%d,\"name\":\"%s\",\"address\":\"%s\"}",
                        first ? "" : ",", g_bt.pads[i].vslot, g_bt.pads[i].name, a);
        first = 0;
    }
    if (len < n) len += snprintf(out + len, (size_t)(n - len), "]}");
    pthread_mutex_unlock(&g_bt.lock);
    return len < n ? len : n - 1;
}
