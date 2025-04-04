#pragma once

#include "./prefix.h"
#include "./types.h"

DEFINE_START

struct kburnDebugColor {
	const char *prefix;
	const char *postfix;
};

typedef struct kburnDebugColors {
	struct kburnDebugColor red;
	struct kburnDebugColor green;
	struct kburnDebugColor yellow;
	struct kburnDebugColor grey;
} kburnDebugColors;

PUBLIC void kburnSetLogBufferEnabled(bool enable);
PUBLIC kburnDebugColors kburnSetColors(kburnDebugColors color_list);

PUBLIC on_debug_log_t kburnSetLogCallback(on_debug_log callback, void *call_context);

DEFINE_END
