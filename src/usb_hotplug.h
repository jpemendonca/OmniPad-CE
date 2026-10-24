#ifndef USB_HOTPLUG_H
#define USB_HOTPLUG_H

int usb_hotplug_init(void);
void usb_hotplug_poll(long now);
void usb_hotplug_cleanup(void);

/* How long the slot's USB pad may stay silent before its virtual pad is released; 0 = never */
long usb_hotplug_idle_release_ms(int slot);

#endif /* USB_HOTPLUG_H */
