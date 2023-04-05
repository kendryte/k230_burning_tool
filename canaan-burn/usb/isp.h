#pragma once

#include "context.h"

kburn_err_t usb_isp_stage1_handshake(kburnDeviceNode *node);

kburn_err_t usb_isp_stage1_get_cpu_info(kburnDeviceNode *node);
