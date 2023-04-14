#pragma once

#include "./prefix.h"
#include "./errors.h"
#include "./types.h"
#include "./types.usb.h"

DEFINE_START

PUBLIC size_t K230BurnISP_LoaderSize(void);

PUBLIC bool K230BurnISP_LoaderRun(kburnDeviceNode *node, on_write_progress page_callback, void *ctx);

DEFINE_END
