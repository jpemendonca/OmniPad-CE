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

static void test_joycon_parse_singleJoyCon_turnsItsStickAQuarter(void)
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

static void test_joycon_start_knownAndOtherPads_getTheirSide(void)
{
    /* arrange */
    joycon_t j;

    /* act and assert */
    joycon_start(&j, VID, PID_L, 5);
    CHECK(j.side == JOYCON_LEFT && j.mate == -1 && j.linked_at == 5, "057e:2006 is a left one, alone");
    joycon_start(&j, VID, PID_R, 5);
    CHECK(j.side == JOYCON_RIGHT, "057e:2007 is a right one");
    joycon_start(&j, VID, 0x2009, 5);
    CHECK(j.side == JOYCON_NONE, "a Switch Pro is not a Joy-Con");
    joycon_start(&j, 0x054C, PID_L, 5);
    CHECK(j.side == JOYCON_NONE, "another vendor's pad is not a Joy-Con");
}

/* A left and a right that connected at t=1000 and hold their L and R from t=2000 */
static void held_pair(joycon_t *l, joycon_t *r)
{
    pad_state st;
    pad_state_reset(&st);
    st.buttons = PAD_L2;
    joycon_start(l, VID, PID_L, 1000);
    joycon_start(r, VID, PID_R, 1000);
    joycon_track(l, &st, 2000);
    joycon_track(r, &st, 2000);
}

static void test_joycon_can_join_bothHeldLongEnough_joins(void)
{
    /* arrange */
    joycon_t l, r;
    held_pair(&l, &r);

    /* act and assert */
    CHECK(!joycon_can_join(&l, &r, 2999), "not before one second of holding");
    CHECK(joycon_can_join(&l, &r, 3000), "after one second of holding");
    CHECK(joycon_can_join(&r, &l, 3000), "either order");
}

static void test_joycon_can_join_oneLetGo_doesNotJoin(void)
{
    /* arrange */
    joycon_t l, r;
    pad_state up;
    held_pair(&l, &r);
    pad_state_reset(&up);

    /* act */
    joycon_track(&r, &up, 2500);

    /* assert */
    CHECK(!joycon_can_join(&l, &r, 3000), "both have to keep holding");
}

static void test_joycon_can_join_twoPlayersLongAfterConnecting_doesNotJoin(void)
{
    /* arrange: both aim in a game minutes after connecting, where L and R are each player's L2 */
    joycon_t l, r;
    pad_state st;
    pad_state_reset(&st);
    st.buttons = PAD_L2;
    joycon_start(&l, VID, PID_L, 1000);
    joycon_start(&r, VID, PID_R, 1000);
    joycon_track(&l, &st, 300000);
    joycon_track(&r, &st, 300000);

    /* act and assert */
    CHECK(!joycon_can_join(&l, &r, 305000), "two players on one Joy-Con each stay apart");
}

static void test_joycon_can_join_secondOneJustConnected_joins(void)
{
    /* arrange: the left one has been in use, the right one has just been switched on */
    joycon_t l, r;
    pad_state st;
    pad_state_reset(&st);
    st.buttons = PAD_L2;
    joycon_start(&l, VID, PID_L, 1000);
    joycon_start(&r, VID, PID_R, 300000);
    joycon_track(&l, &st, 310000);
    joycon_track(&r, &st, 310000);

    /* act and assert */
    CHECK(joycon_can_join(&l, &r, 311000), "a Joy-Con that just connected can join one already in use");
    CHECK(!joycon_first_keeps_slot(&r, &l), "the newer one gives up its slot");
    CHECK(joycon_first_keeps_slot(&l, &r), "the older one keeps its slot");
}

static void test_joycon_wait_over_timeAndButtons_endTheWait(void)
{
    /* arrange */
    joycon_t j;
    pad_state st;
    pad_state_reset(&st);
    joycon_start(&j, VID, PID_L, 1000);

    /* act and assert */
    CHECK(!joycon_wait_over(&j, 61000), "it can wait the whole time joining stays open");
    CHECK(joycon_wait_over(&j, 61001), "then it takes a slot, for a second player");

    st.buttons = PAD_L2 | PAD_R2;
    joycon_track(&j, &st, 2000);
    CHECK(!joycon_wait_over(&j, 2000), "the shoulders are how a player joins, or a finger next to them");

    st.buttons = PAD_CROSS;
    joycon_track(&j, &st, 3000);
    CHECK(joycon_wait_over(&j, 3000), "any other button means someone plays with this one alone");
}

static void test_joycon_can_join_wrongPairs_doNotJoin(void)
{
    /* arrange */
    joycon_t l, r, other, ds4;
    pad_state st;
    pad_state_reset(&st);
    st.buttons = PAD_L2;
    held_pair(&l, &r);
    joycon_start(&other, VID, PID_L, 1000);
    joycon_track(&other, &st, 2000);
    joycon_start(&ds4, 0x054C, 0x09CC, 1000);
    joycon_track(&ds4, &st, 2000);

    /* act and assert */
    CHECK(!joycon_can_join(&l, &other, 3000), "two left ones do not make a controller");
    CHECK(!joycon_can_join(&l, &ds4, 3000), "a Joy-Con does not join another pad");
    r.mate = 2;
    CHECK(!joycon_can_join(&l, &r, 3000), "one that is already joined stays with its mate");
}

int main(void)
{
    test_joycon_merge_eachButton_reachesItsDualSenseButton();
    test_joycon_merge_triggers_followZlAndZr();
    test_joycon_merge_sticks_keepTheirDirections();
    test_joycon_parse_singleJoyCon_turnsItsStickAQuarter();
    test_joycon_merge_battery_showsTheLowerOne();
    test_joycon_start_knownAndOtherPads_getTheirSide();
    test_joycon_can_join_bothHeldLongEnough_joins();
    test_joycon_can_join_oneLetGo_doesNotJoin();
    test_joycon_can_join_twoPlayersLongAfterConnecting_doesNotJoin();
    test_joycon_can_join_secondOneJustConnected_joins();
    test_joycon_wait_over_timeAndButtons_endTheWait();
    test_joycon_can_join_wrongPairs_doNotJoin();
    printf("test_joycon_join: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
