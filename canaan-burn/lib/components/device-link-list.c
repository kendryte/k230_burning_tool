#include <stdlib.h>
#include <string.h>

#include "private/lib/basic/resource-tracker.h"
#include "private/lib/components/device-link-list.h"

port_link_list *port_link_list_init() {
	DeferEnabled;

	port_link_list *ret = calloc(1, sizeof(port_link_list));
	DeferFree(ret);
	if (ret == NULL) {
		return NULL;
	}

	ret->exclusion = lock_init();
	if (ret == NULL) {
		return NULL;
	}

	DeferAbort;
	return ret;
}
DECALRE_DISPOSE(port_link_list_deinit, port_link_list) {
	lock_deinit(context->exclusion);
	free(context);
}
DECALRE_DISPOSE_END()

// void recreate_waitting_list(KBMonCTX monitor) {
// 	autolock(monitor->openDeviceList->exclusion);
// 	_recreate_waitting_list(monitor);
// }

void add_to_device_list(kburnDeviceNode *target) {
	KBMonCTX monitor = target->_monitor;
	debug_trace_function("0x%p, [size=%d]", (void *)target, monitor->openDeviceList->size);
	port_link_element *ele = malloc(sizeof(port_link_element));
	ele->node = target;
	ele->prev = NULL;
	ele->next = NULL;

	autolock(monitor->openDeviceList->exclusion);
	if (monitor->openDeviceList->head) {
		ele->prev = monitor->openDeviceList->tail;
		monitor->openDeviceList->tail->next = ele;

		monitor->openDeviceList->tail = ele;

		monitor->openDeviceList->size++;
	} else {
		monitor->openDeviceList->tail = monitor->openDeviceList->head = ele;
		monitor->openDeviceList->head->prev = NULL;
		monitor->openDeviceList->head->next = NULL;

		monitor->openDeviceList->size++;
	}

	dispose_list_add(target->disposable_list, toDisposable(delete_from_device_list, target));

	// if (_should_insert_waitting_list(target)) {
	// 	_recreate_waitting_list(target->_scope);
	// }
}

static void do_delete(KBMonCTX monitor, port_link_element *target) {
	debug_trace_function("\t0x%p [size=%d]", (void *)target, monitor->openDeviceList->size);

	if (target->prev) {
		target->prev->next = target->next;
	}

	if (target->next) {
		target->next->prev = target->prev;
	}

	if (target == monitor->openDeviceList->head) {
		monitor->openDeviceList->head = monitor->openDeviceList->head->next;
	}

	if (target == monitor->openDeviceList->tail) {
		monitor->openDeviceList->tail = monitor->openDeviceList->tail->prev;
	}

	m_assert(monitor->openDeviceList->size > 0, "delete port from empty list");
	monitor->openDeviceList->size--;

	// if (_should_insert_waitting_list(target->node)) {
	// 	_recreate_waitting_list(monitor);
	// }

	free(target);
}

DECALRE_DISPOSE(delete_from_device_list, kburnDeviceNode) {
	KBMonCTX monitor = context->_monitor;
	debug_trace_function("0x%p [size=%d]", (void *)context, monitor->openDeviceList->size);
	autolock(monitor->openDeviceList->exclusion);
	for (port_link_element *curs = monitor->openDeviceList->head; curs != NULL; curs = curs->next) {
		if (curs->node == context) {
			do_delete(monitor, curs);
			return;
		}
	}
	debug_print(KBURN_LOG_WARN, "  - not found");
	return;
}
DECALRE_DISPOSE_END()

// static inline port_link_element *find_serial_device(KBMonCTX monitor, const char *path) {
// 	port_link_element *curs = NULL;

// 	autolock(monitor->openDeviceList->exclusion);
// 	for (curs = monitor->openDeviceList->head; curs != NULL; curs = curs->next) {
// 		if (!curs->node->serial->init) {
// 			continue;
// 		}
// 		if (strcmp(curs->node->serial->deviceInfo.path, path) == 0) {
// 			break;
// 		}
// 	}
// 	return curs;
// }

static inline port_link_element *find_usb_device_by_vidpidpath(KBMonCTX monitor, uint16_t vid, uint16_t pid, const uint8_t path[MAX_USB_PATH_LENGTH]) {
	port_link_element *curs = NULL;

	autolock(monitor->openDeviceList->exclusion);
	for (curs = monitor->openDeviceList->head; curs != NULL; curs = curs->next) {
		int samePath = memcmp((char *)path, (char *)curs->node->usb->deviceInfo.path, MAX_USB_PATH_LENGTH);
		if (curs->node->usb->deviceInfo.idVendor == vid && curs->node->usb->deviceInfo.idProduct == pid && samePath == 0) {
			break;
		}
	}
	return curs;
}

// kburnDeviceNode *get_device_by_serial_port_path(KBMonCTX monitor, const char path[MAX_USB_PATH_LENGTH]) {
// 	port_link_element *ret = find_serial_device(monitor, path);
// 	return ret ? ret->node : NULL;
// }

kburnDeviceNode *get_device_by_usb_port_path(KBMonCTX monitor, uint16_t vid, uint16_t pid, const uint8_t path[MAX_USB_PATH_LENGTH]) {
	port_link_element *ret = find_usb_device_by_vidpidpath(monitor, vid, pid, path);
	return ret ? ret->node : NULL;
}

// static kburnDeviceNode *has_bind_id_used(KBMonCTX monitor, uint32_t id) {
// 	port_link_element *curs = NULL;

// 	for (curs = monitor->openDeviceList->head; curs != NULL; curs = curs->next) {
// 		if (curs->node->bind_id == id) {
// 			return curs->node;
// 		}
// 	}
// 	return NULL;
// }

// void alloc_new_bind_id(kburnDeviceNode *node) {
// 	uint32_t id = 0;

// 	autolock(node->_scope->openDeviceList->exclusion);

// 	do {
// 		do {
// 			id = rand();
// 		} while (id == 0);
// 	} while (has_bind_id_used(node->_scope, id));
// 	node->bind_id = id;
// }

// kburnDeviceNode *get_device_by_bind_id(KBMonCTX monitor, uint32_t id) {
// 	return has_bind_id_used(monitor, id);
// }
