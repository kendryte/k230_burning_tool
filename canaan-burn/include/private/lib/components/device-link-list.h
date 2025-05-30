#include <stddef.h>
#include <stdint.h>

#include "private/context.h"

#include "private/lib/basic/disposable.h"
#include "private/lib/basic/lock.h"



typedef struct port_link_element {
	struct port_link_element *prev;
	kburnDeviceNode *node;
	struct port_link_element *next;
} port_link_element;

typedef struct port_link_list {
	port_link_element *head;
	port_link_element *tail;
	uint32_t size;
	kb_mutex_t exclusion;
} port_link_list;

port_link_list *port_link_list_init();
DECALRE_DISPOSE_HEADER(port_link_list_deinit, port_link_list);
// void alloc_new_bind_id(kburnDeviceNode *target);
void add_to_device_list(kburnDeviceNode *target);
DECALRE_DISPOSE_HEADER(delete_from_device_list, kburnDeviceNode);
// void recreate_waitting_list(KBMonCTX monitor);
kburnDeviceNode *get_device_by_serial_port_path(KBMonCTX monitor, const char path[MAX_USB_PATH_LENGTH]);
kburnDeviceNode *get_device_by_usb_port_path(KBMonCTX monitor, uint16_t vid, uint16_t pid, const uint8_t path[MAX_USB_PATH_LENGTH]);
kburnDeviceNode *get_device_by_bind_id(KBMonCTX monitor, uint32_t id);


