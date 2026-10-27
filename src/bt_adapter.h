#ifndef BT_ADAPTER_H
#define BT_ADAPTER_H

/* OmniPad's own USB Bluetooth adapter, plugged into the PS5 next to the console's internal radio.
 * The internal radio keeps the DualSense and is never opened. A thread finds the adapter, loads the
 * Realtek firmware patch when the chip needs it and runs the Bluetooth HID host (src/bt) on it;
 * paired pads become virtual DualSense slots like the USB ones. */

int bt_adapter_init(void);
void bt_adapter_cleanup(void);

/* 1 while the adapter thread holds this /dev/ugenX.Y, so the USB pad probe leaves it alone */
int bt_adapter_owns(const char *dev_path);

/* Opens a pairing window for `seconds` (0 closes it). Returns 0 if no adapter is running. */
int bt_adapter_pair(int seconds);

/* Forgets every paired pad (they must be paired again). Returns 0 if no adapter is running. */
int bt_adapter_forget_all(void);

/* Drops every connected pad; they keep their pairing and come back when PS is pressed */
void bt_adapter_drop_pads(void);

/* Adapter and pad state for the dashboard, as a JSON object. Returns its length. */
int bt_adapter_status_json(char *out, int n);

#endif /* BT_ADAPTER_H */
