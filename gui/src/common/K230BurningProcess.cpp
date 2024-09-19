#include "K230BurningProcess.h"
#include "AppGlobalSetting.h"
#include "main.h"
#include "MyException.h"

#include <QFileInfo>
#include <QCryptographicHash>

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

int K230BurningProcess::prepare(QList<struct BurnImageItem> &imageList, quint64 *total_size, quint64 *chunk_size) {
	bool foundLoader = false;
	int max_offset = 0, curr_offset = 0;
	struct BurnImageItem loader;

	foreach(struct BurnImageItem item, imageList) {
		if(item.altName == QString("loader")) {
			foundLoader = true;
			loader = item;
			continue;
		}

		*total_size += item.size;
		curr_offset = item.address + item.size;
		if(curr_offset > max_offset) {
			max_offset = curr_offset;
		}
	}

	if(false == foundLoader) {
		throw(KBurnException(::tr("Can't find Loader")));
	}

	QFile loaderFile(loader.fileName);
	if (!loaderFile.open(QIODeviceBase::ReadOnly)) {
		throw(KBurnException(::tr("Can't Open Image File") + " (" + loader.fileName + ")"));
	}
	QByteArray loaderFileContent = loaderFile.readAll();
	loaderFile.close();

	setStage(::tr("Waiting Stage1 Device"));
	node = reinterpret_cast<kburnDeviceNode *>(inputs.pick(0, 30));
	if (node == NULL) {
		throw KBurnException(tr("Timeout for Waiting Device Plug in"));
	}

	setStage(::tr("Write USB LOADER"), loaderFileContent.size());

	if(!K230BurnISP_RunProgram(node, loaderFileContent.data(), loaderFileContent.size(), K230BurningProcess::serial_isp_progress, this)) {
		throw KBurnException(tr("Write USB LOADER Failed"));
	}

	setStage(::tr("Waiting Stage2 Device"));

	node = reinterpret_cast<kburnDeviceNode *>(inputs.pick(1, 5));
	if (node == NULL) {
		throw KBurnException(tr("Loader Boot Failed"));
	}

	setStage(::tr("Init Device"));

	kburnUsbIspCommandTaget isp_target = (kburnUsbIspCommandTaget)GlobalSetting::flashTarget.getValue();

	if(NULL == (kburn = kburn_create(node))) {
		throw KBurnException(tr("Device Memory error"));
	}

	if (false == kburn_probe(kburn, isp_target, (uint64_t*)chunk_size)) {
		throw KBurnException(tr("Device Can't find Medium as Configured"));
	}

	quint64 capacity = kburn_get_capacity(kburn);

	if(0x00 == capacity) {
		throw KBurnException(tr("Device Can't find Medium as Configured"));
	}

	if(max_offset > capacity) {
		throw KBurnException(tr("Image Bigger than Device Medium Capacity, %1 > %2").arg(max_offset).arg(capacity));
	}

	qDebug() << QString("Medium %1 capacity %2").arg(isp_target).arg(capacity);

	setStage(::tr("Start Downloading..."));

	usb_ok = true;
	write_seq = 0;

	return 0;
}

bool K230BurningProcess::begin(kburn_stor_address_t address, kburn_stor_block_t size)
{
#if 0
	if(KBURN_USB_ISP_SPI_NOR == kburn_get_medium_type(kburn)) {
		quint64 _addr = address, _size = size;
		if(false == kburn_parse_erase_config(kburn, &_addr, &_size)) {
			return false;
		}

		QString str_range = QString("0x%1 - 0x%2").arg(QString::number(_addr, 16)).arg(QString::number(_size, 16));

		setStage(::tr("Erase, ") + str_range, _size);

		if(false == kburn_erase(kburn, _addr, _size, 20)) {
			return false;
		}
	}
#else
	quint64 _addr = address, erase_size;

	if(0x00 == (erase_size = kburn_get_erase_size(kburn))) {
		throw KBurnException(tr("Unknown Error"));
	}

	if(0x00 != (_addr % erase_size)) {
		throw KBurnException(tr("Image Start Offset %1 Should Align to %2 Bytes").arg(address).arg(erase_size));
	}
#endif

	return kburn_write_start(kburn, address, size);
}

bool K230BurningProcess::step(kburn_stor_address_t address, const QByteArray &chunk)
{
	return kburn_write_chunk(kburn, (void *)chunk.constData(), chunk.size());
}

bool K230BurningProcess::end(kburn_stor_address_t address) {
	return kbun_write_end(kburn);
}

QString K230BurningProcess::errormsg()
{
	char *msg = kburn_get_error_msg(kburn);

	return QString(msg);
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
		this->cancel(KBurnException(::tr("Device Disconnected")));
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
