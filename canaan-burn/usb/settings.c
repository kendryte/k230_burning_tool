#include "private/usb/private-types.h"

// #define KBURN_USB_DEFAULT_VID 0x29f1
// #define KBURN_USB_DEFAULT_PID 0x0230

#define KBURN_USB_DEFAULT_VID 0x1a86
#define KBURN_USB_DEFAULT_PID 0x55d2

struct usb_settings usb_default_settings = {
	.vid = KBURN_USB_DEFAULT_VID,
	.pid = KBURN_USB_DEFAULT_PID,
};

CREATE_GETTER_SETTER(FilterVid, vid)
CREATE_GETTER_SETTER(FilterPid, pid)
