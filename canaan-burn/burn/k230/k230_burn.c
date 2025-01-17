#include "public/k230_burn.h"

#include "private/lib/basic/sleep.h"
#include "private/lib/debug/print.h"
#include "private/monitor/usb_types.h"

#include <inttypes.h>

#define RETRY_MAX                   (5)
#define USB_TIMEOUT                 (2000)
#define CMD_FLAG_DEV_TO_HOST        (0x8000)

enum kburn_pkt_cmd {
    KBURN_CMD_NONE = 0,
    KBURN_CMD_REBOOT = 0x01,

    KBURN_CMD_DEV_PROBE = 0x10,
    KBURN_CMD_DEV_GET_INFO = 0x11,

	KBURN_CMD_ERASE_LBA = 0x20,

	KBURN_CMD_WRITE_LBA = 0x21,
	KBURN_CMD_WRITE_LBA_CHUNK = 0x22,

  KBURN_CMD_MAX,
};

enum kburn_pkt_result {
    KBURN_RESULT_NONE = 0,

    KBURN_RESULT_OK = 1,
    KBURN_RESULT_ERROR = 2,

    KBURN_RESULT_ERROR_MSG = 0xFF,

    KBURN_RESULT_MAX,
};

#define KBUNR_USB_PKT_SIZE	(60)

#pragma pack(push,1)

struct kburn_usb_pkt {
    uint16_t cmd;
    uint16_t result; /* only valid in csw */
    uint16_t data_size;
};

struct kburn_usb_pkt_wrap {
    struct kburn_usb_pkt hdr;
    uint8_t data[KBUNR_USB_PKT_SIZE - sizeof(struct kburn_usb_pkt)];
};

#pragma pack(pop)

struct kburn_medium_info {
    uint64_t capacity;
    uint64_t blk_size;
    uint64_t erase_size;
    uint64_t timeout_ms:32;
    uint64_t wp:8;
    uint64_t type:7;
    uint64_t valid:1;
};

struct kburn_t {
    kburnDeviceNode *node;

    struct kburn_medium_info medium_info;

    char error_msg[128];

    int ep_in, ep_out;
    uint16_t ep_out_mps;
    uint64_t capacity;
    uint64_t dl_total, dl_size, dl_offset;
};

static int __get_endpoint(kburn_t *kburn)
{
	const struct libusb_interface_descriptor *setting;
	const struct libusb_endpoint_descriptor *ep;
	struct libusb_config_descriptor *config;
	const struct libusb_interface *intf;
	int if_idx, set_idx, ep_idx, ret;
	struct libusb_device *udev;
	uint8_t trans, dir;

	udev = libusb_get_device(kburn->node->usb->handle);

	ret = libusb_get_active_config_descriptor(udev, &config);
	if (ret)
		return ret;

	for (if_idx = 0; if_idx < config->bNumInterfaces; if_idx++) {
		intf = config->interface + if_idx;

		for (set_idx = 0; set_idx < intf->num_altsetting; set_idx++) {
			setting = intf->altsetting + set_idx;

			for (ep_idx = 0; ep_idx < setting->bNumEndpoints;
				 ep_idx++) {
				ep = setting->endpoint + ep_idx;
				trans = (ep->bmAttributes &
						 LIBUSB_TRANSFER_TYPE_MASK);
				if (trans != LIBUSB_TRANSFER_TYPE_BULK)
					continue;

				dir = (ep->bEndpointAddress &
					   LIBUSB_ENDPOINT_DIR_MASK);
				if (dir == LIBUSB_ENDPOINT_IN) {
					kburn->ep_in = ep->bEndpointAddress;
                } else {
					kburn->ep_out = ep->bEndpointAddress;
					kburn->ep_out_mps = ep->wMaxPacketSize;
                }
			}
		}
	}

	libusb_free_config_descriptor(config);

	return LIBUSB_SUCCESS;
}

static int kburn_write_data(kburn_t *kburn, void *data, int length)
{
    int rc = -1, size = 0;

    kburnDeviceNode *node = kburn->node;

    // if(length <= 64) {
    //     print_buffer(KBURN_LOG_ERROR, "usb write", data, length);
    // }

    rc = libusb_bulk_transfer(/* dev_handle       */ node->usb->handle,
                             /* endpoint         */ kburn->ep_out,
                             /* bulk data        */ data,
                             /* bulk data length */ length,
                             /* transferred      */ &size,
                             /* timeout          */ kburn->medium_info.timeout_ms);

    if ((rc != LIBUSB_SUCCESS) || (size != length)) {
        debug_print(KBURN_LOG_ERROR, "Error - can't write bulk data, error %s, length %d, transfered %d", \
            libusb_strerror((enum libusb_error)rc), length, size);
        return KBrunUsbCommError;
    }

    if(0x00 == (length % kburn->ep_out_mps)) {
        if(LIBUSB_SUCCESS != (rc = libusb_bulk_transfer(node->usb->handle, kburn->ep_out, data, 0, &size, kburn->medium_info.timeout_ms))) {
            debug_print(KBURN_LOG_ERROR, "Error - can't write bulk data ZLP, error %s", libusb_strerror((enum libusb_error)rc));
        }
    }

    return KBurnNoErr;
}

static int kburn_read_data(kburn_t *kburn, void *data, int length, int *is_timeout)
{
    int rc = -1, size = 0;

    kburnDeviceNode *node = kburn->node;

    rc = libusb_bulk_transfer(/* dev_handle       */ node->usb->handle,
                             /* endpoint         */ kburn->ep_in,
                             /* bulk data        */ data,
                             /* bulk data length */ length,
                             /* transferred      */ &size,
                             /* timeout          */ kburn->medium_info.timeout_ms);

    if(is_timeout && (LIBUSB_ERROR_TIMEOUT == rc)) {
        *is_timeout = rc;
    }

    if ((rc != LIBUSB_SUCCESS) || (size != length)) {
        debug_print(KBURN_LOG_ERROR, "Error - can't recv bulk data, error %s, length %d, transfered %d", \
            libusb_strerror((enum libusb_error)rc), length, size);
        return KBrunUsbCommError;
    }

    // if(length <= 64) {
    //     print_buffer(KBURN_LOG_ERROR, "usb recv", data, length);
    // }

    return KBurnNoErr;
}

static int kburn_parse_resp(struct kburn_usb_pkt_wrap *csw, kburn_t *kburn, enum kburn_pkt_cmd cmd, void *result, int *result_size)
{
    if(csw->hdr.cmd != (cmd | CMD_FLAG_DEV_TO_HOST)) {
        debug_print(KBURN_LOG_ERROR, "command recv error resp cmd");
        strncpy(kburn->error_msg, "cmd recv resp error", sizeof(kburn->error_msg));
        return KBrunUsbCommError;
    }

    if(KBURN_RESULT_OK != csw->hdr.result) {
        debug_print(KBURN_LOG_ERROR, "command recv error resp result");
        strncpy(kburn->error_msg, "cmd recv resp error", sizeof(kburn->error_msg));

        if(KBURN_RESULT_ERROR_MSG == csw->hdr.result) {
            csw->data[csw->hdr.data_size] = 0;

            debug_print(KBURN_LOG_ERROR, "command recv error resp, error msg %s", csw->data);
            strncpy(kburn->error_msg, (char *)csw->data, sizeof(kburn->error_msg));
        }

        return KBrunUsbCommError;
    }

    if((NULL == result) || (NULL == result_size) || (0x00 == *result_size)) {
        debug_print(KBURN_LOG_TRACE, "user ignore result data");

        return KBurnNoErr;
    }

    if(csw->hdr.data_size > *result_size) {
        debug_print(KBURN_LOG_ERROR, "command result buffer too small, %d > %d", csw->hdr.data_size, *result_size);

        csw->hdr.data_size = *result_size;
    }

    *result_size = csw->hdr.data_size;
    memcpy(result, &csw->data[0], csw->hdr.data_size);

    return KBurnNoErr;
}

static int kburn_send_cmd(kburn_t *kburn, enum kburn_pkt_cmd cmd, void *data, int size, void *result, int *result_size)
{
    struct kburn_usb_pkt_wrap cbw, csw;

    memset(&cbw, 0, sizeof(cbw));
    memset(&csw, 0, sizeof(csw));

    if(size > (int)sizeof(cbw.data)) {
        debug_print(KBURN_LOG_ERROR, "command data size too large %d", size);
        return KBurnWiredError;
    }

    cbw.hdr.cmd = cmd;
    cbw.hdr.data_size = size;
    if((0x00 != size) && (NULL != data)) {
        memcpy(&cbw.data[0], data, size);
    }

    if(KBurnNoErr != kburn_write_data(kburn, &cbw, sizeof(cbw))) {
        debug_print(KBURN_LOG_ERROR, "command send data failed");
        strncpy(kburn->error_msg, "cmd send failed", sizeof(kburn->error_msg));

        return KBrunUsbCommError;
    }

    if(KBurnNoErr != kburn_read_data(kburn, &csw, sizeof(csw), NULL)) {
        debug_print(KBURN_LOG_ERROR, "command recv data failed");
        strncpy(kburn->error_msg, "cmd recv failed", sizeof(kburn->error_msg));
        return KBrunUsbCommError;
    }

    return kburn_parse_resp(&csw, kburn, cmd, result, result_size);
}

void kburn_nop(struct kburn_t *kburn)
{
    debug_print(KBURN_LOG_DEBUG, "issue a nop command, clear device error status");

    // issue a command, clear device state
    kburn_send_cmd(kburn, KBURN_CMD_NONE, NULL, 0, NULL, NULL);
}

kburnUsbIspCommandTaget kburn_get_medium_type(struct kburn_t *kburn)
{
    if(kburn->medium_info.valid) {
        return kburn->medium_info.type;
    }
    debug_print(KBURN_LOG_ERROR, "unknown meidum type");

    return 0;
}

uint64_t kburn_get_erase_size(struct kburn_t *kburn)
{
    if(kburn->medium_info.valid) {
        return kburn->medium_info.erase_size;
    }
    debug_print(KBURN_LOG_ERROR, "unknown meidum type");

    return 0;
}

uint64_t kburn_get_medium_blk_size(struct kburn_t *kburn)
{
    if(kburn->medium_info.valid) {
        return kburn->medium_info.blk_size;
    }
    debug_print(KBURN_LOG_ERROR, "unknown meidum type");

    return 0;
}

bool kburn_parse_erase_config(struct kburn_t *kburn, uint64_t *offset, uint64_t *size)
{
    uint64_t o, s;
    
    o = *offset;
    s = *size;

    if((o + s) > kburn->medium_info.capacity) {
        return false;
    }

    o = round_up(o, kburn->medium_info.erase_size);
    s = round_down(s, kburn->medium_info.erase_size);

    *offset = o;
    *size = s;

    return true;
}

kburn_t *kburn_create(kburnDeviceNode *node)
{
    kburn_t *kburn = (kburn_t *)malloc(sizeof(struct kburn_t));

    if(kburn) {
        memset(kburn, 0, sizeof(*kburn));
        kburn->node = node;
        kburn->medium_info.timeout_ms = 1000; // default 1s

        if(LIBUSB_SUCCESS != __get_endpoint(kburn)) {
            debug_print(KBURN_LOG_ERROR, "kburn get ep failed");

            free(kburn);
            kburn = NULL;
        }
    }

    return kburn;
}

void kburn_destory(kburn_t *kburn)
{
    if(kburn) {
        free(kburn);
    }
}

char *kburn_get_error_msg(kburn_t *kburn)
{
    return kburn->error_msg;
}

void kburn_reset_chip(kburn_t *kburn)
{
#define REBOOT_MARK 	(0x52626F74)

    struct kburn_usb_pkt_wrap cbw;
    const uint64_t reboot_mark = REBOOT_MARK;

    cbw.hdr.cmd = KBURN_CMD_REBOOT;
    cbw.hdr.data_size = sizeof(uint64_t);

    memcpy(&cbw.data[0], &reboot_mark, sizeof(uint64_t));

    if(KBurnNoErr != kburn_write_data(kburn, &cbw, sizeof(cbw))) {
        debug_print(KBURN_LOG_ERROR, "command send data failed");
        strncpy(kburn->error_msg, "cmd send failed", sizeof(kburn->error_msg));
    }
}

bool kburn_probe(kburn_t *kburn, kburnUsbIspCommandTaget target, uint64_t *chunk_size)
{
    uint8_t data[2];
    uint64_t result[2];
    int result_size = sizeof(result);

    data[0] = target;
    data[1] = 0xFF;

    debug_print(KBURN_LOG_TRACE, "probe target %d", target);

    if (KBurnNoErr != kburn_send_cmd(kburn, KBURN_CMD_DEV_PROBE, data, 2, &result[0], &result_size)) {
        debug_print(KBURN_LOG_ERROR, "kburn probe medium failed");
        return false;
    }

    if(result_size != sizeof(result)) {
        debug_print(KBURN_LOG_ERROR, "kburn probe medium failed, get result size error");
        return false;
    }

    debug_print(KBURN_LOG_INFO, "kburn probe, out chunksize %" PRId64 ", in chunksize %" PRId64 "\n", result[0], result[1]);

    if(chunk_size) {
        *chunk_size = result[0];
    }

    return true;
}

uint64_t kburn_get_capacity(kburn_t *kburn)
{
    struct kburn_medium_info info;
    int info_size = sizeof(info);

    if (KBurnNoErr != kburn_send_cmd(kburn, KBURN_CMD_DEV_GET_INFO, NULL, 0, &info, &info_size)) {
        debug_print(KBURN_LOG_ERROR, "kburn get medium info failed");
        return 0;
    }

    if(info_size != sizeof(info)) {
        debug_print(KBURN_LOG_ERROR, "kburn get medium info error result size. %d != " FMT_SIZET "\n", info_size, sizeof(info));
        return 0;
    }

    memcpy(&kburn->medium_info, &info, sizeof(info));

    debug_print(KBURN_LOG_INFO, "medium info, capacty %" PRId64 ", blk_sz %" PRId64 ", erase_size %" PRId64 ", write protect %d", \
        info.capacity, info.blk_size, info.erase_size, info.wp);

    return info.capacity;
}

bool kburn_erase(struct kburn_t *kburn, uint64_t offset, uint64_t size, int max_retry)
{
    struct kburn_usb_pkt_wrap cbw, csw;
    int retry_times = 0;
    int is_timeout = 0;

    uint64_t cfg[2] = {offset, size};

    debug_print(KBURN_LOG_INFO, "kburn erase medium, offset %" PRId64 ", size %" PRId64 "\n", offset, size);

    if((offset + size) > kburn->medium_info.capacity) {
        debug_print(KBURN_LOG_ERROR, "kburn erase medium exceed");
        strncpy(kburn->error_msg, "kburn erase medium exceed", sizeof(kburn->error_msg));
        return false;
    }

    if(0x01 == kburn->medium_info.wp) {
        debug_print(KBURN_LOG_ERROR, "kburn erase medium failed, wp enabled");
        strncpy(kburn->error_msg, "kburn erase medium failed, wp enabled", sizeof(kburn->error_msg));
        return false;
    }

    /*************************************************************************/

    memset(&cbw, 0, sizeof(cbw));
    memset(&csw, 0, sizeof(csw));

    cbw.hdr.cmd = KBURN_CMD_ERASE_LBA;
    cbw.hdr.data_size = sizeof(cfg);
    memcpy(&cbw.data[0], &cfg[0], sizeof(cfg));

    if(KBurnNoErr != kburn_write_data(kburn, &cbw, sizeof(cbw))) {
        debug_print(KBURN_LOG_ERROR, "command send data failed");
        strncpy(kburn->error_msg, "cmd send failed", sizeof(kburn->error_msg));

        return KBrunUsbCommError;
    }

    do {
        is_timeout = 0;

        if(KBurnNoErr == kburn_read_data(kburn, &csw, sizeof(cbw), &is_timeout)) {
            break;
        }

        if(LIBUSB_ERROR_TIMEOUT != is_timeout) {
            debug_print(KBURN_LOG_INFO, "kburn erase medium read resp failed");
            return false;
        }

        do_sleep(5000);
    } while((retry_times++) < max_retry);

    return KBurnNoErr == kburn_parse_resp(&csw, kburn, KBURN_CMD_ERASE_LBA, NULL, NULL);
}

bool kburn_write_start(struct kburn_t *kburn, uint64_t part_offset, uint64_t part_size, uint64_t file_size)
{
    uint64_t cfg[3] = {part_offset, file_size, part_size};

    if((part_offset + part_size) > kburn->medium_info.capacity) {
        debug_print(KBURN_LOG_ERROR, "kburn write medium exceed");
        strncpy(kburn->error_msg, "kburn write medium exceed", sizeof(kburn->error_msg));
        return false;
    }

    if(0x01 == kburn->medium_info.wp) {
        debug_print(KBURN_LOG_ERROR, "kburn write medium failed, wp enabled");
        strncpy(kburn->error_msg, "kburn write medium failed, wp enabled", sizeof(kburn->error_msg));
        return false;
    }

    if (KBurnNoErr != kburn_send_cmd(kburn, KBURN_CMD_WRITE_LBA, &cfg[0], sizeof(cfg), NULL, NULL)) {
        debug_print(KBURN_LOG_ERROR, "kburn write medium cfg failed");
        return false;
    }

    debug_print(KBURN_LOG_INFO, "kburn write medium cfg succ");

    return true;
}

bool kburn_write_chunk(struct kburn_t *kburn, void *data, uint64_t size)
{
    struct kburn_usb_pkt_wrap csw;

    if(KBurnNoErr == kburn_write_data(kburn, data, size)) {
        return true;
    }

    debug_print(KBURN_LOG_ERROR, "kburn write medium chunk failed,");

    if(KBurnNoErr != kburn_read_data(kburn, &csw, sizeof(csw), NULL)) {
        debug_print(KBURN_LOG_ERROR, "kburn write medium chunk failed, recv error msg failed too.");

        return false;
    }

    if(KBURN_RESULT_ERROR_MSG == csw.hdr.result) {
        csw.data[csw.hdr.data_size] = 0;

        debug_print(KBURN_LOG_ERROR, "command recv error resp, error msg %s", csw.data);
        strncpy(kburn->error_msg, (char *)csw.data, sizeof(kburn->error_msg));
    }

    return false;
}

bool kbrun_write_end(struct kburn_t *kburn)
{
    struct kburn_usb_pkt_wrap csw;

    if(KBurnNoErr != kburn_read_data(kburn, &csw, sizeof(csw), NULL)) {
        debug_print(KBURN_LOG_ERROR, "kburn write medium end, recv error msg failed.");

        return false;
    }

    if(csw.hdr.cmd != (KBURN_CMD_WRITE_LBA | CMD_FLAG_DEV_TO_HOST)) {
        debug_print(KBURN_LOG_ERROR, "kburn write medium end, resp cmd error.");
        strncpy(kburn->error_msg, "kburn write medium end, resp cmd error.", sizeof(kburn->error_msg));

        return false;
    }

    if(KBURN_RESULT_OK != csw.hdr.result) {
        debug_print(KBURN_LOG_ERROR, "command recv error resp result");
        strncpy(kburn->error_msg, "cmd recv resp error", sizeof(kburn->error_msg));

        if(KBURN_RESULT_ERROR_MSG == csw.hdr.result) {
            csw.data[csw.hdr.data_size] = 0;

            debug_print(KBURN_LOG_ERROR, "command recv error resp, error msg %s", csw.data);
            strncpy(kburn->error_msg, (char *)csw.data, sizeof(kburn->error_msg));
        }

        return false;
    }

    debug_print(KBURN_LOG_INFO, "write end, resp msg %s", csw.data);

    kburn_nop(kburn);

    return true;
}

// uint64_t kburn_erase(struct kburn_t *kburn, uint64_t offset, uint64_t size)
// {
//     return 0;
// }

// uint64_t kburn_read(struct kburn_t *kburn, void *data, uint64_t offset, uint64_t size)
// {
//     return 0;
// }
