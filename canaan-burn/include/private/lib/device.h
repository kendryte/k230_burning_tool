#pragma once

#include "private/context.h"
#include "private/lib/basic/disposable.h"

__attribute__((always_inline)) static inline kburnDeviceNode *__p(kburnDeviceNode *v) {
	return v;
}

__attribute__((always_inline)) static inline kburnDeviceNode *__u(kburnUsbDeviceNode *v) {
	return v->parent;
}

#define get_node(node) _Generic((node), kburnDeviceNode * : __p, kburnUsbDeviceNode * : __u)(node)
#define monitorOf(node) get_node(node)->_monitor

#define copy_last_libusb_error(node, errno) _copy_last_libusb_error(get_node(node), errno)
#define set_error(node, kind, code, errstr, ...) _set_error(get_node(node), kind, code, errstr __VA_OPT__(, ) __VA_ARGS__)
#define set_kb_error(node, err, errstr, ...) \
	set_error(node, kburnSplitErrorCode(err).kind, kburnSplitErrorCode(err).code, errstr __VA_OPT__(, ) __VA_ARGS__)
#define set_syserr(node) set_error(node, KBURN_ERROR_KIND_SYSCALL, errno, strerror(errno))
#define clear_error(node) _clear_error(get_node(node))
#define error_compare(node, kind, _code) (get_node(node)->error->code == make_error_code(kind, _code))

void _copy_last_libusb_error(kburnDeviceNode *node, int err);
void _set_error(kburnDeviceNode *node, enum kburnErrorKind kind, int32_t code, const char *errstr, ...) __attribute__((format(printf, 4, 5)));
void _clear_error(kburnDeviceNode *node);

kburn_err_t create_empty_device_instance(KBMonCTX monitor, kburnDeviceNode **output);
void device_instance_collect(kburnDeviceNode *dev);
void mark_destroy_device_node(kburnDeviceNode *dev);

#define use_device(node)                     \
	__extension__({                          \
		kburnDeviceNode *n = get_node(node); \
		bool r = false;                      \
		if (!n->destroy_in_progress) {       \
			r = autolock(n->reference_lock); \
		}                                    \
		r;                                   \
	})
