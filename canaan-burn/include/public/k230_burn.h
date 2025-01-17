#pragma once

#include "./prefix.h"
#include "./errors.h"
#include "./types.h"
#include "./types.usb.h"
#include "./usb.isp.h"

DEFINE_START

PUBLIC uint32_t crc32(uint32_t crc, const unsigned char *buf, uint32_t len);

PUBLIC bool K230BurnISP_RunProgram(kburnDeviceNode *node, const void *programBuffer, size_t programBufferSize, on_write_progress page_callback, void *ctx);

typedef struct kburn_t kburn_t;

PUBLIC kburn_t *kburn_create(kburnDeviceNode *node);
PUBLIC void kburn_destory(kburn_t *kburn);

PUBLIC void kburn_reset_chip(kburn_t *kburn);
PUBLIC void kburn_nop(struct kburn_t *kburn);
PUBLIC bool kburn_probe(kburn_t *kburn, kburnUsbIspCommandTaget target, uint64_t *chunk_size);

PUBLIC kburnUsbIspCommandTaget kburn_get_medium_type(struct kburn_t *kburn);
PUBLIC uint64_t kburn_get_capacity(kburn_t *kburn);
PUBLIC uint64_t kburn_get_erase_size(struct kburn_t *kburn);
PUBLIC bool kburn_parse_erase_config(struct kburn_t *kburn, uint64_t *offset, uint64_t *size);
PUBLIC uint64_t kburn_get_medium_blk_size(struct kburn_t *kburn);

PUBLIC bool kburn_erase(struct kburn_t *kburn, uint64_t offset, uint64_t size, int max_retry);

PUBLIC bool kburn_write_start(struct kburn_t *kburn, uint64_t part_offset, uint64_t part_size, uint64_t file_size);
PUBLIC bool kburn_write_chunk(struct kburn_t *kburn, void *data, uint64_t size);
PUBLIC bool kbrun_write_end(struct kburn_t *kburn);

PUBLIC char *kburn_get_error_msg(kburn_t *kburn);

// PUBLIC uint64_t kburn_erase(kburn_t *kburn, uint64_t offset, uint64_t size);
// PUBLIC uint64_t kburn_read(kburn_t *kburn, void *data, uint64_t offset, uint64_t size);

DEFINE_END
