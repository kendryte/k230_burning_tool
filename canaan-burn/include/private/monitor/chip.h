#pragma once

#include "private/context.h"

bool chip_k230_handshake(kburnUsbDeviceNode *usb);

bool chip_handshake(kburnDeviceNode *node);
