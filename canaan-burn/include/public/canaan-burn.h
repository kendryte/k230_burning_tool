#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "./errors.h"
#include "./prefix.h"
#include "./types.h"
#include "./debug.h"

#if BURN_LIB_COMPILING
#include "private/context.h"
#else
#include "./context.h"
#endif

DEFINE_START

PUBLIC kburn_err_t kburnMonitorCreate(KBMonCTX *ppCtx);
PUBLIC void kburnMonitorDestroy(KBMonCTX monitor);

PUBLIC void kburnMonitorGlobalDestroy();

PUBLIC void kburnMonitorWaitDevicePause(KBMonCTX monitor);
PUBLIC kburn_err_t kburnMonitorWaitDeviceResume(KBMonCTX monitor);
PUBLIC kburn_err_t kburnMonitorStartWaitingDevices(KBMonCTX monitor);

DEFINE_END
