#include "private/monitor/chip.h"
#include "private/lib/debug/print.h"

bool chip_handshake(kburnDeviceNode *node)
{
    switch(node->usb->deviceInfo.idProduct) {
        case 0x230: {
            return chip_k230_handshake(node->usb);
        } break;
        default: {
            debug_print(KBURN_LOG_WARN, "Not support for this product id %4x", node->usb->deviceInfo.idProduct);
        } break;
    }

    return false;
}
