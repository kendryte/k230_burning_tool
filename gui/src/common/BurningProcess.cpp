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
	: scope(scope), imageList(request->imageList) {
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
	quint64 total_size = 0, burned_size = 0, chunk_size = 8192;
	kburn_stor_address_t address = 0;

	Q_ASSERT(_isStarted);

	QThread::currentThread()->setObjectName("burn:" + getTitle());

	throwIfCancel();

	prepare(imageList, &total_size, &chunk_size);
	buffer = new QByteArray(chunk_size, 0);

	setStage(::tr("Downloading..."), total_size);

	QElapsedTimer timer;
	timer.start();

	foreach(struct BurnImageItem item, imageList) {
		if(item.altName == QString("loader")) {
			continue;
		}

		qDebug() << "address" << item.address << "file size" << item.size << "altname" << item.altName << "filename" << item.fileName;

		address = item.address;
		if(false == begin(item.address, item.size)) {
			throw(KBurnException(::tr("Write File to Device failed") + " (" + item.fileName + ")"));
		}

		imageFile.setFileName(item.fileName);
		if (!imageFile.open(QIODeviceBase::ReadOnly)) {
			throw(KBurnException(::tr("Can't Open Image File") + " (" + item.fileName + ")"));
		}
		imageStream = new QDataStream(&imageFile);

		while (!imageStream->atEnd()) {
			int bytesRead = imageStream->readRawData(buffer->data(), buffer->size());

			if (step(address, *buffer)) {
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

        qDebug() << "Total bytes write:" << total_size;
        qDebug() << "Elapsed time (ms):" << elapsedTime;
        qDebug() << "Speed (KB/s):" << speed;
    }
	QString speedStr = QString::number(speed, 'f', 2); // Format to 2 decimal places

	setStage(::tr("Download complete, Speed: ") + speedStr + ::tr("KB/s"), 100);

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
