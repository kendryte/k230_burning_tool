#include "private/lib/basic/lock.h"
#include "private/lib/basic/resource-tracker.h"
#include "private/lib/components/call-user-handler.h"
#include "private/lib/components/device-link-list.h"

#include "private/monitor/subsystem.h"
#include "private/monitor/monitor.h"

#include "private/monitor/usb_types.h"

static uint32_t dbg_index = 0;

disposable_list_t *monitor_global_monitor = NULL;

extern struct usb_settings usb_default_settings;

void kburnMonitorGlobalDestroy() {
	debug_trace_function();
	if (monitor_global_monitor == NULL) {
		return;
	}
	dispose_all(monitor_global_monitor);
	disposable_list_deinit(monitor_global_monitor);
	monitor_global_monitor = NULL;
}

kburn_err_t kburnMonitorCreate(KBMonCTX *ppCtx) {
	debug_trace_function();

	if (!monitor_global_monitor) {
		atexit(kburnMonitorGlobalDestroy);
		monitor_global_monitor = disposable_list_init("monitor global");
	}

	DeferEnabled;

	dbg_index++;

	char *comment1 = DeferFree(CheckNull(sprintf_alloc("monitor[%d].disposables", dbg_index)));
	disposable_list_t *dis = DeferFree(CheckNull(disposable_list_init(comment1)));
	DeferCall(dispose_all, dis);

	char *comment2 = DeferFree(CheckNull(sprintf_alloc("monitor[%d].threads", dbg_index)));
	disposable_list_t *threads = DeferFree(CheckNull(disposable_list_init(comment2)));
	DeferCall(dispose_all, threads);

	register_dispose_pointer(dis, comment1);
	register_dispose_pointer(dis, comment2);

	*ppCtx = MyAlloc(kburnMonitorContext);
	register_dispose_pointer(dis, *ppCtx);

	usb_subsystem_context *usb = MyAlloc(usb_subsystem_context);
	register_dispose_pointer(dis, usb);
	usb->settings = usb_default_settings;

	struct port_link_list *odlist = port_link_list_init();
	dispose_list_add(dis, toDisposable(port_link_list_deinit, odlist));

	memcpy(
		*ppCtx,
		&(kburnMonitorContext){
			.signature = CONTEXT_MEMORY_SIGNATURE,
			.openDeviceList = odlist,
			.disposables = dis,
			.threads = threads,
			.usb = usb,
			.monitor_inited = false,
		},
		sizeof(kburnMonitorContext));

	dispose_list_add(monitor_global_monitor, toDisposable(dispose_all_and_deinit, dis));
	dispose_list_add(monitor_global_monitor, toDisposable(dispose_all_and_deinit, threads));

	IfErrorReturn(global_init_user_handle_thread(*ppCtx));

	DeferAbort;
	return KBurnNoErr;
}

void kburnMonitorDestroy(KBMonCTX monitor) {
	debug_trace_function("(%p)", (void *)monitor);
	if (monitor_global_monitor == NULL) {
		return;
	}
	dispose(bindToList(monitor_global_monitor, toDisposable(dispose_all_and_deinit, monitor->threads)));
	dispose(bindToList(monitor_global_monitor, toDisposable(dispose_all_and_deinit, monitor->disposables)));
}

static DECALRE_DISPOSE(_dispose, kburnMonitorContext) {
	if (context->monitor_inited) {
		usb_monitor_pause(context);
		context->monitor_inited = false;
	}

	if (context->usb->subsystem_inited) {
		usb_subsystem_deinit(context);
	}
}
DECALRE_DISPOSE_END()

void kburnMonitorWaitDevicePause(KBMonCTX monitor) {
	debug_trace_function();

	usb_monitor_pause(monitor);
}

kburn_err_t kburnMonitorWaitDeviceResume(KBMonCTX monitor) {
	debug_trace_function();
	return usb_monitor_resume(monitor);
}

kburn_err_t kburnMonitorStartWaitingDevices(KBMonCTX monitor) {
	debug_trace_function("already_init=%d", monitor->monitor_inited);
	if (monitor->monitor_inited) {
		return KBurnNoErr;
	}

	DeferEnabled;

	kburn_err_t r;

	if (!monitor->usb->subsystem_inited) {
		usb_subsystem_init(monitor);
		DeferCall(usb_subsystem_deinit, monitor);
	}

	r = usb_monitor_prepare(monitor);
	if (r != KBurnNoErr) {
		return r;
	}

	monitor->monitor_inited = true;

	dispose_list_add(monitor->disposables, toDisposable(_dispose, monitor));

	DeferAbort;

	return kburnMonitorWaitDeviceResume(monitor);
}
