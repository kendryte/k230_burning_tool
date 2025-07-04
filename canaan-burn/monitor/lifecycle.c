#include "private/lib/device.h"

#include "private/lib/basic/errors.h"
#include "private/lib/basic/lock.h"
#include "private/lib/basic/resource-tracker.h"
#include "private/lib/basic/sleep.h"
#include "private/lib/components/call-user-handler.h"
#include "private/lib/components/device-link-list.h"

#include "private/monitor/handshake.h"
#include "private/monitor/lifecycle.h"
#include "private/monitor/descriptor.h"
#include "private/monitor/usb_types.h"

/****************************************************
Function: get_endpoint
Description: 获取usb设备的端点。in和out
param1 : dev         枚举出来并符合vid pid的usb设备
param2 : current     当前设备节点
Return: 返回负数说明获取端点失败，返回0为成功
*******************************************************/
static int get_endpoint(libusb_device *dev, uint8_t *out_endpoint_in, uint8_t *out_endpoint_out) {
	debug_trace_function("%p", (void *)dev);
	int k, j;
	struct libusb_config_descriptor *conf_desc;
	const struct libusb_endpoint_descriptor *endpoint;
	*out_endpoint_in = 0;
	*out_endpoint_out = 0;

	/* 获取配置描述符，里面包含端点信息 */
	CheckLibusbError(libusb_get_config_descriptor(dev, 0, &conf_desc));

	/* 默认使用第一个接口 usb_interface[0]*/
	for (j = 0; j < conf_desc->interface[0].num_altsetting; j++) {
		for (k = 0; k < conf_desc->interface[0].altsetting[j].bNumEndpoints; k++) {
			endpoint = &conf_desc->interface[0].altsetting[j].endpoint[k];
			debug_print(KBURN_LOG_TRACE, "\tendpoint[%d].address: %02X", k, endpoint->bEndpointAddress);

			/* 使用批量传输的端点 */
			if (endpoint->bmAttributes & LIBUSB_TRANSFER_TYPE_MASK & LIBUSB_TRANSFER_TYPE_BULK) {
				if (endpoint->bEndpointAddress & LIBUSB_ENDPOINT_IN) {
					*out_endpoint_in = endpoint->bEndpointAddress;
					debug_print(KBURN_LOG_INFO, " * endpoint_in = 0x%02X", *out_endpoint_in);
					if (*out_endpoint_out) {
						goto _quit;
					}
				} else {
					*out_endpoint_out = endpoint->bEndpointAddress;
					debug_print(KBURN_LOG_INFO, " * endpoint_out = 0x%02X", *out_endpoint_out);
					if (*out_endpoint_in) {
						goto _quit;
					}
				}
			}
		}
	}

_quit:
	libusb_free_config_descriptor(conf_desc);
	return LIBUSB_SUCCESS;
}

DECALRE_DISPOSE(destroy_usb_port, kburnUsbDeviceNode) {
	debug_trace_function("USB[0x%p: %s]", (void *)context->device, usb_debug_path_string(context->deviceInfo.path));
	// if (!use_device(context)) {
	// 	return;
	// }

	kburnDeviceNode *n = get_node(context);
	if(!n->destroy_in_progress) {
		debug_print(KBURN_LOG_WARN, COLOR_FMT("not to destory device."), RED);
		// return;
	}

	if (context->isClaim) {
		context->isClaim = false;
		libusb_release_interface(context->handle, 0);
	}

	if (context->isOpen) {
		context->isOpen = false;
		libusb_close(context->handle);
		context->handle = NULL;
		context->device = NULL;
	}

	free(context->deviceInfo.descriptor);

	context->init = false;

	device_instance_collect(get_node(context));
}
DECALRE_DISPOSE_END()

kburn_err_t open_single_usb_port(KBMonCTX monitor, struct libusb_device *dev, bool user_verify, kburnDeviceNode **out_node) {
	DeferEnabled;

	debug_print(KBURN_LOG_TRACE, COLOR_FMT("open_single_usb_port") "(%p)", COLOR_ARG(GREEN), (void *)dev);
	m_assert(monitor->usb->libusb, "usb subsystem is not inited");

	int r;
	kburnDeviceNode *node;
	IfErrorReturn(create_empty_device_instance(monitor, &node));
	debug_print(KBURN_LOG_INFO, "new device created: %p", (void *)node);
	DeferCall(mark_destroy_device_node, node);

	dispose_list_add(node->disposable_list, toDisposable(destroy_usb_port, node->usb));

	node->usb->device = dev;
	node->usb->stage = -1;

	IfUsbErrorLogSetReturn(libusb_open(dev, &node->usb->handle));
	node->usb->isOpen = true;
	debug_print(KBURN_LOG_INFO, "usb port open success, dev=%p, handle=%p", (void *)node->usb->device, (void *)node->usb->handle);

	node->usb->deviceInfo.descriptor = MyAlloc(struct libusb_device_descriptor);
	IfUsbErrorLogSetReturn(libusb_get_device_descriptor(dev, node->usb->deviceInfo.descriptor));
	node->usb->deviceInfo.idVendor = node->usb->deviceInfo.descriptor->idVendor;
	node->usb->deviceInfo.idProduct = node->usb->deviceInfo.descriptor->idProduct;

	IfUsbErrorSetReturn(usb_get_device_path(dev, node->usb->deviceInfo.path));
	usb_convert_path_to_string(node->usb->deviceInfo.path, node->usb->deviceInfo.pathStr);

	debug_print(KBURN_LOG_INFO, "  * vid: %04x", node->usb->deviceInfo.idVendor);
	debug_print(KBURN_LOG_INFO, "  * pid: %04x", node->usb->deviceInfo.idProduct);
	debug_print(KBURN_LOG_INFO, "  * path: %s", usb_debug_path_string(node->usb->deviceInfo.path));

	if (user_verify) {
		if (monitor->on_connect.handler != NULL) {
			if (!CALL_HANDLE_SYNC(monitor->on_connect, node)) {
				set_error(node, KBURN_ERROR_KIND_COMMON, KBurnUserCancel, "on_connect operation canceled by verify callback");
				return make_error_code(KBURN_ERROR_KIND_COMMON, KBurnUserCancel);
			}
		}
		debug_print(KBURN_LOG_INFO, "user verify pass");
	}

#if !defined (WIN32) && !defined (APPLE)
	r = libusb_kernel_driver_active(node->usb->handle, 0);
	if (r == 0) {
		debug_print(KBURN_LOG_DEBUG, "libusb kernel driver is already set to this device");
	} else if (r == 1) {
		r = libusb_detach_kernel_driver(node->usb->handle, 0);
		if (r != LIBUSB_ERROR_NOT_FOUND && r != 0) {
			set_node_error_with_log(r, "libusb_detach_kernel_driver() returns %d", r);
			return make_error_code(KBURN_ERROR_KIND_USB, r);
		}
		debug_print(KBURN_LOG_DEBUG, "libusb kernel driver switch ok");
	} else if (r == LIBUSB_ERROR_NOT_SUPPORTED) {
		debug_print_libusb_result("open_single_usb_port: system not support detach kernel driver", r);
	}
#endif

	// libusb_lock_events(monitor->usb->libusb);
	// libusb_clear_halt(node->usb->handle, 0);
	int lastr = -1;
	for (int i = 0; i < 20; i++) {
		r = libusb_claim_interface(node->usb->handle, 0);
		if (r == 0) {
			debug_print(KBURN_LOG_INFO, "claim interface success, tried %d times", i + 1);
			break;
		}
		if (lastr != r) {
			lastr = r;
			debug_print_libusb_error("libusb_claim_interface", r);
		}
		do_sleep(500);
	}

	if (r != 0) {
		set_node_error_with_log(r, "libusb_claim_interface: can not claim interface in 10s, other program is using this port.");
		return make_error_code(KBURN_ERROR_KIND_USB, r);
	}

	node->usb->isClaim = true;
	debug_print(KBURN_LOG_DEBUG, "libusb_claim_interface success");

	IfUsbErrorSetReturn(get_endpoint(dev, &node->usb->deviceInfo.endpoint_in, &node->usb->deviceInfo.endpoint_out));

	node->usb->init = true;
	node->disconnect_should_call = true;

	if(false == chip_handshake(node)) {
		debug_print(KBURN_LOG_DEBUG, COLOR_FMT("handshake failed."), RED);
		return KBurnUsbProtocolWrong;
	}
	debug_print(KBURN_LOG_DEBUG, COLOR_FMT("handshake success."), GREEN);

	add_to_device_list(node);

	if (out_node) {
		*out_node = node;
	}

	if (monitor->on_confirmed.handler != NULL) {
		if (!CALL_HANDLE_SYNC(monitor->on_confirmed, node)) {
			set_error(node, KBURN_ERROR_KIND_COMMON, KBurnUserCancel, "on_confirmed operation canceled by verify callback");
			return make_error_code(KBURN_ERROR_KIND_COMMON, KBurnUserCancel);
		}
	}

	DeferAbort;

	return KBurnNoErr;
}
