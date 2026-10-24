#include "pad_drivers.h"

#define WIRED   CONN_USB_WIRED
#define DONGLE  CONN_USB_DONGLE_24G
#define IDLE_DONGLE_MS 60000L  /* a dongle stays on USB when its pad switches off; silence is the only hint */

/* Recognised by VID/PID. Exact entries must come before the vendor-wide (pid 0) entry of
 * the same vendor: the first match wins. */
static const pad_driver_t k_drivers[] = {
    /* Nintendo */
    { 0x057e, 0x2009, "Nintendo Switch Pro (USB)",      CTRL_NINTENDO_SWITCH_PRO, WIRED, 0, 0, NULL, NULL },
    { 0x057e, 0x2017, "Nintendo NSO Controller (USB)",  CTRL_NINTENDO_SWITCH_PRO, WIRED, 0, 0, NULL, NULL },
    { 0x057e, 0x2019, "Nintendo NSO Controller (USB)",  CTRL_NINTENDO_SWITCH_PRO, WIRED, 0, 0, NULL, NULL },
    { 0x057e, 0x201a, "Nintendo NSO Controller (USB)",  CTRL_NINTENDO_SWITCH_PRO, WIRED, 0, 0, NULL, NULL },

    /* Sony */
    { 0x054c, 0x05c4, "Sony DualShock 4 (USB)",         CTRL_SONY_DS4, WIRED, 0, 0, NULL,
      "PS5 FW 13.60, USB cable (CUH-ZCT1)" },
    { 0x054c, 0x09cc, "Sony DualShock 4 (USB)",         CTRL_SONY_DS4, WIRED, 0, 0, NULL, NULL },
    { 0x054c, 0x0ba0, "Sony DualShock 4 (USB)",         CTRL_SONY_DS4, WIRED, 0, 0, NULL, NULL },
    { 0x054c, 0x0268, "Sony DualShock 3 (USB)",         CTRL_SONY_DS3, WIRED, 0, 0, NULL, NULL },

    /* Microsoft: the 360 pad speaks XInput, One/Series pads speak GIP */
    { 0x045e, 0x028e, "Xbox 360 / Machenike (XInput)",  CTRL_XBOX_XINPUT, WIRED, 0,
      PADDRV_XINPUT_LED_OUT | PADDRV_X360_KEEPALIVE, NULL, NULL },
    { 0x045e, 0x02d1, "Xbox Controller (USB/XInput)",   CTRL_XBOX_XINPUT, WIRED, 0, PADDRV_GIP, NULL, NULL },
    { 0x045e, 0x02dd, "Xbox Controller (USB/XInput)",   CTRL_XBOX_XINPUT, WIRED, 0, PADDRV_GIP, NULL, NULL },
    { 0x045e, 0x02e3, "Xbox Controller (USB/XInput)",   CTRL_XBOX_XINPUT, WIRED, 0, PADDRV_GIP, NULL, NULL },
    { 0x045e, 0x02ea, "Xbox Controller (USB/XInput)",   CTRL_XBOX_XINPUT, WIRED, 0, PADDRV_GIP, NULL, NULL },
    { 0x045e, 0x0b00, "Xbox Controller (USB/XInput)",   CTRL_XBOX_XINPUT, WIRED, 0, PADDRV_GIP, NULL, NULL },
    { 0x045e, 0x0b05, "Xbox Controller (USB/XInput)",   CTRL_XBOX_XINPUT, WIRED, 0, PADDRV_GIP, NULL, NULL },
    { 0x045e, 0x0b12, "Xbox Controller (USB/XInput)",   CTRL_XBOX_XINPUT, WIRED, 0, PADDRV_GIP, NULL, NULL },
    { 0x045e, 0x0b13, "Xbox Controller (USB/XInput)",   CTRL_XBOX_XINPUT, WIRED, 0, PADDRV_GIP, NULL, NULL },

    /* Machenike, ShanWan, Thunderobot, GameSir (cable, dongle and PC modes) */
    { 0x2345, 0xe02f, "MACHENIKE G5 Pro / ShanWan Pad", CTRL_XBOX_XINPUT, DONGLE, IDLE_DONGLE_MS, PADDRV_XINPUT_LED_OUT, NULL, NULL },
    { 0x2345, 0,      "MACHENIKE G5 Pro / ShanWan Pad", CTRL_XBOX_XINPUT, WIRED,  0, PADDRV_XINPUT_LED_OUT, NULL, NULL },
    { 0x2563, 0,      "MACHENIKE G5 Pro / ShanWan Pad", CTRL_XBOX_XINPUT, WIRED,  0, PADDRV_XINPUT_LED_OUT, NULL, NULL },
    { 0x2f24, 0,      "MACHENIKE G5 Pro / ShanWan Pad", CTRL_XBOX_XINPUT, WIRED,  0, PADDRV_XINPUT_LED_OUT, NULL, NULL },
    { 0x20bc, 0,      "MACHENIKE G5 Pro / ShanWan Pad", CTRL_XBOX_XINPUT, WIRED,  0, PADDRV_XINPUT_LED_OUT, NULL, NULL },
    { 0x3537, 0,      "MACHENIKE G5 Pro / ShanWan Pad", CTRL_XBOX_XINPUT, WIRED,  0, PADDRV_XINPUT_LED_OUT, NULL, NULL },
    { 0x0e8f, 0,      "MACHENIKE G5 Pro / ShanWan Pad", CTRL_XBOX_XINPUT, WIRED,  0, PADDRV_XINPUT_LED_OUT, NULL, NULL },
    { 0x1a34, 0xf517, "Machenike 2.4G Wireless Dongle", CTRL_XBOX_XINPUT, DONGLE, IDLE_DONGLE_MS, PADDRV_XINPUT_LED_OUT, NULL,
      "PS5 FW 13.60, 2.4G dongle (original author)" },
    { 0x1a34, 0,      "ShanWan Gamepad",                CTRL_XBOX_XINPUT, DONGLE, IDLE_DONGLE_MS, PADDRV_XINPUT_LED_OUT, NULL, NULL },

    /* 8BitDo receivers and pads in their HID modes */
    { 0x2dc8, 0x310a, "8BitDo Adapter/Pad (USB)",       CTRL_8BITDO_ADAPTER, DONGLE, IDLE_DONGLE_MS, 0, NULL, NULL },
    { 0x2dc8, 0x310b, "8BitDo Adapter/Pad (USB)",       CTRL_8BITDO_ADAPTER, DONGLE, IDLE_DONGLE_MS, 0, NULL, NULL },
    { 0x2dc8, 0x2100, "8BitDo Adapter/Pad (USB)",       CTRL_8BITDO_ADAPTER, DONGLE, IDLE_DONGLE_MS, 0, NULL, NULL },
    { 0x2dc8, 0x3012, "8BitDo Adapter/Pad (USB)",       CTRL_8BITDO_ADAPTER, DONGLE, IDLE_DONGLE_MS, 0, NULL, NULL },

    /* Generic PC HID pads (DragonRise, Betop, Retro-Bit, Mayflash, PDP, Razer...) */
    { 0x0079, 0, "Generic USB HID Gamepad", CTRL_GENERIC_HID, WIRED, 0, 0, NULL, NULL },
    { 0x0810, 0, "Generic USB HID Gamepad", CTRL_GENERIC_HID, WIRED, 0, 0, NULL, NULL },
    { 0x11ff, 0, "Generic USB HID Gamepad", CTRL_GENERIC_HID, WIRED, 0, 0, NULL, NULL },
    { 0x12bd, 0, "Generic USB HID Gamepad", CTRL_GENERIC_HID, WIRED, 0, 0, NULL, NULL },
    { 0x20d6, 0, "Generic USB HID Gamepad", CTRL_GENERIC_HID, WIRED, 0, 0, NULL, NULL },
    { 0x24c6, 0, "Generic USB HID Gamepad", CTRL_GENERIC_HID, WIRED, 0, 0, NULL, NULL },
    { 0x146b, 0, "Generic USB HID Gamepad", CTRL_GENERIC_HID, WIRED, 0, 0, NULL, NULL },
    { 0x0738, 0, "Generic USB HID Gamepad", CTRL_GENERIC_HID, WIRED, 0, 0, NULL, NULL },
    { 0x1038, 0, "Generic USB HID Gamepad", CTRL_GENERIC_HID, WIRED, 0, 0, NULL, NULL },
    { 0x1532, 0, "Generic USB HID Gamepad", CTRL_GENERIC_HID, WIRED, 0, 0, NULL, NULL },
};

/* Not in the table above, but the USB interface descriptor says XInput (class 0xFF/0x5D).
 * Same first-match rule; the last entry catches every other XInput clone. */
static const pad_driver_t k_xinput_class_drivers[] = {
    /* Reports only on change, and switching it off drops the receiver off USB (it comes
     * back as 2dc8:3016, a HID mode that is not claimed), which frees the slot by itself,
     * so it has no idle rule. Also used on the USB-C cable, where it shows the same ID. */
    { 0x2dc8, 0x3106, "8BitDo Ultimate 2C",             CTRL_XBOX_XINPUT, DONGLE, 0, PADDRV_XINPUT_LED_OUT, NULL,
      "PS5 FW 13.60, 2.4G dongle and USB-C cable" },
    { 0x2dc8, 0,      "XInput Compatible Controller",   CTRL_XBOX_XINPUT, DONGLE, IDLE_DONGLE_MS, PADDRV_XINPUT_LED_OUT, NULL, NULL },
    /* Xbox 360 wireless receiver: XInput, not GIP; its LED command differs from the wired pad's */
    { 0x045e, 0x0719, "Xbox 360 Wireless Receiver",     CTRL_XBOX_XINPUT, DONGLE, IDLE_DONGLE_MS, 0, NULL, NULL },
    { 0,      0,      "XInput Compatible Controller",   CTRL_XBOX_XINPUT, WIRED,  0, PADDRV_XINPUT_LED_OUT, NULL, NULL },
};

static const pad_driver_t *find(const pad_driver_t *table, size_t count, uint16_t vid, uint16_t pid)
{
    for (size_t i = 0; i < count; i++) {
        const pad_driver_t *d = &table[i];
        if ((d->vid == 0 || d->vid == vid) && (d->pid == 0 || d->pid == pid)) return d;
    }
    return NULL;
}

const pad_driver_t *pad_driver_lookup(uint16_t vid, uint16_t pid)
{
    return find(k_drivers, sizeof(k_drivers) / sizeof(k_drivers[0]), vid, pid);
}

const pad_driver_t *pad_driver_for_xinput_class(uint16_t vid, uint16_t pid)
{
    return find(k_xinput_class_drivers, sizeof(k_xinput_class_drivers) / sizeof(k_xinput_class_drivers[0]), vid, pid);
}

int pad_driver_parse(const pad_driver_t *drv, const uint8_t *data, size_t len, pad_state_t *st)
{
    int parsed = drv->parse ? drv->parse(data, len, st) : usb_parse_input_report(drv->type, data, len, st);
    if (!parsed) parsed = usb_parse_input_report(CTRL_GENERIC_HID, data, len, st);
    return parsed;
}
