#pragma once

#include <QByteArray>
#include <QString>
#include <QList>

#define MAGIC_NUM   0x3033324B // "K230"

#define KBURN_FLAG_SPI_NAND_WRITE_WITH_OOB    (1024)

#define KBURN_FLAG_FLAG(flg)    ((flg >> 48) & 0xffff)
#define KBURN_FLAG_VAL1(flg)    ((flg >> 16) & 0xffffffff)
#define KBURN_FLAG_VAL2(flg)    (flg & 0xffff)

struct BurnImageItem
{
	bool operator < (const struct BurnImageItem& other) const {
		return partOffset < other.partOffset;
    }

	QString partName;
	quint64 partOffset = 0;
	quint64 partSize = 0;
	quint64 partEraseSize = 0;
	uint64_t partFlag = 0;

	QString fileName;
	quint64 fileOffset = 0;
	quint64 dataSize = 0;
	quint64 fileSize = 0;
	quint8 paddingValue = 0;
	QByteArray dataSha256;
};
