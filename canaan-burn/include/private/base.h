#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define GENERIC_LINE(type, fn) \
	type:                      \
	fn

#ifdef UNUSED
#elif defined(__GNUC__)
#define UNUSED(x) UNUSED_##x __attribute__((unused))
#elif defined(__LCLINT__)
#define UNUSED(x) /*@unused@*/ x
#else
#define UNUSED(x)
#endif

#ifndef NDEBUG
#define RELEASE_UNUSED(X) X
#else
#define RELEASE_UNUSED(X) UNUSED(X)
#endif

#define CONCAT(a, b) a##b

#define STRINGIZE(x) STRINGIZE2(x)
#define STRINGIZE2(x) #x

#define __round_mask(x, y) ((__typeof__(x))((y)-1))
#define round_up(x, y) ((((x)-1) | __round_mask(x, y))+1)
#define round_down(x, y) ((x) & ~__round_mask(x, y))

#define __VaridicMacro_Helper1(A0, A1, FN, ...) FN
#define __VaridicMacro_Helper2(A0, A1, A2, FN, ...) FN
#define __VaridicMacro_Helper3(A0, A1, A2, A3, FN, ...) FN
#define __VaridicMacro_Helper4(A0, A1, A2, A3, A4, FN, ...) FN
#define __VaridicMacro_Helper5(A0, A1, A2, A3, A4, A5, FN, ...) FN

#define SELECT(NAME, NUM) CONCAT(NAME##_, NUM)

#define GET_COUNT(_1, _2, _3, _4, _5, _6, COUNT, ...) COUNT
#define VA_SIZE(...) GET_COUNT(__VA_ARGS__ __VA_OPT__(, ) 6, 5, 4, 3, 2, 1, 0)

#define __VaridicMacro_Select(NAME, ...) \
	SELECT(NAME, VA_SIZE(__VA_ARGS__))   \
	(__VA_ARGS__)

#define __VaridicMacro_First(a, ...) a
#define __VaridicMacro_Shift(a, ...) \
	__VA_OPT__(, )                   \
	__VA_ARGS__
#define __VaridicMacro_Opt(default, ...) __VaridicMacro_First(__VA_ARGS__ __VA_OPT__(, ) default)

#include "private/lib/basic/macros.alloc.h"
#include "private/lib/basic/macros.error.h"
#include "private/lib/basic/macros.function.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcpp"
#include "private/lib/basic/macros.endian.h"
#pragma GCC diagnostic pop

#include "private/lib/basic/macros.atomic.h"
#include "private/lib/basic/macros.public_interface.h"

#if WIN32
#define FMT_SIZET "%llu"
#else
#define FMT_SIZET "%zu"
#endif

static inline uint64_t guid() {
	int rand(void);
	static uint64_t guid = 0;
	guid += rand() % 9;
	return guid;
}

#include <signal.h>

#define debugger raise(SIGABRT)
