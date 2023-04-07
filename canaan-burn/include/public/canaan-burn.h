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

PUBLIC on_device_list_change_t kburnOnDeviceListChange(KBMonCTX monitor, on_device_list_change change_callback, void *ctx);

PUBLIC on_device_connect_t kburnOnDeviceConnect(KBMonCTX monitor, on_device_connect change_callback, void *ctx);

PUBLIC on_device_disconnect_t kburnOnDeviceDisconnect(KBMonCTX monitor, on_device_disconnect change_callback, void *ctx);

PUBLIC void kburnSetUsbFilterVid(KBMonCTX monitor, uint16_t vid);
PUBLIC uint16_t kburnGetUsbFilterVid(KBMonCTX monitor);

PUBLIC void kburnSetUsbFilterPid(KBMonCTX monitor, uint16_t pid);
PUBLIC uint16_t kburnGetUsbFilterPid(KBMonCTX monitor);

DEFINE_END
