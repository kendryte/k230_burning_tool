#include "config.h"
#include "main.h"
#include <QString>

QString getTitleVersion() {
	QString r;
	r += " (" + ::tr("版本") + ": " + QString::fromLatin1(VERSION_STRING) + " ";

#ifndef NDEBUG
	r += ::tr("调试");
#else
	r += ::tr("发布");
#endif
#if !IS_CI
	r += " * " + ::tr("本地构建");
#endif
	r += ")";
	return r;
}
