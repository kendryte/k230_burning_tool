#pragma once

#include "./prefix.h"
#include "./errors.h"
#include "./types.h"
#include "./types.usb.h"
#include "./usb.isp.h"

DEFINE_START

PUBLIC uint32_t crc32(uint32_t crc, const unsigned char *buf, uint32_t len);

PUBLIC bool K230BurnISP_WriteBurnImageConfig(kburnDeviceNode *node, const void *cfgData, size_t cfgSize, on_write_progress page_callback, void *ctx);

PUBLIC bool K230BurnISP_RunProgram(kburnDeviceNode *node, const void *programBuffer, size_t programBufferSize, on_write_progress page_callback, void *ctx);

PUBLIC bool K230Burn_Check_Alt(kburnDeviceNode *node, const char *alt);

PUBLIC uint32_t K230Burn_Get_Chunk_Size(kburnDeviceNode *node, const char *alt);

PUBLIC int K230Burn_Write_Chunk(kburnDeviceNode *node, const char *alt, unsigned short block, void *buffer, uint32_t length);

DEFINE_END
