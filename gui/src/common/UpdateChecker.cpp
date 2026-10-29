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
#include <QXmlStreamReader>

namespace {
QVersionNumber repositoryVersion(const QByteArray &data, const QString &component) {
	QXmlStreamReader xml(data);
	QVersionNumber latest;
	if (!xml.readNextStartElement() || xml.name() != QLatin1String("Updates")) return {};
	while (xml.readNextStartElement()) {
		if (xml.name() != QLatin1String("PackageUpdate")) {
			xml.skipCurrentElement();
			continue;
		}
		QString name, version;
		while (xml.readNextStartElement()) {
			if (xml.name() == QLatin1String("Name")) name = xml.readElementText();
			else if (xml.name() == QLatin1String("Version")) version = xml.readElementText();
			else xml.skipCurrentElement();
		}
		if (name == component) {
			qsizetype suffix = 0;
			const auto parsed = QVersionNumber::fromString(version, &suffix);
			if (parsed.isNull() || suffix != version.size()) return {};
			if (parsed > latest) latest = parsed;
		}
	}
	// Finish parsing so malformed trailing content is not accepted as a release.
	while (!xml.atEnd()) xml.readNext();
	return xml.hasError() ? QVersionNumber() : latest;
}

QVersionNumber portableVersion(const QByteArray &data) {
	const auto document = QJsonDocument::fromJson(data);
	const auto value = document.object().value("version");
	const int latest = value.toInt(-1);
	if (!document.isObject() || !value.isDouble() || latest < 0 || value.toDouble() != latest) return {};
	// Keep compatibility with the published major * 1000 + minor * 100 + patch feed.
	return QVersionNumber(latest / 1000, (latest % 1000) / 100, latest % 100);
}
}

UpdateChecker::UpdateChecker(QMenu *menu, const QVersionNumber &version, const QString &executable,
	bool automaticChecks, QNetworkAccessManager *networkManager)
	: QObject(menu), updatesMenu(menu), defaultMenuTitle(menu->title()), currentVersion(version) {
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
			return;
		}
		metadataUrl = installed.repository;
		metadataUrl.setPath(metadataUrl.path() + (metadataUrl.path().endsWith('/') ? "" : "/") + "Updates.xml");
		component = installed.component;
	} else {
		metadataUrl = QUrl("https://download.kendryte.com/developer/tools/k230_burningtool/k230_burningtool_lastest.txt");
	}
	network = networkManager ? networkManager : new QNetworkAccessManager(this);
	checkAction = menu->addAction(tr("Check for Updates..."));
	connect(checkAction, &QAction::triggered, this, &UpdateChecker::check);
	if (!installed.isManaged()) {
		connect(menu->addAction(tr("Download Releases...")), &QAction::triggered, this, [] {
			QDesktopServices::openUrl(QUrl("https://github.com/kendryte/k230_burning_tool/releases"));
		});
	}
	statusAction = menu->addAction(QString());
	statusAction->setEnabled(false);
	statusAction->setVisible(false);
	if (automaticChecks) QTimer::singleShot(10000, this, &UpdateChecker::check);
}

void UpdateChecker::setStatus(const QString &status) {
	statusAction->setText(status);
	statusAction->setVisible(true);
	emit statusChanged(status);
}

void UpdateChecker::check() {
	if (reply || !network) return;
	setStatus(tr("Checking for updates..."));
	checkAction->setEnabled(false);
	QNetworkRequest request(metadataUrl);
	request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
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
			const auto data = requestReply->readAll();
			const auto latest = component.isEmpty() ? portableVersion(data) : repositoryVersion(data, component);
			if (!latest.isNull()) {
				updatesMenu->setTitle(latest > currentVersion
					? tr("%1 (v%2 available)").arg(defaultMenuTitle, latest.toString()) : defaultMenuTitle);
				status = latest > currentVersion ? tr("New version available: v%1 (installed: v%2)")
					.arg(latest.toString(), currentVersion.toString()) : tr("You are up to date (v%1)").arg(currentVersion.toString());
			}
		}
		setStatus(status);
		checkAction->setEnabled(true);
		reply.clear();
		requestReply->deleteLater();
	});
}
