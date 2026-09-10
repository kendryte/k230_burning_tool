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

UpdateChecker::UpdateChecker(QMenu *menu, const QVersionNumber &version, const QString &executable, bool automaticChecks)
	: QObject(menu), currentVersion(version) {
	const auto installed = IfwInstallation::detect(executable.isEmpty() ? QCoreApplication::applicationFilePath() : executable);
	if (installed.isManaged()) {
		auto *update = menu->addAction(tr("Update Application..."));
		update->setEnabled(installed.canUpdate());
		connect(update, &QAction::triggered, this, [this] { emit maintenanceRequested(true); });
		auto *manage = menu->addAction(tr("Manage Installation..."));
		manage->setEnabled(!installed.maintenanceTool.isEmpty());
		connect(manage, &QAction::triggered, this, [this] { emit maintenanceRequested(false); });
		if (!installed.canUpdate()) {
			auto *status = menu->addAction(installed.maintenanceTool.isEmpty()
				? tr("Maintenance Tool not found") : tr("Online updates not configured"));
			status->setEnabled(false);
		}
		return;
	}
	network = new QNetworkAccessManager(this);
	checkAction = menu->addAction(tr("Check for Updates..."));
	connect(checkAction, &QAction::triggered, this, &UpdateChecker::check);
	connect(menu->addAction(tr("Download Releases...")), &QAction::triggered, this, [] {
		QDesktopServices::openUrl(QUrl("https://github.com/kendryte/k230_burning_tool/releases"));
	});
	statusAction = menu->addAction(QString());
	statusAction->setEnabled(false);
	statusAction->setVisible(false);
	if (automaticChecks) QTimer::singleShot(10000, this, &UpdateChecker::check);
}

void UpdateChecker::check() {
	if (reply || !network) return;
	statusAction->setText(tr("Checking for updates..."));
	statusAction->setVisible(true);
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
		QString status = tr("Could not check for updates");
		if (requestReply->error() == QNetworkReply::NoError && requestReply->bytesAvailable() <= 65536) {
			const auto document = QJsonDocument::fromJson(requestReply->readAll());
			const auto value = document.object().value("version");
			const int latest = value.toInt(-1);
			if (document.isObject() && value.isDouble() && latest >= 0 && value.toDouble() == latest) {
				const qint64 current = qint64(currentVersion.majorVersion()) * 1000 + currentVersion.minorVersion() * 100 + currentVersion.microVersion();
				status = latest > current ? tr("A new version is available") : tr("You are up to date");
			}
		}
		statusAction->setText(status);
		checkAction->setEnabled(true);
		reply.clear();
		requestReply->deleteLater();
	});
}
