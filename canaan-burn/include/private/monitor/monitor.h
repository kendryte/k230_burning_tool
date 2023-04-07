#pragma once

#include <libusb.h>

#include "private/context.h"

struct passing_data {
	KBMonCTX monitor;
	struct kburnUsbDeviceInfoSlice dev;
	libusb_hotplug_event event;
};

kburn_err_t usb_monitor_prepare(KBMonCTX monitor);
void usb_monitor_destroy(KBMonCTX monitor);
kburn_err_t usb_monitor_resume(KBMonCTX monitor);
void usb_monitor_pause(KBMonCTX monitor);

void push_libusb_event(KBMonCTX monitor, libusb_hotplug_event event, const struct kburnUsbDeviceInfoSlice *devInfo);

kburn_err_t usb_monitor_polling_prepare(KBMonCTX monitor);
void usb_monitor_polling_destroy(KBMonCTX monitor);
kburn_err_t usb_monitor_polling_resume(KBMonCTX monitor);
void usb_monitor_polling_pause(KBMonCTX monitor);

kburn_err_t usb_monitor_callback_prepare(KBMonCTX monitor);
void usb_monitor_callback_destroy(KBMonCTX monitor);
kburn_err_t usb_monitor_callback_resume(KBMonCTX monitor);
void usb_monitor_callback_pause(KBMonCTX monitor);

kburn_err_t usb_monitor_manual_trig(KBMonCTX monitor);