#pragma once

#include <QProcessEnvironment>
#include <QString>
#include <QUrl>

struct IfwInstallation {
	QString root;
	QString maintenanceTool;
	QUrl repository;
	bool isManaged() const { return !root.isEmpty(); }
	bool canUpdate() const { return !maintenanceTool.isEmpty() && !repository.isEmpty(); }
	static IfwInstallation detect(const QString &executable);
	static QProcessEnvironment maintenanceEnvironment();
};
