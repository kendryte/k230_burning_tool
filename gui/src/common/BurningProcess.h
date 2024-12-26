#pragma once

#include "BurningRequest.h"
#include "BurnLibrary.h"
#include "MyException.h"
#include <public/canaan-burn.h>
#include <QFile>
#include <QObject>
#include <QRunnable>
#include <QString>

#include "common/BurnImageItem.h"

class BurningProcess : public QObject, public QRunnable {
	Q_OBJECT

	// QFile imageFile;
	QList<struct BurnImageItem>	imageList;
	class QByteArray *buffer = NULL;
	bool _isCanceled = false;
	bool _isStarted = false;
	bool _isCompleted = false;
	KBurnException _result;

  protected:
    BurningProcess(KBMonCTX scope, const BurningRequest *request);

	KBMonCTX scope;
	bool isAutoCreate = false;

	class QDataStream *imageStream = NULL;

	void setResult(const KBurnException &reason);
	void throwIfCancel();

	void setStage(const QString &title, int bytesToWrite = 0);
	void setProgress(int writtenBytes);

	virtual int prepare(QList<struct BurnImageItem>	&imageList, quint64 *total_size, quint64 *chunk_size) = 0;
	virtual bool begin(struct BurnImageItem& item) = 0;
	virtual bool step(quint64 address, const QByteArray &chunk) = 0;
	virtual bool end(quint64 address) = 0;
	virtual void cleanup(bool success){};
	virtual QString errormsg() = 0;
	virtual void ResetChip(void) = 0;

  public:
    // const qint64 imageSize;
    ~BurningProcess();

	void run() Q_DECL_NOTHROW;
	void _run();
	void schedule();

	virtual QString getTitle() const { return "UNKNOWN JOB"; }
	virtual const QString &getDetailInfo() const = 0;
	virtual bool pollingDevice(kburnDeviceNode *node, BurnLibrary::DeviceEvent event) = 0;
	const KBurnException &getReason() { return _result; }

	bool isCanceled() const { return _isCanceled; }
	bool isStarted() const { return _isStarted; }
	bool isCompleted() const { return _isCompleted; }

	virtual void cancel(const KBurnException reason);
	virtual void cancel();

	enum BurnStage {
		Starting,
		Serial,
		Usb,
	};
  signals:
    void deviceStateNotify();

	void stageChanged(const QString &title);
	void bytesChanged(int maximumBytes);
	void progressChanged(int writtenBytes);

	void cancelRequested();
	void completed(const QString &speed);
	void failed(const KBurnException &reason);
	void updateTitle();
};
