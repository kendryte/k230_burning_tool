#pragma once

#include <QMenu>
#include <QObject>
#include <QPointer>
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
	QVersionNumber currentVersion;
	void check();

  signals:
	void maintenanceRequested(bool update);

  public:
	explicit UpdateChecker(QMenu *menu, const QVersionNumber &version, const QString &executable = QString(), bool automaticChecks = true);
};
