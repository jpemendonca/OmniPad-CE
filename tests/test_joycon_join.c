/* Two Joy-Cons as one controller (src/joycon_join.c). Reports go through the real Joy-Con profile, so
 * a change to how it reads a single Joy-Con shows here too. The button bits come from the recordings
 * in tests/captures, except L, ZL, ZR and +, which those runs did not press. */
#include "joycon_join.h"
#include "bt/bt_profiles.h"

#include <stdio.h>
#include <string.h>

static int failures, checks;

#define CHECK(cond, ...) do { checks++; if (!(cond)) { failures++; printf("FAIL: " __VA_ARGS__); printf("\n"); } } while (0)

#define VID 0x057E
#define PID_L 0x2006
#define PID_R 0x2007

typedef struct {
    const pad_profile *prof;
    unsigned char ctx[PROFILE_CTX_MAX];
    int right;
} side_t;

static void side_open(side_t *s, int right)
{
    uint16_t pid = right ? PID_R : PID_L;
    s->prof = profile_find(VID, pid);
    memset(s->ctx, 0, sizeof s->ctx);
    s->right = right;
    if (s->prof->setup) s->prof->setup(s->ctx, VID, pid);
}

/* Reads a report 0x30 with these button bytes and the raw 12-bit stick of this side. */
static pad_state side_read(side_t *s, unsigned b3, unsigned b4, unsigned b5, int x, int y)
{
    unsigned char r[49];
    unsigned char *stick = r + (s->right ? 9 : 6);
    pad_state st;
    memset(r, 0, sizeof r);
    pad_state_reset(&st);
    r[0] = 0x30;
    r[2] = 0x6e;    /* battery and connection, as both recordings show */
    r[3] = (unsigned char)b3;
    r[4] = (unsigned char)b4;
    r[5] = (unsigned char)b5;
    stick[0] = (unsigned char)(x & 0xFF);
    stick[1] = (unsigned char)((x >> 8 & 0x0F) | (y & 0x0F) << 4);
    stick[2] = (unsigned char)(y >> 4);
    s->prof->parse(s->ctx, r, sizeof r, &st);
    return st;
}

/* Both Joy-Cons at rest, past the stick's rest calibration */
static void pair_settle(side_t *l, side_t *r)
{
    side_open(l, 0);
    side_open(r, 1);
    for (int i = 0; i < 200; i++) {
        side_read(l, 0, 0, 0, 2050, 2258);
        side_read(r, 0, 0, 0, 2170, 1882);
    }
}

static void test_joycon_merge_eachButton_reachesItsDualSenseButton(void)
{
    /* arrange */
    static const struct { int right; unsigned b3, b4, b5; uint32_t want; const char *name; } table[] = {
        { 0, 0, 0, 0x08, PAD_LEFT, "left arrow" },
        { 0, 0, 0, 0x01, PAD_DOWN, "down arrow" },
        { 0, 0, 0, 0x02, PAD_UP, "up arrow" },
        { 0, 0, 0, 0x04, PAD_RIGHT, "right arrow" },
        { 0, 0, 0, 0x40, PAD_L1, "L" },
        { 0, 0, 0, 0x80, PAD_L2, "ZL" },
        { 0, 0, 0x01, 0, PAD_CREATE, "minus" },
        { 0, 0, 0x08, 0, PAD_L3, "left stick press" },
        { 0, 0, 0x20, 0, PAD_TOUCHPAD, "Capture" },
        { 0, 0, 0, 0x20, 0, "left SL" },
        { 0, 0, 0, 0x10, 0, "left SR" },
        { 1, 0x08, 0, 0, PAD_CIRCLE, "A" },
        { 1, 0x04, 0, 0, PAD_CROSS, "B" },
        { 1, 0x02, 0, 0, PAD_TRIANGLE, "X" },
        { 1, 0x01, 0, 0, PAD_SQUARE, "Y" },
        { 1, 0x40, 0, 0, PAD_R1, "R" },
        { 1, 0x80, 0, 0, PAD_R2, "ZR" },
        { 1, 0, 0x02, 0, PAD_OPTIONS, "plus" },
        { 1, 0, 0x04, 0, PAD_R3, "right stick press" },
        { 1, 0, 0x10, 0, PAD_PS, "Home" },
        { 1, 0x20, 0, 0, 0, "right SL" },
        { 1, 0x10, 0, 0, 0, "right SR" },
    };
    side_t l, r;
    pair_settle(&l, &r);

    for (size_t i = 0; i < sizeof table / sizeof table[0]; i++) {
        pad_state left = side_read(&l, 0, 0, 0, 2050, 2258);
        pad_state right = side_read(&r, 0, 0, 0, 2170, 1882);
        if (table[i].right) right = side_read(&r, table[i].b3, table[i].b4, table[i].b5, 2170, 1882);
        else left = side_read(&l, table[i].b3, table[i].b4, table[i].b5, 2050, 2258);
        pad_state out;

        /* act */
        joycon_merge(&out, &left, &right);

        /* assert */
        CHECK(out.buttons == table[i].want, "%s gives %#x, wanted %#x", table[i].name, (unsigned)out.buttons,
              (unsigned)table[i].want);
    }
}

static void test_joycon_merge_triggers_followZlAndZr(void)
{
    /* arrange */
    side_t l, r;
    pair_settle(&l, &r);
    pad_state left = side_read(&l, 0, 0, 0x80, 2050, 2258);
    pad_state right = side_read(&r, 0, 0, 0, 2170, 1882);
    pad_state out;

    /* act */
    joycon_merge(&out, &left, &right);

    /* assert */
    CHECK(out.l2 == 255 && out.r2 == 0, "ZL pulls L2 fully and leaves R2, got %d %d", out.l2, out.r2);
}

static void test_joycon_merge_sticks_keepTheirDirections(void)
{
    /* arrange: travel as far as the recordings reached (raw y grows upwards) */
    side_t l, r;
    pair_settle(&l, &r);
    pad_state left_rest = side_read(&l, 0, 0, 0, 2050, 2258);
    pad_state right_rest = side_read(&r, 0, 0, 0, 2170, 1882);
    pad_state left_up = side_read(&l, 0, 0, 0, 2050, 3330);
    pad_state left_right = side_read(&l, 0, 0, 0, 3412, 2258);
    pad_state right_up = side_read(&r, 0, 0, 0, 2170, 3019);
    pad_state right_left = side_read(&r, 0, 0, 0, 751, 1882);
    pad_state out;

    /* act and assert */
    joycon_merge(&out, &left_rest, &right_rest);
    CHECK(out.lx > 120 && out.lx < 136 && out.ly > 120 && out.ly < 136, "left stick rests centred, got %d %d", out.lx, out.ly);
    CHECK(out.rx > 120 && out.rx < 136 && out.ry > 120 && out.ry < 136, "right stick rests centred, got %d %d", out.rx, out.ry);

    joycon_merge(&out, &left_up, &right_rest);
    CHECK(out.ly < 60 && out.lx > 120 && out.lx < 136, "left stick up goes up, got %d %d", out.lx, out.ly);
    CHECK(out.ry > 120 && out.ry < 136, "the right stick stays where it is, got %d", out.ry);

    joycon_merge(&out, &left_right, &right_rest);
    CHECK(out.lx > 196 && out.ly > 120 && out.ly < 136, "left stick right goes right, got %d %d", out.lx, out.ly);

    joycon_merge(&out, &left_rest, &right_up);
    CHECK(out.ry < 60 && out.rx > 120 && out.rx < 136, "right stick up goes up, got %d %d", out.rx, out.ry);

    joycon_merge(&out, &left_rest, &right_left);
    CHECK(out.rx < 60 && out.ry > 120 && out.ry < 136, "right stick left goes left, got %d %d", out.rx, out.ry);
}

static void test_joycon_parse_fullReport_turnsItsStickAQuarter(void)
{
    /* arrange: recordings cannot compare these sticks, see tests/test_captures.c */
    side_t l, r;
    pair_settle(&l, &r);

    /* act */
    pad_state left_up = side_read(&l, 0, 0, 0, 2050, 3330);
    pad_state left_right = side_read(&l, 0, 0, 0, 3412, 2258);
    pad_state right_up = side_read(&r, 0, 0, 0, 2170, 3019);
    pad_state right_left = side_read(&r, 0, 0, 0, 751, 1882);

    /* assert */
    CHECK(left_up.lx < 60 && left_up.ly > 120 && left_up.ly < 136, "held sideways, left Joy-Con up is left, got %d %d", left_up.lx, left_up.ly);
    CHECK(left_right.ly < 60 && left_right.lx > 120 && left_right.lx < 136, "held sideways, left Joy-Con right is up, got %d %d", left_right.lx, left_right.ly);
    CHECK(right_up.lx > 196 && right_up.ly > 120 && right_up.ly < 136, "held sideways, right Joy-Con up is right, got %d %d", right_up.lx, right_up.ly);
    CHECK(right_left.ly < 60 && right_left.lx > 120 && right_left.lx < 136, "held sideways, right Joy-Con left is up, got %d %d", right_left.lx, right_left.ly);
}

static void test_joycon_merge_battery_showsTheLowerOne(void)
{
    /* arrange */
    pad_state left, right, out;
    pad_state_reset(&left);
    pad_state_reset(&right);
    left.has_battery = right.has_battery = 1;
    left.battery_pct = 75;
    right.battery_pct = 25;

    /* act */
    joycon_merge(&out, &left, &right);

    /* assert */
    CHECK(out.has_battery && out.battery_pct == 25, "the lower battery shows, got %d", out.battery_pct);
}

static void test_joycon_init_setup_goesOutThreeTimes(void)
{
    /* arrange */
    static const unsigned char want[] = { 0x03, 0x40, 0x48, 0x30 };
    side_t r;
    pad_output out;
    unsigned char buf[64];
    memset(&out, 0, sizeof out);
    side_open(&r, 1);

    for (int step = 0; step < 12; step++) {
        /* act */
        int n = r.prof->init_report(r.ctx, step, &out, buf, (int)sizeof buf);

        /* assert */
        CHECK(n > 10 && buf[10] == want[step % 4], "step %d sends subcommand %#x, got %#x", step, want[step % 4], buf[10]);
    }
    CHECK(r.prof->init_report(r.ctx, 12, &out, buf, (int)sizeof buf) == 0, "the sequence ends after three passes");
}

static void test_joycon_start_knownAndOtherPads_getTheirSide(void)
{
    /* arrange */
    joycon_t j;

    /* act and assert */
    joycon_start(&j, VID, PID_L);
    CHECK(j.side == JOYCON_LEFT && j.mate == -1, "057e:2006 is a left one, alone");
    joycon_start(&j, VID, PID_R);
    CHECK(j.side == JOYCON_RIGHT, "057e:2007 is a right one");
    joycon_start(&j, VID, 0x2009);
    CHECK(j.side == JOYCON_NONE, "a Switch Pro is not a Joy-Con");
    joycon_start(&j, 0x054C, PID_L);
    CHECK(j.side == JOYCON_NONE, "another vendor's pad is not a Joy-Con");
}

static void test_joycon_can_pair_leftAndRightAlone_pair(void)
{
    /* arrange */
    joycon_t l, r, other, ds4;
    joycon_start(&l, VID, PID_L);
    joycon_start(&r, VID, PID_R);
    joycon_start(&other, VID, PID_L);
    joycon_start(&ds4, 0x054C, 0x09CC);

    /* act and assert */
    CHECK(joycon_can_pair(&l, &r) && joycon_can_pair(&r, &l), "a left and a right pair, in either order");
    CHECK(!joycon_can_pair(&l, &other), "two left ones do not make a controller");
    CHECK(!joycon_can_pair(&l, &ds4), "a Joy-Con does not pair with another pad");
    r.mate = 2;
    CHECK(!joycon_can_pair(&l, &r), "one that is already joined stays with its mate");
}

static int report_of(unsigned char id, unsigned char *r)
{
    memset(r, 0, 49);
    r[0] = id;
    return 49;
}

static void test_joycon_parse_quiet_readsOnlyThatItIsHere(void)
{
    /* arrange */
    side_t r;
    unsigned char rep[49];
    pad_state st;
    pad_state_reset(&st);
    side_open(&r, 1);
    profile_joycon_quiet(1);

    /* act and assert */
    int n = report_of(0x3F, rep);
    CHECK(r.prof->parse(r.ctx, rep, n, &st) == PARSE_OK, "its default report counts as input, so the host announces it");
    n = report_of(0x21, rep);
    CHECK(r.prof->parse(r.ctx, rep, n, &st) == PARSE_OK, "so does the answer to a subcommand");
    n = report_of(0x30, rep);
    CHECK(r.prof->parse(r.ctx, rep, n, &st) == PARSE_REINIT, "a full report means it must be told to stop");
    CHECK(r.prof->parse(r.ctx, rep, n, &st) == PARSE_NONE, "once: the reports already on their way must not repeat the order");
    profile_joycon_quiet(0);
    CHECK(r.prof->parse(r.ctx, rep, n, &st) == PARSE_FULL, "once the pair is complete, the full report is read");
}

static void test_joycon_init_quiet_asksForTheDefaultReport(void)
{
    /* arrange */
    side_t r;
    pad_output out;
    unsigned char buf[64];
    memset(&out, 0, sizeof out);
    side_open(&r, 1);
    profile_joycon_quiet(1);

    /* act */
    int n = r.prof->init_report(r.ctx, 0, &out, buf, (int)sizeof buf);
    int after = r.prof->init_report(r.ctx, 1, &out, buf + 32, 32);
    profile_joycon_quiet(0);

    /* assert */
    CHECK(n > 11 && buf[10] == 0x03 && buf[11] == 0x3F, "it asks for report 0x3f, got subcommand %#x arg %#x", buf[10], buf[11]);
    CHECK(after == 0, "and sends nothing else");
}

int main(void)
{
    test_joycon_merge_eachButton_reachesItsDualSenseButton();
    test_joycon_merge_triggers_followZlAndZr();
    test_joycon_merge_sticks_keepTheirDirections();
    test_joycon_parse_fullReport_turnsItsStickAQuarter();
    test_joycon_merge_battery_showsTheLowerOne();
    test_joycon_init_setup_goesOutThreeTimes();
    test_joycon_start_knownAndOtherPads_getTheirSide();
    test_joycon_can_pair_leftAndRightAlone_pair();
    test_joycon_parse_quiet_readsOnlyThatItIsHere();
    test_joycon_init_quiet_asksForTheDefaultReport();
    printf("test_joycon_join: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
