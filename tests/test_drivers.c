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
    { 0x045e, 0x0b12, 0, "Xbox Controller (USB/XInput)",   CTRL_XBOX_XINPUT, CONN_USB_WIRED,      0,     PADDRV_GIP, 0 },
    { 0x045e, 0x02ea, 0, "Xbox Controller (USB/XInput)",   CTRL_XBOX_XINPUT, CONN_USB_WIRED,      0,     PADDRV_GIP, 0 },
    { 0x045e, 0x0719, 1, "Xbox 360 Wireless Receiver",     CTRL_XBOX_XINPUT, CONN_USB_DONGLE_24G, 60000, 0, 0 },
    { 0x054c, 0x09cc, 0, "Sony DualShock 4 (USB)",         CTRL_SONY_DS4,    CONN_USB_WIRED,      0,     0, 0 },
    { 0x054c, 0x0268, 0, "Sony DualShock 3 (USB)",         CTRL_SONY_DS3,    CONN_USB_WIRED,      0,     0, 0 },
    { 0x057e, 0x2009, 0, "Nintendo Switch Pro (USB)",      CTRL_NINTENDO_SWITCH_PRO, CONN_USB_WIRED, 0, 0, 0 },
    { 0x2dc8, 0x3012, 0, "8BitDo Adapter/Pad (USB)",       CTRL_8BITDO_ADAPTER, CONN_USB_DONGLE_24G, 60000, 0, 0 },
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
        { 0x0e8d, 0x3603 },  /* PS5 internal Bluetooth radio */
        { 0x0781, 0x5583 },  /* SanDisk flash drive */
        { 0x054c, 0x0b8f },  /* Sony device that is not a pad */
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

int main(void)
{
    test_driver_table();
    replay("DualShock 4", pad_driver_lookup(0x054c, 0x05c4), k_ds4, sizeof(k_ds4) / sizeof(k_ds4[0]));
    replay("8BitDo Ultimate 2C", pad_driver_for_xinput_class(0x2dc8, 0x3106), k_8bitdo, sizeof(k_8bitdo) / sizeof(k_8bitdo[0]));

    if (g_failures) {
        printf("\n%d of %d checks FAILED\n", g_failures, g_checks);
        return 1;
    }
    printf("\nAll %d driver checks passed\n", g_checks);
    return 0;
}
