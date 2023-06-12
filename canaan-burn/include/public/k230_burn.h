#pragma once

#include "./prefix.h"
#include "./errors.h"
#include "./types.h"
#include "./types.usb.h"
#include "./usb.isp.h"

DEFINE_START

PUBLIC size_t K230BurnISP_LoaderSize(kburnUsbIspCommandTaget target);

PUBLIC bool K230BurnISP_LoaderRun(kburnDeviceNode *node, kburnUsbIspCommandTaget target, on_write_progress page_callback, void *ctx);

PUBLIC bool K230Burn_Check_Alt(kburnDeviceNode *node, const char *alt);

PUBLIC uint32_t K230Burn_Get_Chunk_Size(kburnDeviceNode *node, const char *alt);

PUBLIC int K230Burn_Write_Chunk(kburnDeviceNode *node, const char *alt, unsigned short block, void *buffer, uint32_t length);

DEFINE_END
