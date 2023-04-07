#include <libusb.h>

#include "private/lib/debug/print.h"
#include "private/monitor/chip.h"

#define RETRY_TIMES (3)
#define USB_TIMEOUT (5000)

static inline bool _check_cpu_info(unsigned char *info)
{
    return memcmp("K230", info, 4) == 0;
}

static bool k230_get_cpu_info(kburnUsbDeviceNode *usb)
{
    debug_trace_function();

    unsigned char info[32];
    memset(info, 0, sizeof(info));

    int r = libusb_control_transfer(usb->handle,
                                    /* bmRequestType */ (uint8_t)(LIBUSB_ENDPOINT_IN | LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_DEVICE),
                                    /* bRequest      */ 0 /* ISP_STAGE1_CMD_GET_CPU_INFO */,
                                    /* wValue        */ (uint16_t)(((0) << 8) | 0x00),
                                    /* wIndex        */ 0,
                                    /* Data          */ info,
                                    /* wLength       */ 8,
                                    USB_TIMEOUT);

    if (r < LIBUSB_SUCCESS) {
        debug_print(KBURN_LOG_ERROR, "read cpu info failed");
        return false;
    }

    debug_print(KBURN_LOG_TRACE, "cpu info (%d)%s", r, info);

    if (!_check_cpu_info(info)) {
        debug_print(KBURN_LOG_ERROR, "compare cpu info failed");

        return false;
    }

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
