#include "private/lib/components/call-user-handler.h"
#include "private/lib/components/queued-thread.h"

static void user_callback_thread(KBMonCTX UNUSED(monitor), void *_evt) {
	struct user_handler_wrap_data *data = _evt;

	data->handler(data->context, data->device);
	free(data);
}

kburn_err_t global_init_user_handle_thread(KBMonCTX monitor) {
	return event_thread_init(monitor, "userCb", user_callback_thread, &monitor->user_event);
}

void user_handler_wrap_async(struct user_handler_wrap_data *data) {
	event_thread_queue(data->device->_monitor->user_event, data, true);
}

void user_handler_wrap_sync(struct user_handler_wrap_data *data) {
	user_callback_thread(NULL, data);
}

void _user_handler_wrap_async_new(on_device_confirmed handler, void *context, kburnDeviceNode *device) {
	struct user_handler_wrap_data *data = calloc(1, sizeof(struct user_handler_wrap_data));
	data->context = context;
	data->handler = handler;
	data->device = device;
	event_thread_queue(data->device->_monitor->user_event, data, true);
}
