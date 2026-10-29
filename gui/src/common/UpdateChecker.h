#pragma once

#include <QMenu>
#include <QObject>
#include <QPointer>
#include <QUrl>
#include <QVersionNumber>

class QNetworkAccessManager;
class QNetworkReply;
class QAction;

class UpdateChecker : public QObject {
	Q_OBJECT

	QNetworkAccessManager *network = nullptr;
	QPointer<QNetworkReply> reply;
	QAction *checkAction = nullptr;
	QAction *statusAction = nullptr;
	QMenu *updatesMenu;
	QString defaultMenuTitle;
	QVersionNumber currentVersion;
	QUrl metadataUrl;
	QString component;
	void check();
	void setStatus(const QString &status);

  signals:
	void maintenanceRequested(bool update);
	void statusChanged(const QString &status);

  public:
	explicit UpdateChecker(QMenu *menu, const QVersionNumber &version, const QString &executable = QString(),
		bool automaticChecks = true, QNetworkAccessManager *networkManager = nullptr);
};
