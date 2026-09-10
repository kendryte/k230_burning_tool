#include "UpdateChecker.h"
#include "IfwInstallation.h"
#include <QAction>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTimer>

UpdateChecker::UpdateChecker(UpdateButton *button, const QVersionNumber &version, const QString &executable)
	: QObject(button), button(button), currentVersion(version) {
	button->setEnabled(true);
	const auto installed = IfwInstallation::detect(executable.isEmpty() ? QCoreApplication::applicationFilePath() : executable);
	if (installed.isManaged()) {
		button->setTitle(tr("Installation"));
		auto *update = button->addAction(installed.canUpdate() ? tr("Check for Updates...") : tr("Online updates not configured"));
		update->setEnabled(installed.canUpdate());
		connect(update, &QAction::triggered, this, [this] { emit maintenanceRequested(true); });
		auto *manage = button->addAction(tr("Manage / Uninstall..."));
		manage->setEnabled(!installed.maintenanceTool.isEmpty());
		connect(manage, &QAction::triggered, this, [this] { emit maintenanceRequested(false); });
		return;
	}
	network = new QNetworkAccessManager(this);
	checkAction = button->addAction(tr("Check for Updates..."));
	connect(checkAction, &QAction::triggered, this, &UpdateChecker::check);
	connect(button->addAction(tr("Download Releases...")), &QAction::triggered, this, [] {
		QDesktopServices::openUrl(QUrl("https://github.com/kendryte/k230_burning_tool/releases"));
	});
	QTimer::singleShot(10000, this, &UpdateChecker::check);
}

void UpdateChecker::check() {
	if (reply || !network) return;
	button->setTitle(tr("Checking Update..."));
	checkAction->setEnabled(false);
	QNetworkRequest request(QUrl("https://download.kendryte.com/developer/tools/k230_burningtool/k230_burningtool_lastest.txt"));
	request.setTransferTimeout(15000);
	reply = network->get(request);
	auto *requestReply = reply.data();
	QTimer::singleShot(20000, requestReply, [requestReply] {
		if (requestReply->isRunning()) requestReply->abort();
	});
	connect(requestReply, &QNetworkReply::readyRead, this, [requestReply] {
		if (requestReply->bytesAvailable() > 65536) requestReply->abort();
	});
	connect(requestReply, &QNetworkReply::finished, this, [this, requestReply] {
		QString title = tr("Check Update Failed");
		if (requestReply->error() == QNetworkReply::NoError && requestReply->bytesAvailable() <= 65536) {
			const auto document = QJsonDocument::fromJson(requestReply->readAll());
			const auto value = document.object().value("version");
			const int latest = value.toInt(-1);
			if (document.isObject() && value.isDouble() && latest >= 0 && value.toDouble() == latest) {
				const qint64 current = qint64(currentVersion.majorVersion()) * 1000 + currentVersion.minorVersion() * 100 + currentVersion.microVersion();
				title = latest > current ? tr("New Version") : tr("Latest");
			}
		}
		button->setTitle(title);
		checkAction->setEnabled(true);
		reply.clear();
		requestReply->deleteLater();
	});
}
