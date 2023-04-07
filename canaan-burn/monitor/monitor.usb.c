#include <libusb.h>
#include <pthread.h>

#include "private/base.h"

#include "private/lib/device.h"
#include "private/lib/components/call-user-handler.h"
#include "private/lib/components/device-link-list.h"
#include "private/lib/components/queued-thread.h"

#include "private/monitor/monitor.h"
#include "private/monitor/subsystem.h"

#include "private/monitor/descriptor.h"
#include "private/monitor/libusb.list.h"
#include "private/monitor/lifecycle.h"
#include "private/monitor/usb_types.h"

static void init_list_all_usb_devices_threaded(void *UNUSED(_ctx), KBMonCTX monitor, const bool *const q) {
	if (!*q) {
		init_list_all_usb_devices(monitor);
	}
}

void _auto_libusb_free_device_list(libusb_device ***pval) {
	if (*pval == NULL)
		return;
	libusb_free_device_list(*pval, true);
}

static bool open_this_usb_device(KBMonCTX monitor, uint16_t vid, uint16_t pid, const uint8_t in_path[MAX_USB_PATH_LENGTH]) {
	debug_trace_function("%d, %d, %.8s", vid, pid, usb_debug_path_string(in_path));
	libusb_device **__attribute__((cleanup(_auto_libusb_free_device_list))) list = NULL;
	ssize_t dev_count = libusb_get_device_list(monitor->usb->libusb, &list);
	if (!check_libusb(dev_count)) {
		debug_print_libusb_error("libusb_get_device_list()", dev_count);
		return NULL;
	}
	for (int i = 0; i < dev_count; i++) {
		libusb_device *dev = list[i];
		struct libusb_device_descriptor desc;

		int r = libusb_get_device_descriptor(dev, &desc);
		if (!check_libusb(r)) {
			continue;
		}

		if (vid != desc.idVendor || pid != desc.idProduct) {
			continue;
		}

		uint8_t path[MAX_USB_PATH_LENGTH] = {0};
		if (usb_get_device_path(dev, path) < LIBUSB_SUCCESS) {
			continue;
		}

		if (strncmp((const char *)path, (const char *)in_path, MAX_USB_PATH_LENGTH) == 0) {
			kburn_err_t r = open_single_usb_port(monitor, dev, true, NULL);
			if (r != KBurnNoErr) {
				debug_print(KBURN_LOG_ERROR, "failed open single port: %" PRIu64, r);
				return false;
			}
			return true;
		}
	}

	debug_print(KBURN_LOG_ERROR, "failed get device!");

	return false;
}

static void _pump_libusb_event(struct passing_data *recv) {
	debug_print(KBURN_LOG_DEBUG, "handle event in thread.");

	KBMonCTX monitor = ((struct passing_data *)recv)->monitor;
	if(monitor->on_list_change.handler) {
		CALL_HANDLE_SYNC(monitor->on_list_change, NULL);
	}

	libusb_hotplug_event event = ((struct passing_data *)recv)->event;
	kburnUsbDeviceInfoSlice defInfo = ((struct passing_data *)recv)->dev;

	if (LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED == event) {
		debug_print(KBURN_LOG_INFO, "libusb event: ARRIVED");

		if (get_device_by_usb_port_path(monitor, defInfo.idVendor, defInfo.idProduct, defInfo.path) != NULL) {
			debug_print(KBURN_LOG_DEBUG, "port already open. (this may be issue)");
			return;
		}

		open_this_usb_device(monitor, defInfo.idVendor, defInfo.idProduct, defInfo.path);

	} else if (LIBUSB_HOTPLUG_EVENT_DEVICE_LEFT == event) {
		debug_print(KBURN_LOG_INFO, "libusb event: LEFT");
		kburnDeviceNode *node = get_device_by_usb_port_path(monitor, defInfo.idVendor, defInfo.idProduct, defInfo.path);
		if (node != NULL) {
			destroy_usb_port(node->disposable_list, node->usb);
		}
	} else {
		debug_print(KBURN_LOG_ERROR, "Unhandled event %d\n", event);
	}
}

void pump_libusb_event(KBMonCTX UNUSED(monitor), void *recv) {
	_pump_libusb_event(recv);
	free(recv);
}

void push_libusb_event(KBMonCTX monitor, libusb_hotplug_event event, const struct kburnUsbDeviceInfoSlice *devInfo) {
	struct passing_data *data = calloc(1, sizeof(struct passing_data));
	if (data == NULL) {
		debug_print(KBURN_LOG_ERROR, "memory error in libusb event thread");
	}
	data->monitor = monitor;
	data->dev = *devInfo;
	data->event = event;
	event_thread_queue(monitor->usb->event_queue, data, false);
}

void usb_monitor_destroy(KBMonCTX monitor) {
	if (!monitor->usb->monitor_prepared) {
		return;
	}
	monitor->usb->monitor_prepared = false;

	if (monitor->usb->event_queue) {
		event_thread_deinit(monitor, &monitor->usb->event_queue);
		monitor->usb->event_queue = NULL;
	}

	if (monitor->usb->event_mode == USB_EVENT_CALLBACK) {
		usb_monitor_callback_destroy(monitor);
	} else {
		usb_monitor_polling_destroy(monitor);
	}
}

kburn_err_t usb_monitor_prepare(KBMonCTX monitor) {
	if (!monitor->usb->libusb) {
		usb_subsystem_init(monitor);
	}

	debug_trace_function();
	if (monitor->usb->monitor_prepared) {
		debug_print(KBURN_LOG_DEBUG, "\talready inited.");
		return KBurnNoErr;
	}

	monitor->usb->monitor_prepared = true;

	if (libusb_has_capability(LIBUSB_CAP_HAS_HOTPLUG)) {
		debug_print(KBURN_LOG_DEBUG, "libUsbHasWathcer: callback");
		monitor->usb->event_mode = USB_EVENT_CALLBACK;
	} else {
		debug_print(KBURN_LOG_DEBUG, "libUsbHasWathcer: polling");
		monitor->usb->event_mode = USB_EVENT_POLLING;
	}

	IfErrorReturn(event_thread_init(monitor, "libusb pump", pump_libusb_event, &monitor->usb->event_queue));

	if (monitor->usb->event_mode == USB_EVENT_CALLBACK) {
		IfErrorReturn(usb_monitor_callback_prepare(monitor));
	} else {
		IfErrorReturn(usb_monitor_polling_prepare(monitor));
	}

	// thread_create("usb init scan", init_list_all_usb_devices_threaded, NULL, monitor, NULL);

	return KBurnNoErr;
}

void usb_monitor_pause(KBMonCTX monitor) {
	debug_trace_function();
	if (monitor->usb->monitor_enabled) {
		if (monitor->usb->event_mode == USB_EVENT_CALLBACK) {
			usb_monitor_callback_pause(monitor);
		} else {
			usb_monitor_polling_pause(monitor);
		}

		monitor->usb->monitor_enabled = false;
		debug_print(KBURN_LOG_INFO, "USB monitor disabled");
	}
}

kburn_err_t usb_monitor_resume(KBMonCTX monitor) {
	debug_trace_function();
	if (monitor->usb->monitor_enabled) {
		return KBurnNoErr;
	}

	if (!monitor->usb->monitor_prepared) {
		kburn_err_t r = usb_monitor_prepare(monitor);
		if (r != KBurnNoErr) {
			return r;
		}
	}

	if (monitor->usb->event_mode == USB_EVENT_CALLBACK) {
		IfErrorReturn(usb_monitor_callback_resume(monitor));
	} else {
		IfErrorReturn(usb_monitor_polling_resume(monitor));
	}

	return KBurnNoErr;
}

kburn_err_t usb_monitor_manual_trig(KBMonCTX monitor) {
	debug_trace_function();

	if (!monitor->usb->libusb) {
		usb_subsystem_init(monitor);
	}

	thread_create("usb maunal scan", init_list_all_usb_devices_threaded, NULL, monitor, NULL);

	return KBurnNoErr;
}
