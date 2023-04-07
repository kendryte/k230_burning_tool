#include <stddef.h>
#include <cassert>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

#include "canaan-burn.h"

using namespace std;


void _on_device_list_change(void *ctx, void *always_null_ptr) {
	cout << __func__ << endl;
}

bool _on_device_connect(void *ctx, kburnDeviceNode *dev) {
	cout << __func__ << endl;
	return true;
}

void _on_device_disconnect(void *ctx, kburnDeviceNode *dev) {
	cout << __func__ << endl;
}

void _on_device_confirmed(void *ctx, kburnDeviceNode *dev) {
	cout << __func__ << endl;

	// Now, we directly destory the node.
	mark_destroy_device_node(dev);
}

int main(int argc, char **argv) {
	assert(KBURN_ERROR_KIND_SERIAL > UINT32_MAX);
	cout << '\x1B' << 'c';
	flush(cout);

	KBMonCTX monitor = NULL;
	kburn_err_t err = kburnMonitorCreate(&monitor);
	if (err != KBurnNoErr) {
		cerr << "Failed Start: " << err << endl;
		return 1;
	}

	kburnOnDeviceListChange(monitor, _on_device_list_change, NULL);
	kburnOnDeviceConnect(monitor, _on_device_connect, NULL);
	kburnOnDeviceDisconnect(monitor, _on_device_disconnect, NULL);
	kburnOnDeviceConfirmed(monitor, _on_device_confirmed, NULL);

	kburnMonitorStartWaitingDevices(monitor);
	getchar();

	// kburnMonitorWaitDevicePause(monitor);
	// kburnSetUsbFilterVid(monitor, 0x0403);
	// kburnSetUsbFilterPid(monitor, 0x6010);
	// kburnMonitorWaitDeviceResume(monitor);
	// getchar();

	kburnMonitorGlobalDestroy();

	return 0;
}
