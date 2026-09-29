#include "BurningProcess.h"
#include "EventStack.h"
#include <QCryptographicHash>
#include <QMutex>
#include <QString>

#include <public/canaan-burn.h>

class K230BurningProcess : public BurningProcess {
  private:
	bool usb_ok = false;
	EventStack inputs;
	// kburnDeviceMemorySizeInfo devInfo;
	kburnDeviceNode *node = NULL;
	kburn_t *kburn = NULL;
	QMutex kburnMutex;
	QString usbPath;
	QString _detailInfo;

	size_t write_seq;
	uint32_t chunk_size;
	QString currAltName;
	bool verifyAfterWrite = false;
	quint64 verifyOffset = 0;
	quint64 verifySize = 0;
	quint64 verifyPageSize = 0;
	quint64 verifyOobSize = 0;
	QCryptographicHash verifyHash;

	static void serial_isp_progress(void *, const kburnDeviceNode *, size_t, size_t);

	int prepare(QList<struct BurnImageItem> &imageList, quint64 *total_size, quint64 *chunk_size, quint64 *blk_size);
	bool begin(struct BurnImageItem& item);
	bool step(quint64 address, const QByteArray &chunk, quint64 chunk_size);
	// void recreateDeviceStatus(const kburnDeviceNode *);
 	bool end(quint64 address);
	void cleanup(bool success);
	QString errormsg();
	void ResetChip(void);

  public:
	K230BurningProcess(KBMonCTX scope, const K230BurningRequest *request);
	QString getTitle() const;

	bool pollingDevice(kburnDeviceNode *node, BurnLibrary::DeviceEvent event);
	void cancel(const KBurnException reason) override;
	void cancel() override;
	const QString &getDetailInfo() const { return _detailInfo; }
};
