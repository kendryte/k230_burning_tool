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
	// 	setResult(KBurnException(::tr("无法打开系统镜像文件") + " (" + request->systemImageFile + ")"));
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
	qint64 total_size = 0, burned_size = 0;
	bool founLoader = false;

	Q_ASSERT(_isStarted);

	QThread::currentThread()->setObjectName("burn:" + getTitle());
	kburn_stor_address_t address = 0;

	throwIfCancel();

	foreach(struct BurnImageItem item, imageList) {
		qDebug() << "address" << item.address << "file size" << item.size << "altname" << item.altName << "filename" << item.fileName;

		if(item.altName == QString("loader")) {
			founLoader = true;
			continue;
		}
		total_size += item.size;
	}

	if(false == founLoader) {
		throw(KBurnException(::tr("无法找到Loader")));
	}

	qint64 chunkSize = prepare(imageList);
	buffer = new QByteArray(chunkSize, 0);
	setStage(::tr("下载中"), total_size);

	QElapsedTimer timer;
	timer.start();

	foreach(struct BurnImageItem item, imageList) {
		if(item.altName == QString("loader")) {
			continue;
		}
		
		qDebug() << "address" << item.address << "file size" << item.size << "altname" << item.altName << "filename" << item.fileName;

		imageFile.setFileName(item.fileName);

		if (!imageFile.open(QIODeviceBase::ReadOnly)) {
			throw(KBurnException(::tr("无法打开镜像文件") + " (" + item.fileName + ")"));
		}

		chunkSize = setalt(item.altName);
		buffer->resize(chunkSize);

		imageStream = new QDataStream(&imageFile);

		while (!imageStream->atEnd()) {
			imageStream->readRawData(buffer->data(), buffer->size());

			if (step(address, *buffer)) {
				address += chunkSize;
				setProgress(address);
			} else {
				throw KBurnException(tr("设备写入失败，地址: 0x") + QString::number(address, 16));
			}
		}
		imageFile.close();

		delete imageStream;
		imageStream = nullptr;
	}
	end(address);

	qDebug() << "耗时" << timer.elapsed() << "ms";

	setStage(::tr("完成"), 100);
	emit completed();
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
		setResult(KBurnException("未知错误"));
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
	cancel(KBurnException(KBurnCommonError::KBurnUserCancel, ::tr("用户取消操作")));
}

void BurningProcess::throwIfCancel() {
	if (_result.errorCode != KBurnNoErr) {
		throw _result;
	}
	if (_isCanceled) {
		throw KBurnException(tr("用户主动取消"));
	}
}
