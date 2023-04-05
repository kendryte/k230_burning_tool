#include <stdlib.h>
#include <pthread.h>

#include "private/lib/basic/errors.h"
#include "private/lib/basic/lock.h"
#include "private/lib/debug/assert.h"
#include "private/lib/debug/print.h"

#include "private/lib/components/thread-condition.h"

typedef struct thread_condition {
	pthread_cond_t cond;
	kb_mutex_t mutex;
} * thread_condition_t;

thread_condition_t thread_condition_init() {
	thread_condition_t ret = calloc(1, sizeof(struct thread_condition));
	m_assert0(pthread_cond_init(&ret->cond, NULL), "failed init pthread condition");

	ret->mutex = lock_init();

	return ret;
}

void thread_condition_deinit(thread_condition_t *ppctx) {
	thread_condition_t ctx = *ppctx;
	if (ctx == NULL)
		return;
	*ppctx = NULL;

	pthread_cond_broadcast(&ctx->cond);
	pthread_cond_destroy(&ctx->cond);
	lock_deinit(ctx->mutex);
	free(ctx);
}

void _thread_wait_event(thread_condition_t *ppctx, test_condition test, on_condition_is_true do_something, void *state) {
	thread_condition_t ctx = *ppctx;
	if (!ctx)
		return;

	lock(ctx->mutex);
	while (!test(state)) {
		debug_trace_function("wait");
		pthread_cond_wait(&ctx->cond, raw_lock(ctx->mutex));
		debug_trace_function("wakeup");
		if (!*ppctx) {
			unlock(ctx->mutex);
			return;
		}
	}

	do_something(state);

	unlock(ctx->mutex);
}

void thread_fire_event(thread_condition_t *ppctx) {
	thread_condition_t ctx = *ppctx;
	if (!ctx)
		return;

	pthread_cond_broadcast(&ctx->cond);
}

kb_mutex_t thread_get_event_lock(thread_condition_t *ppctx) {
	thread_condition_t ctx = *ppctx;
	if (!ctx)
		return NULL;

	return ctx->mutex;
}

pthread_cond_t *thread_get_event_condition(thread_condition_t *ppctx) {
	thread_condition_t ctx = *ppctx;
	if (!ctx)
		return NULL;

	return &ctx->cond;
}
