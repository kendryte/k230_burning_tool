#pragma once

#include "BurningRequest.h"
#include "BurnLibrary.h"
#include "MyException.h"
#include <public/canaan-burn.h>
#include <QFile>
#include <QMutex>
#include <QObject>
#include <QRunnable>
#include <QString>

#include <atomic>

#include "common/BurnImageItem.h"

class BurningProcess : public QObject, public QRunnable {
	Q_OBJECT

	// QFile imageFile;
	QList<struct BurnImageItem>	imageList;
	class QByteArray *buffer = NULL;
	std::atomic_bool _isCanceled{false};
	std::atomic_bool _isStarted{false};
	std::atomic_bool _isCompleted{false};
	mutable QMutex stateMutex;
	KBurnException _result;

  protected:
    BurningProcess(KBMonCTX scope, const BurningRequest *request);

	KBMonCTX scope;
	bool isAutoCreate = false;

	void setResult(const KBurnException &reason);
	void throwIfCancel();

	void setStage(const QString &title, quint64 bytesToWrite = 0);
	void setStageTitle(const QString &title);
	void setProgress(quint64 writtenBytes);

	virtual int prepare(QList<struct BurnImageItem>	&imageList, quint64 *total_size, quint64 *chunk_size, quint64 *blk_size) = 0;
	virtual bool begin(struct BurnImageItem& item) = 0;
	virtual bool step(quint64 address, const QByteArray &chunk, quint64 chunk_size) = 0;
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
	KBurnException getReason() const;

	bool isCanceled() const { return _isCanceled.load(std::memory_order_acquire); }
	bool isStarted() const { return _isStarted.load(std::memory_order_acquire); }
	bool isCompleted() const { return _isCompleted.load(std::memory_order_acquire); }

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
	void bytesChanged(quint64 maximumBytes);
	void progressChanged(quint64 writtenBytes);
	void speedChanged(const QString &speed);

	void cancelRequested();
	void completed(const QString &speed);
	void failed(const KBurnException &reason);
	void finished();
	void updateTitle();
};
