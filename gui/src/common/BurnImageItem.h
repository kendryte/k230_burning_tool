#pragma once

#include <QString>
#include <QList>

#define MAGIC_NUM   0x3033324B // "K230"

struct BurnImageItem
{
	bool operator < (const struct BurnImageItem& other) const {
		return address < other.address;
    }

	char altName[32];
	uint32_t address;
	uint32_t size;
	QString fileName; 
};
