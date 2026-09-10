#include "config.h"
#include "main.h"
#include <QString>
#include <QVersionNumber>

QString getTitleVersion() {
	QString r;
	const auto version = QVersionNumber(CURRENT_VERSION_MAJOR, CURRENT_VERSION_MINOR, CURRENT_VERSION_PATCH).toString();
	r += " (" + ::tr("Version") + ": v" + version + " " + QString::fromLatin1(VERSION_STRING) + " ";

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
