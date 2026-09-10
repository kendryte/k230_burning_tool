#pragma once

#include <QMenu>
#include <QObject>
#include <QPointer>
#include <QVersionNumber>

class QNetworkAccessManager;
class QNetworkReply;
class QAction;

class UpdateButton : public QMenu {
    Q_OBJECT

  public:
    UpdateButton(QWidget *parent = nullptr) : QMenu(parent){};

  public slots:
    void changeTitle(QString newTitle) { setTitle(newTitle); };
};

class UpdateChecker : public QObject {
	Q_OBJECT

	UpdateButton *button;
	QNetworkAccessManager *network = nullptr;
	QPointer<QNetworkReply> reply;
	QAction *checkAction = nullptr;
	QVersionNumber currentVersion;
	void check();

  signals:
	void maintenanceRequested(bool update);

  public:
	explicit UpdateChecker(UpdateButton *button, const QVersionNumber &version, const QString &executable = QString());
};
