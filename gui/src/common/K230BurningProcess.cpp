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

int K230BurningProcess::prepare(QList<struct BurnImageItem> &imageList, quint64 *total_size, quint64 *chunk_size, quint64 *blk_size) {
	bool foundLoader = false;
	int max_offset = 0, curr_offset = 0, _size;
	struct BurnImageItem loader;

	foreach(struct BurnImageItem item, imageList) {
		if(item.partName == QString("loader")) {
			foundLoader = true;
			loader = item;
			continue;
		}

		_size = item.partSize;
		if(0x00 == item.partSize) {
			_size = item.fileSize;
		}

		*total_size += _size;
		curr_offset = item.partOffset + _size;
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

	for(int retry = 0; retry < 30; retry++) {
		throwIfCancel();

		node = reinterpret_cast<kburnDeviceNode *>(inputs.pick(0, 1));
		if(node) {
			break;
		}
	}
	if (node == NULL) {
		throw KBurnException(tr("Timeout for Waiting Device Plug in"));
	}

	if(0x01 == node->usb->stage) {
		setStage(::tr("Write USB LOADER"), loaderFileContent.size());

		if(!K230BurnISP_RunProgram(node, loaderFileContent.data(), loaderFileContent.size(), K230BurningProcess::serial_isp_progress, this)) {
			throw KBurnException(tr("Write USB LOADER Failed"));
		}

		mark_destroy_device_node(node);
	}

	setStage(::tr("Waiting Stage2 Device"));

	for(int retry = 0; retry < 10; retry++) {
		throwIfCancel();

		node = reinterpret_cast<kburnDeviceNode *>(inputs.pick(1, 1));
		if(node) {
			break;
		}
	}
	if (node == NULL) {
		throw KBurnException(tr("Loader Boot Failed"));
	}

	setStage(::tr("Init Device"));

	kburnUsbIspCommandTaget isp_target = (kburnUsbIspCommandTaget)GlobalSetting::flashTarget.getValue();

	if(NULL == (kburn = kburn_create(node))) {
		throw KBurnException(tr("Device Memory error"));
	}

	kburn_nop(kburn);

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
	BurnLibrary::instance()->localLog(QStringLiteral("Medium %1 capacity %2").arg(isp_target).arg(capacity));

	*blk_size = kburn_get_medium_blk_size(kburn);
	BurnLibrary::instance()->localLog(QStringLiteral("Medium %1 block size %2").arg(isp_target).arg(*blk_size));

	setStage(::tr("Start Downloading..."));

	usb_ok = true;
	write_seq = 0;

	return 0;
}

static inline unsigned long long roundup(unsigned long long value, unsigned long long align)
{
	return ((value - 1)/align + 1) * align;
}

static inline unsigned long long rounddown(unsigned long long value, unsigned long long align)
{
	return value - (value % align);
}

bool K230BurningProcess::begin(struct BurnImageItem& item)
{
    quint64 _medium_erase_size;

    quint64 _part_offset = item.partOffset;
    quint64 _part_size = item.partSize;
    quint64 _part_erase_size = item.partEraseSize;
    quint64 _part_file_size = item.fileSize;

    if (0x00 == (_medium_erase_size = kburn_get_erase_size(kburn))) {
        throw KBurnException(tr("Unknown Error"));
    }

    if (0x00 != (_part_offset % _medium_erase_size)) {
        throw KBurnException(tr("Image Start Offset %1 Should Align to %2 Bytes").arg(_part_offset).arg(_medium_erase_size));
    }

    if (0x00 != _part_erase_size) {
        quint64 _erase_start = _part_offset + _part_file_size;
        quint64 _erase_end = _part_offset + _part_erase_size;

        // Align the erase start to the medium erase size (round up)
        if (_erase_start % _medium_erase_size != 0) {
            _erase_start = ((_erase_start + _medium_erase_size - 1) / _medium_erase_size) * _medium_erase_size;
        }

        // Align the erase end to the medium erase size (round down)
        if (_erase_end % _medium_erase_size != 0) {
            _erase_end = (_erase_end / _medium_erase_size) * _medium_erase_size;
        }

        quint64 _erase_size = _erase_end > _erase_start ? _erase_end - _erase_start : 0;

        BurnLibrary::instance()->localLog(QStringLiteral("Erase 0x%1 to 0x%2").arg(_erase_start, 0, 16).arg(_erase_start + _erase_size, 0, 16));

		if(0 < _erase_size) {
			if (kburn_erase(kburn, _erase_start, _erase_size, 30)) {
				BurnLibrary::instance()->localLog(QStringLiteral("Erase 0x%1 to 0x%2 successful").arg(_erase_start, 0, 16).arg(_erase_start + _erase_size, 0, 16));
			} else {
				BurnLibrary::instance()->localLog(QStringLiteral("Erase 0x%1 to 0x%2 failed").arg(_erase_start, 0, 16).arg(_erase_start + _erase_size, 0, 16));
			}
		}
    }

    // Start writing
    return kburn_write_start(kburn, _part_offset, _part_size, _part_file_size);
}

bool K230BurningProcess::step(quint64 address, const QByteArray &chunk, quint64 chunk_size)
{
	return kburn_write_chunk(kburn, (void *)chunk.constData(), chunk_size);
}

bool K230BurningProcess::end(quint64 address) {
	return kbrun_write_end(kburn);
}

void K230BurningProcess::ResetChip(void) {
	kburn_reset_chip(kburn);
}

QString K230BurningProcess::errormsg()
{
	char *msg = kburn_get_error_msg(kburn);

	return QString(msg);
}

void K230BurningProcess::cleanup(bool success) {
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
	bool skip_boortom = false;

	BurnLibrary::instance()->localLog(QStringLiteral("Check New Device, Stage: %1, Path %2, PollPath: %3, Event %4").arg(node->usb->stage).arg(node->usb->deviceInfo.pathStr).arg(this->usbPath).arg(event));

	if(this->usbPath == QString("WaitingConnect")) {
		if((BurnLibrary::DeviceEvent::BootRomConfirmed == event) || \
			(BurnLibrary::DeviceEvent::UbootISPConfirmed == event))
		{
			isOk = true;

			if(BurnLibrary::DeviceEvent::UbootISPConfirmed == event) {
				skip_boortom = true;
			}
			this->usbPath = QString(node->usb->deviceInfo.pathStr);
			emit updateTitle();
		}
	} else {
		isOk = this->usbPath == node->usb->deviceInfo.pathStr;
	}

	if (!isOk) {
		return false;
	}

	if (event == BurnLibrary::DeviceEvent::BootRomConfirmed) {
		inputs.set(0, node);
	} else if (event == BurnLibrary::DeviceEvent::UbootISPConfirmed) {
		if(isAutoCreate || skip_boortom) {
			inputs.set(0, node);
		}
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
