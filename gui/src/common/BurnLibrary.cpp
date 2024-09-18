#include "BurnLibrary.h"
#include "AppGlobalSetting.h"
#include "BurningProcess.h"

#include "main.h"
#include <public/canaan-burn.h>
#include <iostream>

#include <QDebug>
#include <QMessageBox>
#include <QThreadPool>
#include <QEventLoop>
#include <QTimer>

BurnLibrary::~BurnLibrary() {
	kburnSetColors(previousColors);

	kburnSetLogCallback(previousOnDebugLog.handler, previousOnDebugLog.context);

	kburnOnBeforeDeviceOpen(ctx, previousOnBeforeDeviceOpen.handler, previousOnBeforeDeviceOpen.context);
	kburnOnDeviceConnect(ctx, previousOnConnect.handler, previousOnConnect.context);
	kburnOnDeviceDisconnect(ctx, previousOnDisconnect.handler, previousOnDisconnect.context);
	kburnOnDeviceConfirmed(ctx, previousOnConfirmed.handler, previousOnConfirmed.context);

	_pool->waitForDone(5000);
	delete _pool;
}

BurnLibrary::BurnLibrary(QWidget *parent) : parent(parent) {
	burnCtrlAutoBurn = false;

	_pool = new QThreadPool();
	GlobalSetting::appBurnThread.connectLambda(_pool, [=](uint value) {
		if (value > 20) {
			GlobalSetting::appBurnThread.setValue(20);
			value = 20;
		}
		_pool->setMaxThreadCount(value);
	});
}

BurnLibrary *BurnLibrary::_instance = NULL;

BurnLibrary *BurnLibrary::instance() {
	return BurnLibrary::_instance;
}

KBMonCTX BurnLibrary::context() {
	Q_ASSERT(BurnLibrary::_instance != NULL);
	return BurnLibrary::_instance->ctx;
}

void BurnLibrary::createInstance(QWidget *parent) {
	Q_ASSERT(BurnLibrary::_instance == NULL);
	auto instance = new BurnLibrary(parent);

	BurnLibrary::_instance = instance;
};

void BurnLibrary::deleteInstance() {
	delete BurnLibrary::_instance;
	BurnLibrary::_instance = NULL;
}

void BurnLibrary::fatalAlert(kburn_err_t err) {
	auto e = kburnSplitErrorCode(err);
	QMessageBox msg(
		QMessageBox::Icon::Critical, ::tr("Error"),
		::tr("System Error:") + '\n' + ::tr("Type: ") + QString::number(e.kind) + ", " + ::tr("Code: ") + QString::number(e.code),
		QMessageBox::StandardButton::Close, parent);
	msg.exec();
	abort();
}

void BurnLibrary::start() {
	previousColors = kburnSetColors((kburnDebugColors){
		.red = {.prefix = "<span style=\"color: red\">",    .postfix = "</span>"},
		.green = {.prefix = "<span style=\"color: lime\">",   .postfix = "</span>"},
		.yellow = {.prefix = "<span style=\"color: yellow\">", .postfix = "</span>"},
		.grey = {.prefix = "<span style=\"color: grey\">",   .postfix = "</span>"},
	});

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpmf-conversions"
	previousOnDebugLog = kburnSetLogCallback(
		[](void *self, kburnLogType level, const char *cstr) { reinterpret_cast<BurnLibrary *>(self)->handleDebugLog(level, cstr); }, this);

	KBMonCTX context;
	kburn_err_t err = kburnMonitorCreate(&context);
	if (err != KBurnNoErr) {
		fatalAlert(err);
	}

	ctx = context;

	previousOnBeforeDeviceOpen = kburnOnBeforeDeviceOpen(
		ctx, [](void *self, const char *const path) { return reinterpret_cast<BurnLibrary *>(self)->handleBeforeDeviceOpen(path); }, this);

	previousOnConnect = kburnOnDeviceConnect(
		ctx, [](void *self, kburnDeviceNode *dev) { return reinterpret_cast<BurnLibrary *>(self)->handleDeviceConnect(dev); }, this);

	previousOnDisconnect = kburnOnDeviceDisconnect(
		ctx, [](void *self, kburnDeviceNode *dev) { return reinterpret_cast<BurnLibrary *>(self)->handleDeviceDisonnect(dev); }, this);

	previousOnConfirmed = kburnOnDeviceConfirmed(
		ctx, [](void *self, kburnDeviceNode *dev) { return reinterpret_cast<BurnLibrary *>(self)->handleHandleDeviceConfirmed(dev); }, this);

#pragma GCC diagnostic pop

	err = kburnMonitorStartWaitingDevices(ctx);
	if (err != KBurnNoErr) {
		fatalAlert(err);
	}
}

void BurnLibrary::handleDebugLog(kburnLogType level, const char *cstr) {
	auto line = QString::fromUtf8(cstr);
	std::cerr << cstr << std::endl;
	emit onDebugLog(level == KBURN_LOG_TRACE, line);
}

void BurnLibrary::localLog(const QString &line) {
	qDebug() << line;
	emit onDebugLog(false, line);
}

BurningProcess *BurnLibrary::prepareBurning(const BurningRequest *request) {
	if (hasBurning(request)) {
		return NULL;
	}
	BurningProcess *work = BurningRequest::reqeustFactory(ctx, request);

	jobs[request->getIdentity()] = work;

	return work;
}

void BurnLibrary::executeBurning(BurningProcess *work) {
	Q_ASSERT(jobs.values().contains(work));
	work->schedule();
	emit jobListChanged();
}

bool BurnLibrary::hasBurning(const BurningRequest *request) {
	auto req = new K230BurningRequest();
	req->usbPath = QString("WaitingConnect");

	bool have = jobs.contains(request->getIdentity()) || jobs.contains(req->getIdentity());

	delete req;

	return have;
}

bool BurnLibrary::deleteBurning(BurningProcess *task) {
	bool found = false;

	for (auto it = jobs.begin(); it != jobs.end(); it++) {
		if (it.value() == task) {
			jobs.erase(it);
			found = true;
			break;
		}
	}

	if (!found) {
		qErrnoWarning("wired state: work \"%.10s\" not in registry", task->getTitle().toLatin1().constData());
		return false;
	}

	emit jobListChanged();
	if (task->isStarted() && !task->isCompleted()) {
		// TODO: 有些情况需要cancel()而不是等结束
		connect(task, &BurningProcess::completed, task, &BurningProcess::deleteLater);
	} else {
		task->deleteLater();
	}

	return true;
}

static bool should_skip_job(const BurningProcess *p) {
	return !p->isStarted() || p->isCompleted() || p->isCanceled();
}

void BurnLibrary::setAutoBurnFlag(bool isAuto) {
	burnCtrlAutoBurn = isAuto;
}

bool BurnLibrary::handleBeforeDeviceOpen(const char* const path) {
	QString _path(path);

	auto request = new K230BurningRequest();
	request->usbPath = _path;

	if(hasBurning(request)) {
		qDebug() << "Jobs already start for " << _path;

		delete request;
		return true;
	}

	if(burnCtrlAutoBurn) {
		// create new jobs.
		emit onBeforDeviceOpen(_path);

		// wait 3s for burn jobs create.
		for(int i = 0; i < 6; i++) {
			QEventLoop loop;
			QTimer::singleShot(int(500), &loop, SLOT(quit()));
			loop.exec();

			if(hasBurning(request)) {
				qDebug() << "Jobs have create for " << _path;

				delete request;
				return true;
			}
		}
	}
	delete request;

	qDebug() << "Jobs not create for " << _path;

	return false;
}

bool BurnLibrary::handleDeviceConnect(kburnDeviceNode *dev) {
	for (auto p : jobs) {
		if (should_skip_job(p)) {
			continue;
		}

		if(0x02 == dev->usb->stage) {
			if (p->pollingDevice(dev, UbootISPConnected)) {
				return true;
			} else {
				return false;
			}
		}
	}

	return true;
}

void BurnLibrary::handleDeviceDisonnect(kburnDeviceNode *dev) {
	for (auto p : jobs) {
		if (should_skip_job(p)) {
			continue;
		}

		if(0x01 == dev->usb->stage) {
			p->pollingDevice(dev, BootRomDisconnected);
		} else if(0x02 == dev->usb->stage) {
			p->pollingDevice(dev, UbootISPDisconnected);
		}
	}
}

bool BurnLibrary::handleHandleDeviceConfirmed(kburnDeviceNode *dev) {
	for (auto p : jobs) {
		if (should_skip_job(p)) {
			continue;
		}

		DeviceEvent evt;
		if(0x01 == dev->usb->stage) {
			evt = BootRomConfirmed;
		} else if(0x02 == dev->usb->stage) {
			evt = UbootISPConfirmed;
		} else {
			evt = InvaildEvent;
		}

		if (p->pollingDevice(dev, evt)) {
			qDebug() << "wanted USB device handle: " << QString::number(dev->usb->deviceInfo.idVendor, 16) << ':'
					 << QString::number(dev->usb->deviceInfo.idProduct, 16) << QChar::LineFeed;
			return true;
		}
	}

	qDebug() << "unknown USB device handle: " << QString::number(dev->usb->deviceInfo.idVendor, 16) << ':'
			 << QString::number(dev->usb->deviceInfo.idProduct, 16) << QChar::LineFeed;

	return false;
}
