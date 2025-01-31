#include "public/debug.h"

#include "private/base.h"

#include "private/lib/debug/color.h"
#include "private/lib/debug/print.h"

kburnDebugColors g_debug_colors = {
#ifndef WIN32
	.red = {.prefix = "\x1B[38;5;9m",  .postfix = "\x1B[0m"},
	.green = {.prefix = "\x1B[38;5;10m", .postfix = "\x1B[0m"},
	.yellow = {.prefix = "\x1B[38;5;11m", .postfix = "\x1B[0m"},
	.grey = {.prefix = "\x1B[2m",       .postfix = "\x1B[0m"},
#else
	.red = {.prefix = "",  .postfix = ""},
	.green = {.prefix = "", .postfix = ""},
	.yellow = {.prefix = "", .postfix = ""},
	.grey = {.prefix = "",       .postfix = ""},
#endif
};

kburnDebugColors kburnSetColors(kburnDebugColors color_list) {
	kburnDebugColors old = g_debug_colors;
	g_debug_colors = color_list;

	return old;
}
