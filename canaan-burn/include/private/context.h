#pragma once

#include <stdbool.h>
#include <string.h>

#include "public/types.h"

#define CONTEXT_MEMORY_SIGNATURE "kburnCtx"

typedef struct kburnMonitorContext {
	char signature[sizeof(CONTEXT_MEMORY_SIGNATURE)];

	on_device_list_change_t on_list_change;
	on_device_connect_t on_connect;
	on_device_disconnect_t on_disconnect;
	on_device_confirmed_t on_confirmed;

	struct disposable_list *const disposables;
	struct disposable_list *const threads;
	struct port_link_list *const openDeviceList;
	struct event_queue_thread *user_event;

	struct usb_subsystem_context *const usb;

	bool monitor_inited;
} kburnMonitorContext;

typedef kburnMonitorContext *KBMonCTX;

static inline bool validateContext(KBMonCTX ptr) {
	return memcmp(ptr->signature, CONTEXT_MEMORY_SIGNATURE, sizeof(CONTEXT_MEMORY_SIGNATURE)) == 0;
}
