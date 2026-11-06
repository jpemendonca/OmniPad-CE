#include "capture.h"
#include "log.h"
#include "util.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CAPTURE_MAX_ENTRIES 40000   /* a DS4 on USB sends 250 reports/s: 160 s of it, in about 4 MB */
#define CAPTURE_REPORT_MAX  80      /* a DS4 Bluetooth report is 78 bytes */
#define CAPTURE_LABEL_MAX   48

typedef struct {
    int32_t  t;                     /* ms since the recording started */
    char     kind;
    int8_t   slot;
    int8_t   result;
    uint16_t vid, pid;
    uint16_t len;                   /* the report's real length; only CAPTURE_REPORT_MAX bytes are kept */
    uint32_t buttons;
    uint8_t  axes[6];               /* lx ly rx ry l2 r2 */
    uint8_t  data[CAPTURE_REPORT_MAX];
} capture_entry_t;

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static capture_entry_t *g_entries;
static int  g_count;
static int  g_slot = -1;
static long g_started_at, g_ends_at;
static int  g_full;
static volatile int g_on;           /* read without the lock: the cheap check on every report */

int capture_start(int slot, int seconds)
{
    if (seconds < 1) seconds = 1;
    if (seconds > 300) seconds = 300;
    pthread_mutex_lock(&g_lock);
    if (!g_entries) g_entries = malloc(sizeof(capture_entry_t) * CAPTURE_MAX_ENTRIES);
    if (!g_entries) {
        pthread_mutex_unlock(&g_lock);
        log_line("capture: no memory for %d reports", CAPTURE_MAX_ENTRIES);
        return 0;
    }
    g_count = 0;
    g_full = 0;
    g_slot = slot;
    g_started_at = now_ms();
    g_ends_at = g_started_at + seconds * 1000L;
    g_on = 1;
    pthread_mutex_unlock(&g_lock);
    log_line("capture: recording slot %d for up to %d s", slot, seconds);
    return 1;
}

void capture_stop(void)
{
    pthread_mutex_lock(&g_lock);
    int was_on = g_on;
    g_on = 0;
    int count = g_count;
    pthread_mutex_unlock(&g_lock);
    if (was_on) log_line("capture: stopped with %d entries", count);
}

int capture_wants(int slot)
{
    if (!g_on) return 0;
    if (now_ms() >= g_ends_at) {
        g_on = 0;
        return 0;
    }
    return slot == g_slot;
}

static capture_entry_t *take_entry(void)
{
    if (!g_on || !g_entries) return NULL;
    if (g_count >= CAPTURE_MAX_ENTRIES) {
        g_on = 0;
        g_full = 1;
        log_line("capture: buffer full at %d entries, recording stopped", g_count);
        return NULL;
    }
    capture_entry_t *e = &g_entries[g_count++];
    memset(e, 0, sizeof(*e));
    e->t = (int32_t)(now_ms() - g_started_at);
    return e;
}

void capture_mark(const char *label)
{
    pthread_mutex_lock(&g_lock);
    capture_entry_t *e = take_entry();
    if (e) {
        e->kind = 'M';
        e->slot = (int8_t)g_slot;
        int n = 0;
        for (const char *p = label; *p && n < CAPTURE_LABEL_MAX; p++) {
            char c = *p;
            int ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                     c == '_' || c == '-' || c == '+' || c == ':';
            e->data[n++] = (uint8_t)(ok ? c : '_');
        }
        e->len = (uint16_t)n;
    }
    pthread_mutex_unlock(&g_lock);
}

void capture_add(char kind, int slot, uint16_t vid, uint16_t pid, const uint8_t *rep, int len, int result,
                 uint32_t buttons, uint8_t lx, uint8_t ly, uint8_t rx, uint8_t ry, uint8_t l2, uint8_t r2)
{
    if (!capture_wants(slot) || len <= 0) return;
    pthread_mutex_lock(&g_lock);
    capture_entry_t *e = take_entry();
    if (e) {
        e->kind = kind;
        e->slot = (int8_t)slot;
        e->result = (int8_t)result;
        e->vid = vid;
        e->pid = pid;
        e->len = (uint16_t)len;
        e->buttons = buttons;
        uint8_t axes[6] = { lx, ly, rx, ry, l2, r2 };
        memcpy(e->axes, axes, sizeof(axes));
        memcpy(e->data, rep, (size_t)(len < CAPTURE_REPORT_MAX ? len : CAPTURE_REPORT_MAX));
    }
    pthread_mutex_unlock(&g_lock);
}

static int format_entry(const capture_entry_t *e, char *out, int n)
{
    if (e->kind == 'M') return snprintf(out, (size_t)n, "M %d %.*s\n", (int)e->t, (int)e->len, (const char *)e->data);

    char hex[CAPTURE_REPORT_MAX * 2 + 1];
    if (e->len > CAPTURE_REPORT_MAX) {
        strcpy(hex, "-");           /* cut short: replay skips it */
    } else {
        for (int i = 0; i < e->len; i++) snprintf(hex + i * 2, 3, "%02x", e->data[i]);
        hex[e->len * 2] = '\0';
    }
    return snprintf(out, (size_t)n, "%c %d %d %04x %04x %d %s %d %u %d %d %d %d %d %d\n",
                    e->kind, (int)e->t, e->slot, e->vid, e->pid, e->len, hex, e->result, (unsigned)e->buttons,
                    e->axes[0], e->axes[1], e->axes[2], e->axes[3], e->axes[4], e->axes[5]);
}

int capture_read(int since, char *out, int n, int *next, int *total, int *state)
{
    int len = 0;
    (void)capture_wants(g_slot);    /* ends a recording whose time is up */
    pthread_mutex_lock(&g_lock);
    if (since < 0) since = 0;
    int i = since;
    char line[CAPTURE_REPORT_MAX * 2 + 96];
    for (; g_entries && i < g_count; i++) {
        int m = format_entry(&g_entries[i], line, (int)sizeof(line));
        if (m <= 0 || len + m >= n) break;
        memcpy(out + len, line, (size_t)m);
        len += m;
    }
    *next = i;
    *total = g_count;
    *state = g_on ? 1 : g_full ? 3 : g_entries ? 2 : 0;
    pthread_mutex_unlock(&g_lock);
    out[len < n ? len : n - 1] = '\0';
    return len;
}
