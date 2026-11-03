#include "remap.h"

#include "log.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#define MAX_COMBO 3
#define RULES_TEXT 384   /* room for REMAP_MAX_RULES of the longest rule */

typedef struct {
    uint32_t from, to;
    int      nfrom;
} rule_t;

typedef struct {
    uint16_t vid, pid;
    int      nrules;
    rule_t   rules[REMAP_MAX_RULES];
} pad_rules_t;

static const struct { const char *name; uint32_t bit; } k_buttons[] = {
    { "cross", PAD_BTN_CROSS }, { "circle", PAD_BTN_CIRCLE }, { "square", PAD_BTN_SQUARE },
    { "triangle", PAD_BTN_TRIANGLE }, { "l1", PAD_BTN_L1 }, { "r1", PAD_BTN_R1 },
    { "l2", PAD_BTN_L2 }, { "r2", PAD_BTN_R2 }, { "l3", PAD_BTN_L3 }, { "r3", PAD_BTN_R3 },
    { "options", PAD_BTN_OPTIONS }, { "create", PAD_BTN_CREATE }, { "touchpad", PAD_BTN_TOUCHPAD },
    { "ps", PAD_BTN_PS }, { "up", PAD_DPAD_UP }, { "down", PAD_DPAD_DOWN },
    { "left", PAD_DPAD_LEFT }, { "right", PAD_DPAD_RIGHT },
};
#define NBUTTONS (sizeof k_buttons / sizeof k_buttons[0])

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static pad_rules_t g_pads[REMAP_MAX_PADS];
static int g_npads;
static atomic_int g_active;
static char g_path[256];

static uint32_t button_by_name(const char *s, size_t n)
{
    for (size_t i = 0; i < NBUTTONS; i++)
        if (strlen(k_buttons[i].name) == n && strncmp(k_buttons[i].name, s, n) == 0) return k_buttons[i].bit;
    return 0;
}

static const char *button_name(uint32_t bit)
{
    for (size_t i = 0; i < NBUTTONS; i++) if (k_buttons[i].bit == bit) return k_buttons[i].name;
    return "?";
}

/* Reads "a+b=c". PS is refused as a source: it is how a user reaches the profile screen, and a
 * rule must never be able to take it away. */
static int parse_rule(const char *s, size_t n, rule_t *r)
{
    const char *eq = memchr(s, '=', n);
    const char *p = s;

    if (!eq) return 0;
    r->to = button_by_name(eq + 1, (size_t)(s + n - eq - 1));
    r->from = 0;
    r->nfrom = 0;
    if (!r->to) return 0;
    while (p < eq) {
        const char *plus = memchr(p, '+', (size_t)(eq - p));
        const char *end = plus ? plus : eq;
        uint32_t bit = button_by_name(p, (size_t)(end - p));
        if (!bit || bit == PAD_BTN_PS || (r->from & bit) || r->nfrom == MAX_COMBO) return 0;
        r->from |= bit;
        r->nfrom++;
        p = plus ? plus + 1 : eq;
        if (plus && p == eq) return 0;
    }
    if (!r->nfrom || (r->nfrom == 1 && r->from == r->to)) return 0;
    return 1;
}

/* Longer combos come first: L3+R3 must win over a rule for L3 alone. */
static void insert_rule(pad_rules_t *out, const rule_t *r)
{
    int i = out->nrules++;
    while (i > 0 && out->rules[i - 1].nfrom < r->nfrom) {
        out->rules[i] = out->rules[i - 1];
        i--;
    }
    out->rules[i] = *r;
}

static int parse_rules(const char *text, pad_rules_t *out)
{
    const char *p = text;

    out->nrules = 0;
    while (*p) {
        const char *comma = strchr(p, ',');
        size_t n = comma ? (size_t)(comma - p) : strlen(p);
        rule_t r;
        if (out->nrules == REMAP_MAX_RULES || !parse_rule(p, n, &r)) return 0;
        for (int i = 0; i < out->nrules; i++) if (out->rules[i].from == r.from) return 0;
        insert_rule(out, &r);
        if (!comma) break;
        p = comma + 1;
        if (!*p) return 0;
    }
    return 1;
}

static void format_rules(const pad_rules_t *pad, char *out, size_t n)
{
    size_t len = 0;

    out[0] = 0;
    for (int i = 0; i < pad->nrules; i++) {
        const rule_t *r = &pad->rules[i];
        int first = 1;
        if (i) len += (size_t)snprintf(out + len, n - len, ",");
        for (size_t b = 0; b < NBUTTONS; b++) {
            if (!(r->from & k_buttons[b].bit)) continue;
            len += (size_t)snprintf(out + len, n - len, "%s%s", first ? "" : "+", k_buttons[b].name);
            first = 0;
        }
        len += (size_t)snprintf(out + len, n - len, "=%s", button_name(r->to));
    }
}

static pad_rules_t *find_pad(uint16_t vid, uint16_t pid)
{
    for (int i = 0; i < g_npads; i++) if (g_pads[i].vid == vid && g_pads[i].pid == pid) return &g_pads[i];
    return NULL;
}

static void set_locked(uint16_t vid, uint16_t pid, const pad_rules_t *rules)
{
    pad_rules_t *pad = find_pad(vid, pid);

    if (!rules->nrules) {
        if (pad) *pad = g_pads[--g_npads];
    } else {
        if (!pad) pad = &g_pads[g_npads++];
        *pad = *rules;
        pad->vid = vid;
        pad->pid = pid;
    }
    atomic_store(&g_active, g_npads > 0);
}

static int save_locked(void)
{
    char tmp[sizeof g_path + 4];
    FILE *f;

    if (!g_path[0]) return 1;
    snprintf(tmp, sizeof tmp, "%s.tmp", g_path);
    f = fopen(tmp, "w");
    if (!f) {
        log_line("remap: cannot write %s", tmp);
        return 0;
    }
    for (int i = 0; i < g_npads; i++) {
        char text[RULES_TEXT];
        format_rules(&g_pads[i], text, sizeof text);
        fprintf(f, "%04x:%04x %s\n", g_pads[i].vid, g_pads[i].pid, text);
    }
    if (fclose(f) != 0 || rename(tmp, g_path) != 0) {
        log_line("remap: cannot save %s", g_path);
        return 0;
    }
    return 1;
}

void remap_init(const char *path)
{
    char line[RULES_TEXT + 16];
    FILE *f;

    pthread_mutex_lock(&g_lock);
    g_npads = 0;
    atomic_store(&g_active, 0);
    snprintf(g_path, sizeof g_path, "%s", path);
    f = fopen(path, "r");
    while (f && fgets(line, sizeof line, f)) {
        unsigned vid, pid;
        char text[RULES_TEXT];
        pad_rules_t rules;
        if (sscanf(line, "%x:%x %383s", &vid, &pid, text) != 3 || !parse_rules(text, &rules) ||
            (!find_pad((uint16_t)vid, (uint16_t)pid) && g_npads == REMAP_MAX_PADS)) {
            log_line("remap: line ignored: %s", line);
            continue;
        }
        set_locked((uint16_t)vid, (uint16_t)pid, &rules);
    }
    if (f) fclose(f);
    if (g_npads) log_line("remap: rules for %d controller model(s)", g_npads);
    pthread_mutex_unlock(&g_lock);
}

int remap_active(void)
{
    return atomic_load(&g_active);
}

void remap_apply(pad_state_t *st)
{
    const uint32_t in = st->buttons;
    uint32_t drop = 0, add = 0, used = 0;
    uint8_t l2 = st->l2, r2 = st->r2;

    pthread_mutex_lock(&g_lock);
    const pad_rules_t *pad = find_pad(st->vid, st->pid);
    for (int i = 0; pad && i < pad->nrules; i++) {
        const rule_t *r = &pad->rules[i];
        /* A trigger with a rule of its own leaves its axis, or a light touch under the digital
         * threshold would still reach the game */
        if (r->from == PAD_BTN_L2) l2 = 0;
        if (r->from == PAD_BTN_R2) r2 = 0;
    }
    for (int i = 0; pad && i < pad->nrules; i++) {
        const rule_t *r = &pad->rules[i];
        uint8_t level = r->from == PAD_BTN_L2 ? st->l2 : r->from == PAD_BTN_R2 ? st->r2 : 255;
        int analog = r->from == PAD_BTN_L2 || r->from == PAD_BTN_R2;
        int on = (in & r->from) == r->from && !(used & r->from);
        if (on) {
            used |= r->from;
            drop |= r->from;
            add |= r->to;
        }
        /* One trigger onto the other carries its travel, also below the digital threshold */
        if (on || analog) {
            if (r->to == PAD_BTN_L2 && level > l2) l2 = level;
            if (r->to == PAD_BTN_R2 && level > r2) r2 = level;
        }
    }
    pthread_mutex_unlock(&g_lock);
    if (!pad) return;
    st->buttons = (in & ~drop) | add;
    st->l2 = l2;
    st->r2 = r2;
}

int remap_set(uint16_t vid, uint16_t pid, const char *rules)
{
    pad_rules_t parsed;
    int ok;

    if (!parse_rules(rules, &parsed)) {
        log_line("remap: rules not understood for %04x:%04x: %s", vid, pid, rules);
        return 0;
    }
    pthread_mutex_lock(&g_lock);
    if (parsed.nrules && !find_pad(vid, pid) && g_npads == REMAP_MAX_PADS) {
        pthread_mutex_unlock(&g_lock);
        log_line("remap: no room for %04x:%04x", vid, pid);
        return 0;
    }
    set_locked(vid, pid, &parsed);
    ok = save_locked();
    pthread_mutex_unlock(&g_lock);
    log_line("remap: %04x:%04x now has %d rule(s)", vid, pid, parsed.nrules);
    return ok;
}

int remap_json(char *out, int n)
{
    int len = snprintf(out, (size_t)n, "{\"pads\":[");

    pthread_mutex_lock(&g_lock);
    for (int i = 0; i < g_npads && len < n; i++) {
        char text[RULES_TEXT];
        format_rules(&g_pads[i], text, sizeof text);
        len += snprintf(out + len, (size_t)(n - len), "%s{\"vid\":%u,\"pid\":%u,\"rules\":\"%s\"}",
                        i ? "," : "", g_pads[i].vid, g_pads[i].pid, text);
    }
    pthread_mutex_unlock(&g_lock);
    if (len < n) len += snprintf(out + len, (size_t)(n - len), "]}");
    return len;
}
