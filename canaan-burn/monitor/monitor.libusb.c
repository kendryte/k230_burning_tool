#include <stdlib.h>
#include <libusb.h>

#include "private/lib/debug/color.h"
#include "private/lib/debug/print.h"

#include "private/monitor/monitor.h"

#include "private/monitor/descriptor.h"
#include "private/monitor/usb_types.h"

static int on_libusb_hotplug_event(struct libusb_context *UNUSED(ctx), struct libusb_device *dev, libusb_hotplug_event event, void *user_data) {
	KBMonCTX monitor = user_data;
	kburnUsbDeviceInfoSlice devInfo;

	if (LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED == event) {
		debug_print(KBURN_LOG_DEBUG, "libusb event: " COLOR_FMT("ARRIVED"), COLOR_ARG(GREEN));

		int ret = usb_get_vid_pid_path(dev, &devInfo.idVendor, &devInfo.idProduct, devInfo.path);
		if (ret < LIBUSB_SUCCESS) {
			return 0;
		}

		push_libusb_event(monitor, event, &devInfo);
	} else if (LIBUSB_HOTPLUG_EVENT_DEVICE_LEFT == event) {
		debug_print(KBURN_LOG_DEBUG, "libusb event: " COLOR_FMT("LEFT"), COLOR_ARG(YELLOW));

		int ret = usb_get_vid_pid_path(dev, &devInfo.idVendor, &devInfo.idProduct, devInfo.path);
		if (ret < LIBUSB_SUCCESS) {
			return 0;
		}

		push_libusb_event(monitor, event, &devInfo);
	} else {
		debug_print(KBURN_LOG_WARN, "Unhandled event %d\n", event);
	}

	event_thread_fire(monitor->usb->event_queue);

	return 0;
}

kburn_err_t usb_monitor_callback_prepare(KBMonCTX UNUSED(monitor)) {
	return KBurnNoErr;
}

void usb_monitor_callback_destroy(KBMonCTX UNUSED(monitor)) {
}

void usb_monitor_callback_pause(KBMonCTX monitor) {
	libusb_hotplug_deregister_callback(monitor->usb->libusb, monitor->usb->event_handle);
}

kburn_err_t usb_monitor_callback_resume(KBMonCTX monitor) {
	debug_print(KBURN_LOG_INFO, "\tlibusb_hotplug_register_callback: [%04x:%04x]", subsystem_settings.vid, subsystem_settings.pid);

	IfUsbErrorLogReturn(libusb_hotplug_register_callback(
		monitor->usb->libusb, LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED | LIBUSB_HOTPLUG_EVENT_DEVICE_LEFT, 0,
		subsystem_settings.vid == KBURN_VIDPID_FILTER_ANY ? LIBUSB_HOTPLUG_MATCH_ANY : subsystem_settings.vid,
		subsystem_settings.pid == KBURN_VIDPID_FILTER_ANY ? LIBUSB_HOTPLUG_MATCH_ANY : subsystem_settings.pid, LIBUSB_HOTPLUG_MATCH_ANY,
		on_libusb_hotplug_event, monitor, &monitor->usb->event_handle));

	return KBurnNoErr;
}
