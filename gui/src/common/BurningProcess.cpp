#include "BurningProcess.h"
#include "BurnLibrary.h"
#include "main.h"
#include "MyException.h"
#include <QByteArray>
#include <QDataStream>
#include <QFile>
#include <QFileInfo>
#include <QFuture>
#include <QPromise>
#include <QThread>
#include <QElapsedTimer>

#include "AppGlobalSetting.h"

BurningProcess::BurningProcess(KBMonCTX scope, const BurningRequest *request)
	: scope(scope), imageList(request->imageList), isAutoCreate(request->isAutoCreate) {
	// imageFile(request->systemImageFile), imageSize(imageFile.size()) {
	this->setAutoDelete(false);

	// if (!imageFile.open(QIODeviceBase::ReadOnly)) {
	// 	setResult(KBurnException(::tr("Can't Open Image File") + " (" + request->systemImageFile + ")"));
	// 	return;
	// }
}

BurningProcess::~BurningProcess() {
	if (imageStream) {
		delete imageStream;
	}
	if (buffer) {
		delete buffer;
	}
}

void BurningProcess::setResult(const KBurnException &reason) {
	_result = reason;
}

void BurningProcess::schedule() {
	if (!_isStarted && !_isCanceled) {
		_isStarted = true;
		BurnLibrary::instance()->getThreadPool()->start(this);
	}
}

void BurningProcess::_run() {
	QFile imageFile;

	bool founLoader = false;
	quint64 total_size = 0, burned_size = 0, chunk_size = 8192, block_size = 0, part_flag = 0;

	quint32 flag_flag, flag_val1, flag_val2;
	quint64 block_size_bak = 0, chunk_size_bak = 0;

	kburn_stor_address_t address = 0;

	kburnUsbIspCommandTaget isp_target = (kburnUsbIspCommandTaget)GlobalSetting::flashTarget.getValue();

	Q_ASSERT(_isStarted);

	QThread::currentThread()->setObjectName("burn:" + getTitle());

	throwIfCancel();
	prepare(imageList, &total_size, &chunk_size, &block_size);
	throwIfCancel();

	setStage(::tr("Downloading..."), total_size);

	QElapsedTimer timer;
	timer.start();

	buffer = new QByteArray(chunk_size, 0);

	foreach(struct BurnImageItem item, imageList) {
		if(item.partName == QString("loader")) {
			continue;
		}

		throwIfCancel();

		BurnLibrary::instance()->localLog(QStringLiteral("Partition %1, offset %2, size %3; File %4, size %5").arg(item.partName).arg(item.partOffset).arg(item.partSize).arg(item.fileName).arg(item.fileSize));

		imageFile.setFileName(item.fileName);
		if (!imageFile.open(QIODeviceBase::ReadOnly)) {
			throw(KBurnException(::tr("Can't Open Image File") + " (" + item.fileName + ")"));
		}
		imageStream = new QDataStream(&imageFile);

		address = item.partOffset;
		if(false == begin(item)) {
			throw(KBurnException(::tr("Start Write File to Device failed") + " (" + item.fileName + ")"));
		}

		part_flag = item.partFlag;
		if(0x00 != part_flag) {
			flag_flag = KBURN_FLAG_FLAG(part_flag);
			flag_val1 = KBURN_FLAG_VAL1(part_flag);
			flag_val2 = KBURN_FLAG_VAL2(part_flag);

			BurnLibrary::instance()->localLog(QStringLiteral("Flag: flag %1, val1 %2, val2 %3").arg(flag_flag).arg(flag_val1).arg(flag_val2));

			if((KBURN_USB_ISP_SPI_NAND == isp_target) && (KBURN_FLAG_SPI_NAND_WRITE_WITH_OOB == flag_flag)) {
				quint32 page_size_with_oob = flag_val1 + flag_val2;

				block_size_bak = block_size;
				chunk_size_bak = chunk_size;

				block_size = page_size_with_oob;
				chunk_size = ((chunk_size / page_size_with_oob) - 1) * page_size_with_oob;
				buffer->resize(chunk_size);

				BurnLibrary::instance()->localLog(QStringLiteral("New block size %1, chunk size %2").arg(block_size).arg(chunk_size));
			}
		} else {
			flag_flag = 0;
			flag_val1 = 0;
			flag_val2 = 0;

			if(0x00 != block_size_bak) {
				block_size = block_size_bak;
				block_size_bak = 0;
			}

			if(0x00 != chunk_size_bak) {
				buffer->resize(chunk_size_bak);
				chunk_size_bak = 0;
			}
		}

		while (!imageStream->atEnd()) {
			throwIfCancel();

			int bytesRead = imageStream->readRawData(buffer->data(), buffer->size());

			if ((bytesRead > 0) && (bytesRead % block_size != 0)) {
				memset(buffer->data() + bytesRead, 0, block_size - (bytesRead % block_size));
				bytesRead += (block_size - (bytesRead % block_size)); // Update bytesRead to reflect the padded size
			}

			if (step(address, *buffer, bytesRead)) {
				address += bytesRead;

				burned_size += bytesRead;
				setProgress(burned_size);
			} else {
				throw KBurnException(tr("Write File to Device failed") + tr(" at 0x") + QString::number(address, 16) + tr(", Message: ") + errormsg());
			}
		}
		imageFile.close();

		delete imageStream;
		imageStream = nullptr;

        end(address);
	}

	qint64 elapsedTime = timer.elapsed();
	double speed = 0.0f;
 	if (elapsedTime > 0) {
        speed = (total_size / 1024.0) / (elapsedTime / 1000.0);

		BurnLibrary::instance()->localLog(QStringLiteral("Total bytes write: %1, Elapsed time (ms): %2, Speed: %3 KB/s").arg(total_size).arg(elapsedTime).arg(speed));
    }
	QString speedStr = QString::number(speed, 'f', 2); // Format to 2 decimal places

	setStage(::tr("Download complete, Speed: ") + speedStr + ::tr("KB/s"), 100);

	bool auto_reset_chip = GlobalSetting::autoResetChipAfterBurn.getValue();
	if(auto_reset_chip) {
		BurnLibrary::instance()->localLog(QStringLiteral("Auto Reset Chip"));
		ResetChip();
	}

	emit completed(speedStr);
}

void BurningProcess::run() Q_DECL_NOTHROW {
	try {
		_run();
		cleanup(true);
	} catch (KBurnException &e) {
		setResult(e); // may get result after return
		emit failed(_result);
		cleanup(false);
	} catch (...) {
		setResult(KBurnException("Unknown Error"));
		emit failed(_result);
		cleanup(false);
	}
	_isCompleted = true;
}

void BurningProcess::setProgress(int value) {
	throwIfCancel();
	emit progressChanged(value);
}

void BurningProcess::setStage(const QString &title, int bytes) {
	throwIfCancel();
	emit progressChanged(0);
	emit bytesChanged(bytes);
	emit stageChanged(title);
}

void BurningProcess::cancel(const KBurnException reason) {
	if (!_isCanceled) {
		_isCanceled = true;
		if (_result.errorCode == KBurnNoErr) {
			setResult(reason);
		}
		emit cancelRequested();
	}
}

void BurningProcess::cancel() {
	cancel(KBurnException(KBurnCommonError::KBurnUserCancel, ::tr("User Canceled")));
}

void BurningProcess::throwIfCancel() {
	if (_result.errorCode != KBurnNoErr) {
		throw _result;
	}
	if (_isCanceled) {
		throw KBurnException(tr("User Canceled"));
	}
}
