#include "IfwInstallation.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>

IfwInstallation IfwInstallation::detect(const QString &executable) {
	IfwInstallation result;
#if defined(Q_OS_LINUX) || defined(Q_OS_WIN)
	const QString canonical = QFileInfo(executable).canonicalFilePath();
	if (canonical.isEmpty()) return result;
	const QString root = QDir(QFileInfo(canonical).absolutePath()).absoluteFilePath("../..");
	QFile marker(QDir(root).filePath("ifw-installation.json"));
	if (!marker.open(QIODevice::ReadOnly) || marker.size() > 16384) return result;
	const auto document = QJsonDocument::fromJson(marker.readAll());
	if (!document.isObject()) return result;
	const auto object = document.object();
	const QString variant = object.value("variant").toString();
	if (object.value("schema").toInt() != 1 || (variant != "normal" && variant != "avalon")) return result;
#ifdef Q_OS_WIN
	const QString platform = "windows";
	const QString name = variant == "avalon" ? "Avalon_Home_Series_Firmware_Upgrade_Tool.exe" : "K230BurningTool.exe";
	const QString toolName = "MaintenanceTool.exe";
#else
	const QString platform = "linux";
	const QString name = "K230BurningTool";
	const QString toolName = "MaintenanceTool";
#endif
	const QString relative = "app/bin/" + name;
	if (object.value("platform").toString() != platform || object.value("executable").toString() != relative ||
		QFileInfo(QDir(root).filePath(relative)).canonicalFilePath() != canonical) return result;
	result.root = QDir::cleanPath(root);
	const QFileInfo tool(QDir(result.root).filePath(toolName));
	if (tool.isFile() && tool.isExecutable() && !tool.isSymLink()) result.maintenanceTool = tool.absoluteFilePath();
	const QUrl repository(object.value("repository").toString());
	if (repository.isValid() && repository.scheme() == "https" && !repository.host().isEmpty() &&
		repository.userInfo().isEmpty() && !repository.hasQuery() && !repository.hasFragment()) result.repository = repository;
#endif
	return result;
}

QProcessEnvironment IfwInstallation::maintenanceEnvironment() {
	auto environment = QProcessEnvironment::systemEnvironment();
	for (const QString &key : {"LD_LIBRARY_PATH", "LD_PRELOAD", "QT_PLUGIN_PATH", "QT_QPA_PLATFORM_PLUGIN_PATH",
		"QTDIR", "QML2_IMPORT_PATH", "QML_IMPORT_PATH", "APPIMAGE", "APPDIR", "APPIMAGE_EXTRACT_AND_RUN"}) {
		environment.remove(key);
	}
	return environment;
}
