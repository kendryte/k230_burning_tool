#include <math.h>

#include "public/canaan-burn.h"

#include "private/lib/device.h"

#include "private/lib/components/call-user-handler.h"
#include "private/lib/components/device-link-list.h"

#include "private/monitor/subsystem.h"

#include "private/monitor/descriptor.h"
#include "private/monitor/lifecycle.h"
#include "private/monitor/usb_types.h"

static inline bool match_device(int vid, int pid, const struct libusb_device_descriptor *desc) {
	if ((uint16_t)vid != (uint16_t)KBURN_VIDPID_FILTER_ANY && vid != desc->idVendor) {
		return false;
	}
	if ((uint16_t)pid != (uint16_t)KBURN_VIDPID_FILTER_ANY && pid != desc->idProduct) {
		return false;
	}
	return true;
}

kburn_err_t init_list_all_usb_devices(KBMonCTX monitor) {
	debug_trace_function("%.4x:%.4x", monitor->usb->settings.vid, monitor->usb->settings.pid);
	struct libusb_device_descriptor desc;

	libusb_device **list;
	ssize_t r = libusb_get_device_list(monitor->usb->libusb, &list);
	if (!check_libusb(r)) {
		debug_print_libusb_error("libusb_get_device_list()", r);
		return r;
	}

	int cnt = r;
	for (int i = 0; i < cnt; i++) {
		libusb_device *dev = list[i];

		r = libusb_get_device_descriptor(dev, &desc);
		if (!check_libusb(r)) {
			debug_print_libusb_error("[init/poll] libusb_get_device_descriptor()", r);
			continue;
		}
		debug_print(KBURN_LOG_DEBUG, "[init/poll] found device: [%.4x:%.4x]", desc.idVendor, desc.idProduct);

		if (!match_device(subsystem_settings.vid, subsystem_settings.pid, &desc)) {
			debug_print(KBURN_LOG_DEBUG, "[init/poll] \tnot wanted device.");
			continue;
		}

		uint8_t path[MAX_USB_PATH_LENGTH] = {0};
		if (usb_get_device_path(dev, path) < LIBUSB_SUCCESS) {
			debug_print(KBURN_LOG_ERROR, "[init/poll] \tget path failed.");
			continue;
		}

		debug_print(KBURN_LOG_DEBUG, "[init/poll] \tpath: %s", usb_debug_path_string(path));

		if (get_device_by_usb_port_path(monitor, desc.idVendor, desc.idProduct, path) != NULL) {
			debug_print(KBURN_LOG_DEBUG, "[init/poll] \tdevice already opened, ignore.");
			continue;
		} else {
			debug_print(KBURN_LOG_DEBUG, "[init/poll] \topen");
			// IfErrorReturn(open_single_usb_port(monitor, dev, true, NULL));

			kburnUsbDeviceInfoSlice devInfo;

			if(!monitor->on_before_open.handler) {
				libusb_free_device_list(list, true);

				debug_print(KBURN_LOG_ERROR, COLOR_FMT("User have no on_before_open callback"), RED);

				return KBurnNoErr;
			}

			if(!CALL_HANDLE_SYNC(monitor->on_before_open, usb_debug_path_string(path))) {
				libusb_free_device_list(list, true);

				debug_print(KBURN_LOG_ERROR, COLOR_FMT("on_before_open no burn process canceled"), RED);

				return KBurnNoErr;
			}

			int ret = usb_get_vid_pid_path(dev, &devInfo.idVendor, &devInfo.idProduct, devInfo.path);
			if (ret < LIBUSB_SUCCESS) {
				libusb_free_device_list(list, true);

				debug_print(KBURN_LOG_ERROR, COLOR_FMT("usb_get_vid_pid_path failed"), RED);

				return KBurnNoErr;
			}

			push_libusb_event(monitor, LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED, &devInfo);
			event_thread_fire(monitor->usb->event_queue);

			libusb_free_device_list(list, true);

			// only open one device
			return KBurnNoErr;
		}
	}
	libusb_free_device_list(list, true);

	return KBurnNoErr;
}

size_t list_usb_ports(struct libusb_context *libusb, kburnUsbDeviceInfoSlice *out, size_t max_size) {
	debug_trace_function();

	m_assert_ptr(libusb, "use not init libusb instance");

	libusb_device **list;
	ssize_t count = libusb_get_device_list(libusb, &list);
	if (!check_libusb(count)) {
		debug_print_libusb_error("libusb_get_device_list()", count);
		return count;
	}
	debug_print(KBURN_LOG_INFO, COLOR_FMT("libusb_get_device_list") ": " FMT_SIZET, RED, (size_t)count);

	size_t index = 0;
	for (size_t i = 0; i < (size_t)count; i++) {
		libusb_device *dev = list[i];
		struct libusb_device_descriptor desc;
		int r = libusb_get_device_descriptor(dev, &desc);
		if (!check_libusb(r)) {
			debug_print_libusb_error("libusb_get_device_descriptor()", r);
			continue;
		}

		if (index >= max_size) {
			index++;
			continue;
		}

		out[index].idProduct = desc.idProduct;
		out[index].idVendor = desc.idVendor;
		out[index].bcdDevice = desc.bcdDevice;

		r = usb_get_device_path(dev, &out[index].path[0]);
		if (!kburn_not_error(r)) {
			debug_print_libusb_error("usb_get_device_path()", r);
		}

		index++;
	}
	libusb_free_device_list(list, true);

	return index;
}
