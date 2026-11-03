/* The whole Bluetooth host over a simulated USB adapter, on a simulated clock. The chip below has
 * the habits seen on the PS5, and each test holds a bug that cost a day there. This does not prove
 * a real chip behaves: it proves the host does not walk back into what real chips were seen to do.
 * The rules of the chip are written where they apply, with the date they were seen. */
#include "bt_sched.h"
#include "host_int.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures, checks;

#define CHECK(cond, ...) do { checks++; if (!(cond)) { failures++; printf("FAIL: " __VA_ARGS__); printf("\n"); } } while (0)

/* ---- the clock and the log, which util.c and log.c give the payload ---- */

static long g_now = 1000;
static int g_log = 0;

long now_ms(void) { return g_now; }

void log_line(const char *fmt, ...)
{
    va_list ap;
    if (!g_log) return;
    va_start(ap, fmt);
    printf("[%8ld] ", g_now);
    vprintf(fmt, ap);
    printf("\n");
    va_end(ap);
}

/* ---- the chip ---- */

#define SIM_PKT      260
#define SIM_PENDING  512
#define SIM_READY    256
#define SIM_SENT     16384
#define SIM_LATENCY  2      /* ms until a command's reply */
#define CLONE_HANG   5000   /* a clone hung ~5 s on a command sent while a pad reported (PS5, 2 Nov) */
#define PAD_PERIOD   10     /* the DS4 measured 97 reports/s on the PS5 (SPECS.md) */
#define XBOX_SILENT  61000  /* an Xbox subscribed without its values read stayed silent 61 s (PS5, 2 Nov) */

typedef struct { long at; int acl, len; unsigned char d[SIM_PKT]; } sim_pkt;
typedef struct { int len; unsigned char d[SIM_PKT]; } sim_ready;
typedef struct { unsigned op; long at; } sim_sent;

typedef struct {
    unsigned char addr[6];
    int on;
    unsigned handle;
    int streaming;
    long next_report;
    int seq, sent;
    /* DS4 */
    int called, full, cfg_seen, sig_id;
    unsigned host_ctrl, host_intr;
    /* Xbox */
    int creating, reads, subscribed;
    long create_at, stream_at, advert_at;
} sim_pad;

typedef struct {
    bt_cmdq_t q;
    int clone;              /* hangs on a command that arrives while a pad reports */
    int deaf;               /* takes commands and never answers */
    int unplugged;
    long air;               /* ms for a pad's answer over the air */

    long busy_until;        /* a reply is owed until then: no room for another command */
    long last_pad_acl;
    int early, hangs, ready_overflow;

    sim_sent sent[SIM_SENT];
    int nsent;

    int inquiry_on;
    long inquiry_end, last_tick;
    int inq_started, inq_done;
    int scan_on;
    unsigned scan_itv;
    long scan_on_ms, overlap_ms;
    int create_during_inquiry, create_during_scan;
    int inq_while_linking;

    sim_pad ds4, xbox;

    sim_pkt pending[SIM_PENDING];
    int npending;
    sim_ready evq[SIM_READY], aclq[SIM_READY];
    int ev_head, ev_n, acl_head, acl_n;
} chip_t;

static chip_t g_c;

static const unsigned char DS4_ADDR[6]  = { 0x1b, 0x10, 0x93, 0x5f, 0x89, 0x90 };
static const unsigned char XBOX_ADDR[6] = { 0x25, 0x50, 0x68, 0xd7, 0x6a, 0xf4 };
#define DS4_HANDLE  0x081
#define XBOX_HANDLE 0x040
#define DS4_CID_CTRL 0x70
#define DS4_CID_INTR 0x71

static void put_le16(unsigned char *p, unsigned v) { p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8); }
static unsigned get_le16(const unsigned char *p) { return (unsigned)p[0] | (unsigned)p[1] << 8; }

static void schedule(long delay, int acl, const unsigned char *d, int len)
{
    sim_pkt *p;
    if (g_c.npending == SIM_PENDING) { g_c.ready_overflow++; return; }
    p = &g_c.pending[g_c.npending++];
    p->at = g_now + delay;
    p->acl = acl;
    p->len = len;
    memcpy(p->d, d, (size_t)len);
}

static void event(long delay, unsigned code, const unsigned char *params, int plen)
{
    unsigned char b[SIM_PKT];
    b[0] = (unsigned char)code;
    b[1] = (unsigned char)plen;
    if (plen) memcpy(b + 2, params, (size_t)plen);
    schedule(delay, 0, b, plen + 2);
}

static void reply_complete(long delay, unsigned op, int status, const unsigned char *ret, int rlen)
{
    unsigned char p[32] = { 1 };
    put_le16(p + 1, op);
    p[3] = (unsigned char)status;
    if (rlen) memcpy(p + 4, ret, (size_t)rlen);
    event(delay, 0x0E, p, 4 + rlen);
}

static void reply_status(long delay, unsigned op, int status)
{
    unsigned char p[4] = { (unsigned char)status, 1 };
    put_le16(p + 2, op);
    event(delay, 0x0F, p, 4);
}

static int is_status_command(unsigned op)
{
    switch (op) {
    case OP_INQUIRY: case OP_CREATE_CONNECTION: case OP_DISCONNECT: case OP_ACCEPT_CONNECTION:
    case OP_REJECT_CONNECTION: case OP_AUTH_REQUESTED: case OP_SET_ENCRYPTION:
    case 0x200D: case 0x2013: case 0x2019: case 0x2025: case 0x2026:
        return 1;
    default:
        return 0;
    }
}

static void pad_acl(unsigned handle, unsigned cid, const unsigned char *d, int n)
{
    unsigned char b[SIM_PKT];
    put_le16(b, handle | 0x2000);
    put_le16(b + 2, (unsigned)n + 4);
    put_le16(b + 4, (unsigned)n);
    put_le16(b + 6, cid);
    memcpy(b + 8, d, (size_t)n);
    schedule(g_c.air, 1, b, n + 8);
}

static void le_connection_complete(long delay, int status)
{
    unsigned char m[19] = { 0x01 };
    m[1] = (unsigned char)status;
    put_le16(m + 2, XBOX_HANDLE);
    memcpy(m + 6, XBOX_ADDR, 6);
    event(delay, 0x3E, m, 19);
}

static void link_complete_event(long delay, unsigned code, unsigned handle, int on)
{
    unsigned char p[4] = { 0 };
    put_le16(p + 1, handle);
    p[3] = (unsigned char)on;
    event(delay, code, p, code == 0x06 ? 3 : 4);
}

/* A command as the chip takes it, past the queue the host keeps in front of it */
static int chip_command(void *ctx, uint16_t op, const uint8_t *p, int n)
{
    long lat = SIM_LATENCY;
    (void)ctx;
    (void)n;

    if (g_c.nsent < SIM_SENT) {
        g_c.sent[g_c.nsent].op = op;
        g_c.sent[g_c.nsent].at = g_now;
        g_c.nsent++;
    }
    if (g_c.deaf) return 1;

    /* CSR8510 clones answer a command that arrives early with status 0x07 and drop it, an LE
     * Create Connection among them (PS5, 2 Nov) */
    if (g_now < g_c.busy_until) {
        g_c.early++;
        if (is_status_command(op)) reply_status(lat, op, 0x07);
        else reply_complete(lat, op, 0x07, NULL, 0);
        return 1;
    }
    if (g_c.clone && g_now - g_c.last_pad_acl < 100) {
        g_c.hangs++;
        lat = CLONE_HANG;
    }
    g_c.busy_until = g_now + lat;

    if (is_status_command(op)) reply_status(lat, op, 0);

    switch (op) {
    case OP_READ_BUFFER_SIZE: {
        unsigned char r[7];
        put_le16(r, 1021);
        r[2] = 64;
        put_le16(r + 3, 8);
        put_le16(r + 5, 8);
        reply_complete(lat, op, 0, r, 7);
        break;
    }
    case OP_READ_BD_ADDR: {
        static const unsigned char us[6] = { 1, 2, 3, 4, 5, 6 };
        reply_complete(lat, op, 0, us, 6);
        break;
    }
    case 0x2002: {
        unsigned char r[3];
        put_le16(r, 251);
        r[2] = 8;
        reply_complete(lat, op, 0, r, 3);
        break;
    }
    case 0x200B:
        g_c.scan_itv = get_le16(p + 1);
        reply_complete(lat, op, 0, NULL, 0);
        break;
    case 0x200C:
        g_c.scan_on = p[0];
        reply_complete(lat, op, 0, NULL, 0);
        break;
    case 0x200D:
        /* A Classic search left an LE connection unanswered for a minute on a clone (PS5, 2 Nov) */
        if (g_c.inquiry_on) g_c.create_during_inquiry++;
        if (g_c.scan_on) g_c.create_during_scan++;
        g_c.xbox.creating = 1;
        g_c.xbox.create_at = g_now;
        break;
    case 0x200E:
        reply_complete(lat, op, 0, NULL, 0);
        if (g_c.xbox.creating) {
            g_c.xbox.creating = 0;
            le_connection_complete(lat + 1, 0x02);
        }
        break;
    case 0x2019:
        link_complete_event(lat + 20, 0x08, XBOX_HANDLE, 1);
        break;
    case 0x2025: {
        unsigned char m[66] = { 0x08 };
        event(lat + 5, 0x3E, m, 66);
        break;
    }
    case OP_INQUIRY:
        if (g_c.xbox.creating || (g_c.xbox.handle && !g_c.xbox.streaming && g_c.xbox.on))
            g_c.inq_while_linking++;
        g_c.inquiry_on = 1;
        g_c.inq_started++;
        g_c.inquiry_end = g_now + (long)p[3] * 1280;
        break;
    case OP_INQUIRY_CANCEL:
        g_c.inquiry_on = 0;
        reply_complete(lat, op, 0, NULL, 0);
        break;
    case OP_ACCEPT_CONNECTION: {
        unsigned char c[11] = { 0 };
        put_le16(c + 1, DS4_HANDLE);
        memcpy(c + 3, DS4_ADDR, 6);
        c[9] = 1;
        g_c.ds4.handle = DS4_HANDLE;
        event(lat + 30, 0x03, c, 11);
        break;
    }
    case OP_AUTH_REQUESTED:
        event(lat + 5, 0x17, DS4_ADDR, 6);
        break;
    case OP_LINK_KEY_REPLY: {
        reply_complete(lat, op, 0, DS4_ADDR, 6);
        link_complete_event(lat + 20, 0x06, DS4_HANDLE, 0);
        break;
    }
    case OP_SET_ENCRYPTION:
        link_complete_event(lat + 20, 0x08, DS4_HANDLE, 1);
        break;
    case OP_DISCONNECT: {
        unsigned char d[4] = { 0 };
        unsigned hd = get_le16(p) & 0x0FFF;
        sim_pad *pad = hd == XBOX_HANDLE ? &g_c.xbox : &g_c.ds4;
        put_le16(d + 1, hd);
        d[3] = 0x16;
        pad->handle = 0;
        pad->streaming = 0;
        event(lat + 10, 0x05, d, 4);
        break;
    }
    default:
        if (!is_status_command(op)) {
            static const unsigned char zeros[16];
            reply_complete(lat, op, 0, zeros, sizeof zeros);
        }
        break;
    }
    return 1;
}

/* ---- the DS4: calls back, and takes the channels the host opens ---- */

static void ds4_signal(sim_pad *d, const unsigned char *s, int n)
{
    unsigned char r[16];
    if (n < 4) return;
    switch (s[0]) {
    case SIG_CONN_REQ: {
        unsigned psm = get_le16(s + 4), scid = get_le16(s + 6);
        unsigned ours = psm == PSM_HID_CTRL ? DS4_CID_CTRL : DS4_CID_INTR;
        if (psm == PSM_HID_CTRL) d->host_ctrl = scid;
        else d->host_intr = scid;
        r[0] = SIG_CONN_RSP; r[1] = s[1]; put_le16(r + 2, 8);
        put_le16(r + 4, ours); put_le16(r + 6, scid); put_le16(r + 8, 0); put_le16(r + 10, 0);
        pad_acl(d->handle, CID_SIGNALING, r, 12);
        r[0] = SIG_CFG_REQ; r[1] = (unsigned char)(++d->sig_id); put_le16(r + 2, 4);
        put_le16(r + 4, scid); put_le16(r + 6, 0);
        pad_acl(d->handle, CID_SIGNALING, r, 8);
        break;
    }
    case SIG_CFG_REQ: {
        unsigned dcid = get_le16(s + 4);
        r[0] = SIG_CFG_RSP; r[1] = s[1]; put_le16(r + 2, 6);
        put_le16(r + 4, dcid == DS4_CID_CTRL ? d->host_ctrl : d->host_intr);
        put_le16(r + 6, 0); put_le16(r + 8, 0);
        pad_acl(d->handle, CID_SIGNALING, r, 10);
        if (dcid == DS4_CID_INTR) {
            d->streaming = 1;
            d->next_report = g_now + 50;
        }
        break;
    }
    default:
        break;
    }
}

static void ds4_report(sim_pad *d)
{
    unsigned char r[80] = { 0xA1 };
    int n;
    if (d->full) {
        r[1] = 0x11; r[2] = 0xC0;
        r[4] = 0x55; r[5] = 0x66; r[6] = 0x77; r[7] = 0x88;
        r[8] = 0x08;
        n = 79;
    } else {
        r[1] = 0x01;
        r[2] = 0x11; r[3] = 0x22; r[4] = 0x33; r[5] = 0x44;
        r[6] = 0x08;
        n = 11;
    }
    pad_acl(d->handle, d->host_intr, r, n);
    d->sent++;
}

/* ---- the Xbox Series S: the GATT table and notifications captured on the PS5 (2 Nov) ---- */

static const struct { unsigned decl, value, uuid, props; } k_xbox_chars[] = {
    { 0x17, 0x18, 0x2a4a, 0x02 },
    { 0x19, 0x1a, 0x2a4c, 0x04 },
    { 0x1b, 0x1c, 0x2a4b, 0x02 },
    { 0x1d, 0x1e, 0x2a4d, 0x12 },
    { 0x21, 0x22, 0x2a4d, 0x0e },
};
static const struct { unsigned handle, uuid; } k_xbox_descs[] = {
    { 0x1f, 0x2902 }, { 0x20, 0x2908 }, { 0x23, 0x2908 },
};
#define XBOX_SVC_START 0x16
#define XBOX_SVC_END   0x23
#define XBOX_CCCD      0x1f
#define XBOX_INPUT     0x1e
#define XBOX_READS_ALL 0x0F

static const unsigned char k_xbox_frames[][16] = {
    { 0x00, 0x80, 0xff, 0x7f, 0x00, 0x80, 0xff, 0x7f },
    { 0x3e, 0x85, 0x43, 0x71, 0x89, 0x7b, 0x03, 0x84 },
    { 0xab, 0x7a, 0x5a, 0x84, 0xa6, 0x8a, 0x3b, 0x7b },
    { 0xbd, 0x84, 0xa0, 0x86, 0x26, 0x84, 0xe2, 0x85 },
};

static void att_error(sim_pad *x, unsigned req, unsigned handle)
{
    unsigned char r[5] = { 0x01, (unsigned char)req };
    put_le16(r + 2, handle);
    r[4] = 0x0A;
    pad_acl(x->handle, 4, r, 5);
}

static void xbox_att(sim_pad *x, const unsigned char *a, int n)
{
    unsigned char r[200];
    int len = 0;
    size_t i;

    switch (a[0]) {
    case 0x02:
        r[0] = 0x03;
        put_le16(r + 1, 185);
        pad_acl(x->handle, 4, r, 3);
        break;
    case 0x06:
        r[0] = 0x07;
        put_le16(r + 1, XBOX_SVC_START);
        put_le16(r + 3, XBOX_SVC_END);
        pad_acl(x->handle, 4, r, 5);
        break;
    case 0x08: {
        unsigned start = get_le16(a + 1), end = get_le16(a + 3);
        r[0] = 0x09;
        r[1] = 7;
        len = 2;
        for (i = 0; i < sizeof k_xbox_chars / sizeof k_xbox_chars[0]; i++) {
            if (k_xbox_chars[i].decl < start || k_xbox_chars[i].decl > end) continue;
            put_le16(r + len, k_xbox_chars[i].decl);
            r[len + 2] = (unsigned char)k_xbox_chars[i].props;
            put_le16(r + len + 3, k_xbox_chars[i].value);
            put_le16(r + len + 5, k_xbox_chars[i].uuid);
            len += 7;
        }
        if (len == 2) att_error(x, a[0], start);
        else pad_acl(x->handle, 4, r, len);
        break;
    }
    case 0x04: {
        unsigned start = get_le16(a + 1), end = get_le16(a + 3);
        r[0] = 0x05;
        r[1] = 1;
        len = 2;
        for (i = 0; i < sizeof k_xbox_descs / sizeof k_xbox_descs[0]; i++) {
            if (k_xbox_descs[i].handle < start || k_xbox_descs[i].handle > end) continue;
            put_le16(r + len, k_xbox_descs[i].handle);
            put_le16(r + len + 2, k_xbox_descs[i].uuid);
            len += 4;
        }
        if (len == 2) att_error(x, a[0], start);
        else pad_acl(x->handle, 4, r, len);
        break;
    }
    case 0x0A: {
        unsigned hd = get_le16(a + 1);
        r[0] = 0x0B;
        r[1] = hd == 0x23 ? 3 : 1;
        r[2] = hd == 0x23 ? 2 : 1;
        if (hd == 0x18) x->reads |= 1;
        if (hd == 0x1c) x->reads |= 2;
        if (hd == 0x1e) x->reads |= 4;
        if (hd == 0x22) x->reads |= 8;
        pad_acl(x->handle, 4, r, 3);
        break;
    }
    case 0x12:
        r[0] = 0x13;
        pad_acl(x->handle, 4, r, 1);
        if (n >= 3 && get_le16(a + 1) == XBOX_CCCD) {
            x->subscribed = 1;
            x->stream_at = g_now + (x->reads == XBOX_READS_ALL ? 30 : XBOX_SILENT);
        }
        break;
    default:
        break;
    }
}

static void xbox_notify(sim_pad *x)
{
    unsigned char r[19] = { 0x1B };
    put_le16(r + 1, XBOX_INPUT);
    memcpy(r + 3, k_xbox_frames[x->sent % 4], 16);
    pad_acl(x->handle, 4, r, 19);
    x->sent++;
}

static void xbox_advert(sim_pad *x)
{
    unsigned char m[16] = { 0x02, 1, 0x00, 0x00 };
    memcpy(m + 4, x->addr, 6);
    m[10] = 4;
    m[11] = 3; m[12] = 0x19; m[13] = 0xC4; m[14] = 0x03;
    event(1, 0x3E, m, 16);
}

/* ---- the transport ---- */

static int sim_acl_send(void *ctx, const unsigned char *pkt, int len)
{
    unsigned hd = get_le16(pkt) & 0x0FFF, cid;
    unsigned char done[5] = { 1 };
    sim_pad *p = hd == XBOX_HANDLE ? &g_c.xbox : &g_c.ds4;
    (void)ctx;

    if (g_c.unplugged) return 0;
    put_le16(done + 1, hd);
    put_le16(done + 3, 1);
    event(SIM_LATENCY, 0x13, done, 5);
    if (len < 8 || !p->handle) return 1;
    cid = get_le16(pkt + 6);
    if (hd == XBOX_HANDLE) {
        if (cid == 4) xbox_att(p, pkt + 8, len - 8);
    } else if (cid == CID_SIGNALING) {
        ds4_signal(p, pkt + 8, len - 8);
    } else if (len >= 10 && pkt[9] == 0x11 && (pkt[8] == HID_DATA_OUTPUT || pkt[8] == 0x52)) {
        p->full = 1;                /* the DS4 moves to report 0x11 on an output report 0x11 */
    }
    return 1;
}

static int sim_cmd(void *ctx, unsigned op, const void *p, int n)
{
    (void)ctx;
    if (g_c.unplugged) return 0;
    if (!bt_cmdq_push(&g_c.q, op, p, n)) return 0;
    bt_cmdq_flush(&g_c.q, chip_command, &g_c);
    return 1;
}

/* A scan at the normal window took a DS4 from 97 to 28 reports/s, and so does a search
 * (PS5, 2 Nov): two reports of three are lost while the radio is taken. */
static int radio_taken(void)
{
    return g_c.inquiry_on || (g_c.scan_on && g_c.scan_itv != 0x0400);
}

static void pad_tick(sim_pad *p, void (*report)(sim_pad *))
{
    while (p->streaming && g_now >= p->next_report) {
        p->next_report += PAD_PERIOD;
        if (radio_taken() && ++p->seq % 3) continue;
        report(p);
    }
}

static void chip_tick(long dt)
{
    int i, kept = 0;

    if (g_c.scan_on) g_c.scan_on_ms += dt;
    if (g_c.scan_on && g_c.inquiry_on) g_c.overlap_ms += dt;

    if (g_c.inquiry_on && g_now >= g_c.inquiry_end) {
        unsigned char st[1] = { 0 };
        g_c.inquiry_on = 0;
        g_c.inq_done++;
        event(1, 0x01, st, 1);
    }

    {
        sim_pad *x = &g_c.xbox;
        if (x->on && x->creating && !g_c.inquiry_on && g_now - x->create_at >= 30) {
            x->creating = 0;
            x->handle = XBOX_HANDLE;
            le_connection_complete(1, 0);
        }
        if (x->on && !x->handle && !x->creating && g_c.scan_on && g_now >= x->advert_at) {
            x->advert_at = g_now + (g_c.scan_itv == 0x0400 ? 640 : 100);
            xbox_advert(x);
        }
        if (x->handle && x->subscribed && !x->streaming && g_now >= x->stream_at) {
            x->streaming = 1;
            x->next_report = g_now;
        }
        pad_tick(x, xbox_notify);
    }
    {
        sim_pad *d = &g_c.ds4;
        if (d->on && !d->called) {
            unsigned char r[10];
            memcpy(r, DS4_ADDR, 6);
            r[6] = 0x08; r[7] = 0x25; r[8] = 0x00;
            r[9] = 1;
            d->called = 1;
            event(1, 0x04, r, 10);
        }
        pad_tick(d, ds4_report);
    }

    for (i = 0; i < g_c.npending; i++) {
        sim_pkt *p = &g_c.pending[i];
        if (p->at > g_now) {
            if (kept != i) g_c.pending[kept] = *p;
            kept++;
            continue;
        }
        if (p->acl) {
            sim_ready *r;
            if (g_c.acl_n == SIM_READY) { g_c.ready_overflow++; continue; }
            r = &g_c.aclq[(g_c.acl_head + g_c.acl_n++) % SIM_READY];
            r->len = p->len;
            memcpy(r->d, p->d, (size_t)p->len);
            g_c.last_pad_acl = g_now;
        } else {
            sim_ready *r;
            if (g_c.ev_n == SIM_READY) { g_c.ready_overflow++; continue; }
            bt_cmdq_on_event(&g_c.q, p->d, p->len);
            r = &g_c.evq[(g_c.ev_head + g_c.ev_n++) % SIM_READY];
            r->len = p->len;
            memcpy(r->d, p->d, (size_t)p->len);
        }
    }
    g_c.npending = kept;
}

static int sim_pump(void *ctx, int timeout_ms)
{
    long dt = timeout_ms > 0 ? timeout_ms : 1;
    (void)ctx;
    if (g_c.unplugged) return -1;
    bt_cmdq_flush(&g_c.q, chip_command, &g_c);
    g_now += dt;
    chip_tick(dt);
    bt_cmdq_flush(&g_c.q, chip_command, &g_c);
    return g_c.ev_n || g_c.acl_n;
}

static int sim_next_event(void *ctx, unsigned char *out, int max)
{
    sim_ready *r;
    (void)ctx;
    if (!g_c.ev_n || (r = &g_c.evq[g_c.ev_head])->len > max) return 0;
    memcpy(out, r->d, (size_t)r->len);
    g_c.ev_head = (g_c.ev_head + 1) % SIM_READY;
    g_c.ev_n--;
    return r->len;
}

static int sim_next_acl(void *ctx, unsigned char *out, int max)
{
    sim_ready *r;
    (void)ctx;
    if (!g_c.acl_n || (r = &g_c.aclq[g_c.acl_head])->len > max) return 0;
    memcpy(out, r->d, (size_t)r->len);
    g_c.acl_head = (g_c.acl_head + 1) % SIM_READY;
    g_c.acl_n--;
    return r->len;
}

static void sim_close(void *ctx) { (void)ctx; }

static const hci_ops k_sim = {
    .cmd = sim_cmd, .acl_send = sim_acl_send, .pump = sim_pump,
    .next_event = sim_next_event, .next_acl = sim_next_acl, .close = sim_close,
};

/* ---- what the adapter's glue sees ---- */

static struct {
    int connects[HOST_MAX_PADS], disconnects[HOST_MAX_PADS], states[HOST_MAX_PADS];
    pad_state last[HOST_MAX_PADS];
    uint16_t pid[HOST_MAX_PADS];
    long first_state_at[HOST_MAX_PADS];
} g_seen;

static void seen_connect(void *ud, int slot, const pad_info *info)
{
    (void)ud;
    g_seen.connects[slot]++;
    g_seen.pid[slot] = info->pid;
}

static void seen_state(void *ud, int slot, const pad_state *st)
{
    (void)ud;
    if (!g_seen.states[slot]) g_seen.first_state_at[slot] = g_now;
    g_seen.states[slot]++;
    g_seen.last[slot] = *st;
}

static void seen_disconnect(void *ud, int slot)
{
    (void)ud;
    g_seen.disconnects[slot]++;
}

static const host_events k_events = {
    .on_connect = seen_connect, .on_state = seen_state, .on_disconnect = seen_disconnect,
};

static int g_auto_on, g_auto_mode;
static int g_auto_window;
static long g_next_auto;

static host_t *sim_open(int clone)
{
    hci_t hci = { &k_sim, &g_c };
    host_t *h;

    memset(&g_c, 0, sizeof g_c);
    memset(&g_seen, 0, sizeof g_seen);
    bt_cmdq_reset(&g_c.q);
    g_c.clone = clone;
    g_c.air = 15;
    g_c.scan_itv = 0x0060;
    g_c.last_pad_acl = -100000;
    memcpy(g_c.ds4.addr, DS4_ADDR, 6);
    memcpy(g_c.xbox.addr, XBOX_ADDR, 6);
    g_now += 100000;
    remove("build/sim_bt.db");

    h = host_open(hci, "build/sim_bt.db", &k_events);
    if (h) host_set_le_quiet_when_linked(h, clone);
    g_auto_on = 0;
    g_auto_mode = -1;
    g_auto_window = 0;
    g_next_auto = 0;
    return h;
}

/* The stored pads, as pairing leaves them */
static void store_ds4(host_t *h)
{
    db_rec *r = db_put(h, DS4_ADDR);
    r->vid = 0x054C;
    r->pid = 0x09CC;
    r->le = 0;
}

static void store_xbox(host_t *h)
{
    db_rec *r = db_put(h, XBOX_ADDR);
    r->vid = 0x045E;
    r->pid = 0x0B13;
    r->le = 1;
    r->addr_type = 0;
}

/* One turn of the adapter's thread (bt_adapter.c), background search included */
static void adapter_turn(host_t *h)
{
    host_poll(h, 10);
    if (!g_auto_on) return;
    {
        long now = now_ms();
        int mode = bt_auto_mode(1, 0, host_linked_count(h), g_c.clone);
        if (mode != g_auto_mode) {
            g_auto_mode = mode;
            host_tune_set(h, "inq_len", mode == BT_AUTO_PEEK ? BT_AUTO_PEEK_INQ : BT_AUTO_FULL_INQ);
            if (g_auto_window && host_pairing_left(h) > 0) host_pair(h, 0);
            g_auto_window = 0;
            g_next_auto = now + (mode == BT_AUTO_PEEK ? BT_AUTO_PEEK_MS : 0);
        }
        if (mode != BT_AUTO_OFF && host_pairing_left(h) == 0 && now >= g_next_auto) {
            host_pair(h, mode == BT_AUTO_PEEK ? BT_AUTO_PEEK_S : BT_AUTO_FULL_S);
            g_auto_window = 1;
            g_next_auto = now + (mode == BT_AUTO_PEEK ? BT_AUTO_PEEK_MS : 0);
        }
    }
}

static void run(host_t *h, long ms)
{
    long end = g_now + ms;
    while (g_now < end && !host_transport_lost(h)) adapter_turn(h);
}

/* Reports the host delivered to this slot per second, over ms of running */
static int rate(host_t *h, int slot, long ms)
{
    int before = g_seen.states[slot];
    run(h, ms);
    return (int)((g_seen.states[slot] - before) * 1000L / ms);
}

static int commands_since(int from) { return g_c.nsent - from; }

static int count_op_since(int from, unsigned op)
{
    int i, n = 0;
    for (i = from; i < g_c.nsent; i++) if (g_c.sent[i].op == op) n++;
    return n;
}

static int slot_of_addr(host_t *h, const unsigned char *addr)
{
    int i;
    for (i = 0; i < HOST_MAX_PADS; i++)
        if (h->links[i].used && memcmp(h->links[i].addr, addr, 6) == 0) return i;
    return -1;
}

/* ---- the command queue ---- */

static uint16_t g_q_sent[8];
static int g_q_nsent, g_q_alive;

static int q_send(void *ctx, uint16_t op, const uint8_t *p, int n)
{
    (void)ctx; (void)p; (void)n;
    if (g_q_nsent < 8) g_q_sent[g_q_nsent] = op;
    g_q_nsent++;
    return g_q_alive;
}

static void q_arrange(bt_cmdq_t *q)
{
    bt_cmdq_reset(q);
    g_q_nsent = 0;
    g_q_alive = 1;
}

static void test_bt_cmdq_flush_oneCredit_waitsForTheReply(void)
{
    /* arrange */
    static bt_cmdq_t q;
    static const uint8_t complete[6] = { 0x0E, 4, 1, 0x0C, 0x20, 0 };
    static const uint8_t status[6] = { 0x0F, 4, 0, 1, 0x0D, 0x20 };
    q_arrange(&q);
    bt_cmdq_push(&q, 0x200C, NULL, 0);
    bt_cmdq_push(&q, 0x200D, NULL, 0);

    /* act */
    bt_cmdq_flush(&q, q_send, NULL);
    int first = g_q_nsent;
    bt_cmdq_on_event(&q, complete, sizeof complete);
    bt_cmdq_flush(&q, q_send, NULL);
    int second = g_q_nsent;
    bt_cmdq_push(&q, 0x200C, NULL, 0);
    bt_cmdq_on_event(&q, status, sizeof status);
    bt_cmdq_flush(&q, q_send, NULL);

    /* assert */
    CHECK(first == 1, "only one command goes out on one credit, got %d", first);
    CHECK(second == 2 && g_q_sent[1] == 0x200D, "the next goes when the reply gives the credit back");
    CHECK(g_q_nsent == 3, "credits come from byte 3 of a Command Status too, got %d", g_q_nsent);
}

static void test_bt_cmdq_flush_noReply_goesOnAfterTheWait(void)
{
    /* arrange */
    static bt_cmdq_t q;
    q_arrange(&q);
    bt_cmdq_push(&q, 1, NULL, 0);
    bt_cmdq_push(&q, 2, NULL, 0);
    bt_cmdq_flush(&q, q_send, NULL);

    /* act */
    g_now += BT_CMD_REPLY_WAIT_MS;
    bt_cmdq_flush(&q, q_send, NULL);
    int at_the_limit = g_q_nsent;
    g_now += 1;
    bt_cmdq_flush(&q, q_send, NULL);

    /* assert */
    CHECK(at_the_limit == 1, "the controller keeps its turn until the wait is over");
    CHECK(g_q_nsent == 2, "a controller that never answers does not stop the queue for good");
}

static void test_bt_cmdq_push_full_refusesAndKeepsOrder(void)
{
    /* arrange */
    static bt_cmdq_t q;
    q_arrange(&q);
    q.credits = 0;
    q.sent_at = g_now;
    for (int i = 0; i < BT_CMDQ_LEN; i++) CHECK(bt_cmdq_push(&q, (unsigned)i, NULL, 0), "room for %d", i);

    /* act */
    int over = bt_cmdq_push(&q, 99, NULL, 0);
    int too_long = bt_cmdq_push(&q, 99, NULL, 256);
    q.credits = 3;
    bt_cmdq_flush(&q, q_send, NULL);

    /* assert */
    CHECK(over == 0, "a full queue refuses the command");
    CHECK(too_long == 0, "parameters over 255 bytes are refused");
    CHECK(g_q_sent[0] == 0 && g_q_sent[1] == 1 && g_q_sent[2] == 2, "commands go in the order they came");
}

static void test_bt_cmdq_flush_transportGone_stops(void)
{
    /* arrange */
    static bt_cmdq_t q;
    q_arrange(&q);
    q.credits = 5;
    g_q_alive = 0;
    bt_cmdq_push(&q, 1, NULL, 0);
    bt_cmdq_push(&q, 2, NULL, 0);

    /* act */
    bt_cmdq_flush(&q, q_send, NULL);

    /* assert */
    CHECK(g_q_nsent == 1, "nothing more is sent once the adapter is gone, got %d", g_q_nsent);
}

static void test_bt_auto_mode_table_keepsTheRules(void)
{
    /* assert */
    CHECK(bt_auto_mode(0, 0, 0, 0) == BT_AUTO_OFF, "no search with every slot taken");
    CHECK(bt_auto_mode(1, 1, 0, 0) == BT_AUTO_OFF, "no search around rest mode");
    CHECK(bt_auto_mode(1, 0, 0, 0) == BT_AUTO_FULL, "full search with nothing connected");
    CHECK(bt_auto_mode(1, 0, 0, 1) == BT_AUTO_FULL, "a clone searches too with nothing connected");
    CHECK(bt_auto_mode(1, 0, 1, 0) == BT_AUTO_PEEK, "a good chip takes a short look with a pad connected");
    CHECK(bt_auto_mode(1, 0, 1, 1) == BT_AUTO_OFF, "a clone with a pad connected is left alone");
}

/* ---- the host over the chip ---- */

static void test_host_open_strictChip_nothingArrivesEarly(void)
{
    /* act */
    host_t *h = sim_open(0);

    /* assert */
    CHECK(h != NULL, "the host starts on the chip");
    if (!h) return;
    CHECK(h->le_ok, "the host saw LE");
    CHECK(h->br.max == 8, "the host read the ACL buffers, got %d", h->br.max);
    CHECK(g_c.early == 0, "no command arrives before the chip gives the turn, got %d", g_c.early);
    CHECK(g_c.nsent > 5, "start-up sent its commands, got %d", g_c.nsent);
    host_close(h);
}

static void test_ds4_reconnect_reachesFullReport(void)
{
    /* arrange */
    host_t *h = sim_open(0);
    store_ds4(h);
    g_c.ds4.on = 1;

    /* act */
    run(h, 6000);

    /* assert */
    int s = slot_of_addr(h, DS4_ADDR);
    CHECK(s >= 0, "the DS4 has a slot");
    if (s < 0) { host_close(h); return; }
    CHECK(h->links[s].ready, "the link is ready");
    CHECK(g_seen.connects[s] == 1, "one virtual DualSense, got %d", g_seen.connects[s]);
    CHECK(g_seen.pid[s] == 0x09CC, "known as the stored pad");
    CHECK(g_c.ds4.full, "the host asked for the full report");
    CHECK(h->links[s].full, "the host reads the full report");
    CHECK(g_seen.last[s].lx == 0x55 && g_seen.last[s].ry == 0x88, "sticks of report 0x11, got %02x %02x",
          g_seen.last[s].lx, g_seen.last[s].ry);
    CHECK(g_c.early == 0, "no command arrived early, got %d", g_c.early);
    host_close(h);
}

static void test_ds4_streaming_storedXboxOff_keepsItsRate(void)
{
    /* arrange: a stored LE pad that is off kept the radio scanning, and the DS4 fell from 97 to
     * 28 reports/s with gaps over 1 s (PS5, 2 Nov) */
    host_t *h = sim_open(0);
    store_ds4(h);
    store_xbox(h);
    g_c.ds4.on = 1;
    run(h, 6000);
    int s = slot_of_addr(h, DS4_ADDR);

    /* act */
    int from = g_c.nsent;
    int r = rate(h, s, 60000);

    /* assert */
    CHECK(r >= 90, "the DS4 keeps its rate, got %d reports/s", r);
    CHECK(g_c.scan_on && g_c.scan_itv == 0x0400, "the scan stays on, in its light form (itv %#x, on %d)",
          g_c.scan_itv, g_c.scan_on);
    CHECK(commands_since(from) == 0, "a settled link gets no commands, got %d", commands_since(from));
    host_close(h);
}

static void test_ds4_streaming_clone_getsNoCommandAtAll(void)
{
    /* arrange: a clone hangs ~5 s on any command that arrives while a pad reports */
    host_t *h = sim_open(1);
    store_ds4(h);
    store_xbox(h);
    g_auto_on = 1;
    g_c.ds4.on = 1;
    run(h, 8000);
    int s = slot_of_addr(h, DS4_ADDR);
    int hangs_before = g_c.hangs;

    /* act */
    int from = g_c.nsent;
    int r = rate(h, s, 120000);

    /* assert */
    CHECK(r >= 90, "the DS4 keeps its rate on the clone, got %d reports/s", r);
    CHECK(!g_c.scan_on, "no LE scan on a clone with a pad connected");
    CHECK(commands_since(from) == 0, "not one command in 2 minutes of play, got %d", commands_since(from));
    CHECK(count_op_since(from, OP_READ_BUFFER_SIZE) == 0, "ACL traffic counts as life: no probes");
    CHECK(g_c.hangs == hangs_before, "the clone does not hang during play, got %d", g_c.hangs - hangs_before);
    CHECK(hangs_before == 0, "nor while the pad connects, got %d", hangs_before);
    host_close(h);
}

static void test_search_nothingConnected_takesTurnsWithLeScan(void)
{
    /* arrange: with both on, a clone never finished an inquiry and heard pads only as one
     * restarted (PS5, 2 Nov) */
    host_t *h = sim_open(0);
    g_auto_on = 1;

    /* act */
    run(h, 60000);

    /* assert */
    CHECK(g_c.inq_done >= 5, "inquiry rounds run to their end, got %d of %d", g_c.inq_done, g_c.inq_started);
    CHECK(g_c.scan_on_ms >= 12000, "the LE scan gets its turns, got %ld ms", g_c.scan_on_ms);
    CHECK(g_c.overlap_ms <= 500, "search and scan do not share the radio, got %ld ms", g_c.overlap_ms);
    CHECK(g_c.early == 0, "no command arrived early, got %d", g_c.early);
    host_close(h);
}

static void test_xbox_reconnect_duringSearch_connectsAndSpeaks(void)
{
    /* arrange: a stored Xbox switched on while the background search runs */
    host_t *h = sim_open(0);
    store_xbox(h);
    g_auto_on = 1;
    run(h, 2000);
    g_c.xbox.on = 1;
    long on_at = g_now;

    /* act */
    run(h, 20000);

    /* assert */
    int s = slot_of_addr(h, XBOX_ADDR);
    CHECK(s >= 0, "the Xbox has a slot");
    if (s < 0) { host_close(h); return; }
    CHECK(g_c.create_during_inquiry == 0, "the search stops before LE Create Connection, got %d", g_c.create_during_inquiry);
    CHECK(g_c.create_during_scan == 0, "the scan stops before LE Create Connection, got %d", g_c.create_during_scan);
    CHECK(g_c.inq_while_linking == 0, "no search starts while it links, got %d", g_c.inq_while_linking);
    CHECK(g_c.xbox.reads == XBOX_READS_ALL, "every readable value was read before the subscribe, got %#x", g_c.xbox.reads);
    CHECK(g_seen.connects[s] == 1, "one virtual DualSense, got %d", g_seen.connects[s]);
    CHECK(g_seen.states[s] > 0 && g_seen.first_state_at[s] - on_at < 15000,
          "it speaks in seconds, not in a minute: %ld ms", g_seen.first_state_at[s] - on_at);
    CHECK(g_seen.pid[s] == 0x0B13, "known as the stored pad");
    CHECK(g_seen.last[s].lx >= 0x7a && g_seen.last[s].lx <= 0x85, "real sticks reach the slot, got %02x", g_seen.last[s].lx);
    CHECK(g_c.early == 0, "no command arrived early: an early Create Connection is dropped, got %d", g_c.early);
    host_close(h);
}

static void test_xbox_advertInFlight_duringInquiry_stopsTheSearch(void)
{
    /* arrange: an advert still on its way when the next inquiry starts */
    host_t *h = sim_open(0);
    store_xbox(h);
    g_auto_on = 1;
    while (!g_c.inquiry_on) adapter_turn(h);
    g_c.xbox.on = 1;

    /* act */
    xbox_advert(&g_c.xbox);
    run(h, 8000);

    /* assert */
    CHECK(g_c.create_during_inquiry == 0, "a search left an LE connection unanswered for a minute, got %d",
          g_c.create_during_inquiry);
    CHECK(slot_of_addr(h, XBOX_ADDR) >= 0 && g_seen.states[slot_of_addr(h, XBOX_ADDR)] > 0, "the Xbox connects and speaks");
    host_close(h);
}

static void test_xbox_slowLink_duringPageSearch_noInquiryUntilReady(void)
{
    /* arrange: the search the Pair button opens outlives the link, and this link takes longer
     * than the LE scan's turn */
    host_t *h = sim_open(0);
    store_xbox(h);
    host_tune_set(h, "inq_len", BT_AUTO_FULL_INQ);
    g_c.air = 300;
    g_c.xbox.on = 1;
    host_pair(h, 120);

    /* act */
    run(h, 30000);

    /* assert */
    int s = slot_of_addr(h, XBOX_ADDR);
    CHECK(s >= 0 && g_seen.states[s] > 0, "the Xbox connects and speaks");
    CHECK(g_c.inq_while_linking == 0, "no inquiry starts until the link is ready, got %d", g_c.inq_while_linking);
    host_close(h);
}

static void test_xbox_and_ds4_together_bothPlay(void)
{
    /* arrange */
    host_t *h = sim_open(0);
    store_ds4(h);
    store_xbox(h);
    g_auto_on = 1;
    g_c.ds4.on = 1;
    run(h, 6000);
    g_c.xbox.on = 1;
    run(h, 30000);
    int d = slot_of_addr(h, DS4_ADDR), x = slot_of_addr(h, XBOX_ADDR);
    CHECK(d >= 0 && x >= 0 && d != x, "each has its own slot (%d, %d)", d, x);
    if (d < 0 || x < 0) { host_close(h); return; }

    /* act: the good chip keeps its short look every 20 s, which is why this is lower than alone */
    int ds4_before = g_seen.states[d], xbox_before = g_seen.states[x];
    run(h, 60000);

    /* assert */
    int ds4_rate = (g_seen.states[d] - ds4_before) / 60, xbox_rate = (g_seen.states[x] - xbox_before) / 60;
    CHECK(ds4_rate >= 75, "the DS4 still plays, got %d reports/s", ds4_rate);
    CHECK(xbox_rate >= 75, "the Xbox still plays, got %d reports/s", xbox_rate);
    CHECK(g_seen.connects[d] == 1 && g_seen.connects[x] == 1, "nobody reconnected");
    CHECK(g_c.early == 0, "no command arrived early, got %d", g_c.early);
    host_close(h);
}

static void test_ds4_switchedOffAndOn_comesBack(void)
{
    /* arrange */
    host_t *h = sim_open(0);
    store_ds4(h);
    g_c.ds4.on = 1;
    run(h, 6000);
    int s = slot_of_addr(h, DS4_ADDR);
    unsigned char gone[4] = { 0 };

    /* act */
    put_le16(gone + 1, DS4_HANDLE);
    gone[3] = 0x13;
    memset(&g_c.ds4, 0, sizeof g_c.ds4);
    memcpy(g_c.ds4.addr, DS4_ADDR, 6);
    event(1, 0x05, gone, 4);
    run(h, 3000);
    int freed = slot_of_addr(h, DS4_ADDR) < 0;
    g_c.ds4.on = 1;
    run(h, 6000);

    /* assert */
    CHECK(freed, "the link is let go when the pad switches off");
    CHECK(g_seen.disconnects[s] == 1, "the virtual pad is told once, got %d", g_seen.disconnects[s]);
    int back = slot_of_addr(h, DS4_ADDR);
    CHECK(back >= 0 && h->links[back].ready, "it comes back with PS");
    CHECK(back >= 0 && g_seen.connects[back] >= 1, "with a virtual DualSense again");
    host_close(h);
}

static void test_transport_adapterPulled_isLost(void)
{
    /* arrange */
    host_t *h = sim_open(0);
    run(h, 1000);

    /* act */
    g_c.unplugged = 1;
    run(h, 100);

    /* assert */
    CHECK(host_transport_lost(h), "the host says the adapter is gone, so it can be reopened");
    host_close(h);
}

static void test_transport_chipGoesDeaf_isLostInSeconds(void)
{
    /* arrange: rest mode can leave the USB device open but deaf */
    host_t *h = sim_open(0);
    store_ds4(h);
    g_c.ds4.on = 1;
    run(h, 6000);

    /* act */
    g_c.deaf = 1;
    memset(&g_c.ds4, 0, sizeof g_c.ds4);
    long t0 = g_now;
    run(h, 60000);

    /* assert */
    CHECK(host_transport_lost(h), "probes find the chip gone");
    CHECK(g_now - t0 < 40000, "in less than 40 s, got %ld ms", g_now - t0);
    g_c.deaf = 0;
    host_close(h);
}

int main(void)
{
    setvbuf(stdout, NULL, _IOLBF, 0);
    g_log = getenv("BT_SIM_LOG") != NULL;
    test_bt_cmdq_flush_oneCredit_waitsForTheReply();
    test_bt_cmdq_flush_noReply_goesOnAfterTheWait();
    test_bt_cmdq_push_full_refusesAndKeepsOrder();
    test_bt_cmdq_flush_transportGone_stops();
    test_bt_auto_mode_table_keepsTheRules();
    test_host_open_strictChip_nothingArrivesEarly();
    test_ds4_reconnect_reachesFullReport();
    test_ds4_streaming_storedXboxOff_keepsItsRate();
    test_ds4_streaming_clone_getsNoCommandAtAll();
    test_search_nothingConnected_takesTurnsWithLeScan();
    test_xbox_reconnect_duringSearch_connectsAndSpeaks();
    test_xbox_advertInFlight_duringInquiry_stopsTheSearch();
    test_xbox_slowLink_duringPageSearch_noInquiryUntilReady();
    test_xbox_and_ds4_together_bothPlay();
    test_ds4_switchedOffAndOn_comesBack();
    test_transport_adapterPulled_isLost();
    test_transport_chipGoesDeaf_isLostInSeconds();
    printf("test_bt_sim: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
