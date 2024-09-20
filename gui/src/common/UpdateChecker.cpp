#include "UpdateChecker.h"
#include "BurnLibrary.h"
#include "config.h"
#include "main.h"
#include <QEventLoop>
#include <QException>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMenu>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QThreadPool>
#include <QTimer>

#define CURRENT_VERSION_MAJOR		2
#define CURRENT_VERSION_MINOR		0
#define CURRENT_VERSION_PATCH		0

UpdateChecker::UpdateChecker(UpdateButton *button) : button(button) {
	setAutoDelete(false);
	connect(this, &UpdateChecker::giveTip, button, &UpdateButton::changeTitle);
	QTimer::singleShot(10000, [=] { QThreadPool::globalInstance()->start(this); });
}

void UpdateChecker::run() {
	try {
		_run();
	} catch (QException e) {
		emit giveTip(::tr("Check Update Failed"));
	}
}

void UpdateChecker::_run() {
	emit giveTip(::tr("Checking Update..."));

	QNetworkAccessManager mgr;
	QNetworkRequest request{QUrl("https://kendryte-download.canaan-creative.com/k230/downloads/burn_tool/k230_burningtool_lastest.txt")};

	QNetworkReply *reply = mgr.get(request);

	QEventLoop eventLoop;
	QObject::connect(reply, &QNetworkReply::finished, &eventLoop, &QEventLoop::quit);
	eventLoop.exec();

	if (reply->error() != QNetworkReply::NoError) {
		BurnLibrary::instance()->onDebugLog(false, ::tr("Can't Checking Update: ") + reply->errorString());
		emit giveTip(::tr("Can't Checking Update: ") + QString::number(reply->error()));
		return;
	}

	QJsonDocument jsonResponse = QJsonDocument::fromJson(reply->readAll());

	int new_ver = jsonResponse.object().value("version").toInt();
	QString new_hash = jsonResponse.object().value("hash").toString().toLower();

	int now_ver = CURRENT_VERSION_MAJOR * 1000 + CURRENT_VERSION_MINOR * 100 + CURRENT_VERSION_PATCH;
	QString now_hash = QString::fromLatin1(VERSION_HASH).toLower();

	BurnLibrary::instance()->localLog(QStringLiteral("newest version is: %1").arg(new_ver));
	BurnLibrary::instance()->localLog(QStringLiteral("my     version is: %1").arg(now_ver));

	BurnLibrary::instance()->localLog(QStringLiteral("newest hash is: ") + new_hash);
	BurnLibrary::instance()->localLog(QStringLiteral("my     hash is: ") + now_hash);

	if(new_ver > now_ver) {
		emit giveTip(::tr("New Version"));
	} else {
		emit giveTip(::tr("Latest"));
	}
}
