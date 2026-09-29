#include "BurningProcess.h"
#include "BurnLibrary.h"
#include "main.h"
#include "MyException.h"
#include <QByteArray>
#include <QByteArrayView>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QFuture>
#include <QPromise>
#include <QThread>
#include <QElapsedTimer>
#include <QMutexLocker>

#include "AppGlobalSetting.h"

#include <algorithm>
#include <limits>

static QString formatTransferSpeed(double bytesPerSecond) {
	if (bytesPerSecond >= 1024.0 * 1024.0) {
		return QString::number(bytesPerSecond / (1024.0 * 1024.0), 'f', 2) + " MiB/s";
	}
	if (bytesPerSecond >= 1024.0) {
		return QString::number(bytesPerSecond / 1024.0, 'f', 2) + " KiB/s";
	}
	return QString::number(bytesPerSecond, 'f', 0) + " B/s";
}

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
	if (buffer) {
		delete buffer;
	}
}

void BurningProcess::setResult(const KBurnException &reason) {
	QMutexLocker locker(&stateMutex);
	if (!_isCanceled.load(std::memory_order_relaxed))
		_result = reason;
}

KBurnException BurningProcess::getReason() const {
	QMutexLocker locker(&stateMutex);
	return _result;
}

void BurningProcess::schedule() {
	bool expected = false;
	if (!isCanceled() && _isStarted.compare_exchange_strong(
			expected, true, std::memory_order_acq_rel)) {
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

	Q_ASSERT(isStarted());

	QThread::currentThread()->setObjectName("burn:" + getTitle());

	throwIfCancel();
	prepare(imageList, &total_size, &chunk_size, &block_size);
	throwIfCancel();

	setStage(::tr("Downloading..."), total_size);

	QElapsedTimer timer;
	timer.start();
	qint64 lastSpeedUpdate = 0;

	if (chunk_size > static_cast<quint64>(std::numeric_limits<qsizetype>::max()))
		throw KBurnException(tr("Device returned an excessive write chunk size"));
	buffer = new QByteArray(static_cast<qsizetype>(chunk_size), 0);

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
		if (!item.fileSize || item.dataSize > item.fileSize ||
		    item.fileOffset > static_cast<quint64>(imageFile.size()) ||
		    item.dataSize > static_cast<quint64>(imageFile.size()) - item.fileOffset ||
		    item.fileOffset > static_cast<quint64>(std::numeric_limits<qint64>::max()) ||
		    !imageFile.seek(static_cast<qint64>(item.fileOffset))) {
			throw KBurnException(tr("Invalid image source range") +
			                     " (" + item.fileName + ")");
		}

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
				quint64 page_size_with_oob =
					static_cast<quint64>(flag_val1) + flag_val2;
				if (!page_size_with_oob || chunk_size / page_size_with_oob <= 1)
					throw KBurnException(tr("Invalid SPI NAND OOB chunk layout"));

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

		quint64 logicalRemaining = item.fileSize;
		quint64 dataRemaining = item.dataSize;
		QCryptographicHash dataHash(QCryptographicHash::Sha256);

		while (logicalRemaining) {
			throwIfCancel();

			const quint64 logicalBytes = std::min<quint64>(
				logicalRemaining, static_cast<quint64>(buffer->size()));
			const quint64 dataBytes = std::min(logicalBytes, dataRemaining);
			if (dataBytes) {
				const qint64 bytesRead = imageFile.read(
					buffer->data(), static_cast<qint64>(dataBytes));
				if (bytesRead != static_cast<qint64>(dataBytes)) {
					throw KBurnException(tr("Read image file failed") +
							     " (" + item.fileName + ")");
				}
				if (!item.dataSha256.isEmpty())
					dataHash.addData(QByteArrayView(buffer->constData(), bytesRead));
			}
			if (logicalBytes > dataBytes) {
				memset(buffer->data() + static_cast<qsizetype>(dataBytes),
				       item.paddingValue,
				       static_cast<size_t>(logicalBytes - dataBytes));
			}

			quint64 transferBytes = logicalBytes;
			if (transferBytes % block_size) {
				const quint64 padding = block_size - transferBytes % block_size;
				if (transferBytes + padding > static_cast<quint64>(buffer->size()))
					throw KBurnException(tr("Invalid aligned write chunk size"));
				memset(buffer->data() + static_cast<qsizetype>(transferBytes), 0,
				       static_cast<size_t>(padding));
				transferBytes += padding;
			}

			if (step(address, *buffer, transferBytes)) {
				address += transferBytes;

				burned_size += transferBytes;
				setProgress(burned_size);

				qint64 elapsed = timer.elapsed();
				if (elapsed > 0 &&
				    (elapsed - lastSpeedUpdate >= 250 || burned_size >= total_size)) {
					double bytesPerSecond = burned_size * 1000.0 / elapsed;
					emit speedChanged(formatTransferSpeed(bytesPerSecond));
					lastSpeedUpdate = elapsed;
				}
			} else {
				throw KBurnException(tr("Write File to Device failed") + tr(" at 0x") + QString::number(address, 16) + tr(", Message: ") + errormsg());
			}

			logicalRemaining -= logicalBytes;
			dataRemaining -= dataBytes;
		}
		imageFile.close();

		if (dataRemaining ||
		    (!item.dataSha256.isEmpty() && dataHash.result() != item.dataSha256)) {
			throw KBurnException(tr("Image SHA-256 mismatch") +
			                     " (" + item.partName + ")");
		}

		if (!end(address)) {
			throw KBurnException(tr("Finish writing file to device failed") +
			                     " (" + item.fileName + "), " + errormsg());
		}

		if (block_size_bak) {
			block_size = block_size_bak;
			block_size_bak = 0;
		}
		if (chunk_size_bak) {
			chunk_size = chunk_size_bak;
			buffer->resize(chunk_size);
			chunk_size_bak = 0;
		}
	}

	qint64 elapsedTime = timer.elapsed();
	double bytesPerSecond = elapsedTime > 0
		? total_size * 1000.0 / elapsedTime
		: 0.0;
	QString speedStr = formatTransferSpeed(bytesPerSecond);
	BurnLibrary::instance()->localLog(
		QStringLiteral("Total bytes written: %1, elapsed time (ms): %2, speed: %3")
			.arg(total_size).arg(elapsedTime).arg(speedStr));

	setStage(::tr("Download complete, Speed: ") + speedStr, 100);

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
		BurnLibrary::instance()->localLog(QStringLiteral("Burn failed: %1").arg(e.errorMessage));
		setResult(e); // may get result after return
		emit failed(getReason());
		cleanup(false);
	} catch (...) {
		BurnLibrary::instance()->localLog(QStringLiteral("Burn failed: unexpected exception"));
		setResult(KBurnException("Unknown Error"));
		emit failed(getReason());
		cleanup(false);
	}
	_isCompleted.store(true, std::memory_order_release);
	emit finished();
}

void BurningProcess::setProgress(quint64 value) {
	throwIfCancel();
	emit progressChanged(value);
}

void BurningProcess::setStage(const QString &title, quint64 bytes) {
	throwIfCancel();
	emit progressChanged(0);
	emit bytesChanged(bytes);
	emit stageChanged(title);
}

void BurningProcess::setStageTitle(const QString &title) {
	throwIfCancel();
	emit stageChanged(title);
}

void BurningProcess::cancel(const KBurnException reason) {
	bool notify = false;
	{
		QMutexLocker locker(&stateMutex);
		if (!_isCanceled.load(std::memory_order_relaxed)) {
			if (_result.errorCode == KBurnNoErr)
				_result = reason;
			_isCanceled.store(true, std::memory_order_release);
			notify = true;
		}
	}
	if (notify)
		emit cancelRequested();
}

void BurningProcess::cancel() {
	cancel(KBurnException(KBurnCommonError::KBurnUserCancel, ::tr("User Canceled")));
}

void BurningProcess::throwIfCancel() {
	QMutexLocker locker(&stateMutex);
	if (_isCanceled.load(std::memory_order_acquire)) {
		throw _result.errorCode == KBurnNoErr
			? KBurnException(KBurnCommonError::KBurnUserCancel,
					 tr("User Canceled"))
			: _result;
	}
	if (_result.errorCode != KBurnNoErr)
		throw _result;
}
