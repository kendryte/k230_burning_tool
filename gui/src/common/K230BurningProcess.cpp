#include "K230BurningProcess.h"
#include "AppGlobalSetting.h"
#include "main.h"
#include "MyException.h"
#include <public/canaan-burn.h>

#include <QFileInfo>

#define CHUNK_SIZE 1024 * 1024

K230BurningProcess::K230BurningProcess(KBMonCTX scope, const K230BurningRequest *request)
	: BurningProcess(scope, request), usbPath(request->usbPath), inputs(2) {
	this->setAutoDelete(false);
};

QString K230BurningProcess::getTitle() const {
#if WIN32
	return usbPath;
#else
	return QFileInfo(usbPath).baseName();
#endif
}

void K230BurningProcess::serial_isp_progress(void *self, const kburnDeviceNode *dev, size_t current, size_t length) {
	auto _this = reinterpret_cast<K230BurningProcess *>(self);
	_this->setProgress(current);
}

qint64 K230BurningProcess::prepare() {

	setStage(::tr("等待设备上电"));

	node = reinterpret_cast<kburnDeviceNode *>(inputs.pick(0, 30));
	if (node == NULL) {
		throw KBurnException(tr("设备上电超时"));
	}

	setStage(::tr("写入USB LOADER"), K230BurnISP_LoaderSize());

	if(!K230BurnISP_LoaderRun(node, K230BurningProcess::serial_isp_progress, this)) {
		throw KBurnException(tr("写入USB LOADER失败"));
	}

	setStage(::tr("等待USB ISP启动"));

	node = reinterpret_cast<kburnDeviceNode *>(inputs.pick(1, 20));
	if (node == NULL) {
		throw KBurnException(tr("ISP启动超时"));
	}

	// usb_ok = true;

	// if (!kburnUsbIspGetMemorySize(node, (kburnUsbIspCommandTaget)GlobalSetting::flashTarget.getValue(), &devInfo)) {
	// 	throw KBurnException(node->error->code, node->error->errorMessage);
	// }

	// if (imageSize > devInfo.storage_size) {
	// 	throw KBurnException(tr("文件过大"));
	// }

	return 0;

	// return ((CHUNK_SIZE / devInfo.block_size) + (CHUNK_SIZE % devInfo.block_size > 0 ? 1 : 0)) * devInfo.block_size;
}

#include "AppGlobalSetting.h"

void K230BurningProcess::cleanup(bool success) {
	qDebug() << __func__, __LINE__;

	if(node) {
		mark_destroy_device_node(node);
	}

	// if (!usb_ok) {
	// 	return;
	// }
	// uint32_t color = GlobalSetting::usbLedLevel.getValue();
	// if (success) {
	// 	color = color << 8;
	// } else {
	// 	color = color << 16;
	// }
	// kburnUsbIspLedControl(node, GlobalSetting::usbLedPin.getValue(), kburnConvertColor(color));
}

bool K230BurningProcess::step(kburn_stor_address_t address, const QByteArray &chunk) {
	qDebug() << __func__, __LINE__;

	return true;

	// size_t block = address / devInfo.block_size;

	// return kburnUsbIspWriteChunk(node, devInfo, block, (void *)chunk.constData(), chunk.size());
}

bool K230BurningProcess::pollingDevice(kburnDeviceNode *node, BurnLibrary::DeviceEvent event) {
	bool isOk = false;

	qDebug() << __func__ << __LINE__ << "Stage" << node->usb->stage << "Path" << QString(node->usb->deviceInfo.pathStr) << "this->usbPath" << this->usbPath;

	if((this->usbPath == QString("WaitingConnect")) && \
		(0x01 == node->usb->stage) && \
		(event != BurnLibrary::DeviceEvent::UbootISPDisconnected))
	{
		isOk = true;
		this->usbPath = QString(node->usb->deviceInfo.pathStr);
		emit updateTitle();
	} else {
		isOk = this->usbPath == node->usb->deviceInfo.pathStr;
	}

	if (!isOk) {
		return false;
	}

	if (event == BurnLibrary::DeviceEvent::BootRomConfirmed) {
		inputs.set(0, node);
	} else if (event == BurnLibrary::DeviceEvent::UbootISPConfirmed) {
		inputs.set(1, node);
	} else if (event == BurnLibrary::DeviceEvent::UbootISPDisconnected) {
		this->cancel(KBurnException(::tr("设备断开")));
	}

	emit deviceStateNotify();

	return true;
}

// void K230BurningProcess::recreateDeviceStatus(const kburnDeviceNode *dev) {
// 	QString &val = _detailInfo;
// 	val += tr("Serial Device: ");
// 	if (dev->serial->init || dev->serial->isUsbBound) {
// 		val += dev->serial->deviceInfo.path;
// 		val += '\n';
// 		val += tr("  * init: ");
// 		val += dev->serial->init ? tr("yes") : tr("no");
// 		val += '\n';
// 		val += tr("  * isOpen: ");
// 		val += dev->serial->isOpen ? tr("yes") : tr("no");
// 		val += '\n';
// 		val += tr("  * isConfirm: ");
// 		val += dev->serial->isConfirm ? tr("yes") : tr("no");
// 		val += '\n';
// 		val += tr("  * isUsbBound: ");
// 		val += dev->serial->isUsbBound ? tr("yes") : tr("no");
// 	} else {
// 		val += tr("not connected");
// 	}
// 	val += '\n';

// 	val += tr("Usb Device: ");
// 	if (dev->usb->init) {
// 		for (auto i = 0; i < MAX_USB_PATH_LENGTH - 1; i++) {
// 			val += QString::number(dev->usb->deviceInfo.path[i], 16) + QChar(':');
// 		}
// 		val.chop(1);
// 		val += '\n';

// 		val += "  * VID: " + QString::number(dev->usb->deviceInfo.idVendor, 16) + '\n';
// 		val += "  * PID: " + QString::number(dev->usb->deviceInfo.idProduct, 16) + '\n';
// 	} else {
// 		val += tr("not connected");
// 	}
// 	val += '\n';

// 	if (dev->error->code) {
// 		val += tr("  * error status: ");
// 		auto errs = kburnSplitErrorCode(dev->error->code);
// 		val += tr("kind: ");
// 		val += QString::number(errs.kind >> 32);
// 		val += ", ";
// 		val += tr("code: ");
// 		val += QString::number(errs.code);
// 		val += ", ";
// 		val += QString::fromLatin1(dev->error->errorMessage);
// 	}
// }
