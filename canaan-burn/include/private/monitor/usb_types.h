#pragma once

#include "private/base.h"

#include "private/lib/components/thread.h"
#include "private/lib/components/queued-thread.h"

#include <libusb.h>

typedef struct kburnUsbDeviceInfoSlice kburnUsbDeviceInfoSlice;

enum UsbEventMode
{
	USB_EVENT_UNKNOWN = 0,
	USB_EVENT_POLLING,
	USB_EVENT_CALLBACK,
};

struct usb_settings {
	uint16_t vid;
	uint16_t pid;
};

typedef struct usb_subsystem_context {
	struct usb_settings settings;

	bool subsystem_inited;
	bool detach_kernel_driver;

	bool monitor_prepared;
	bool monitor_enabled;

	struct libusb_context *libusb;

	kbthread libusb_thread;
	enum UsbEventMode event_mode;
	union {
		struct polling_context *polling_context;
		libusb_hotplug_callback_handle event_handle;
	};

	event_queue_thread_t event_queue;
} usb_subsystem_context;

#define debug_print_libusb_result(msg, err, ...)                                                                                     \
	debug_print(                                                                                                                     \
		err < 0 ? KBURN_LOG_ERROR : KBURN_LOG_DEBUG, msg ": %s[%d] %s", __VA_ARGS__ __VA_OPT__(, ) libusb_error_name(err), (int)err, \
		libusb_strerror(err));

#define debug_print_libusb_error(msg, err, ...)                            \
	__extension__({                                                        \
		if (err < 0) {                                                     \
			debug_print_libusb_result(msg, err __VA_OPT__(, ) __VA_ARGS__) \
		}                                                                  \
	})

#define CheckLibusbError(action)                               \
	__extension__({                                            \
		int _err = action;                                     \
		if (_err < LIBUSB_SUCCESS) {                           \
			debug_print_libusb_error(#action "-failed", _err); \
			return _err;                                       \
		}                                                      \
		_err;                                                  \
	})

#define check_libusb(err) (err >= LIBUSB_SUCCESS)

#define set_node_error_with_log(err, msg, ...)                          \
	__extension__({                                                     \
		if (0)                                                          \
			(void)0;                                                    \
		debug_print_libusb_result(msg, err __VA_OPT__(, ) __VA_ARGS__); \
		copy_last_libusb_error(node, err);                              \
		err = make_error_code(KBURN_ERROR_KIND_USB, err);               \
	})

#define IfUsbErrorLogSetReturn(action)                                \
	__extension__({                                                   \
		if (0)                                                        \
			(void)0;                                                  \
		IfErrorReturn(check_libusb, action, set_node_error_with_log); \
	})

#define set_node_error(err, msg)       \
	if (0)                             \
		(void)0;                       \
	copy_last_libusb_error(node, err); \
	err = make_error_code(KBURN_ERROR_KIND_USB, err);

#define IfUsbErrorSetReturn(action)                          \
	__extension__({                                          \
		if (0)                                               \
			(void)0;                                         \
		IfErrorReturn(check_libusb, action, set_node_error); \
	})

#define log_libusb_only(err, msg, ...) debug_print_libusb_result(msg, err __VA_OPT__(, ) __VA_ARGS__);
#define IfUsbErrorLogReturn(action)                           \
	__extension__({                                           \
		if (0)                                                \
			(void)0;                                          \
		IfErrorReturn(check_libusb, action, log_libusb_only); \
	})

#define set_node_k_error(err, msg) copy_last_libusb_error(node, err);

#define IfErrorSetReturn(action)                                  \
	__extension__({                                               \
		if (0)                                                    \
			(void)0;                                              \
		IfErrorReturn(kburn_not_error, action, set_node_k_error); \
	})

#ifdef get_settings
#undef subsystem_settings
#undef get_settings
#undef CREATE_GETTER_SETTER
#else
#define get_settings(node) get_monitor(node)->usb->settings
#define subsystem_settings (monitor->usb->settings)
#define CREATE_GETTER_SETTER(Field, config_field) _CREATE_GETTER_SETTER(Usb, Field, config_field)
#endif
