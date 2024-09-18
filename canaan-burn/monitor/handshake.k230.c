#include <stdlib.h>

#include <libusb.h>

#include "private/lib/debug/print.h"
#include "private/monitor/handshake.h"

#define RETRY_TIMES (3)
#define USB_TIMEOUT (5000)

static bool k230_get_cpu_info(kburnUsbDeviceNode *usb)
{
    debug_trace_function();

    unsigned char info[32];
    memset(info, 0, sizeof(info));

    int r = libusb_control_transfer(/* dev_handle    */ usb->handle,
                                    /* bmRequestType */ (uint8_t)(LIBUSB_ENDPOINT_IN | LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_DEVICE),
                                    /* bRequest      */ 0 /* ISP_STAGE1_CMD_GET_CPU_INFO */,
                                    /* wValue        */ (uint16_t)(((0) << 8) | 0x00),
                                    /* wIndex        */ 0,
                                    /* Data          */ info,
                                    /* wLength       */ sizeof(info),
                                    /* timeout       */ USB_TIMEOUT);

    if (r < LIBUSB_SUCCESS) {
        debug_print(KBURN_LOG_ERROR, "read cpu info failed");
        return false;
    }

    debug_print(KBURN_LOG_TRACE, "cpu info (%d)%s", r, info);

	if (0x00 == memcmp(info, "Uboot Stage for K230", r)) {
        usb->stage = 2;
	} else if(0x00 == memcmp(info, "K230", 4)) {
        usb->stage = 1;
    } else {
        debug_print(KBURN_LOG_ERROR, "compare cpu info failed");
        return false;
    }

    debug_print(KBURN_LOG_TRACE, "k230 in stage %d", usb->stage);

    return true;
}

bool chip_k230_handshake(kburnUsbDeviceNode *usb)
{
    for (int i = 0; i < RETRY_TIMES; i++) {
		if (true == k230_get_cpu_info(usb)) {
            debug_print(KBURN_LOG_ERROR, "k230 handshake success, tried %d times", i + 1);
            return true;
        }
    }

    return false;
}
