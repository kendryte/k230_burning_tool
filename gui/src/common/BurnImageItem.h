#pragma once

#include <QString>
#include <QList>

#define MAGIC_NUM   0x3033324B // "K230"

struct BurnImageItem
{
	char altName[32];
	uint32_t address;
	uint32_t size;
	QString fileName; 
};

struct BurnImageConfigItem {
	uint32_t    address;
	uint32_t    size;
	char        altName[32];
};

struct BurnImageConfig {
	uint32_t cfgMagic;
	uint32_t cfgTarget;
	uint32_t cfgCount;
	uint32_t cfgCrc32;
	struct BurnImageConfigItem cfgs[0];
};
