#include <windows.h>
#include <dbt.h>
#include <usbiodef.h>

#include "private/base.h"
#include "private/lib/basic/array.h"
#include "private/lib/basic/errors.h"
#include "private/lib/components/thread.h"
#include "private/lib/debug/print.h"

#include "private/monitor/monitor.h"
#include "private/monitor/libusb.list.h"
#include "private/monitor/usb_types.h"

#include "public/canaan-burn.h"

typedef struct polling_context {
	kbthread thread;
	HWND window;
	bool notify;
	dynamic_array_t *prevList;
	struct libusb_context *libusb;
} polling_context;

static inline bool match_device(int vid, int pid, const kburnUsbDeviceInfoSlice *devInfo) {
	if ((uint16_t)vid != (uint16_t)KBURN_VIDPID_FILTER_ANY && vid != devInfo->idVendor) {
		return false;
	}
	if ((uint16_t)pid != (uint16_t)KBURN_VIDPID_FILTER_ANY && pid != devInfo->idProduct) {
		return false;
	}
	return true;
}

static void push_event(KBMonCTX monitor, bool add, const kburnUsbDeviceInfoSlice *devInfo) {
	libusb_hotplug_event event = add ? LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED : LIBUSB_HOTPLUG_EVENT_DEVICE_LEFT;
	if (match_device(subsystem_settings.vid, subsystem_settings.pid, devInfo)) {
		push_libusb_event(monitor, event, devInfo);
	}
}

static void fill_usb_array(KBMonCTX monitor, dynamic_array_t *array) {
	ssize_t r = list_usb_ports(monitor->usb->polling_context->libusb, array->body, array->size);
	if (r <= 0)
		return;

	size_t dev_count = (size_t)r;

	while (dev_count > array->size) {
		debug_print(KBURN_LOG_INFO, "resize device buffer to: " FMT_SIZET, dev_count);
		array_fit(array, dev_count + 5);

		r = list_usb_ports(monitor->usb->polling_context->libusb, array->body, array->size);

		if (r <= 0)
			return;

		dev_count = (size_t)r;
	}

	array->length = dev_count;
}

static inline bool is_same_port(kburnUsbDeviceInfoSlice *a, kburnUsbDeviceInfoSlice *b) {
	if (memcmp(a->path, b->path, MAX_USB_PATH_LENGTH) != 0) {
		return false;
	}
	if (a->idProduct != b->idProduct) {
		return false;
	}
	if (a->idVendor != b->idVendor) {
		return false;
	}
	if (a->bcdDevice != b->bcdDevice) {
		return false;
	}
	return true;
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	static __thread KBMonCTX monitor;

	if (msg == WM_CREATE) {
		CREATESTRUCT *input = (void *)lParam;
		monitor = input->lpCreateParams;
		m_assert(validateContext(monitor), "WM_CREATE invalid input.");

		fill_usb_array(monitor, monitor->usb->polling_context->prevList);
	}

	if (msg == WM_DESTROY) {
		PostQuitMessage(0);
	}

	if (msg != WM_DEVICECHANGE) {
		return DefWindowProc(hwnd, msg, wParam, lParam);
	}

	// if (wParam != DBT_DEVNODES_CHANGED) {
	// 	return 0;
	// }

	// debug_trace_function("DBT_DEVNODES_CHANGED");

	dynamic_array_t *oldList = monitor->usb->polling_context->prevList;
	dynamic_array_t newList;
	array_make(newList, kburnUsbDeviceInfoSlice, oldList->length + 5);
	fill_usb_array(monitor, &newList);

	bool *same = calloc(sizeof(bool), oldList->length);

	// debug_print(KBURN_LOG_ERROR, "newList: %u", newList.length);
	kburnUsbDeviceInfoSlice *ptr = newList.body;
	for (size_t i = 0; i < newList.length; i++, ptr++) {
		// debug_print(KBURN_LOG_ERROR, "newList: " FMT_SIZET " %s", i, usb_debug_path_string(ptr->path));

		kburnUsbDeviceInfoSlice *lptr = oldList->body;
		bool alreadyExists = false;
		for (size_t j = 0; j < oldList->length; j++, lptr++) {
			if (same[j])
				continue;

			if (is_same_port(ptr, lptr)) {
				same[j] = true;
				alreadyExists = true;
				break;
			}
		}

		if (!alreadyExists) {
			debug_print(KBURN_LOG_ERROR, "new device connect");
			push_event(monitor, true, ptr);
		}
	}

	//	debug_print(KBURN_LOG_ERROR, "oldList: %u", oldList->length);
	kburnUsbDeviceInfoSlice *lptr = oldList->body;
	for (size_t j = 0; j < oldList->length; j++, lptr++) {
		// debug_print(KBURN_LOG_ERROR, "oldList: " FMT_SIZET " %s", j, usb_debug_path_string(lptr->path));
		if (!same[j]) {
			debug_print(KBURN_LOG_ERROR, "old device remove");
			push_event(monitor, false, lptr);
		}
	}

	array_move(oldList, &newList);

	event_thread_fire(monitor->usb->event_queue);

	return 0;
}

static HWND create_event_window(KBMonCTX monitor) {
	// http://www.winprog.org/tutorial/simple_window.html

	WNDCLASSEX wc;

	static const char *windowClass = "usbEventReceiver";

	// Step 1: Registering the Window Class
	wc.cbSize = sizeof(WNDCLASSEX);
	wc.style = 0;
	wc.lpfnWndProc = WndProc;
	wc.cbClsExtra = 0;
	wc.cbWndExtra = 0;
	wc.hInstance = NULL;
	// wc.hInstance = GetModuleHandleA(NULL);
	wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);
	wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
	wc.lpszMenuName = NULL;
	wc.lpszClassName = windowClass;
	wc.hIconSm = LoadIcon(NULL, IDI_APPLICATION);

	if (!RegisterClassEx(&wc)) {
		debug_print_win32("WIN32 RegisterClassEx Failed!");
		return NULL;
	}
	debug_print(KBURN_LOG_DEBUG, "WIN32 RegisterClassEx OK");

	// Step 2: Creating the Window
	HWND hwnd = CreateWindowEx(
		WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW | WS_EX_STATICEDGE, windowClass, "usb event handle window", WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, 200,
		200, NULL, NULL, NULL, monitor);

	if (hwnd == NULL) {
		debug_print_win32("WIN32 CreateWindowEx Failed!");
		return NULL;
	}

	debug_print(KBURN_LOG_DEBUG, "WIN32 CreateWindowEx OK");

	return hwnd;
}

static void usb_monitor_polling_thread(void *UNUSED(context), KBMonCTX monitor, const bool *const quit) {
	monitor->usb->polling_context->prevList = array_create(kburnUsbDeviceInfoSlice, 10);

	int r = libusb_init(&monitor->usb->polling_context->libusb);
	m_assert(r >= 0, "libusb init fail in windows event thread (return %d)", r);

	HWND hwnd = create_event_window(monitor);
	if (!hwnd) {
		m_abort("create window failed");
	}
	monitor->usb->polling_context->window = hwnd;

	// register event
	DEV_BROADCAST_DEVICEINTERFACE NotificationFilter;
	ZeroMemory(&NotificationFilter, sizeof(NotificationFilter));
	NotificationFilter.dbcc_size = sizeof(DEV_BROADCAST_DEVICEINTERFACE);
	NotificationFilter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
	NotificationFilter.dbcc_classguid = GUID_DEVINTERFACE_USB_DEVICE;

	HDEVNOTIFY hDevNotify = RegisterDeviceNotification(hwnd, &NotificationFilter, DEVICE_NOTIFY_WINDOW_HANDLE);
	if (NULL == hDevNotify) {
		debug_print_win32("WIN32 RegisterDeviceNotification Failed!");
	}
	debug_print(KBURN_LOG_DEBUG, "WIN32 RegisterDeviceNotification OK");

	if (!ShowWindow(hwnd, SW_HIDE)) {
		debug_print_win32("WIN32 ShowWindow Failed!");
	}

	if (!UpdateWindow(hwnd)) {
		debug_print_win32("WIN32 UpdateWindow Failed!");
	}

	// Step 3: The Message Loop
	MSG Msg;
	while (GetMessage(&Msg, NULL, 0, 0) > 0 && !*quit) {
		TranslateMessage(&Msg);
		DispatchMessage(&Msg);
		debug_print(KBURN_LOG_TRACE, "WindowMessage: %d", Msg.message);
	}
	debug_print(KBURN_LOG_INFO, "WIN32 message queue finished");

	if (!UnregisterDeviceNotification(hDevNotify)) {
		debug_print_win32("WIN32 UnregisterDeviceNotification Failed!");
	}

	if (!DestroyWindow(hwnd)) {
		debug_print_win32("WIN32 DestroyWindow Failed!");
	}

	libusb_exit(monitor->usb->polling_context->libusb);
	monitor->usb->polling_context->libusb = NULL;

	array_delete(monitor->usb->polling_context->prevList);
}

static DECALRE_DISPOSE(quit_windows_thread, struct polling_context) {
	DWORD threadId = GetWindowThreadProcessId(context->window, NULL);
	debug_print(KBURN_LOG_INFO, "window thread id: %lu", threadId);
	if (!PostThreadMessage(threadId, WM_QUIT, 0, 0)) {
		debug_print_win32("WIN32 PostThreadMessage Failed!");
	}
}
DECALRE_DISPOSE_END()

kburn_err_t usb_monitor_polling_prepare(KBMonCTX monitor) {
	if (monitor->usb->polling_context) {
		return KBurnNoErr;
	}

	monitor->usb->polling_context = MyAlloc(struct polling_context);
	IfErrorReturn(thread_create("usb poll", usb_monitor_polling_thread, NULL, monitor, &monitor->usb->polling_context->thread));
	dispose_list_add(monitor->threads, toDisposable(quit_windows_thread, monitor->usb->polling_context));
	thread_resume(monitor->usb->polling_context->thread);

	return KBurnNoErr;
}

void usb_monitor_polling_destroy(KBMonCTX monitor) {
	if (!monitor->usb->polling_context)
		return;

	if (monitor->usb->polling_context->window) {
		// ?
	}

	if (monitor->usb->polling_context->thread) {
		thread_destroy(monitor, monitor->usb->polling_context->thread);
		monitor->usb->polling_context->thread = NULL;
	}

	free(monitor->usb->polling_context);
	monitor->usb->polling_context = NULL;
}

void usb_monitor_polling_pause(KBMonCTX monitor) {
	monitor->usb->polling_context->notify = false;
}

kburn_err_t usb_monitor_polling_resume(KBMonCTX monitor) {
	monitor->usb->polling_context->notify = true;
	return KBurnNoErr;
}
