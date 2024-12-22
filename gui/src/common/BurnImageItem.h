#pragma once

#include <QString>
#include <QList>

#define MAGIC_NUM   0x3033324B // "K230"

struct BurnImageItem
{
	bool operator < (const struct BurnImageItem& other) const {
		return partOffset < other.partOffset;
    }

	QString partName;
	uint32_t partOffset;
	uint32_t partSize;

	QString fileName; 
	uint32_t fileSize;
};
