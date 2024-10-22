#pragma once

#include "main.h"
#include <QMap>
#include <QObject>
#include <QString>

class BurnLibrary : public QObject {
	Q_OBJECT

	QWidget *const parent;
	KBMonCTX ctx;
	static class BurnLibrary *_instance;

	kburnDebugColors previousColors;

	on_debug_log_t previousOnDebugLog;
	// on_device_list_change_t previousOnDeviceListChange;
	on_before_device_open_t previousOnBeforeDeviceOpen;

	on_device_connect_t previousOnConnect;
	on_device_disconnect_t previousOnDisconnect;
	on_device_confirmed_t previousOnConfirmed;

	// kburnSerialDeviceList list = {0, NULL};
	// QList<kburnSerialDeviceInfoSlice> knownSerialPorts;
	QMap<QString, class BurningProcess *> jobs; // FIXME: need mutex

	BurnLibrary(QWidget *parent);

	void fatalAlert(kburn_err_t err);

	class QThreadPool *_pool;

  public:
	bool burnCtrlAutoBurn;

    ~BurnLibrary();

	// void reloadList();
	void start();

	static void createInstance(QWidget *parent);
	static void deleteInstance();
	static KBMonCTX context();
	static BurnLibrary *instance();

	class BurningProcess *prepareBurning(const class BurningRequest *request);
	bool deleteBurning(class BurningProcess *task);
	void executeBurning(class BurningProcess *task);
	bool hasBurning(const BurningRequest *req);
	uint getBurningJobCount() { return jobs.size(); }

	QThreadPool *getThreadPool() { return _pool; }

	enum DeviceEvent {
		InvalidEvent = 0,
		BootRomConnected,
		BootRomDisconnected,
		BootRomConfirmed,
		UbootISPConnected,
		UbootISPDisconnected,
		UbootISPConfirmed,
	};

	void localLog(const QString &line);

  private:
    void handleDebugLog(kburnLogType type, const char *message);

	bool handleBeforeDeviceOpen(const char* const path);

	bool handleDeviceConnect(kburnDeviceNode *dev);
    void handleDeviceDisonnect(kburnDeviceNode *dev);
    bool handleHandleDeviceConfirmed(kburnDeviceNode *dev);

  signals:
    void jobListChanged();
	void onBeforDeviceOpen(QString path);
    void onDebugLog(bool isTrace, QString message);

  public slots:
	void setAutoBurnFlag(bool isAuto);
};
