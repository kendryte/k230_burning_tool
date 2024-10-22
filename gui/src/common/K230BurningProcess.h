#include "BurningProcess.h"
#include "EventStack.h"
#include <QString>

#include <public/canaan-burn.h>

class K230BurningProcess : public BurningProcess {
  private:
	bool usb_ok = false;
	EventStack inputs;
	// kburnDeviceMemorySizeInfo devInfo;
	kburnDeviceNode *node = NULL;
	kburn_t *kburn = NULL;
	QString usbPath;
	QString _detailInfo;

	size_t write_seq;
	uint32_t chunk_size;
	QString currAltName;

	static void serial_isp_progress(void *, const kburnDeviceNode *, size_t, size_t);

	int prepare(QList<struct BurnImageItem> &imageList, quint64 *total_size, quint64 *chunk_size);
	bool begin(kburn_stor_address_t address, kburn_stor_block_t size);
	bool step(kburn_stor_address_t address, const QByteArray &chunk);
	// void recreateDeviceStatus(const kburnDeviceNode *);
 	bool end(kburn_stor_address_t address);
	void cleanup(bool success);
	QString errormsg();
	void ResetChip(void);

  public:
	K230BurningProcess(KBMonCTX scope, const K230BurningRequest *request);
	QString getTitle() const;

	bool pollingDevice(kburnDeviceNode *node, BurnLibrary::DeviceEvent event);
	const QString &getDetailInfo() const { return _detailInfo; }
};
