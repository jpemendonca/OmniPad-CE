/* Per-model driver regression tests.
 *
 * 1. Pins how every controller in src/pad_drivers.c is recognised and handled, so a new
 *    entry or an edit to one model cannot silently change another.
 * 2. Replays input reports captured from real controllers and checks the decoded state.
 *    Add captures here whenever a model is verified on hardware.
 *
 * Build: clang -Isrc src/pad_drivers.c src/usb_controllers.c src/util.c src/log.c tests/test_drivers.c */
#include <stdio.h>
#include <string.h>

#include "../src/pad_drivers.h"

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond, ...) do { \
    g_checks++; \
    if (!(cond)) { g_failures++; printf("  FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } \
} while (0)

/* ---- 1. driver table ---- */

typedef struct {
    uint16_t vid, pid;
    int via_xinput_class;  /* recognised by its interface descriptor, not by VID/PID */
    const char *name;
    usb_controller_type_t type;
    pad_conn_type_t conn;
    long idle_release_ms;
    uint32_t flags;
    int verified;
} expect_t;

static const expect_t k_expect[] = {
    /* verified on hardware */
    { 0x054c, 0x05c4, 0, "Sony DualShock 4 (USB)",         CTRL_SONY_DS4,    CONN_USB_WIRED,      0,     0, 1 },
    { 0x2dc8, 0x3106, 1, "8BitDo Ultimate 2C",             CTRL_XBOX_XINPUT, CONN_USB_DONGLE_24G, 0,     PADDRV_XINPUT_LED_OUT, 1 },
    { 0x1a34, 0xf517, 0, "Machenike 2.4G Wireless Dongle", CTRL_XBOX_XINPUT, CONN_USB_DONGLE_24G, 60000, PADDRV_XINPUT_LED_OUT, 1 },
    /* not verified yet: behaviour carried over from the original code */
    { 0x1a34, 0x1234, 0, "ShanWan Gamepad",                CTRL_XBOX_XINPUT, CONN_USB_DONGLE_24G, 60000, PADDRV_XINPUT_LED_OUT, 0 },
    { 0x2345, 0xe02f, 0, "MACHENIKE G5 Pro / ShanWan Pad", CTRL_XBOX_XINPUT, CONN_USB_DONGLE_24G, 60000, PADDRV_XINPUT_LED_OUT, 0 },
    { 0x2345, 0xe02b, 0, "MACHENIKE G5 Pro / ShanWan Pad", CTRL_XBOX_XINPUT, CONN_USB_WIRED,      0,     PADDRV_XINPUT_LED_OUT, 0 },
    { 0x2f24, 0x0001, 0, "MACHENIKE G5 Pro / ShanWan Pad", CTRL_XBOX_XINPUT, CONN_USB_WIRED,      0,     PADDRV_XINPUT_LED_OUT, 0 },
    { 0x045e, 0x028e, 0, "Xbox 360 / Machenike (XInput)",  CTRL_XBOX_XINPUT, CONN_USB_WIRED,      0,     PADDRV_XINPUT_LED_OUT | PADDRV_X360_KEEPALIVE, 0 },
    { 0x045e, 0x0b12, 0, "Xbox Controller (USB/XInput)",   CTRL_XBOX_XINPUT, CONN_USB_WIRED,      0,     PADDRV_GIP | PADDRV_SHARE_END_26, 0 },
    { 0x045e, 0x0b13, 0, "Xbox Controller (USB/XInput)",   CTRL_XBOX_XINPUT, CONN_USB_WIRED,      0,     PADDRV_GIP, 0 },
    { 0x045e, 0x02ea, 0, "Xbox Controller (USB/XInput)",   CTRL_XBOX_XINPUT, CONN_USB_WIRED,      0,     PADDRV_GIP, 0 },
    { 0x045e, 0x0719, 1, "Xbox 360 Wireless Receiver",     CTRL_XBOX_XINPUT, CONN_USB_DONGLE_24G, 60000, 0, 0 },
    { 0x054c, 0x09cc, 0, "Sony DualShock 4 (USB)",         CTRL_SONY_DS4,    CONN_USB_WIRED,      0,     0, 0 },
    { 0x054c, 0x0268, 0, "Sony DualShock 3 (USB)",         CTRL_SONY_DS3,    CONN_USB_WIRED,      0,     0, 0 },
    { 0x057e, 0x2009, 0, "Nintendo Switch Pro (USB)",      CTRL_NINTENDO_SWITCH_PRO, CONN_USB_WIRED, 0, PADDRV_SLOW_START, 0 },
    { 0x0738, 0x8838, 0, "Mad Catz FightStick TE (PS3)",   CTRL_GENERIC_HID, CONN_USB_WIRED,      0,     0, 0 },
    { 0x0738, 0x1234, 0, "Generic USB HID Gamepad",        CTRL_GENERIC_HID, CONN_USB_WIRED,      0,     0, 0 },
    { 0x2dc8, 0x3012, 0, "8BitDo Adapter/Pad (USB)",       CTRL_8BITDO_ADAPTER, CONN_USB_DONGLE_24G, 60000, 0, 0 },
    { 0x2dc8, 0x310a, 0, "8BitDo Ultimate 2C",             CTRL_XBOX_XINPUT, CONN_USB_DONGLE_24G, 0,     PADDRV_XINPUT_LED_OUT, 0 },
    { 0x2dc8, 0x310b, 0, "8BitDo Ultimate 2",              CTRL_XBOX_XINPUT, CONN_USB_DONGLE_24G, 0,     PADDRV_XINPUT_LED_OUT, 0 },
    { 0x2dc8, 0x3109, 1, "XInput Compatible Controller",   CTRL_XBOX_XINPUT, CONN_USB_DONGLE_24G, 60000, PADDRV_XINPUT_LED_OUT, 0 },
    { 0x0079, 0x0006, 0, "Generic USB HID Gamepad",        CTRL_GENERIC_HID, CONN_USB_WIRED,      0,     0, 0 },
    { 0xbeef, 0x0001, 1, "XInput Compatible Controller",   CTRL_XBOX_XINPUT, CONN_USB_WIRED,      0,     PADDRV_XINPUT_LED_OUT, 0 },
};

static void test_driver_table(void)
{
    printf("Driver table...\n");
    for (size_t i = 0; i < sizeof(k_expect) / sizeof(k_expect[0]); i++) {
        const expect_t *e = &k_expect[i];
        const pad_driver_t *d = pad_driver_lookup(e->vid, e->pid);
        if (e->via_xinput_class) {
            CHECK(d == NULL, "%04x:%04x should only be recognised by its XInput descriptor", e->vid, e->pid);
            d = pad_driver_for_xinput_class(e->vid, e->pid);
        }
        CHECK(d != NULL, "%04x:%04x has no driver", e->vid, e->pid);
        if (!d) continue;
        CHECK(strcmp(d->name, e->name) == 0, "%04x:%04x name '%s'", e->vid, e->pid, d->name);
        CHECK(d->type == e->type, "%04x:%04x type %d", e->vid, e->pid, d->type);
        CHECK(d->conn == e->conn, "%04x:%04x conn %d", e->vid, e->pid, d->conn);
        CHECK(d->idle_release_ms == e->idle_release_ms, "%04x:%04x idle %ld", e->vid, e->pid, d->idle_release_ms);
        CHECK(d->flags == e->flags, "%04x:%04x flags 0x%x", e->vid, e->pid, d->flags);
        CHECK((d->verified != NULL) == e->verified, "%04x:%04x verified mark", e->vid, e->pid);
    }

    /* Devices that must never be claimed as controllers */
    static const uint16_t k_not_pads[][2] = {
        { 0x2dc8, 0x3016 },  /* 8BitDo receiver HID mode while no pad is connected */
        { 0x2dc8, 0x301c },  /* the same, on the 2dc8:310a (issue 3) */
        { 0x0e8d, 0x3603 },  /* PS5 internal Bluetooth radio */
        { 0x0781, 0x5583 },  /* SanDisk flash drive */
        { 0x054c, 0x0b8f },  /* Sony device that is not a pad */
        { 0x1038, 0x12cd },  /* SteelSeries Arctis Nova Pro headset (issue 7) */
        { 0x1532, 0x0527 },  /* Razer headset: same risk, same vendor rule removed */
    };
    for (size_t i = 0; i < sizeof(k_not_pads) / sizeof(k_not_pads[0]); i++) {
        CHECK(pad_driver_lookup(k_not_pads[i][0], k_not_pads[i][1]) == NULL,
              "%04x:%04x must not get a driver", k_not_pads[i][0], k_not_pads[i][1]);
    }

    /* The old entry point must agree with the table */
    const char *name = NULL;
    CHECK(usb_identify_controller(0x054c, 0x05c4, &name) == CTRL_SONY_DS4 && strcmp(name, "Sony DualShock 4 (USB)") == 0,
          "usb_identify_controller disagrees with the table");
    CHECK(usb_identify_controller(0x2dc8, 0x3016, &name) == CTRL_UNKNOWN, "2dc8:3016 identified");
}

/* ---- 2. real reports ---- */

typedef struct {
    const char *what;
    const char *hex;       /* report bytes as captured */
    uint32_t buttons;
    int lx, ly, rx, ry, l2, r2;
} capture_t;

static size_t parse_hex(const char *hex, uint8_t *out, size_t cap)
{
    size_t n = 0;
    unsigned v;
    int used;
    while (n < cap && sscanf(hex, " %2x%n", &v, &used) == 1) {
        out[n++] = (uint8_t)v;
        hex += used;
    }
    return n;
}

static void replay(const char *model, const pad_driver_t *drv, const capture_t *caps, size_t count)
{
    printf("Real reports: %s...\n", model);
    for (size_t i = 0; i < count; i++) {
        const capture_t *c = &caps[i];
        uint8_t buf[64];
        size_t len = parse_hex(c->hex, buf, sizeof(buf));
        pad_state_t st;
        pad_state_neutral(&st);
        int ok = pad_driver_parse(drv, buf, len, &st);
        CHECK(ok, "%s: %s not parsed", model, c->what);
        CHECK(st.buttons == c->buttons, "%s: %s buttons 0x%08x, expected 0x%08x", model, c->what, st.buttons, c->buttons);
        CHECK(st.lx == c->lx && st.ly == c->ly && st.rx == c->rx && st.ry == c->ry,
              "%s: %s sticks %d,%d %d,%d", model, c->what, st.lx, st.ly, st.rx, st.ry);
        CHECK(st.l2 == c->l2 && st.r2 == c->r2, "%s: %s triggers %d,%d", model, c->what, st.l2, st.r2);
    }
}

/* DualShock 4 CUH-ZCT1 (054c:05c4) on a USB cable, PS5 FW 13.60.
 * First bytes of the 64-byte reports logged by the diag4 build, controller at rest. */
static const capture_t k_ds4[] = {
    { "at rest #1", "01 7e 80 82 82 08 00 54 00 00 97 c6 16 f5 ff 0a 00 fe ff 49 ff 9b 1f 94", 0, 126, 128, 130, 130, 0, 0 },
    { "at rest #2", "01 7d 80 83 82 08 00 74 00 00 ee dd 16 f5 ff 08 00 fd ff 4e ff 95 1f a5", 0, 125, 128, 131, 130, 0, 0 },
};

/* 8BitDo Ultimate 2C (2dc8:3106) through its 2.4G dongle, read with tools/probe_xinput.py.
 * 20-byte XInput reports; the trailing 11 16 30 68 00 10 is 8BitDo-specific and ignored. */
static const capture_t k_8bitdo[] = {
    { "left stick off-centre", "00 14 00 00 00 00 7e fd 3e fd 00 00 00 00 11 16 30 68 00 10", 0, 125, 130, 128, 128, 0, 0 },
    { "d-pad up",              "00 14 01 00 00 00 00 00 00 00 00 00 00 00 11 16 30 68 00 10", PAD_DPAD_UP, 128, 128, 128, 128, 0, 0 },
    { "d-pad up + R3",         "00 14 81 00 00 00 00 00 00 00 00 00 00 00 11 16 30 68 00 10", PAD_DPAD_UP | PAD_BTN_R3, 128, 128, 128, 128, 0, 0 },
    { "R3",                    "00 14 80 00 00 00 00 00 00 00 00 00 00 00 11 16 30 68 00 10", PAD_BTN_R3, 128, 128, 128, 128, 0, 0 },
    { "d-pad down + right",    "00 14 0a 00 00 00 00 00 00 00 00 00 00 00 11 16 30 68 00 10", PAD_DPAD_DOWN | PAD_DPAD_RIGHT, 128, 128, 128, 128, 0, 0 },
    { "d-pad right, L2 light", "00 14 08 00 14 00 00 00 00 00 00 00 00 00 11 16 30 68 00 10", PAD_DPAD_RIGHT, 128, 128, 128, 128, 20, 0 },
    { "L2 pressed",            "00 14 00 00 c9 00 00 00 00 00 00 00 00 00 11 16 30 68 00 10", PAD_BTN_L2, 128, 128, 128, 128, 201, 0 },
};

/* ---- 3. GIP packets ---- */

static int parse_on(uint16_t pid, const char *hex, pad_state_t *st)
{
    uint8_t buf[64];
    size_t len = parse_hex(hex, buf, sizeof(buf));
    pad_state_neutral(st);
    return pad_driver_parse(pad_driver_lookup(0x045e, pid), buf, len, st);
}

#define GIP_PRESS_A   "20 00 05 0e 10 00 00 00 00 00 00 00 00 00 00 00 00 00"
#define GIP_TAIL_0    "00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00"
#define GIP_TAIL_SHARE "01 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00"

static void test_gip_statusPacket_isNotInput(void)
{
    /* arrange: bytes rebuilt from the log of issue 4, where it read as 0x30 with LX=32 LY=79 */
    pad_state_t st;

    /* act */
    int parsed = parse_on(0x0b12, "03 20 4f 04 00 01 00 00", &st);

    /* assert */
    CHECK(!parsed, "a status packet must not reach the virtual pad");
}

static void test_gip_input_stillDecoded(void)
{
    /* arrange */
    pad_state_t st;

    /* act */
    int parsed = parse_on(0x02ea, GIP_PRESS_A, &st);

    /* assert */
    CHECK(parsed && st.buttons == PAD_BTN_CROSS, "A on a GIP report, got 0x%08x", st.buttons);
}

static void test_gip_guide_pressAndRelease(void)
{
    /* arrange */
    pad_state_t st;

    /* act */
    int pressed = parse_on(0x0b12, "07 20 02 02 01 5b", &st);
    uint32_t held = st.buttons;
    int released = parse_on(0x0b12, "07 20 03 02 00 5b", &st);

    /* assert */
    CHECK(pressed && held == PAD_BTN_PS, "Guide pressed, got 0x%08x", held);
    CHECK(released && st.buttons == 0, "Guide released with no ghost press, got 0x%08x", st.buttons);
}

static void test_gip_xinputReport_stillDecoded(void)
{
    /* arrange: a clone that uses a GIP ID and speaks XInput */
    pad_state_t st;

    /* act */
    int parsed = parse_on(0x02ea, "00 14 01 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00", &st);

    /* assert */
    CHECK(parsed && st.buttons == PAD_DPAD_UP, "XInput report, got 0x%08x", st.buttons);
}

static void test_gip_shareOnSeries_actsAsTouchpad(void)
{
    /* arrange */
    pad_state_t st;

    /* act */
    int pressed = parse_on(0x0b12, GIP_PRESS_A " " GIP_TAIL_SHARE, &st);
    uint32_t with_share = st.buttons;
    parse_on(0x0b12, GIP_PRESS_A " " GIP_TAIL_0, &st);
    uint32_t without_share = st.buttons;

    /* assert */
    CHECK(pressed && with_share == (PAD_BTN_CROSS | PAD_BTN_TOUCHPAD), "Share pressed, got 0x%08x", with_share);
    CHECK(without_share == PAD_BTN_CROSS, "Share released, got 0x%08x", without_share);
}

static void test_gip_shareByte_onlyOnTheSeriesCable(void)
{
    /* arrange */
    pad_state_t st;

    /* act */
    parse_on(0x02ea, GIP_PRESS_A " " GIP_TAIL_SHARE, &st);
    uint32_t xbox_one = st.buttons;
    parse_on(0x0b12, "20 00 05 0e 10 00 00 00 00 00 00 00 00 00 00 00 00 00 01", &st);
    uint32_t short_report = st.buttons;
    /* 30 bytes: 26 from the end is the buttons byte, not a Share byte */
    parse_on(0x0b12, "20 00 05 0e 11 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00", &st);
    uint32_t mid_report = st.buttons;

    /* assert */
    CHECK(mid_report == PAD_BTN_CROSS, "the Share byte must not land on the buttons, got 0x%08x", mid_report);
    CHECK(xbox_one == PAD_BTN_CROSS, "other GIP models keep that byte unread, got 0x%08x", xbox_one);
    CHECK(short_report == PAD_BTN_CROSS, "a short report has no Share byte, got 0x%08x", short_report);
}

/* ---- 4. Mad Catz FightStick TE (PS3) ---- */

static int parse_stick(const char *hex, pad_state_t *st)
{
    uint8_t buf[32];
    size_t len = parse_hex(hex, buf, sizeof(buf));
    pad_state_neutral(st);
    return pad_driver_parse(pad_driver_lookup(0x0738, 0x8838), buf, len, st);
}

static void test_ps3_stick_atRest_pressesNothing(void)
{
    /* arrange: bytes 1 and 2 are the ones report BVUV showed; the generic layout read them as a stick */
    pad_state_t st;

    /* act */
    int parsed = parse_stick("00 00 08 80 80 80 80 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00", &st);

    /* assert */
    CHECK(parsed && st.buttons == 0, "nothing is held at rest, got 0x%08x", st.buttons);
    CHECK(st.lx == 128 && st.ly == 128 && st.rx == 128 && st.ry == 128, "sticks centred, got %d,%d %d,%d", st.lx, st.ly, st.rx, st.ry);
    CHECK(st.l2 == 0 && st.r2 == 0, "triggers released");
}

static void test_ps3_stick_buttonsAndHat_areRead(void)
{
    /* arrange */
    pad_state_t st;

    /* act: cross, then R2 + PS, then the hat down-left */
    parse_stick("02 00 08 80 80 80 80", &st);
    uint32_t cross = st.buttons;
    parse_stick("80 10 08 80 80 80 80", &st);
    uint32_t r2_ps = st.buttons;
    int r2 = st.r2;
    parse_stick("00 00 05 80 80 80 80", &st);
    uint32_t hat = st.buttons;

    /* assert */
    CHECK(cross == PAD_BTN_CROSS, "cross, got 0x%08x", cross);
    CHECK(r2_ps == (PAD_BTN_R2 | PAD_BTN_PS) && r2 == 255, "R2 and PS, got 0x%08x", r2_ps);
    CHECK(hat == (PAD_DPAD_DOWN | PAD_DPAD_LEFT), "hat down-left, got 0x%08x", hat);
}

static void test_ps3_stick_shortReport_isNotInput(void)
{
    /* arrange */
    pad_state_t st;

    /* act */
    int parsed = parse_stick("00 00 08", &st);

    /* assert */
    CHECK(!parsed, "a short report must not fall through to the generic layout");
}

int main(void)
{
    test_driver_table();
    test_ps3_stick_atRest_pressesNothing();
    test_ps3_stick_buttonsAndHat_areRead();
    test_ps3_stick_shortReport_isNotInput();
    test_gip_statusPacket_isNotInput();
    test_gip_input_stillDecoded();
    test_gip_guide_pressAndRelease();
    test_gip_xinputReport_stillDecoded();
    test_gip_shareOnSeries_actsAsTouchpad();
    test_gip_shareByte_onlyOnTheSeriesCable();
    replay("DualShock 4", pad_driver_lookup(0x054c, 0x05c4), k_ds4, sizeof(k_ds4) / sizeof(k_ds4[0]));
    replay("8BitDo Ultimate 2C", pad_driver_for_xinput_class(0x2dc8, 0x3106), k_8bitdo, sizeof(k_8bitdo) / sizeof(k_8bitdo[0]));

    if (g_failures) {
        printf("\n%d of %d checks FAILED\n", g_failures, g_checks);
        return 1;
    }
    printf("\nAll %d driver checks passed\n", g_checks);
    return 0;
}
