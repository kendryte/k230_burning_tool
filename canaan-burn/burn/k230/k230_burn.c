#include "public/k230_burn.h"

#include "private/lib/basic/sleep.h"
#include "private/lib/debug/print.h"

#include "dfu.h"
#include "usb_dfu.h"
#include "dfu_file.h"

static struct dfu_if *get_alt_dif(kburnDeviceNode *node, const char *alt)
{
    struct dfu_if *pdfu;

    for (pdfu = node->usb->dfu; pdfu != NULL; pdfu = pdfu->next) {
        if(0x00 == strcmp(pdfu->alt_name, alt)) {
            return pdfu;
        }
        // debug_print(KBURN_LOG_INFO, "%s != %s", alt, pdfu->alt_name);
    }
    return NULL;
}

bool K230Burn_Check_Alt(kburnDeviceNode *node, const char *alt)
{
    return NULL != get_alt_dif(node, alt);
}

uint32_t K230Burn_Get_Chunk_Size(kburnDeviceNode *node, const char *alt)
{
    struct dfu_if *pdfu = get_alt_dif(node, alt);

    if(NULL == pdfu) {
        debug_print(KBURN_LOG_ERROR, "Can not found alt %s", alt);
        return 0;
    }

    debug_print(KBURN_LOG_INFO, "chunksize %d", pdfu->chunk_size);

    return pdfu->chunk_size;
}

int K230Burn_Write_Chunk(kburnDeviceNode *node, const char *alt, unsigned short block, void *buffer, uint32_t length)
{
	int ret;
	struct dfu_status dst;

    struct dfu_if *pdfu = get_alt_dif(node, alt);

    if(NULL == pdfu) {
        debug_print(KBURN_LOG_ERROR, "Can not found alt %s", alt);
        return -1;
    }

    if(length) {
        ret = dfu_download(pdfu->dev_handle, pdfu->interface, length, block, length ? buffer : NULL);
        if (ret < 0) {
            debug_print(KBURN_LOG_WARN, "Error during download (%s)", libusb_error_name(ret));
            goto out;
        }

        do {
            ret = dfu_get_status(pdfu, &dst);
            if (ret < 0) {
                debug_print(KBURN_LOG_ERROR, "Error during download get_status (%s)", libusb_error_name(ret));
                goto out;
            }

            if (dst.bState == DFU_STATE_dfuDNLOAD_IDLE || dst.bState == DFU_STATE_dfuERROR)
                break;

            /* Wait while device executes flashing */
            do_sleep(dst.bwPollTimeout);
        } while (1);

        if (dst.bStatus != DFU_STATUS_OK) {
            debug_print(KBURN_LOG_ERROR," failed!\n");
            debug_print(KBURN_LOG_ERROR,"DFU state(%u) = %s, status(%u) = %s\n", dst.bState,
                dfu_state_to_string(dst.bState), dst.bStatus,
                dfu_status_to_string(dst.bStatus));
            ret = -1;
            goto out;
        }
    } else if(0x00 == length) {
        /* send one zero sized download request to signalize end */
        ret = dfu_download(pdfu->dev_handle, pdfu->interface, 0, block, NULL);
        if (ret < 0) {
            debug_print(KBURN_LOG_ERROR, "Error sending completion packet (%s)", libusb_error_name(ret));
            goto out;
        }

get_status:
        /* Transition to MANIFEST_SYNC state */
        ret = dfu_get_status(pdfu, &dst);
        if (ret < 0) {
            debug_print(KBURN_LOG_WARN, "unable to read DFU status after completion (%s)", libusb_error_name(ret));
            goto out;
        }

        ret = 1;

        debug_print(KBURN_LOG_DEBUG, "DFU state(%u) = %s, status(%u) = %s\n", dst.bState,
            dfu_state_to_string(dst.bState), dst.bStatus,
            dfu_status_to_string(dst.bStatus));

        do_sleep(dst.bwPollTimeout);

        /* FIXME: deal correctly with ManifestationTolerant=0 / WillDetach bits */
        switch (dst.bState) {
        case DFU_STATE_dfuMANIFEST_SYNC:
        case DFU_STATE_dfuMANIFEST:
            /* some devices (e.g. TAS1020b) need some time before we
            * can obtain the status */
            do_sleep(1000);
            goto get_status;
            break;
        case DFU_STATE_dfuMANIFEST_WAIT_RST:
            debug_print(KBURN_LOG_DEBUG, "Resetting USB to switch back to runtime mode\n");

            // ret = libusb_reset_device(dif->dev_handle);
            // if (ret < 0 && ret != LIBUSB_ERROR_NOT_FOUND) {
            //     fprintf(stderr, "error resetting after download (%s)\n",
            //         libusb_error_name(ret));
            // }

            break;
        case DFU_STATE_dfuIDLE:
            break;
        }
        debug_print(KBURN_LOG_DEBUG, "Done!\n");
    }
out:
	return ret;
}
