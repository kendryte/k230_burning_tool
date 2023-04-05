#pragma once

#include "private/context.h"
#include "private/lib/basic/event-queue.h"

#include "private/lib/components/thread.h"
#include <pthread.h>



typedef struct event_queue_thread *event_queue_thread_t;
typedef void (*event_handler)(KBMonCTX monitor, void *event);

kburn_err_t event_thread_init(KBMonCTX monitor, const char *title, event_handler handler, event_queue_thread_t *out);
kburn_err_t event_thread_queue(event_queue_thread_t thread, void *event, bool auto_fire);
void event_thread_fire(event_queue_thread_t thread);
void event_thread_deinit(KBMonCTX monitor, event_queue_thread_t *thread);


