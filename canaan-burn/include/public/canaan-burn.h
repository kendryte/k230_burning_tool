#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "./errors.h"
#include "./prefix.h"
#include "./types.h"
#include "./debug.h"
#include "./usb.isp.h"

#if BURN_LIB_COMPILING
#include "private/context.h"
#else
#include "./context.h"
#endif

#include "./k230_burn.h"

DEFINE_START

PUBLIC kburn_err_t kburnMonitorCreate(KBMonCTX *ppCtx);
PUBLIC void kburnMonitorDestroy(KBMonCTX monitor);

PUBLIC void kburnMonitorGlobalDestroy();

PUBLIC void kburnMonitorWaitDevicePause(KBMonCTX monitor);
PUBLIC kburn_err_t kburnMonitorWaitDeviceResume(KBMonCTX monitor);
PUBLIC kburn_err_t kburnMonitorStartWaitingDevices(KBMonCTX monitor);

PUBLIC on_before_device_open_t kburnOnBeforeDeviceOpen(KBMonCTX monitor, on_before_device_open change_callback, void *ctx);

PUBLIC on_device_connect_t kburnOnDeviceConnect(KBMonCTX monitor, on_device_connect change_callback, void *ctx);

PUBLIC on_device_disconnect_t kburnOnDeviceDisconnect(KBMonCTX monitor, on_device_disconnect change_callback, void *ctx);

PUBLIC on_device_confirmed_t kburnOnDeviceConfirmed(KBMonCTX monitor, on_device_confirmed change_callback, void *ctx);

PUBLIC void kburnSetUsbFilterVid(KBMonCTX monitor, uint16_t vid);
PUBLIC uint16_t kburnGetUsbFilterVid(KBMonCTX monitor);

PUBLIC void kburnSetUsbFilterPid(KBMonCTX monitor, uint16_t pid);
PUBLIC uint16_t kburnGetUsbFilterPid(KBMonCTX monitor);

PUBLIC kburn_err_t kburnMonitorManualTrig(KBMonCTX monitor);
PUBLIC void mark_destroy_device_node(kburnDeviceNode *instance);

DEFINE_END
