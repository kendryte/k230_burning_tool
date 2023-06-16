#include "public/k230_burn.h"

#include "private/lib/basic/sleep.h"
#include "private/monitor/usb_types.h"

#define RETRY_MAX                   (5)
#define USB_TIMEOUT                 (5000)
#define KENDRYTE_OUT_ENDPOINT       (0x01)
#define KENDRYTE_IN_ENDPOINT        (0x81)
#define U32_HIGH_U16(addr)          ((addr) >> 16)
#define U32_LOW_U16(addr)           ((addr)&0xffff)

enum USB_KENDRYTE_REQUEST_BASIC {
    EP0_GET_CPU_INFO        = 0,
    EP0_SET_DATA_ADDRESS    = 1,
    EP0_SET_DATA_LENGTH     = 2,
    // EP0_FLUSH_CACHES        = 3, // may not support ?
    EP0_PROG_START          = 4,
};

static kburn_err_t K230BurnISP_get_cpu_info(kburnDeviceNode *node, unsigned char info[32])
{
    memset(info, 0, 32);

    int r = libusb_control_transfer(/* dev_handle    */ node->usb->handle,
                                    /* bmRequestType */ (uint8_t)(LIBUSB_ENDPOINT_IN | LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_DEVICE),
                                    /* bRequest      */ EP0_GET_CPU_INFO,
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

static kburn_err_t K230BurnISP_set_data_addr(kburnDeviceNode *node, uint32_t addr)
{
    int r = libusb_control_transfer(/* dev_handle    */ node->usb->handle,
                                    /* bmRequestType */ LIBUSB_ENDPOINT_OUT | LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_DEVICE,
                                    /* bRequest      */ EP0_SET_DATA_ADDRESS,
                                    /* wValue        */ U32_HIGH_U16(addr),
                                    /* wIndex        */ U32_LOW_U16(addr),
                                    /* Data          */ 0,
                                    /* wLength       */ 0,
                                    /* timeout       */ USB_TIMEOUT);

    if (r < LIBUSB_SUCCESS) {
        debug_print(KBURN_LOG_ERROR, "Error - can't set the address on kendryte device: %i\n", r);
        return KBrunUsbCommError;
    }

    return KBurnNoErr;
}

// static kburn_err_t K230BurnISP_set_data_length(kburnDeviceNode *node, uint32_t length)
// {
//     /* tell the device the length of the file to be uploaded */
//     int r = libusb_control_transfer(/* dev_handle    */ node->usb->handle,
//                                       /* bmRequestType */ (uint8_t)(LIBUSB_ENDPOINT_OUT | LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_DEVICE),
//                                       /* bRequest      */ EP0_SET_DATA_LENGTH,
//                                       /* wValue        */ U32_HIGH_U16(length),
//                                       /* wIndex        */ U32_LOW_U16(length),
//                                       /* Data          */ 0,
//                                       /* wLength       */ 0,
//                                       /* timeout       */ USB_TIMEOUT);

//     if (r < LIBUSB_SUCCESS) {
//         debug_print(KBURN_LOG_ERROR, "Error - can't set data length on kendryte device: %i\n", r);
//         return KBrunUsbCommError;
//     }

//     return KBurnNoErr;
// }

static kburn_err_t K230BurnISP_boot_from_addr(kburnDeviceNode *node, uint32_t addr)
{
    int r = libusb_control_transfer(/* dev_handle    */ node->usb->handle,
                                      /* bmRequestType */ (uint8_t)(LIBUSB_ENDPOINT_OUT | LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_DEVICE),
                                      /* bRequest      */ EP0_PROG_START,
                                      /* wValue        */ U32_HIGH_U16(addr),
                                      /* wIndex        */ U32_LOW_U16(addr),
                                      /* Data          */ 0,
                                      /* wLength       */ 0,
                                      /* timeout       */ USB_TIMEOUT);

    if (r < LIBUSB_SUCCESS) {
        debug_print(KBURN_LOG_ERROR, "Error - can't start the uploaded binary on the kendryte device: %i\n", r);
        return KBrunUsbCommError;
    }

    return KBurnNoErr;
}

static kburn_err_t K230BurnISP_write_data(kburnDeviceNode *node, unsigned char *data, int length)
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

kburn_err_t K230BurnISP_read_data(kburnDeviceNode *node, unsigned char *data, int length)
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

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define K230_SRAM_CFG_ADDR          (0x80250000)
#define K230_SRAM_APP_ADDR          (0x80300000)
#define K230_SRAM_PAGE_SIZE         (1024)

bool K230BurnISP_Greeting(kburnDeviceNode *node) {
    unsigned char info[32];

    if(KBurnNoErr != K230BurnISP_get_cpu_info(node, &info[0])) {
        return false;
    }

    if(0x00 != memcmp(info, "K230", 4)) {
        return false;
    }

    return true;
}

bool K230BurnISP_WriteMemory(kburnDeviceNode *node, const uint8_t *data, const size_t data_size, const uint32_t address, on_write_progress cb, void *ctx) {
#if 1
	const uint32_t pages = (data_size + K230_SRAM_PAGE_SIZE - 1) / K230_SRAM_PAGE_SIZE;
	debug_trace_function("base=0x%X, size=" FMT_SIZET ", pages=%d", address, data_size, pages);

    uint32_t chunk_addr = address;
    uint32_t chunk_size = K230_SRAM_PAGE_SIZE;
    uint8_t chunk_data[K230_SRAM_PAGE_SIZE];

	if (cb) {
		cb(ctx, node, 0, data_size);
	}

    if(KBurnNoErr != K230BurnISP_set_data_addr(node, chunk_addr)) {
        debug_print(KBURN_LOG_ERROR, COLOR_FMT("set write data addr failed"), RED);
        return false;
    }

	for (uint32_t page = 0; page < pages; page++) {
		uint32_t offset = page * K230_SRAM_PAGE_SIZE;
		chunk_addr = address + offset;
		if (offset + K230_SRAM_PAGE_SIZE > data_size) {
			chunk_size = data_size % K230_SRAM_PAGE_SIZE;
		} else {
			chunk_size = K230_SRAM_PAGE_SIZE;
		}

		// debug_print(KBURN_LOG_TRACE, "[mem write]: page=%u [0x%X], size=%u, offset=%u", page, chunk_addr, chunk_size, offset);

		memcpy(chunk_data, data + offset, chunk_size);

        // if(KBurnNoErr != K230BurnISP_set_data_addr(node, chunk_addr)) {
        //     debug_print(KBURN_LOG_ERROR, COLOR_FMT("set write data addr failed"), RED);
        //     return false;
        // }

        // if(KBurnNoErr != K230BurnISP_set_data_length(node, chunk_size)) {
        //     debug_print(KBURN_LOG_ERROR, COLOR_FMT("set write data lenght failed"), RED);
        //     return false;
        // }

        if(KBurnNoErr != K230BurnISP_write_data(node, chunk_data, chunk_size)) {
            debug_print(KBURN_LOG_ERROR, COLOR_FMT("write data failed"), RED);
            return false;
        }

		if (cb) {
			cb(ctx, node, page * K230_SRAM_PAGE_SIZE + chunk_size, data_size);
		}
	}
#else
    (void)ctx;
    (void)cb;

	if (cb) {
		cb(ctx, node, 0, data_size);
	}

    if(KBurnNoErr != K230BurnISP_set_data_addr(node, address)) {
        debug_print(KBURN_LOG_ERROR, COLOR_FMT("set write data addr failed"), RED);
        return false;
    }

    if(KBurnNoErr != K230BurnISP_write_data(node, (unsigned char *)data, data_size)) {
        debug_print(KBURN_LOG_ERROR, COLOR_FMT("write data failed"), RED);
        return false;
    }

	if (cb) {
		cb(ctx, node, data_size, data_size);
	}

#endif

	return true;
}

bool K230BurnISP_RunProgram(kburnDeviceNode *node, const void *programBuffer, size_t programBufferSize, on_write_progress page_callback, void *ctx) {
	debug_trace_function("node[0x%p]", (void *)node);

	if (!K230BurnISP_Greeting(node)) {
        debug_print(KBURN_LOG_ERROR, COLOR_FMT("greeting failed"), RED);

		return false;
	}

	do_sleep(10);
	if (!K230BurnISP_WriteMemory(node, (const uint8_t *)programBuffer, programBufferSize, K230_SRAM_APP_ADDR, page_callback, ctx)) {
        debug_print(KBURN_LOG_ERROR, COLOR_FMT("Write data to sram failed"), RED);

		return false;
	}

	do_sleep(100);
	if (!K230BurnISP_Greeting(node)) {
		debug_print(KBURN_LOG_ERROR, COLOR_FMT("	re-greeting failed..."), RED);
		return false;
	}

	do_sleep(10);
    if(KBurnNoErr != K230BurnISP_boot_from_addr(node, K230_SRAM_APP_ADDR)) {
        debug_print(KBURN_LOG_ERROR, COLOR_FMT("boot from sram failed"), RED);

        return false;
    }

    node->destroy_in_progress = true;

    return true;
}

bool K230BurnISP_WriteBurnImageConfig(kburnDeviceNode *node, const void *cfgData, size_t cfgSize, on_write_progress page_callback, void *ctx) {
	debug_trace_function("node[0x%p]", (void *)node);

	if (!K230BurnISP_Greeting(node)) {
        debug_print(KBURN_LOG_ERROR, COLOR_FMT("greeting failed"), RED);

		return false;
	}

	do_sleep(10);
	if (!K230BurnISP_WriteMemory(node, (const uint8_t *)cfgData, cfgSize, K230_SRAM_CFG_ADDR, page_callback, ctx)) {
        debug_print(KBURN_LOG_ERROR, COLOR_FMT("Write data to sram failed"), RED);

		return false;
	}

	do_sleep(20);
	if (!K230BurnISP_Greeting(node)) {
		debug_print(KBURN_LOG_ERROR, COLOR_FMT("	re-greeting failed..."), RED);
		return false;
	}

    return true;
}
