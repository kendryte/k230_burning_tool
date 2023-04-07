#pragma once

#include "./prefix.h"
#include "./errors.h"
#include "./types.h"
#include "./types.usb.h"

DEFINE_START

PUBLIC kburn_err_t K230BurnISP_get_cpu_info(kburnDeviceNode *node, char info[32]);

PUBLIC kburn_err_t K230BurnISP_flush_cache(kburnDeviceNode *node);

PUBLIC kburn_err_t K230BurnISP_set_data_addr(kburnDeviceNode *node, unsigned int addr);

PUBLIC kburn_err_t K230BurnISP_set_data_length(kburnDeviceNode *node, unsigned int length);

PUBLIC kburn_err_t K230BurnISP_write_data(kburnDeviceNode *node, const unsigned char *const data, unsigned int length);

PUBLIC kburn_err_t K230BurnISP_read_data(kburnDeviceNode *node, unsigned char *data, unsigned int length);

PUBLIC kburn_err_t K230BurnISP_boot_from_addr(kburnDeviceNode *node, unsigned int addr);

DEFINE_END
