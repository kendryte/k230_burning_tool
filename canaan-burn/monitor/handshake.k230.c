#include <stdlib.h>

#include <libusb.h>

#include "private/lib/debug/print.h"
#include "private/monitor/handshake.h"

#include "usb_dfu.h"
#include "dfu_file.h"
#include "quirks.h"

#define RETRY_TIMES (3)
#define USB_TIMEOUT (5000)

/* USB string descriptor should contain max 126 UTF-16 characters
 * but 254 would even accommodate a UTF-8 encoding + NUL terminator */
#define MAX_DESC_STR_LEN 254

/*
 * Look for a descriptor in a concatenated descriptor list. Will
 * return upon the first match of the given descriptor type. Returns length of
 * found descriptor, limited to res_size
 */
static int find_descriptor(const uint8_t *desc_list, int list_len,
    uint8_t desc_type, void *res_buf, int res_size)
{
	int p = 0;

	if (list_len < 2)
		return (-1);

	while (p + 1 < list_len) {
		int desclen;

		desclen = (int) desc_list[p];
		if (desclen == 0) {
            debug_print(KBURN_LOG_WARN, "Invalid descriptor list");
			return -1;
		}
		if (desc_list[p + 1] == desc_type) {
			if (desclen > res_size)
				desclen = res_size;
			if (p + desclen > list_len)
				desclen = list_len - p;
			memcpy(res_buf, &desc_list[p], desclen);
			return desclen;
		}
		p += (int) desc_list[p];
	}
	return -1;
}

/*
 * Get a string descriptor that's UTF-8 (or ASCII) encoded instead
 * of UTF-16 encoded like the USB specification mandates. Some
 * devices, like the GD32VF103, both violate the spec in this way
 * and store important information in the serial number field. This
 * function does NOT append a NUL terminator to its buffer, so you
 * must use the returned length to ensure you stay within bounds.
 */
static int get_utf8_string_descriptor(libusb_device_handle *devh, uint8_t desc_index, unsigned char *data, int length)
{
	unsigned char tbuf[255];
	uint16_t langid;
	int r, outlen;

	/* get the language IDs and pick the first one */
	r = libusb_get_string_descriptor(devh, 0, 0, tbuf, sizeof(tbuf));
	if (r < 0) {
		debug_print(KBURN_LOG_WARN, "Failed to retrieve language identifiers");
		return r;
	}
	if (r < 4 || tbuf[0] < 4 || tbuf[1] != LIBUSB_DT_STRING) {		/* must have at least one ID */
		debug_print(KBURN_LOG_WARN, "Broken LANGID string descriptor");
		return -1;
	}
	langid = tbuf[2] | (tbuf[3] << 8);

	r = libusb_get_string_descriptor(devh, desc_index, langid, tbuf, sizeof(tbuf));
	if (r < 0) {
		debug_print(KBURN_LOG_WARN, "Failed to retrieve string descriptor %d", desc_index);
		return r;
	}
	if (r < 2 || tbuf[0] < 2) {
		debug_print(KBURN_LOG_WARN, "String descriptor %d too short", desc_index);
		return -1;
	}
	if (tbuf[1] != LIBUSB_DT_STRING) {	/* sanity check */
		debug_print(KBURN_LOG_WARN, "Malformed string descriptor %d, type = 0x%02x", desc_index, tbuf[1]);
		return -1;
	}
	if (tbuf[0] > r) {	/* if short read,           */
		debug_print(KBURN_LOG_WARN, "Patching string descriptor %d length (was %d, received %d)", desc_index, tbuf[0], r);
		tbuf[0] = r;	/* fix up descriptor length */
	}

	outlen = tbuf[0] - 2;
	if (length < outlen)
		outlen = length;

	memcpy(data, tbuf + 2, outlen);

	return outlen;
}

/*
 * Similar to libusb_get_string_descriptor_ascii but will allow
 * truncated descriptors (descriptor length mismatch) seen on
 * e.g. the STM32F427 ROM bootloader.
 */
static int get_string_descriptor_ascii(libusb_device_handle *devh,
    uint8_t desc_index, unsigned char *data, int length)
{
	unsigned char buf[255];
	int r, di, si;

	r = get_utf8_string_descriptor(devh, desc_index, buf, sizeof(buf));
	if (r < 0)
		return r;

	/* convert from 16-bit unicode to ascii string */
	for (di = 0, si = 0; si + 1 < r && di < length; si += 2) {
		if (buf[si + 1])	/* high byte of unicode char */
			data[di++] = '?';
		else
			data[di++] = buf[si];
	}
	data[di] = '\0';
	return di;
}

uint16_t get_quirks(uint16_t vendor, uint16_t product, uint16_t bcdDevice)
{
	uint16_t quirks = 0;

	/* Device returns bogus bwPollTimeout values */
	if ((vendor == VENDOR_OPENMOKO || vendor == VENDOR_FIC) &&
	    product >= PRODUCT_FREERUNNER_FIRST &&
	    product <= PRODUCT_FREERUNNER_LAST)
		quirks |= QUIRK_POLLTIMEOUT;

	if (vendor == VENDOR_VOTI &&
	    (product == PRODUCT_OPENPCD || product == PRODUCT_SIMTRACE ||
	     product == PRODUCT_OPENPICC))
		quirks |= QUIRK_POLLTIMEOUT;

	/* Reports wrong DFU version in DFU descriptor */
	if (vendor == VENDOR_LEAFLABS &&
	    product == PRODUCT_MAPLE3 &&
	    bcdDevice == 0x0200)
		quirks |= QUIRK_FORCE_DFU11;

	/* old devices(bcdDevice == 0) return bogus bwPollTimeout values */
	if (vendor == VENDOR_SIEMENS &&
	    (product == PRODUCT_PXM40 || product == PRODUCT_PXM50) &&
	    bcdDevice == 0)
		quirks |= QUIRK_POLLTIMEOUT;

	/* M-Audio Transit returns bogus bwPollTimeout values */
	if (vendor == VENDOR_MIDIMAN &&
	    product == PRODUCT_TRANSIT)
		quirks |= QUIRK_POLLTIMEOUT;

	/* Some GigaDevice GD32 devices have improperly-encoded serial numbers
	 * and bad DfuSe descriptors which we use serial number to correct.
	 * They also "leave" without a DFU_GETSTATUS request */
	if (vendor == VENDOR_GIGADEVICE &&
	    product == PRODUCT_GD32) {
		quirks |= QUIRK_UTF8_SERIAL;
		quirks |= QUIRK_DFUSE_LAYOUT;
		quirks |= QUIRK_DFUSE_LEAVE;
	}

	return (quirks);
}

static bool k230_check_is_dfu(kburnUsbDeviceNode *usb)
{
    struct libusb_device_descriptor desc;
	struct libusb_config_descriptor *cfg;
	const struct libusb_interface *uif;
	const struct libusb_interface_descriptor *intf;

	struct dfu_if *pdfu;
	struct usb_dfu_func_descriptor func_dfu;
	// struct dfu_status sts;

    int ret;
	int cfg_idx;
	int intf_idx;
	int alt_idx;

	char alt_name[MAX_DESC_STR_LEN + 1];
	char serial_name[MAX_DESC_STR_LEN + 1];

    debug_trace_function();

    if (LIBUSB_SUCCESS != libusb_get_device_descriptor(usb->device, &desc)) {
        debug_print(KBURN_LOG_ERROR, "get dev desc failed");
        return false;
    }

	for (cfg_idx = 0; cfg_idx != desc.bNumConfigurations; cfg_idx++) {
		memset(&cfg, 0, sizeof(cfg));
		memset(&func_dfu, 0, sizeof(func_dfu));

		ret = libusb_get_config_descriptor(usb->device, cfg_idx, &cfg);
		if ((ret != 0) || (NULL == cfg)) {
            debug_print(KBURN_LOG_ERROR, "get dev config desc failed");
			return false;
        }

        debug_print(KBURN_LOG_ERROR, "desc dump:type %x, interface number %d", cfg->bDescriptorType, cfg->bNumInterfaces);

		ret = find_descriptor(cfg->extra, cfg->extra_length, USB_DT_DFU, &func_dfu, sizeof(func_dfu));
		if (ret > -1)
			goto found_dfu;

		for (intf_idx = 0; intf_idx < cfg->bNumInterfaces; intf_idx++) {
			uif = &cfg->interface[intf_idx];
			if (!uif)
				break;

			for (alt_idx = 0; alt_idx < cfg->interface[intf_idx].num_altsetting; alt_idx++) {
				intf = &uif->altsetting[alt_idx];

				if (intf->bInterfaceClass != 0xfe || intf->bInterfaceSubClass != 1)
					continue;

				ret = find_descriptor(intf->extra, intf->extra_length, USB_DT_DFU, &func_dfu, sizeof(func_dfu));
				if (ret > -1)
					goto found_dfu;
			}
		}

		libusb_free_config_descriptor(cfg);
		continue;

found_dfu:
        debug_print(KBURN_LOG_DEBUG, "device is dfu\n");

		if (func_dfu.bLength == 7) {
			debug_print(KBURN_LOG_DEBUG,"Deducing device DFU version from functional descriptor length\n");
			func_dfu.bcdDFUVersion = libusb_cpu_to_le16(0x0100);
		} else if (func_dfu.bLength < 9) {
			debug_print(KBURN_LOG_DEBUG,"Error obtaining DFU functional descriptor\n");
			debug_print(KBURN_LOG_DEBUG,"Please report this as a bug!\n");
			debug_print(KBURN_LOG_DEBUG,"Warning: Assuming DFU version 1.0\n");
			func_dfu.bcdDFUVersion = libusb_cpu_to_le16(0x0100);
			debug_print(KBURN_LOG_DEBUG,"Warning: Transfer size can not be detected\n");
			func_dfu.wTransferSize = 0;
		}

		for (intf_idx = 0; intf_idx < cfg->bNumInterfaces; intf_idx++) {
			int multiple_alt;

			uif = &cfg->interface[intf_idx];
			if (!uif)
				break;

			multiple_alt = uif->num_altsetting > 0;

			for (alt_idx = 0; alt_idx < uif->num_altsetting; alt_idx++) {
				int dfu_mode;
				uint16_t quirks;

				quirks = get_quirks(desc.idVendor, desc.idProduct, desc.bcdDevice);

				intf = &uif->altsetting[alt_idx];

				if (intf->bInterfaceClass != 0xfe || intf->bInterfaceSubClass != 1)
					continue;

				dfu_mode = (intf->bInterfaceProtocol == 2);
				/* e.g. DSO Nano has bInterfaceProtocol 0 instead of 2 */
				// if (func_dfu.bcdDFUVersion == 0x011a && intf->bInterfaceProtocol == 0)
				// 	dfu_mode = 1;

				/* LPC DFU bootloader has bInterfaceProtocol 1 (Runtime) instead of 2 */
				// if (desc.idVendor == 0x1fc9 && desc.idProduct == 0x000c && intf->bInterfaceProtocol == 1)
				// 	dfu_mode = 1;

				/*
				 * Old Jabra devices may have bInterfaceProtocol 0 instead of 2.
				 * Also runtime PID and DFU pid are the same.
				 * In DFU mode, the configuration descriptor has only 1 interface.
				 */
				// if (desc.idVendor == 0x0b0e && intf->bInterfaceProtocol == 0 && cfg->bNumInterfaces == 1)
				// 	dfu_mode = 1;

				// ret = libusb_open(dev, &devh);
				// if (ret) {
				// 	debug_print(KBURN_LOG_WARN, "Cannot open DFU device %04x:%04x found on devnum %i (%s)",
				// 	      desc.idVendor, desc.idProduct, libusb_get_device_address(dev),
				// 	      libusb_error_name(ret));
				// 	break;
				// }

				if (intf->iInterface != 0) {
					ret = get_string_descriptor_ascii(usb->handle, intf->iInterface, (void *)alt_name, MAX_DESC_STR_LEN);
				} else {
					ret = -1;
				}
				if (ret < 1)
					strcpy(alt_name, "UNKNOWN");

				if (desc.iSerialNumber != 0) {
					if (quirks & QUIRK_UTF8_SERIAL) {
						ret = get_utf8_string_descriptor(usb->handle, desc.iSerialNumber, (void *)serial_name, MAX_DESC_STR_LEN - 1);
						if (ret >= 0)
							serial_name[ret] = '\0';
					} else {
						ret = get_string_descriptor_ascii(usb->handle, desc.iSerialNumber, (void *)serial_name, MAX_DESC_STR_LEN);
					}
				} else {
					ret = -1;
				}
				if (ret < 1)
					strcpy(serial_name, "UNKNOWN");
				// libusb_close(devh);

                pdfu = malloc(sizeof(*pdfu));
				if(NULL == pdfu) {
					debug_print(KBURN_LOG_ERROR, "Out of memory");
					break;
				}

				memset(pdfu, 0, sizeof(*pdfu));

				pdfu->func_dfu = func_dfu;
				pdfu->dev = usb->device;//libusb_ref_device(dev);
				pdfu->dev_handle = usb->handle;

				pdfu->quirks = quirks;
				pdfu->vendor = desc.idVendor;
				pdfu->product = desc.idProduct;
				pdfu->bcdDevice = desc.bcdDevice;
				pdfu->configuration = cfg->bConfigurationValue;
				pdfu->interface = intf->bInterfaceNumber;
				pdfu->altsetting = intf->bAlternateSetting;
				// pdfu->devnum = libusb_get_device_address(dev);
				// pdfu->busnum = libusb_get_bus_number(dev);

				pdfu->alt_name = strdup(alt_name);
				if (pdfu->alt_name == NULL) {
					debug_print(KBURN_LOG_ERROR, "Out of memory %s", pdfu->alt_name);
				} else {
					debug_print(KBURN_LOG_TRACE, "alt_name %s", pdfu->alt_name);
				}

				pdfu->serial_name = strdup(serial_name);
				if (pdfu->serial_name == NULL) {
					debug_print(KBURN_LOG_ERROR, "Out of memory %s", pdfu->serial_name);
				} else {
					debug_print(KBURN_LOG_TRACE, "serial_name %s", pdfu->serial_name);
				}

				if (dfu_mode) {
					pdfu->flags |= DFU_IFF_DFU;
				}

				if (multiple_alt) {
					pdfu->flags |= DFU_IFF_ALT;
				}

				if (pdfu->quirks & QUIRK_FORCE_DFU11) {
					pdfu->func_dfu.bcdDFUVersion = libusb_cpu_to_le16(0x0110);
				}

				pdfu->bMaxPacketSize0 = desc.bMaxPacketSize0;

				int transfer_size = libusb_le16_to_cpu(pdfu->func_dfu.wTransferSize);
				if (transfer_size) {
					debug_print(KBURN_LOG_TRACE, "Device returned transfer size %i\n", transfer_size);
				}

#ifdef __linux__
				/* limited to 4k in libusb Linux backend */
				if ((int)transfer_size > 4096) {
					transfer_size = 4096;
					debug_print(KBURN_LOG_TRACE, "Limited transfer size to %i\n", transfer_size);
				}
#endif /* __linux__ */

				if (transfer_size < pdfu->bMaxPacketSize0) {
					transfer_size = pdfu->bMaxPacketSize0;
					debug_print(KBURN_LOG_TRACE, "Adjusted transfer size to %i\n", transfer_size);
				}
				pdfu->chunk_size = transfer_size;

				// if(0 > (ret = dfu_get_status(pdfu, &sts))) {
				// 	debug_print(KBURN_LOG_ERROR, "Get dfu status failed %d", ret);
				// 	goto out;
				// }

				pdfu->next = usb->dfu;
				usb->dfu = pdfu;
			}
		}

		libusb_free_config_descriptor(cfg);

        usb->stage = 2;
        return true;
    }

// out:
    debug_print(KBURN_LOG_DEBUG, "device is not dfu\n");

    return false;
}

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

    if(0x00 == memcmp(info, "K230", 4)) {
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
        if(k230_check_is_dfu(usb)) {
            debug_print(KBURN_LOG_INFO, "is dfu device");

            return true;
        } else if (true == k230_get_cpu_info(usb)) {
            debug_print(KBURN_LOG_ERROR, "k230 handshake success, tried %d times", i + 1);

            return true;
        }
    }

    return false;
}
