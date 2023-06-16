#include "BurningProcess.h"
#include "EventStack.h"
#include <QString>

class K230BurningProcess : public BurningProcess {
  private:
	bool usb_ok = false;
	EventStack inputs;
	// kburnDeviceMemorySizeInfo devInfo;
	kburnDeviceNode *node = NULL;
	QString usbPath;
	QString _detailInfo;

	size_t write_seq;
	uint32_t chunk_size;
	QString currAltName;

	static void serial_isp_progress(void *, const kburnDeviceNode *, size_t, size_t);

	qint64 prepare(QList<struct BurnImageItem> &imageList);
	bool step(kburn_stor_address_t address, const QByteArray &chunk);
	void cleanup(bool success);
	// void recreateDeviceStatus(const kburnDeviceNode *);
 	bool end(kburn_stor_address_t address);
	qint64 setalt(const QString &alt);

  public:
	K230BurningProcess(KBMonCTX scope, const K230BurningRequest *request);
	QString getTitle() const;

	bool pollingDevice(kburnDeviceNode *node, BurnLibrary::DeviceEvent event);
	const QString &getDetailInfo() const { return _detailInfo; }
};
