#include "private/context.h"

#include "private/lib/basic/errors.h"
#include "private/lib/basic/event-queue.h"
#include "private/lib/basic/lock.h"
#include "private/lib/basic/resource-tracker.h"

#include "private/lib/components/thread.h"
#include "private/lib/components/queued-thread.h"

typedef struct event_queue_thread {
	kbthread thread;
	queue_t queue;
	event_handler handler;
	KBMonCTX monitor;
} event_queue_thread;

static DECALRE_DISPOSE(event_thread_queue_deinit, event_queue_thread *) {
	event_queue_thread *obj = *context;
	*context = NULL;
	queue_destroy(obj->queue, free);
}
DECALRE_DISPOSE_END()

static inline bool queue_is_not_empty(event_queue_thread *context) {
	return queue_size(context->queue) > 0;
}
static inline void work_in_queue(event_queue_thread *context) {
	void *data;
	while ((data = queue_shift(context->queue)) != NULL) {
		debug_trace_function("queue:" FMT_SIZET " - 0x%p", queue_size(context->queue) + 1, (void *)data);
		context->handler(context->monitor, data);
	}
}

void event_queue_thread_main(void *_ctx, KBMonCTX monitor, const bool *const quit) {
	event_queue_thread *context = _ctx;

	context->monitor = monitor;
	while (!*quit) {
		current_thread_wait_event(queue_is_not_empty, work_in_queue, context);
	}
}

void event_thread_deinit(KBMonCTX monitor, event_queue_thread **queue_thread) {
	if ((*queue_thread)->thread) {
		thread_destroy(monitor, (*queue_thread)->thread);
	}
	event_thread_queue_deinit(monitor->disposables, queue_thread);
	*queue_thread = NULL;
}

kburn_err_t event_thread_init(KBMonCTX monitor, const char *title, event_handler handler, event_queue_thread **out) {
	DeferEnabled;

	event_queue_thread *ret = DeferFree(MyAlloc(event_queue_thread));
	register_dispose_pointer(monitor->disposables, ret);
	*out = ret;
	DeferDispose(monitor->disposables, out, event_thread_queue_deinit);

	ret->handler = handler;

	IfErrorReturn(queue_create(&ret->queue));

	IfErrorReturn(thread_create(title, event_queue_thread_main, ret, monitor, &ret->thread));
	thread_resume(ret->thread);

	DeferAbort;
	return KBurnNoErr;
}

kburn_err_t event_thread_queue(event_queue_thread *et, void *event, bool auto_fire) {
	if (!et) {
		debug_print(KBURN_LOG_WARN, "push to thread after queue deinited.");
		return make_error_code(KBURN_ERROR_KIND_COMMON, KBurnObjectDestroy);
	}

	thread_event_autolock(thread_get_condition(et->thread));

	debug_trace_function("queue:" FMT_SIZET " + 0x%p", queue_size(et->queue), (void *)event);
	kburn_err_t r = queue_push(et->queue, event);

	if (auto_fire) {
		event_thread_fire(et);
	}

	return r;
}

void event_thread_fire(event_queue_thread *et) {
	thread_fire_event(thread_get_condition(et->thread));
}
