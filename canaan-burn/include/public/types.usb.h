#pragma once

#include "./prefix.h"

#define MAX_USB_PATH_LENGTH 9 // 当前usb最大7级，首个字符是bus number，多一个0用于字符串比较

#define KBURN_VIDPID_FILTER_ANY 0xFFFF // NO USE

struct kburnUsbDeviceInfoSlice {
	uint16_t idVendor;
	uint16_t idProduct;
	uint16_t bcdDevice;
	uint8_t path[MAX_USB_PATH_LENGTH];
	// #ifdef __linux__
	//  uint32_t major;
	//  uint32_t minor;
	// #endif
};

typedef struct kburnUsbDeviceInfo {
	PCONST uint16_t idVendor;
	PCONST uint16_t idProduct;
	PCONST uint8_t path[MAX_USB_PATH_LENGTH];
	PCONST char pathStr[MAX_USB_PATH_LENGTH * 3 + 1];
	PCONST struct libusb_device_descriptor *descriptor;

	PCONST uint8_t endpoint_in;  /* 端点 in */
	PCONST uint8_t endpoint_out; /* 端点 out */

	// #ifdef __linux__
	// 	PCONST uint32_t major;
	// 	PCONST uint32_t minor;
	// #endif

} kburnUsbDeviceInfo;

typedef struct kburnUsbDeviceNode {
	PCONST struct kburnDeviceNode *parent;

	PCONST bool init;

	PCONST bool isOpen;
	PCONST bool isClaim;

	PCONST int stage; /* 1: bootrom, 2: uboot */

	PCONST kburnUsbDeviceInfo deviceInfo;

	PCONST struct libusb_device *device;
	PCONST struct libusb_device_handle *handle;

	PCONST struct dfu_if *dfu;
} kburnUsbDeviceNode;

typedef struct kburn_usb_device_list {
	PCONST size_t size;
	PCONST struct kburnUsbDeviceInfoSlice *list;
} kburnUsbDeviceList;

typedef struct kburn_usb_loader_info {
	uint8_t *data;
	size_t size;
} kburnUsbLoaderInfo;
