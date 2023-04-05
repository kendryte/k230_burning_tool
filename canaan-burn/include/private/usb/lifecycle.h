#include "private/context.h"
#include "private/lib/basic/disposable.h"

DECALRE_DISPOSE_HEADER(destroy_usb_port, kburnUsbDeviceNode);
kburn_err_t open_single_usb_port(KBMonCTX monitor, struct libusb_device *dev, bool user_verify, kburnDeviceNode **out_node);
