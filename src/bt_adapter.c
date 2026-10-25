#include "bt_adapter.h"
#include "bt_rtl.h"
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
#define BT_INQUIRY_ROUNDS   8     /* about three minutes to put a pad in pairing mode */
#define BT_INQUIRY_LEN      16    /* x 1.28 s */
#define BT_MAX_SEEN         32

typedef struct {
    uint8_t  addr[6];
    uint8_t  psrm;           /* page scan repetition mode */
    uint16_t clock_offset;
    uint32_t cod;
} bt_seen_t;

typedef struct {
    int      fd;
    char     path[32];
    uint16_t vid, pid;
    uint8_t  ep_evt;
    uint16_t mps_evt;
#ifdef __PROSPERO__
    struct usb_fs_endpoint eps[1];
#endif
    void    *frame[1];
    uint32_t frame_len[1];
    uint8_t  rx[64];
    int      pending;
    uint8_t  acc[512];       /* event bytes not yet consumed; events span several packets */
    int      acc_len;
    int      gone;           /* the adapter stopped answering (unplugged) */
} bt_dev_t;

static struct {
    pthread_t       thread;
    volatile int    stop;
    int             running;
    pthread_mutex_t lock;
    char            owned[32];
    char            done[32];      /* adapter already tested; retried after it is replugged */
    uint32_t        done_ids;
    uint32_t        logged_ids[6][21];
} g_bt = { .lock = PTHREAD_MUTEX_INITIALIZER };

int bt_adapter_owns(const char *dev_path)
{
    pthread_mutex_lock(&g_bt.lock);
    int owns = g_bt.owned[0] && strcmp(g_bt.owned, dev_path) == 0;
    pthread_mutex_unlock(&g_bt.lock);
    return owns;
}

static void set_owned(const char *path)
{
    pthread_mutex_lock(&g_bt.lock);
    snprintf(g_bt.owned, sizeof(g_bt.owned), "%s", path ? path : "");
    pthread_mutex_unlock(&g_bt.lock);
}

static void addr_str(const uint8_t *a, char *out)
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

static int is_gamepad(uint32_t cod)
{
    /* Major class Peripheral, minor Gamepad */
    return ((cod >> 8) & 0x1f) == 0x05 && ((cod >> 2) & 0x0f) == 0x02;
}

#ifdef __PROSPERO__

/* The console's own radio. Only MediaTek has been seen there (0e8d:3603 on ugen0.2); external
 * MediaTek USB adapters are rare, so the whole vendor is left alone. */
static int is_internal_radio(uint16_t vid, uint16_t pid)
{
    (void)pid;
    return vid == 0x0e8d;
}

/* Finds the HCI interface (class E0/01/01) and its event endpoint */
static int find_hci_interface(int fd, uint8_t *ep_evt, uint16_t *mps_evt)
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
    *ep_evt = 0;
    for (int off = 0; off + 2 <= len;) {
        int dl = raw[off];
        if (dl < 2 || off + dl > len) break;
        if (raw[off + 1] == 0x04 && dl >= 9) {
            in_hci = raw[off + 2] == 0 && raw[off + 3] == 0 &&
                     raw[off + 5] == 0xE0 && raw[off + 6] == 0x01 && raw[off + 7] == 0x01;
        } else if (raw[off + 1] == 0x05 && dl >= 7 && in_hci && !*ep_evt) {
            if ((raw[off + 2] & 0x80) && (raw[off + 3] & 0x03) == 0x03) {
                *ep_evt = raw[off + 2];
                *mps_evt = (uint16_t)(raw[off + 4] | (raw[off + 5] & 0x07) << 8);
            }
        }
        off += dl;
    }
    return *ep_evt != 0;
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
    init.ep_index_max = 1;
    if (ioctl(d->fd, USB_FS_INIT, &init) != 0) {
        log_line("bt: USB_FS_INIT failed on %s (%s)", d->path, strerror(errno));
        return 0;
    }

    struct usb_fs_open op;
    memset(&op, 0, sizeof(op));
    op.max_bufsize = sizeof(d->rx);
    op.max_frames = 1;
    op.ep_index = 0;
    op.ep_no = d->ep_evt;
    if (ioctl(d->fd, USB_FS_OPEN, &op) != 0) {
        log_line("bt: cannot open event endpoint 0x%02x on %s (%s)", d->ep_evt, d->path, strerror(errno));
        struct usb_fs_uninit un = {0};
        ioctl(d->fd, USB_FS_UNINIT, &un);
        return 0;
    }
    if (d->mps_evt == 0 || d->mps_evt > sizeof(d->rx)) d->mps_evt = sizeof(d->rx);

    d->frame[0] = d->rx;
    d->eps[0].ppBuffer = d->frame;
    d->eps[0].pLength = d->frame_len;
    d->eps[0].nFrames = 1;
    d->eps[0].flags = USB_FS_FLAG_SINGLE_SHORT_OK;
    d->eps[0].timeout = USB_FS_TIMEOUT_NONE;
    return 1;
}

static void bt_close(bt_dev_t *d)
{
    if (d->fd < 0) return;
    if (d->pending) {
        struct usb_fs_stop st = { .ep_index = 0 };
        ioctl(d->fd, USB_FS_STOP, &st);
    }
    struct usb_fs_uninit un = {0};
    ioctl(d->fd, USB_FS_UNINIT, &un);
    close(d->fd);
    d->fd = -1;
}

/* One interrupt packet from the event endpoint into acc; 1 on data, 0 on timeout, -1 on error.
 * The IN transfer stays queued across calls so no event is lost between them. */
static int read_packet(bt_dev_t *d, int timeout_ms)
{
    if (!d->pending) {
        d->frame_len[0] = d->mps_evt;
        struct usb_fs_start start = { .ep_index = 0 };
        if (ioctl(d->fd, USB_FS_START, &start) != 0 && errno != EBUSY) {
            d->gone = 1;
            return -1;
        }
        d->pending = 1;
    }

    long deadline = now_ms() + timeout_ms;
    for (;;) {
        struct usb_fs_complete c;
        memset(&c, 0, sizeof(c));
        if (ioctl(d->fd, USB_FS_COMPLETE, &c) == 0) {
            d->pending = 0;
            int st = d->eps[0].status;
            if (st != USB_ERR_NORMAL_COMPLETION && st != USB_ERR_SHORT_XFER) {
                log_line("bt: event endpoint error %d", st);
                d->gone = 1;
                return -1;
            }
            int n = (int)d->frame_len[0];
            if (n > (int)sizeof(d->acc) - d->acc_len) n = (int)sizeof(d->acc) - d->acc_len;
            memcpy(d->acc + d->acc_len, d->rx, (size_t)n);
            d->acc_len += n;
            return 1;
        }
        if (errno != EBUSY && errno != EAGAIN) {
            d->gone = 1;
            return -1;
        }
        long left = deadline - now_ms();
        if (left <= 0 || g_bt.stop) return 0;

        fd_set rset;
        FD_ZERO(&rset);
        FD_SET(d->fd, &rset);
        struct timeval tv = { 0, (left < 10 ? left : 10) * 1000 };
        select(d->fd + 1, &rset, NULL, NULL, &tv);
    }
}

/* One complete HCI event; returns its parameter length, or -1 on timeout or error */
static int read_event(bt_dev_t *d, uint8_t *code, uint8_t *params, int max, int timeout_ms)
{
    long deadline = now_ms() + timeout_ms;
    for (;;) {
        if (d->acc_len >= 2 && d->acc_len >= 2 + d->acc[1]) {
            int plen = d->acc[1];
            *code = d->acc[0];
            memcpy(params, d->acc + 2, (size_t)(plen < max ? plen : max));
            d->acc_len -= 2 + plen;
            memmove(d->acc, d->acc + 2 + plen, (size_t)d->acc_len);
            return plen < max ? plen : max;
        }
        long left = deadline - now_ms();
        if (left <= 0 || read_packet(d, (int)left) < 0) return -1;
    }
}

static int hci_send(bt_dev_t *d, uint16_t opcode, const uint8_t *params, int len)
{
    uint8_t buf[3 + 255];
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

/* Sends a command and waits for its Command Complete (return parameters, status first) or
 * Command Status (one status byte). Returns the reply length, or -1. */
static int hci_cmd(bt_dev_t *d, uint16_t opcode, const uint8_t *params, int len,
                   uint8_t *reply, int max, int timeout_ms)
{
    if (hci_send(d, opcode, params, len) != 0) return -1;
    long deadline = now_ms() + timeout_ms;
    uint8_t code, p[255];
    for (;;) {
        long left = deadline - now_ms();
        if (left <= 0) break;
        int n = read_event(d, &code, p, sizeof(p), (int)left);
        if (n < 0) break;
        if (code == 0x0E && n >= 3 && (p[1] | p[2] << 8) == opcode) {
            int r = n - 3 < max ? n - 3 : max;
            memcpy(reply, p + 3, (size_t)r);
            return r;
        }
        if (code == 0x0F && n >= 4 && (p[2] | p[3] << 8) == opcode) {
            if (max > 0) reply[0] = p[0];
            return 1;
        }
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
    int want = rtl_expected_project(v->lmp_sub, v->hci_rev);
    uint8_t r[8];
    if (hci_cmd(d, 0xFC6D, NULL, 0, r, sizeof(r), 1000) < 2 || r[0] != 0) {
        log_line("bt: Realtek ROM version not readable, staying on ROM firmware");
        return;
    }
    uint8_t rom = r[1];

    if (RTL8761BU_FW_LEN == 0) {
        log_line("bt: this build carries no Realtek firmware, staying on ROM firmware");
        return;
    }
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

    if (hci_cmd(d, 0x1009, NULL, 0, r, sizeof(r), 1000) >= 7 && r[0] == 0) {
        char a[18];
        addr_str(r + 1, a);
        log_line("bt: adapter address %s", a);
    }
    hci_cmd(d, 0x0C03, NULL, 0, r, sizeof(r), 3000);
    return !d->gone;
}

static void eir_name(const uint8_t *eir, int len, char *out, int max)
{
    out[0] = 0;
    for (int i = 0; i + 1 < len && eir[i];) {
        int ln = eir[i], typ = eir[i + 1];
        if ((typ == 0x08 || typ == 0x09) && i + 1 + ln <= len) {
            int n = ln - 1 < max - 1 ? ln - 1 : max - 1;
            memcpy(out, eir + i + 2, (size_t)n);
            out[n] = 0;
            return;
        }
        i += ln + 1;
    }
}

/* Opens a baseband link to dev and closes it again: proves paging works on the console */
static void test_link(bt_dev_t *d, const bt_seen_t *dev)
{
    char a[18];
    addr_str(dev->addr, a);
    uint8_t p[13], r[255], code;
    r[0] = 0xff;
    memcpy(p, dev->addr, 6);
    p[6] = 0x18; p[7] = 0xCC;            /* DM1..DH5 packet types */
    p[8] = dev->psrm;
    p[9] = 0;
    p[10] = (uint8_t)dev->clock_offset;
    p[11] = (uint8_t)((dev->clock_offset >> 8) | 0x80);
    p[12] = 1;                            /* allow role switch */
    if (hci_cmd(d, 0x0405, p, sizeof(p), r, sizeof(r), 2000) < 1 || r[0] != 0) {
        log_line("bt: link test to %s refused (status 0x%02x)", a, r[0]);
        return;
    }
    long deadline = now_ms() + 15000;
    while (now_ms() < deadline && !g_bt.stop) {
        int n = read_event(d, &code, r, sizeof(r), 500);
        if (n < 0) {
            if (d->gone) return;
            continue;
        }
        if (code == 0x03 && n >= 3) {
            uint16_t handle = (uint16_t)((r[1] | r[2] << 8) & 0x0fff);
            if (r[0] != 0) {
                log_line("bt: link test to %s failed, status 0x%02x", a, r[0]);
                return;
            }
            log_line("bt: link test to %s OK (handle 0x%03x). The adapter works on this console.", a, handle);
            sleep_ms(1000);
            uint8_t dp[3] = { (uint8_t)handle, (uint8_t)(handle >> 8), 0x13 };
            hci_cmd(d, 0x0406, dp, sizeof(dp), r, sizeof(r), 2000);
            return;
        }
    }
    log_line("bt: link test to %s timed out", a);
}

static void run_inquiry(bt_dev_t *d)
{
    uint8_t r[255], code;
    uint8_t mode = 0x02;                  /* extended results carry the name */
    hci_cmd(d, 0x0C45, &mode, 1, r, sizeof(r), 1000);

    bt_seen_t seen[BT_MAX_SEEN];
    int nseen = 0, tested = 0;
    log_line("bt: searching for %d rounds of %d s. Put the DS4 in pairing mode (hold Share + PS).",
             BT_INQUIRY_ROUNDS, BT_INQUIRY_LEN * 128 / 100);

    for (int round = 1; round <= BT_INQUIRY_ROUNDS && !tested && !g_bt.stop && !d->gone; round++) {
        uint8_t iq[5] = { 0x33, 0x8B, 0x9E, BT_INQUIRY_LEN, 0 }; /* GIAC */
        if (hci_cmd(d, 0x0401, iq, sizeof(iq), r, sizeof(r), 2000) < 1 || r[0] != 0) {
            log_line("bt: inquiry not started");
            return;
        }
        int found_gamepad = -1;
        long deadline = now_ms() + BT_INQUIRY_LEN * 1280 + 3000;
        while (now_ms() < deadline && !g_bt.stop) {
            int n = read_event(d, &code, r, sizeof(r), 500);
            if (n < 0) {
                if (d->gone) return;
                continue;
            }
            if (code == 0x01) break;      /* Inquiry Complete */
            if ((code != 0x02 && code != 0x22 && code != 0x2F) || n < 15) continue;

            /* Results 0x02 / 0x22 / 0x2F differ only after the page scan repetition mode */
            const uint8_t *addr = r + 1;
            uint32_t cod;
            uint16_t clock;
            int8_t rssi = 0;
            char name[40] = "";
            if (code == 0x02) {
                cod = r[10] | r[11] << 8 | (uint32_t)r[12] << 16;
                clock = (uint16_t)(r[13] | r[14] << 8);
            } else {
                cod = r[9] | r[10] << 8 | (uint32_t)r[11] << 16;
                clock = (uint16_t)(r[12] | r[13] << 8);
                rssi = (int8_t)r[14];
                if (code == 0x2F) eir_name(r + 15, n - 15, name, sizeof(name));
            }

            int known = 0;
            for (int i = 0; i < nseen; i++) {
                if (memcmp(seen[i].addr, addr, 6) == 0) { known = 1; break; }
            }
            if (known || nseen == BT_MAX_SEEN) continue;
            bt_seen_t *s = &seen[nseen++];
            memcpy(s->addr, addr, 6);
            s->psrm = r[7];
            s->clock_offset = clock;
            s->cod = cod;

            char a[18];
            addr_str(addr, a);
            log_line("bt: found %s class %06x%s rssi %d name '%s'",
                     a, cod, is_gamepad(cod) ? " (gamepad)" : "", rssi, name);
            if (is_gamepad(cod) && found_gamepad < 0) found_gamepad = nseen - 1;
        }
        if (found_gamepad >= 0) {
            hci_cmd(d, 0x0402, NULL, 0, r, sizeof(r), 1000); /* Inquiry_Cancel */
            test_link(d, &seen[found_gamepad]);
            tested = 1;
        } else {
            log_line("bt: round %d done, %d device(s) so far, no gamepad yet", round, nseen);
        }
    }
    if (!tested && !d->gone && !g_bt.stop) {
        log_line("bt: no gamepad found. Unplug and plug the adapter again to repeat the test.");
    }
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
            uint8_t ep = 0;
            uint16_t mps = 0;
            if (!find_hci_interface(fd, &ep, &mps)) {
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
            d->ep_evt = ep;
            d->mps_evt = mps;
            return 1;
        }
    }
    return 0;
}

/* Forgets the tested adapter once it has been unplugged, so plugging it in again repeats the test */
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

static void *bt_worker(void *arg)
{
    (void)arg;
    while (!g_bt.stop) {
        check_done_unplugged();
        bt_dev_t d;
        memset(&d, 0, sizeof(d));
        d.fd = -1;
        if (!find_adapter(&d)) {
            sleep_ms(BT_SCAN_INTERVAL_MS);
            continue;
        }

        set_owned(d.path);
        log_line("bt: using adapter %04x:%04x on %s (events on 0x%02x, %u-byte packets)",
                 d.vid, d.pid, d.path, d.ep_evt, d.mps_evt);
        if (bt_open(&d) && bring_up(&d)) run_inquiry(&d);
        if (d.gone) log_line("bt: adapter stopped answering (unplugged?)");
        bt_close(&d);
        set_owned(NULL);

        snprintf(g_bt.done, sizeof(g_bt.done), "%s", d.path);
        g_bt.done_ids = (uint32_t)d.vid << 16 | d.pid;
    }
    return NULL;
}

int bt_adapter_init(void)
{
    g_bt.stop = 0;
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
