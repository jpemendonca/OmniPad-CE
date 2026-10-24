#ifndef PAD_DRIVERS_H
#define PAD_DRIVERS_H

/* One entry per supported USB controller model.
 *
 * Everything that differs between models lives in its entry: how it is recognised, the
 * protocol it speaks, whether it is a dongle, how long it may stay silent and which
 * extra packets it needs. Supporting a new controller means adding an entry, never
 * editing a shared code path, so models that already work are not touched. A model
 * that needs its own report decoding gets its own `parse` instead of a change to the
 * shared protocol parser. tests/test_drivers.c pins every verified entry and replays
 * real reports captured from them. */

#include "pad_types.h"
#include "usb_controllers.h"

#include <stdint.h>
#include <stddef.h>

/* Extra behaviour a driver opts into */
#define PADDRV_GIP             (1u << 0)  /* Xbox One/Series: GIP power-on + LED, EP 0x82/0x02, ACK packets */
#define PADDRV_X360_KEEPALIVE  (1u << 1)  /* re-send the player LED every 1.5s (genuine Xbox 360 pad) */
#define PADDRV_XINPUT_LED_OUT  (1u << 2)  /* set the player LED on the interrupt OUT endpoint once connected */

typedef int (*pad_parse_fn)(const uint8_t *data, size_t len, pad_state_t *st);

typedef struct {
    uint16_t              vid;
    uint16_t              pid;              /* 0 = any product from this vendor */
    const char           *name;             /* shown in the log and the dashboard */
    usb_controller_type_t type;             /* protocol: wake-up handshake and default parser */
    pad_conn_type_t       conn;             /* CONN_USB_WIRED or CONN_USB_DONGLE_24G */
    long                  idle_release_ms;  /* free the slot after this long without input; 0 = never */
    uint32_t              flags;            /* PADDRV_* */
    pad_parse_fn          parse;            /* NULL = the shared parser for `type` */
    const char           *verified;         /* where it was tested on real hardware, or NULL */
} pad_driver_t;

/* Driver for a device recognised by VID/PID, or NULL */
const pad_driver_t *pad_driver_lookup(uint16_t vid, uint16_t pid);

/* Driver for a device that was not in the VID/PID table but whose USB interface says it is
 * an XInput pad (class 0xFF, subclass 0x5D). Never NULL. */
const pad_driver_t *pad_driver_for_xinput_class(uint16_t vid, uint16_t pid);

/* Decode one input report with the driver's parser, falling back to the generic HID layout */
int pad_driver_parse(const pad_driver_t *drv, const uint8_t *data, size_t len, pad_state_t *st);

#endif /* PAD_DRIVERS_H */
