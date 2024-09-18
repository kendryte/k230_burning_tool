#include "config.h"
#include "main.h"
#include <QString>

QString getTitleVersion() {
	QString r;
	r += " (" + ::tr("Version") + ": " + QString::fromLatin1(VERSION_STRING) + " ";

#ifndef NDEBUG
	r += ::tr("Debug");
#else
	r += ::tr("Release");
#endif
#if !IS_CI
	r += " * " + ::tr("Local Build");
#endif
	r += ")";
	return r;
}
