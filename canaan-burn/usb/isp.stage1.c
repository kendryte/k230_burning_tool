#include "context.h"
#include "basic/disposable.h"
#include "basic/errors.h"
#include "basic/resource-tracker.h"
#include "descriptor.h"
#include "device.h"
#include "private-types.h"

#include "isp.h"

#define RETRY_TIMES				(3)
#define USB_TIMEOUT 			(5000)
#define STAGE_ADDR_MSB(addr) 	((addr) >> 16)
#define STAGE_ADDR_LSB(addr) 	((addr)&0xffff)

enum USB_KENDRYTE_REQUEST_BASIC {
	ISP_STAGE1_CMD_GET_CPU_INFO = 0,
	ISP_STAGE1_CMD_SET_DATA_ADDRESS,
	ISP_STAGE1_CMD_SET_DATA_LENGTH,
	ISP_STAGE1_CMD_FLUSH_CACHES,
	ISP_STAGE1_CMD_PROG_START
};

static bool _check_cpu_info(uint16_t id, unsigned char *info)
{
	if (0x230 == id) {
		return memcmp("K230", info, 4) == 0;
	}

	return false;
}

kburn_err_t usb_isp_stage1_handshake(kburnDeviceNode *node) {

	for(int i = 0; i < RETRY_TIMES; i++) {
		if(KBurnNoErr == usb_isp_stage1_get_cpu_info(node)) {
			debug_print(KBURN_LOG_ERROR, "stage1 handshake success, tried %d times", i + 1);

			return KBurnNoErr;
		}
	}

	return KBurnUsbProtocolWrong;
}

kburn_err_t usb_isp_stage1_get_cpu_info(kburnDeviceNode *node)
{
	debug_trace_function();

	unsigned char info[32];
	memset(info, 0, sizeof(info));

	int r = libusb_control_transfer(node->usb->handle,
									/* bmRequestType */ (uint8_t)(LIBUSB_ENDPOINT_IN | LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_DEVICE),
									/* bRequest      */ ISP_STAGE1_CMD_GET_CPU_INFO,
									/* wValue        */ (uint16_t)(((0) << 8) | 0x00),
									/* wIndex        */ 0,
									/* Data          */ info,
									/* wLength       */ 8,
									USB_TIMEOUT);

	if (r < LIBUSB_SUCCESS) {
		debug_print(KBURN_LOG_ERROR, "read cpu info failed");
		return KBurnUsbProtocolWrong;
	}

	debug_print(KBURN_LOG_TRACE, "cpu info (%d)%s", r, info);

	if (!_check_cpu_info(node->usb->deviceInfo.idProduct, info)) {
		debug_print(KBURN_LOG_ERROR, "compare cpu info failed");
		return KBurnUsbProtocolWrong;
	}

	return KBurnNoErr;
}

// kburn_err_t usb_isp_stage1_flush_cache(kburnDeviceNode *node)
// {

// }

// kburn_err_t usb_isp_stage1_set_data_addr(kburnDeviceNode *node, int addr)
// {

// }

// kburn_err_t usb_isp_stage1_set_data_length(kburnDeviceNode *node, int length)
// {

// }

// kburn_err_t usb_isp_stage1_write_data(kburnDeviceNode *node, const uint8_t *data, int size)
// {

// }
