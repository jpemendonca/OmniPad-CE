#ifndef BT_ADAPTER_H
#define BT_ADAPTER_H

/* OmniPad's own USB Bluetooth adapter, plugged into the PS5 next to the console's internal radio.
 * The internal radio keeps the DualSense and is never opened. This build brings the adapter up,
 * loads the Realtek firmware patch when needed and logs an inquiry plus one test link to the first
 * gamepad it finds; pairing and input come in a later build. */

int bt_adapter_init(void);
void bt_adapter_cleanup(void);

/* 1 while the adapter thread holds this /dev/ugenX.Y, so the USB pad probe leaves it alone */
int bt_adapter_owns(const char *dev_path);

#endif /* BT_ADAPTER_H */
