#include "public/k230_burn.h"

#include "private/monitor/usb_types.h"

#define RETRY_MAX                   (5)
#define USB_TIMEOUT                 (5000)
#define KENDRYTE_OUT_ENDPOINT       (0x01)
#define KENDRYTE_IN_ENDPOINT        (0x81)
#define U32_HIGH_U16(addr)          ((addr) >> 16)
#define U32_LOW_U16(addr)           ((addr)&0xffff)

enum USB_KENDRYTE_REQUEST_BASIC {
    EP0_GET_CPU_INFO = 0,
    EP0_SET_DATA_ADDRESS,
    EP0_SET_DATA_LENGTH,
    EP0_FLUSH_CACHES,
    EP0_PROG_START
};

kburn_err_t K230BurnISP_get_cpu_info(kburnDeviceNode *node, char info[32])
{
    memset(info, 0, 32);

    int r = libusb_control_transfer(/* dev_handle    */ node->usb->handle,
                                    /* bmRequestType */ (uint8_t)(LIBUSB_ENDPOINT_IN | LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_DEVICE),
                                    /* bRequest      */ 0 /* ISP_STAGE1_CMD_GET_CPU_INFO */,
                                    /* wValue        */ (uint16_t)(((0) << 8) | 0x00),
                                    /* wIndex        */ 0,
                                    /* Data          */ info,
                                    /* wLength       */ 8,
                                    /* timeout       */ USB_TIMEOUT);

    if (r < LIBUSB_SUCCESS) {
        debug_print(KBURN_LOG_ERROR, "read cpu info failed");
        return KBrunUsbCommError;
    }

    return KBurnNoErr;
}

kburn_err_t K230BurnISP_flush_cache(kburnDeviceNode *node)
{
    int r = libusb_control_transfer(node->usb->handle,
                                    /* bmRequestType */ (uint8_t)(LIBUSB_ENDPOINT_OUT | LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_DEVICE),
                                    /* bRequest      */ EP0_FLUSH_CACHES,
                                    /* wValue        */ 0,
                                    /* wIndex        */ 0,
                                    /* Data          */ 0,
                                    /* wLength       */ 0,
                                    USB_TIMEOUT);

    if (r < LIBUSB_SUCCESS) {
        debug_print(KBURN_LOG_ERROR, "Error - can't flush cache: %i\n", r);
        return KBrunUsbCommError;
    }

    return KBurnNoErr;
}

kburn_err_t K230BurnISP_set_data_addr(kburnDeviceNode *node, unsigned int addr)
{
    int r = libusb_control_transfer(/* dev_handle    */ node->usb->handle,
                                    /* bmRequestType */ LIBUSB_ENDPOINT_OUT | LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_DEVICE,
                                    /* bRequest      */ EP0_SET_DATA_ADDRESS,
                                    /* wValue        */ U32_HIGH_U16(stage_addr),
                                    /* wIndex        */ U32_LOW_U16(stage_addr),
                                    /* Data          */ 0,
                                    /* wLength       */ 0,
                                    /* timeout       */ USB_TIMEOUT);

    if (r < LIBUSB_SUCCESS) {
        debug_print(KBURN_LOG_ERROR, "Error - can't set the address on kendryte device: %i\n", r);
        return KBrunUsbCommError;
    }

    return KBurnNoErr;
}

kburn_err_t K230BurnISP_set_data_length(kburnDeviceNode *node, unsigned int length)
{
    /* tell the device the length of the file to be uploaded */
    int r = = libusb_control_transfer(/* dev_handle    */ node->usb->handle,
                                      /* bmRequestType */ (uint8_t)(LIBUSB_ENDPOINT_OUT | LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_DEVICE),
                                      /* bRequest      */ EP0_SET_DATA_LENGTH,
                                      /* wValue        */ STAGE_ADDR_MSB(len),
                                      /* wIndex        */ STAGE_ADDR_LSB(len),
                                      /* Data          */ 0,
                                      /* wLength       */ 0,
                                      /* timeout       */ USB_TIMEOUT);

    if (r < LIBUSB_SUCCESS) {
        debug_print(KBURN_LOG_ERROR, "Error - can't set data length on kendryte device: %i\n", r);
        return KBrunUsbCommError;
    }

    return KBurnNoErr;
}

kburn_err_t K230BurnISP_write_data(kburnDeviceNode *node, const unsigned char *const data, unsigned int length)
{
    int r = -1, size = 0, retry = 0;

    do {
        retry ++;

        r = libusb_bulk_transfer(/* dev_handle       */ node->usb->handle,
                                 /* endpoint         */ KENDRYTE_OUT_ENDPOINT,
                                 /* bulk data        */ data,
                                 /* bulk data length */ length,
                                 /* transferred      */ &size,
                                 /* timeout          */ USB_TIMEOUT);

        if (r == LIBUSB_ERROR_PIPE) {
            libusb_clear_halt(node->usb->handle, KENDRYTE_OUT_ENDPOINT);
        }
    } while ((r == LIBUSB_ERROR_PIPE) && (retry < RETRY_MAX));

    if (r != LIBUSB_SUCCESS) {
        debug_print(KBURN_LOG_ERROR, "Error - can't send bulk data to kendryte CPU: %s", libusb_strerror((enum libusb_error)r));
        return KBrunUsbCommError;
    }

    if (size != length) {
        debug_print(KBURN_LOG_ERROR, "Error - write data length not equal %d != %d", size, length);
        return KBrunUsbCommError;
    }

    return KBurnNoErr;
}

kburn_err_t K230BurnISP_read_data(kburnDeviceNode *node, unsigned char *data, unsigned int length)
{
    int r = -1, size = 0, retry = 0;

    do {
        retry ++;

        r = libusb_bulk_transfer(/* dev_handle       */ node->usb->handle,
                                 /* endpoint         */ KENDRYTE_IN_ENDPOINT,
                                 /* bulk data        */ data,
                                 /* bulk data length */ length,
                                 /* transferred      */ &size,
                                 /* timeout          */ USB_TIMEOUT);

        if (r == LIBUSB_ERROR_PIPE) {
            libusb_clear_halt(node->usb->handle, KENDRYTE_IN_ENDPOINT);
        }
    } while ((r == LIBUSB_ERROR_PIPE) && (retry < RETRY_MAX));

    if (r != LIBUSB_SUCCESS) {
        debug_print(KBURN_LOG_ERROR, "Error - can't read bulk data to kendryte CPU: %s", libusb_strerror((enum libusb_error)r));
        return KBrunUsbCommError;
    }

    if (size != length) {
        debug_print(KBURN_LOG_ERROR, "Error - read data length not equal %d != %d", size, length);
        return KBrunUsbCommError;
    }

    return KBurnNoErr;
}

kburn_err_t K230BurnISP_boot_from_addr(kburnDeviceNode *node, unsigned int addr)
{
    int r = = libusb_control_transfer(/* dev_handle    */ node->usb->handle,
                                      /* bmRequestType */ (uint8_t)(LIBUSB_ENDPOINT_OUT | LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_DEVICE),
                                      /* bRequest      */ rqst,
                                      /* wValue        */ STAGE_ADDR_MSB(stage_addr),
                                      /* wIndex        */ STAGE_ADDR_LSB(stage_addr),
                                      /* Data          */ 0,
                                      /* wLength       */ 0,
                                      /* timeout       */ USB_TIMEOUT);

    if (r < LIBUSB_SUCCESS) {
        debug_print(KBURN_LOG_ERROR, "Error - can't start the uploaded binary on the kendryte device: %i\n", r);
        return KBrunUsbCommError;
    }

    return KBurnNoErr;
}
