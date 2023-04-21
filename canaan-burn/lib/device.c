#include "private/lib/device.h"

#include "private/lib/basic/resource-tracker.h"
#include "private/lib/components/device-link-list.h"

DEFINE_REGISTER_SWAPPER(kburnOnBeforeDeviceOpen, monitor->on_before_open, on_before_device_open)
DEFINE_REGISTER_SWAPPER(kburnOnDeviceConnect, monitor->on_connect, on_device_connect)
DEFINE_REGISTER_SWAPPER(kburnOnDeviceDisconnect, monitor->on_disconnect, on_device_disconnect)
DEFINE_REGISTER_SWAPPER(kburnOnDeviceConfirmed, monitor->on_confirmed, on_device_confirmed)

static void destroy_device(void *UNUSED(ctx), kburnDeviceNode *context) {
	debug_trace_function();

	if (context->disconnect_should_call && context->_monitor->on_disconnect.handler) {
		debug_print(KBURN_LOG_DEBUG, "\tmonitor::on_disconnect()");
		context->_monitor->on_disconnect.handler(context->_monitor->on_disconnect.context, context);
	}

	clear_error(context);
	disposable_list_t *dlist = context->disposable_list;
	dispose_all(dlist);
	disposable_list_deinit(dlist);
}

// static DECALRE_DISPOSE(dispose_device, kburnDeviceNode) {
// 	mark_destroy_device_node(context);
// }
// DECALRE_DISPOSE_END()

void device_instance_collect(kburnDeviceNode *instance) {
	debug_trace_function();
	if (/* !instance->serial->init && */ !instance->usb->init) {
		mark_destroy_device_node(instance);
	}
}

void mark_destroy_device_node(kburnDeviceNode *instance) {
	debug_trace_function();
	instance->destroy_in_progress = true;
	lock_deinit(instance->reference_lock);
}

kburn_err_t create_empty_device_instance(KBMonCTX monitor, kburnDeviceNode **output) {
	DeferEnabled;

	disposable_list_t *disposable_list = CheckNull(disposable_list_init("device instance"));

	kburnDeviceError *error = MyAlloc(kburnDeviceError);
	register_dispose_pointer(disposable_list, error);

	kburnUsbDeviceNode *usb = MyAlloc(kburnUsbDeviceNode);
	register_dispose_pointer(disposable_list, usb);

	kburnDeviceNode *empty_device_instance = MyAlloc(kburnDeviceNode);
	register_dispose_pointer(disposable_list, empty_device_instance);

	memcpy(
		empty_device_instance,
		&(kburnDeviceNode){
			.disposable_list = disposable_list,
			.error = error,
			.chipInfo = NULL,
			// .serial = serial,
			.usb = usb,
			._monitor = monitor,
			.destroy_in_progress = false,
			.bind_id = 0,
			.reference_lock = lock_init(),
		},
		sizeof(kburnDeviceNode));

	lock_bind_destruct(empty_device_instance->reference_lock, destroy_device, NULL, empty_device_instance);
	DeferCall(device_instance_collect, empty_device_instance);

	empty_device_instance->guid = (uint64_t)empty_device_instance;
	empty_device_instance->usb->parent = empty_device_instance;

	*output = empty_device_instance;

	DeferAbort;

	return KBurnNoErr;
}
