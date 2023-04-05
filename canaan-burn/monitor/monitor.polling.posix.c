#include "private/base.h"

#include "private/lib/components/thread.h"

#include "private/monitor/monitor.h"
#include "private/usb/private-types.h"

typedef struct polling_context {
	kbthread thread;
} polling_context;

kburn_err_t usb_monitor_polling_prepare(KBMonCTX UNUSED(monitor)) {
	TODO;
	return KBurnNoErr;
}
void usb_monitor_polling_destroy(KBMonCTX UNUSED(monitor)) {
}
void usb_monitor_polling_pause(KBMonCTX UNUSED(monitor)) {
}
kburn_err_t usb_monitor_polling_resume(KBMonCTX UNUSED(monitor)) {
	return KBurnNoErr;
}
