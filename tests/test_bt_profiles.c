/* Bluetooth report parsers (src/bt/bt_profiles.c), for the pads validated on the PS5.
 * Stick values of the Xbox frames are the notifications the real pad sent on 2026-11-02. Its button
 * bytes and the DS4 frames follow the layout in bt_profiles.c: the log did not record them raw. */
#include "bt_profiles.h"

#include <stdio.h>
#include <string.h>

static int failures, checks;

#define CHECK(cond, ...) do { checks++; if (!(cond)) { failures++; printf("FAIL: " __VA_ARGS__); printf("\n"); } } while (0)

static unsigned char g_ctx[PROFILE_CTX_MAX];

static const pad_profile *open_profile(uint16_t vid, uint16_t pid)
{
    const pad_profile *p = profile_find(vid, pid);
    memset(g_ctx, 0, sizeof g_ctx);
    if (p && p->setup) p->setup(g_ctx, vid, pid);
    return p;
}

/* The real Xbox Series S notification, with the report id the host puts in front of it */
static int xbox_frame(unsigned char *rep, const unsigned char sticks[8])
{
    memset(rep, 0, 17);
    rep[0] = 0x01;
    memcpy(rep + 1, sticks, 8);
    return 17;
}

static void test_profile_find_validatedPads_keepTheirProfile(void)
{
    /* arrange */
    static const struct { uint16_t vid, pid; const char *name; } known[] = {
        { 0x054C, 0x05C4, "DualShock 4" },
        { 0x054C, 0x09CC, "DualShock 4" },
        { 0x054C, 0x0CE6, "DualSense" },
        { 0x045E, 0x0B13, "Xbox" },
        { 0x045E, 0x0B20, "Xbox" },
        { 0x057E, 0x2009, "Switch Pro" },
        { 0x057E, 0x2006, "Joy-Con" },
        { 0x057E, 0x2007, "Joy-Con" },
        { 0x2DC8, 0x6012, "8BitDo Ultimate 2" },
    };

    for (size_t i = 0; i < sizeof known / sizeof known[0]; i++) {
        /* act */
        const pad_profile *p = profile_find(known[i].vid, known[i].pid);

        /* assert */
        CHECK(p && strcmp(p->name, known[i].name) == 0, "%04x:%04x stays on profile %s",
              known[i].vid, known[i].pid, known[i].name);
    }
    CHECK(profile_find(0x1234, 0x5678) == NULL, "an unknown pad has no profile: it goes to the generic one");
}

static void test_xbox_parse_realFrames_readSticks(void)
{
    /* arrange */
    static const struct { unsigned char raw[8]; uint8_t lx, ly, rx, ry; } real[] = {
        { { 0x00, 0x80, 0xff, 0x7f, 0x00, 0x80, 0xff, 0x7f }, 0x80, 0x7f, 0x80, 0x7f },
        { { 0x3e, 0x85, 0x43, 0x71, 0x89, 0x7b, 0x03, 0x84 }, 0x85, 0x71, 0x7b, 0x84 },
        { { 0xab, 0x7a, 0x5a, 0x84, 0xa6, 0x8a, 0x3b, 0x7b }, 0x7a, 0x84, 0x8a, 0x7b },
        { { 0xbd, 0x84, 0xa0, 0x86, 0x26, 0x84, 0xe2, 0x85 }, 0x84, 0x86, 0x84, 0x85 },
        { { 0xf6, 0x84, 0x54, 0x7b, 0xa1, 0x7b, 0xd4, 0x83 }, 0x84, 0x7b, 0x7b, 0x83 },
    };
    const pad_profile *p = open_profile(0x045E, 0x0B13);
    unsigned char rep[17];

    for (size_t i = 0; i < sizeof real / sizeof real[0]; i++) {
        pad_state st;
        pad_state_reset(&st);
        int n = xbox_frame(rep, real[i].raw);

        /* act */
        int r = p->parse(g_ctx, rep, n, &st);

        /* assert */
        CHECK(r == PARSE_OK, "frame %zu is read", i);
        CHECK(st.lx == real[i].lx && st.ly == real[i].ly && st.rx == real[i].rx && st.ry == real[i].ry,
              "frame %zu sticks %02x %02x %02x %02x, got %02x %02x %02x %02x", i,
              real[i].lx, real[i].ly, real[i].rx, real[i].ry, st.lx, st.ly, st.rx, st.ry);
        CHECK(st.buttons == 0, "frame %zu has no button pressed, got %#x", i, st.buttons);
    }
}

static void test_xbox_parse_seriesButtons_mapToDualSense(void)
{
    /* arrange */
    static const struct { int at; unsigned char bit; uint32_t want; const char *name; } map[] = {
        { 14, 0x01, PAD_CROSS, "A" },       { 14, 0x02, PAD_CIRCLE, "B" },
        { 14, 0x08, PAD_SQUARE, "X" },      { 14, 0x10, PAD_TRIANGLE, "Y" },
        { 14, 0x40, PAD_L1, "LB" },         { 14, 0x80, PAD_R1, "RB" },
        { 15, 0x04, PAD_TOUCHPAD, "View" }, { 15, 0x08, PAD_OPTIONS, "Menu" },
        { 15, 0x10, PAD_PS, "Xbox" },       { 15, 0x20, PAD_L3, "L3" },
        { 15, 0x40, PAD_R3, "R3" },         { 16, 0x01, PAD_CREATE, "Share" },
        { 13, 1, PAD_UP, "up" },            { 13, 3, PAD_RIGHT, "right" },
        { 13, 5, PAD_DOWN, "down" },        { 13, 7, PAD_LEFT, "left" },
    };
    const pad_profile *p = open_profile(0x045E, 0x0B13);

    for (size_t i = 0; i < sizeof map / sizeof map[0]; i++) {
        unsigned char rep[17] = { 0x01 };
        pad_state st;
        pad_state_reset(&st);
        rep[map[i].at] = map[i].bit;

        /* act */
        p->parse(g_ctx, rep, sizeof rep, &st);

        /* assert */
        CHECK(st.buttons == map[i].want, "%s gives only its own button, got %#x", map[i].name, st.buttons);
    }
}

static void test_xbox_parse_triggers_scaleAndPress(void)
{
    /* arrange */
    const pad_profile *p = open_profile(0x045E, 0x0B13);
    unsigned char rep[17] = { 0x01 };
    pad_state st;
    pad_state_reset(&st);
    rep[9] = 0xFF; rep[10] = 0x03;     /* LT fully in: 10 bits */
    rep[11] = 0x00; rep[12] = 0x02;    /* RT half way */

    /* act */
    p->parse(g_ctx, rep, sizeof rep, &st);

    /* assert */
    CHECK(st.l2 == 255, "LT at the end of its 10 bits reads 255, got %u", st.l2);
    CHECK(st.r2 == 128, "RT half way reads 128, got %u", st.r2);
    CHECK((st.buttons & (PAD_L2 | PAD_R2)) == (PAD_L2 | PAD_R2), "pressed triggers also press L2 and R2");
}

static void test_xbox_parse_oneSShareByte_isTouchpad(void)
{
    /* arrange: the Xbox One S has no Share button, so the same bit cannot mean Create */
    const pad_profile *p = open_profile(0x045E, 0x0B20);
    unsigned char rep[17] = { 0x01 };
    pad_state st;
    pad_state_reset(&st);
    rep[16] = 0x01;

    /* act */
    p->parse(g_ctx, rep, sizeof rep, &st);

    /* assert */
    CHECK(st.buttons == PAD_TOUCHPAD, "on a pad without Share it is the touchpad, got %#x", st.buttons);
}

static void test_xbox_parse_batteryReport_setsLevel(void)
{
    /* arrange */
    const pad_profile *p = open_profile(0x045E, 0x0B13);
    const unsigned char rep[2] = { 0x04, 0x12 };
    pad_state st;
    pad_state_reset(&st);

    /* act */
    int r = p->parse(g_ctx, rep, sizeof rep, &st);

    /* assert */
    CHECK(r == PARSE_OK && st.has_battery, "the dashboard gets a battery");
    CHECK(st.battery_pct == 66 && st.charging == 1, "level 2 of 3 charging, got %u%% %u", st.battery_pct, st.charging);
}

static void test_ds4_parse_shortReport_readsBasics(void)
{
    /* arrange: the 10-byte report the pad starts with */
    const pad_profile *p = open_profile(0x054C, 0x09CC);
    const unsigned char rep[10] = { 0x01, 10, 20, 30, 40, 0x24, 0x01, 0x01, 200, 0 };
    pad_state st;
    pad_state_reset(&st);

    /* act */
    int r = p->parse(g_ctx, rep, sizeof rep, &st);

    /* assert */
    CHECK(r == PARSE_OK, "the short report is not the full one yet");
    CHECK(st.lx == 10 && st.ly == 20 && st.rx == 30 && st.ry == 40, "sticks in order");
    CHECK(st.l2 == 200 && st.r2 == 0, "triggers");
    CHECK(st.buttons & PAD_CROSS, "cross");
    CHECK(st.buttons & PAD_L1, "L1");
    CHECK(st.buttons & PAD_PS, "PS");
    CHECK(st.buttons & PAD_DOWN, "hat 4 is down");
}

static void test_ds4_parse_fullReport_readsEverything(void)
{
    /* arrange: 78 bytes, the size seen on the PS5 */
    const pad_profile *p = open_profile(0x054C, 0x09CC);
    unsigned char rep[78] = { 0x11, 0xC0, 0x00 };
    pad_state st;
    pad_state_reset(&st);
    rep[3] = 11; rep[4] = 22; rep[5] = 33; rep[6] = 44;
    rep[7] = 0x08 | 0x20;              /* hat released, cross */
    rep[8] = 0x20;                     /* options */
    rep[9] = 0x02;                     /* touchpad click */
    rep[10] = 77; rep[11] = 88;
    rep[15] = 0x34; rep[16] = 0x12;    /* gyro x */
    rep[21] = 0xCE; rep[22] = 0xFF;    /* accel x: -50 */
    rep[32] = 0x04;                    /* level 4 of 8, no cable */
    rep[35] = 1;                       /* one touch report */
    rep[37] = 0x05; rep[38] = 0x23; rep[39] = 0x41; rep[40] = 0x56;
    rep[41] = 0x80;

    /* act */
    int r = p->parse(g_ctx, rep, sizeof rep, &st);

    /* assert */
    CHECK(r == PARSE_FULL, "report 0x11 ends the wake-up sequence, got %d", r);
    CHECK(st.lx == 11 && st.ly == 22 && st.rx == 33 && st.ry == 44, "sticks start at byte 3");
    CHECK(st.l2 == 77 && st.r2 == 88, "triggers at bytes 10 and 11");
    CHECK(st.buttons & PAD_CROSS, "cross");
    CHECK(st.buttons & PAD_OPTIONS, "options");
    CHECK(st.buttons & PAD_TOUCHPAD, "touchpad click");
    CHECK(!(st.buttons & (PAD_UP | PAD_DOWN | PAD_LEFT | PAD_RIGHT)), "hat 8 is released");
    CHECK(st.has_motion && st.gyro[0] == 0x1234 && st.accel[0] == -50, "motion reaches the virtual pad");
    CHECK(st.has_battery && st.battery_pct == 50 && !st.charging, "battery 50%%, got %u", st.battery_pct);
    CHECK(st.has_touch && st.touch[0].down && st.touch[0].x == 0x123 && st.touch[0].y == 0x564,
          "first finger, got %u %u", st.touch[0].x, st.touch[0].y);
    CHECK(!st.touch[1].down, "second finger up");
}

static void test_parse_shortFrames_areRefused(void)
{
    /* arrange */
    const pad_profile *ds4 = open_profile(0x054C, 0x09CC);
    unsigned char rep[78] = { 0x11 };
    pad_state st;
    pad_state_reset(&st);

    /* act */
    int full = ds4->parse(g_ctx, rep, 35, &st);
    rep[0] = 0x01;
    int basic = ds4->parse(g_ctx, rep, 9, &st);

    /* assert */
    CHECK(full == PARSE_NONE, "a cut 0x11 report is not read past its end");
    CHECK(basic == PARSE_NONE, "a cut 0x01 report is not read past its end");
}

int main(void)
{
    test_profile_find_validatedPads_keepTheirProfile();
    test_xbox_parse_realFrames_readSticks();
    test_xbox_parse_seriesButtons_mapToDualSense();
    test_xbox_parse_triggers_scaleAndPress();
    test_xbox_parse_oneSShareByte_isTouchpad();
    test_xbox_parse_batteryReport_setsLevel();
    test_ds4_parse_shortReport_readsBasics();
    test_ds4_parse_fullReport_readsEverything();
    test_parse_shortFrames_areRefused();
    printf("test_bt_profiles: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
