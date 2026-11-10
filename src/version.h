#ifndef VERSION_H
#define VERSION_H

#define ANYPAD_NAME             "OmniPad CE"
/* Test builds pass their own label and number (make -f ps5.mk VERSION=... BUILD_NUM=...) */
#ifndef ANYPAD_VERSION
#define ANYPAD_VERSION          "1.7.0"
#endif
#ifndef ANYPAD_BUILD
#define ANYPAD_BUILD            77
#endif
#define ANYPAD_AUTHOR           "Diego Lima & PS5 Scene Community"
#define ANYPAD_DESCRIPTION      "USB, 2.4G dongle and Bluetooth (own USB adapter) gamepads for PS5 (FW 7.00 - 13.60)"

/* Supported firmware bounds: Relapse exploit enables 7.00 through 13.60 */
#define ANYPAD_MIN_FW           0x07000000  /* 7.00 */
#define ANYPAD_MAX_FW           0x1360FFFF  /* 13.60.xx */

/* Not 8095: the Dump Forge payload uses it, and the PS5 browser opened Dump Forge instead (8 Nov) */
#define ANYPAD_DEFAULT_PORT     8105
#define ANYPAD_TCP_STREAM_PORT  9045

#endif /* VERSION_H */
