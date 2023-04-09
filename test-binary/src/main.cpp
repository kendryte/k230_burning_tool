#include <stddef.h>
#include <cassert>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

#include <unistd.h>
#include "canaan-burn.h"

using namespace std;

// bool should_start = false;
// bool flag_done = false;

void _on_device_list_change(void *ctx, void *always_null_ptr) {
	cout << __func__ << endl;
}

bool _on_device_connect(void *ctx, kburnDeviceNode *dev) {
	/**
	 * 设备连接之后，开启定时器，如果没有断开连接，或者是被调用confirm回调，说明连接超时，进行提醒
	*/
	cout << __func__ << " path " << dev->usb->deviceInfo.pathStr << " stage " << dev->usb->stage << endl;

	// return should_start;
	return true;
}

void _on_device_disconnect(void *ctx, kburnDeviceNode *dev) {
	/**
	 * 设备连接之后断开，可能是被主动断开，或者是握手失败
	*/
	cout << __func__ << " path " << dev->usb->deviceInfo.pathStr << " stage " << dev->usb->stage << endl;
}

void _on_device_confirmed(void *ctx, kburnDeviceNode *dev) {
	/**
	 * 设备握手成功，开始进行下载
	 * 
	 * 推荐流程，如果是多设备下载，需要新建线程池，在新的线程中进行下载
	 * 		1. 下载loader并启动loader
	 * 		2. 下载文件
	*/
	cout << __func__ << " path " << dev->usb->deviceInfo.pathStr << " stage " << dev->usb->stage << endl;

	// 用完设备之后，调用该函数，标记可以进行回收
	// if(0x02 == dev->usb->stage) {
		mark_destroy_device_node(dev);
	// }

	// flag_done = true;
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

	// should_start = true;			// 输入之后，使能开始下载
	// getchar();
	// kburnMonitorManualTrig(monitor);		// 手动开始下载
	// while(false == flag_done) {
	// 	sleep(2);
	// }

	// kburnMonitorWaitDevicePause(monitor);
	// kburnSetUsbFilterVid(monitor, 0x0403);
	// kburnSetUsbFilterPid(monitor, 0x6010);
	// kburnMonitorWaitDeviceResume(monitor);
	// getchar();

	kburnMonitorGlobalDestroy();

	return 0;
}
