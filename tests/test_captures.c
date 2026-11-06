/* Replays the recordings in tests/captures/ (made on the PS5 with tools/record_pads.py) through the
 * parsers of this build, and checks that every report decodes exactly as it did on the build that
 * recorded it. A controller that was approved on the PS5 keeps decoding the same way.
 *
 * A change meant to alter how a recorded controller decodes is checked on the PS5, then the
 * recording is refreshed with: ./build/test_captures --update */
#include "pad_drivers.h"
#include "bt/bt_profiles.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures, checks;

#define CHECK(cond, ...) do { checks++; if (!(cond)) { failures++; printf("FAIL: " __VA_ARGS__); printf("\n"); } } while (0)

#define CAPTURE_DIR "tests/captures"
#define LINE_MAX_LEN 512
#define SHOWN_PER_FILE 5

typedef struct {
    char kind;
    int t, slot, len, result;
    unsigned vid, pid, buttons;
    int axes[6];
    char hex[LINE_MAX_LEN];
} entry_t;

typedef struct {
    uint16_t vid, pid;
    const pad_profile *prof;
    int seen;
    pad_state st;
    unsigned char ctx[PROFILE_CTX_MAX];
} bt_pad_t;

static int parse_entry(const char *line, entry_t *e)
{
    int n = sscanf(line, "%c %d %d %x %x %d %511s %d %u %d %d %d %d %d %d", &e->kind, &e->t, &e->slot, &e->vid,
                   &e->pid, &e->len, e->hex, &e->result, &e->buttons, &e->axes[0], &e->axes[1], &e->axes[2],
                   &e->axes[3], &e->axes[4], &e->axes[5]);
    return n == 15 && (e->kind == 'U' || e->kind == 'B');
}

static int unhex(const char *hex, uint8_t *out, int cap)
{
    int n = 0;
    unsigned v;
    while (hex[0] && hex[1] && n < cap && sscanf(hex, "%2x", &v) == 1) {
        out[n++] = (uint8_t)v;
        hex += 2;
    }
    return n;
}

/* What this build makes of one recorded report: result, buttons and axes. 0 if it cannot be replayed. */
static int replay(const entry_t *e, bt_pad_t *pads, int *npads, int *result, unsigned *buttons, int axes[6])
{
    uint8_t rep[128];
    if (strcmp(e->hex, "-") == 0) return 0;
    int len = unhex(e->hex, rep, (int)sizeof rep);
    if (len != e->len) return 0;

    if (e->kind == 'U') {
        const pad_driver_t *drv = pad_driver_lookup((uint16_t)e->vid, (uint16_t)e->pid);
        if (!drv) drv = pad_driver_for_xinput_class((uint16_t)e->vid, (uint16_t)e->pid);
        pad_state_t st;
        pad_state_neutral(&st);
        *result = pad_driver_parse(drv, rep, (size_t)len, &st);
        *buttons = st.buttons;
        int a[6] = { st.lx, st.ly, st.rx, st.ry, st.l2, st.r2 };
        memcpy(axes, a, sizeof a);
        return 1;
    }

    bt_pad_t *p = NULL;
    for (int i = 0; i < *npads; i++)
        if (pads[i].vid == e->vid && pads[i].pid == e->pid) p = &pads[i];
    if (!p) {
        if (*npads >= 4) return 0;
        p = &pads[(*npads)++];
        memset(p, 0, sizeof *p);
        p->vid = (uint16_t)e->vid;
        p->pid = (uint16_t)e->pid;
        p->prof = profile_find(p->vid, p->pid);
        if (p->prof && p->prof->setup) p->prof->setup(p->ctx, p->vid, p->pid);
        pad_state_reset(&p->st);
    }
    if (!p->prof) return 0;     /* the generic profile needs the pad's HID descriptor, not recorded */
    if (!p->seen) {
        /* The host keeps one state per pad, and the recording starts with it already filled */
        p->st.buttons = e->buttons;
        p->st.lx = (uint8_t)e->axes[0]; p->st.ly = (uint8_t)e->axes[1];
        p->st.rx = (uint8_t)e->axes[2]; p->st.ry = (uint8_t)e->axes[3];
        p->st.l2 = (uint8_t)e->axes[4]; p->st.r2 = (uint8_t)e->axes[5];
    }
    *result = p->prof->parse(p->ctx, rep, len, &p->st);
    *buttons = p->st.buttons;
    int a[6] = { p->st.lx, p->st.ly, p->st.rx, p->st.ry, p->st.l2, p->st.r2 };
    memcpy(axes, a, sizeof a);
    return p->seen++ > 0;       /* the first report only fills the state */
}

static int read_only_result(const entry_t *e)
{
    return e->kind == 'U' ? e->result == 0 : e->result == PARSE_NONE;
}

static void test_capture_file_replays_likeWhenRecorded(const char *path, int update)
{
    /* arrange */
    FILE *f = fopen(path, "r");
    if (!f) { CHECK(0, "%s cannot be opened", path); return; }
    char out_path[600];
    snprintf(out_path, sizeof out_path, "%s.new", path);
    FILE *out = update ? fopen(out_path, "w") : NULL;
    bt_pad_t pads[4];
    int npads = 0, reports = 0, compared = 0, skipped = 0, shown = 0, file_failures = 0;
    char line[LINE_MAX_LEN];

    while (fgets(line, sizeof line, f)) {
        entry_t e;
        if (!parse_entry(line, &e)) {
            if (out) fputs(line, out);
            continue;
        }
        reports++;

        /* act */
        int result = 0, axes[6];
        unsigned buttons = 0;
        int ok = replay(&e, pads, &npads, &result, &buttons, axes);

        /* assert */
        if (!ok) {
            skipped++;
            if (out) fputs(line, out);
            continue;
        }
        compared++;
        int same = result == e.result &&
                   (read_only_result(&e) || (buttons == e.buttons && memcmp(axes, e.axes, sizeof axes) == 0));
        if (out) {
            fprintf(out, "%c %d %d %04x %04x %d %s %d %u %d %d %d %d %d %d\n", e.kind, e.t, e.slot, e.vid, e.pid,
                    e.len, e.hex, result, buttons, axes[0], axes[1], axes[2], axes[3], axes[4], axes[5]);
            continue;
        }
        checks++;
        if (!same) {
            failures++;
            file_failures++;
            if (shown++ < SHOWN_PER_FILE)
                printf("FAIL: %s at %d ms: %04x:%04x %s decoded as result %d buttons 0x%08x axes %d %d %d %d %d %d, "
                       "recorded result %d buttons 0x%08x axes %d %d %d %d %d %d\n",
                       path, e.t, e.vid, e.pid, e.hex, result, buttons, axes[0], axes[1], axes[2], axes[3], axes[4],
                       axes[5], e.result, e.buttons, e.axes[0], e.axes[1], e.axes[2], e.axes[3], e.axes[4], e.axes[5]);
        }
    }
    fclose(f);
    if (out) {
        fclose(out);
        rename(out_path, path);
        printf("%s: %d reports refreshed\n", path, compared);
        return;
    }
    printf("%s: %d reports, %d compared, %d skipped, %d differ\n", path, reports, compared, skipped, file_failures);
    CHECK(compared > 0 || reports == 0, "%s replays at least one report", path);
}

int main(int argc, char **argv)
{
    int update = argc > 1 && strcmp(argv[1], "--update") == 0;
    DIR *d = opendir(CAPTURE_DIR);
    int files = 0;
    if (d) {
        struct dirent *de;
        while ((de = readdir(d)) != NULL) {
            size_t n = strlen(de->d_name);
            if (n < 5 || strcmp(de->d_name + n - 4, ".txt") != 0) continue;
            char path[512];
            snprintf(path, sizeof path, "%s/%s", CAPTURE_DIR, de->d_name);
            test_capture_file_replays_likeWhenRecorded(path, update);
            files++;
        }
        closedir(d);
    }
    printf("test_captures: %d recordings, %d reports checked, %d failures\n", files, checks, failures);
    return failures ? 1 : 0;
}
