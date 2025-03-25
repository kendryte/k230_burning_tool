#pragma once

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
	uint32_t partOffset;
	uint32_t partSize;
	uint32_t partEraseSize;
	uint64_t partFlag;

	QString fileName; 
	uint32_t fileSize;
};
