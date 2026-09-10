#pragma once

#include <public/canaan-burn.h>
#include <QFutureWatcher>
#include <QList>
#include <QWidget>

namespace Ui {
class SingleBurnWindow;
}

class SingleBurnWindow : public QWidget {
	Q_OBJECT

	const bool isAutoCreate;
	Ui::SingleBurnWindow *ui;
	class BurningProcess *work;
	QList<QMetaObject::Connection> stateConnections;
	bool isDone = false;
	class BurningRequest *request;
	QString progressText;
	QString writeSpeed;
	quint64 progressMaximum = 0;

	void setStartState();
	void releaseWork();
	void deleteWork(bool preserveRequest = false);
	void autoDismiss(bool success);
	void updateProgressFormat();

  public:
	explicit SingleBurnWindow(QWidget *parent, class BurningRequest *request);
	~SingleBurnWindow();
	void setProgressInfinit();

	auto getWork() const { return work; }
	bool hasFinished() const { return isDone; }
	void dismiss();
	void showEvent(QShowEvent *event);

  private slots:
	void updateTitle();
	void setCompleteState(const QString &speed);
	void setErrorState(const class KBurnException &reason);
	void setCancellingState();

	void setProgressText(const QString &tip);
	void setWriteSpeed(const QString &speed);
	void handleDeviceStateChange();
	void bytesChanged(quint64 maximumBytes);
	void progressChanged(quint64 writtenBytes);

	void on_btnRetry_clicked();
	void on_btnDismiss_clicked();
	void on_btnTerminate_clicked();

  signals:
	bool retryRequested(class BurningRequest *request);
};
