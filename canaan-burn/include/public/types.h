#pragma once

#include <time.h>

#include "./errors.h"
#include "./prefix.h"

DEFINE_START

typedef uint32_t kburn_stor_address_t;
typedef uint32_t kburn_stor_block_t;

typedef struct kburnDeviceError {
	PCONST kburn_err_t code; // (kburnErrorKind << 32) + (slip error code / serial port error code)
	PCONST char *errorMessage;
} kburnDeviceError;

typedef struct kburnDeviceNode {
	PCONST uint64_t guid;
	PCONST kburnDeviceError *const error;
	PCONST void *chipInfo;
	PCONST struct disposable_list *disposable_list;
	// struct kburnSerialDeviceNode *const serial;
	struct kburnUsbDeviceNode *const usb;
	PCONST struct kburnMonitorContext *const _monitor;

	PCONST bool destroy_in_progress;
	PCONST bool disconnect_should_call;
	PCONST uint32_t bind_id;
	PCONST time_t bind_wait_timing;

	PCONST struct mlock *reference_lock;
} kburnDeviceNode;

typedef enum kburnLogType {
	KBURN_LOG_BUFFER,
	KBURN_LOG_TRACE,
	KBURN_LOG_DEBUG,
	KBURN_LOG_INFO,
	KBURN_LOG_WARN,
	KBURN_LOG_ERROR,
} kburnLogType;

#define CONCAT(a, b) a##b
#define declare_callback(ret, name, ...)                       \
	typedef ret (*name)(void *ctx __VA_OPT__(, ) __VA_ARGS__); \
	typedef struct CONCAT(name, _callback_bundle) {            \
		name handler;                                          \
		void *context;                                         \
	} CONCAT(name, _t)

// Used by Monitor
declare_callback(bool, on_before_device_open, const char *const path);
declare_callback(bool, on_device_connect, kburnDeviceNode *dev);
declare_callback(void, on_device_disconnect, kburnDeviceNode *dev);
declare_callback(bool, on_device_confirmed, kburnDeviceNode *dev);

declare_callback(void, on_write_progress, const kburnDeviceNode *dev, size_t current, size_t length);
declare_callback(void, on_debug_log, kburnLogType type, const char *message);

#include "./types.usb.h"

DEFINE_END
