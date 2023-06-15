#pragma once

#include <QString>
#include <QList>

struct BurnImageItem
{
	char altName[32];
	uint32_t address;
	uint32_t size;
	QString fileName; 
};
