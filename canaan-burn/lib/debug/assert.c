#include "assert.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "private/lib/debug/color.h"
#include "private/lib/debug/print.h"

void m_assert_print_abort(const char *ncondition_str, const char *file, int line, const char *message, ...) {
	static char buffer[1024];

	_debug_format_prefix(buffer, 1024, file, line);
	fputs(buffer, stderr);
	fputs(COLOR_START(YELLOW), stderr);
	fputs("Assertion Failed: ", stderr);
	fputs(COLOR_END(YELLOW), stderr);
	fputs(ncondition_str, stderr);
	fputs("\n\t", stderr);

	va_list argptr;
	va_start(argptr, message);
	vfprintf(stderr, message, argptr);
	va_end(argptr);

	fputs("\n", stderr);

	abort();
}
