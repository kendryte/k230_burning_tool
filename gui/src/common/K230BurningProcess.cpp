#include "K230BurningProcess.h"
#include "AppGlobalSetting.h"
#include "main.h"
#include "MyException.h"
#include <public/canaan-burn.h>

#include <QFileInfo>
#include <QCryptographicHash>

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

qint64 K230BurningProcess::prepare(QList<struct BurnImageItem> &imageList) {
	bool founLoader = false;
	qint64 chunkSize = 4096; // default
	struct BurnImageItem loader;

	foreach(struct BurnImageItem item, imageList) {
		if(item.altName == QString("loader")) {
			founLoader = true;
			loader = item;
		}
	}

	if(false == founLoader) {
		throw(KBurnException(::tr("无法找到Loader")));
	}

	QFile loaderFile(loader.fileName);
	if (!loaderFile.open(QIODeviceBase::ReadOnly)) {
		throw(KBurnException(::tr("无法打开镜像文件") + " (" + loader.fileName + ")"));
	}
	QByteArray loaderFileContent = loaderFile.readAll();
	loaderFile.close();

	int itemCount = imageList.size() - 1;
	if(0 >= itemCount) {
		throw(KBurnException(::tr("文件数量不足")));
	}

	QByteArray cfgByteArray(itemCount * sizeof(struct BurnImageConfigItem) + sizeof(struct BurnImageConfig), 0);
	struct BurnImageConfig *cfg = (struct BurnImageConfig *)cfgByteArray.data();

	int cfgIndex = 0;

	foreach(struct BurnImageItem item, imageList) {
		if(item.altName == QString("loader")) {
			continue;
		}
		cfg->cfgs[cfgIndex].size = item.size;
		cfg->cfgs[cfgIndex].address = item.address;
		strncpy(cfg->cfgs[cfgIndex].altName, qPrintable(item.altName), 32);

		cfgIndex++;
	}

	if(itemCount != cfgIndex) {
		qDebug() << __func__ << __LINE__ << itemCount << cfgIndex;
		throw(KBurnException(::tr("未知异常")));
	}

	cfg->cfgMagic = 0x3033324B;
	cfg->cfgTarget = (kburnUsbIspCommandTaget)GlobalSetting::flashTarget.getValue();
	cfg->cfgCount = itemCount;
	cfg->cfgCrc32 = crc32(0, (const unsigned char *)(cfgByteArray.data() + sizeof(struct BurnImageConfig)), cfgByteArray.size() - sizeof(struct BurnImageConfig));

	qDebug() << __func__ << __LINE__ << cfgByteArray.toHex();

	setStage(::tr("等待设备上电"));
	node = reinterpret_cast<kburnDeviceNode *>(inputs.pick(0, 30));
	if (node == NULL) {
		throw KBurnException(tr("设备上电超时"));
	}

	/* TODO: 写入dfu配置 */
	setStage(::tr("写入USB 配置"), cfgByteArray.size());

	if(!K230BurnISP_WriteBurnImageConfig(node, cfgByteArray.data(), cfgByteArray.size(), K230BurningProcess::serial_isp_progress, this)) {
		throw KBurnException(tr("写入USB 配置失败"));
	}

	setStage(::tr("写入USB LOADER"), loaderFileContent.size());

	if(!K230BurnISP_RunProgram(node, loaderFileContent.data(), loaderFileContent.size(), K230BurningProcess::serial_isp_progress, this)) {
		throw KBurnException(tr("写入USB LOADER失败"));
	}

	setStage(::tr("等待USB ISP启动"));

	node = reinterpret_cast<kburnDeviceNode *>(inputs.pick(1, 10));
	if (node == NULL) {
		throw KBurnException(tr("ISP启动超时"));
	}

	usb_ok = true;
	write_seq = 0;

	return chunkSize;
}

qint64 K230BurningProcess::setalt(const QString &alt)
{
	currAltName = alt;

	if(false == K230Burn_Check_Alt(node, qPrintable(currAltName))) {
		throw KBurnException(tr("未知异常"));
	}

	chunk_size = K230Burn_Get_Chunk_Size(node, qPrintable(currAltName));

	if(0 == chunk_size) {
		throw KBurnException(tr("获取信息失败"));
	}

	return chunk_size;
}

#include "AppGlobalSetting.h"

void K230BurningProcess::cleanup(bool success) {
	qDebug() << __func__ << __LINE__;

	if(node) {
		mark_destroy_device_node(node);
	}

	if (!usb_ok) {
		return;
	}

	// uint32_t color = GlobalSetting::usbLedLevel.getValue();
	// if (success) {
	// 	color = color << 8;
	// } else {
	// 	color = color << 16;
	// }
	// kburnUsbIspLedControl(node, GlobalSetting::usbLedPin.getValue(), kburnConvertColor(color));
}

bool K230BurningProcess::step(kburn_stor_address_t address, const QByteArray &chunk) {
	// size_t block = address / chunk_size;

	return 0 < K230Burn_Write_Chunk(node, qPrintable(currAltName), write_seq++, (void *)chunk.constData(), chunk.size());
}

bool K230BurningProcess::end(kburn_stor_address_t address) {
	// size_t block = address / chunk_size;

	return 0x00 == K230Burn_Write_Chunk(node, qPrintable(currAltName), write_seq++, NULL, 0);
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
