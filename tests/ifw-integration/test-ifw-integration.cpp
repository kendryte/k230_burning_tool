#include "common/IfwInstallation.h"
#include "common/UpdateChecker.h"
#include <QAction>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <memory>

class IfwIntegrationTests : public QObject {
    Q_OBJECT
    std::unique_ptr<QTemporaryDir> temp;
    QString executable, tool;
    QJsonObject marker;

    void write(const QString &path, const QByteArray &contents) {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(contents), qint64(contents.size()));
        file.close();
    }

    void saveMarker() {
        write(temp->path() + "/ifw-installation.json", QJsonDocument(marker).toJson());
    }

  private slots:
    void init() {
        temp.reset(new QTemporaryDir);
        QVERIFY(temp->isValid());
        QVERIFY(QDir().mkpath(temp->path() + "/app/bin"));
#ifdef Q_OS_WIN
        const QString platform = "windows", filename = "K230BurningTool.exe", toolName = "MaintenanceTool.exe";
#else
        const QString platform = "linux", filename = "K230BurningTool", toolName = "MaintenanceTool";
#endif
        executable = temp->path() + "/app/bin/" + filename;
        tool = temp->path() + "/" + toolName;
        write(executable, "application fixture");
        write(tool, "maintenance fixture");
        QVERIFY(QFile::setPermissions(tool, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        marker = {{"schema", 1}, {"platform", platform}, {"variant", "normal"},
                  {"executable", "app/bin/" + filename}, {"repository", "https://updates.example.test/linux/x86_64/normal"}};
        saveMarker();
    }

    void detectsManagedInstallation() {
        const auto installed = IfwInstallation::detect(executable);
        QVERIFY(installed.isManaged());
        QVERIFY(installed.canUpdate());
        QCOMPARE(installed.root, temp->path());
        QCOMPARE(installed.maintenanceTool, tool);
    }

    void portableCopiesStayPortable() {
        QVERIFY(QFile::remove(temp->path() + "/ifw-installation.json"));
        QVERIFY(!IfwInstallation::detect(executable).isManaged());
        UpdateButton menu;
        UpdateChecker checker(&menu, QVersionNumber(2, 2, 4), executable);
        QCOMPARE(menu.actions().size(), 2);
        QVERIFY(menu.actions()[0]->isEnabled());
    }

    void rejectsInvalidMarkers() {
        marker["executable"] = "../../elsewhere";
        saveMarker();
        QVERIFY(!IfwInstallation::detect(executable).isManaged());
        write(temp->path() + "/ifw-installation.json", QByteArray(20000, 'x'));
        QVERIFY(!IfwInstallation::detect(executable).isManaged());
    }

    void offlineInstallationDisablesOnlineUpdates() {
        marker["repository"] = "";
        saveMarker();
        QVERIFY(IfwInstallation::detect(executable).isManaged());
        QVERIFY(!IfwInstallation::detect(executable).canUpdate());
        UpdateButton menu;
        UpdateChecker checker(&menu, QVersionNumber(2, 2, 4), executable);
        QVERIFY(!menu.actions()[0]->isEnabled());
        QVERIFY(menu.actions()[1]->isEnabled());
    }

    void menuDelegatesToFramework() {
        UpdateButton menu;
        UpdateChecker checker(&menu, QVersionNumber(2, 2, 4), executable);
        QSignalSpy requests(&checker, &UpdateChecker::maintenanceRequested);
        menu.actions()[0]->trigger();
        menu.actions()[1]->trigger();
        QCOMPARE(requests.size(), 2);
        QCOMPARE(requests.at(0).at(0).toBool(), true);
        QCOMPARE(requests.at(1).at(0).toBool(), false);
    }

    void missingToolDoesNotBecomePortable() {
        QVERIFY(QFile::remove(tool));
        const auto installed = IfwInstallation::detect(executable);
        QVERIFY(installed.isManaged());
        QVERIFY(installed.maintenanceTool.isEmpty());
        UpdateButton menu;
        UpdateChecker checker(&menu, QVersionNumber(2, 2, 4), executable);
        QVERIFY(!menu.actions()[0]->isEnabled());
        QVERIFY(!menu.actions()[1]->isEnabled());
    }

    void insecureRepositoryIsRejected() {
        for (const QString &url : {"http://example.test", "https://user:pass@example.test", "https://example.test?token=x"}) {
            marker["repository"] = url;
            saveMarker();
            QVERIFY(!IfwInstallation::detect(executable).canUpdate());
        }
    }

    void maintenanceEnvironmentIsIsolated() {
        qputenv("QT_PLUGIN_PATH", "/application/plugins");
        qputenv("APPDIR", "/portable/application");
        const auto environment = IfwInstallation::maintenanceEnvironment();
        QVERIFY(!environment.contains("QT_PLUGIN_PATH"));
        QVERIFY(!environment.contains("APPDIR"));
        QCOMPARE(qgetenv("APPDIR"), QByteArray("/portable/application"));
        qunsetenv("QT_PLUGIN_PATH");
        qunsetenv("APPDIR");
    }
};

QTEST_MAIN(IfwIntegrationTests)
#include "test-ifw-integration.moc"
