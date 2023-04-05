#include <stdarg.h>
#include <stdlib.h>
#include <libusb.h>

#include "private/lib/basic/errors.h"
#include "private/lib/basic/string.h"
#include "private/lib/device.h"

void _copy_last_libusb_error(kburnDeviceNode *node, int err) {
	clear_error(node);
	node->error->code = make_error_code(KBURN_ERROR_KIND_USB, err);
	const char *name = libusb_error_name(err);
	const char *desc = libusb_strerror(err);
	size_t size = m_snprintf(NULL, 0, "%s: %s", name, desc);
	char *buff = malloc(size + 1);
	m_snprintf(buff, (size + 1), "%s: %s", name, desc);
	node->error->errorMessage = buff;
	debug_print(KBURN_LOG_INFO, COLOR_FMT("copy_last_libusb_error") ": %d - %s", COLOR_ARG(RED), err, node->error->errorMessage);
}

void _set_error(kburnDeviceNode *node, enum kburnErrorKind kind, int32_t code, const char *error, ...) {
	int32_t e = make_error_code(kind, code);
	clear_error(node);
	node->error->code = e;

	va_list args;
	va_start(args, error);
	node->error->errorMessage = vsprintf_alloc(error, args);
	va_end(args);

	debug_print(KBURN_LOG_ERROR, COLOR_FMT("set_error") ": %s", COLOR_ARG(RED), node->error->errorMessage);
}

void _clear_error(kburnDeviceNode *node) {
	node->error->code = 0;
	if (node->error->errorMessage != NULL) {
		debug_print(KBURN_LOG_INFO, "clear_error: free last");
		free(node->error->errorMessage);
		node->error->errorMessage = NULL;
	}
}
