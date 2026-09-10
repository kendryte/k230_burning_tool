#include "common/IfwInstallation.h"
#include "common/UpdateChecker.h"
#include <QAction>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QXmlStreamReader>
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

    void mainWindowHasSingleUpdatesMenu() {
        QFile file(QFINDTESTDATA("../../gui/src/MainWindow.ui"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QXmlStreamReader xml(&file);
        QStringList menus;
        int updatesReferences = 0;
        while (!xml.atEnd()) {
            xml.readNext();
            if (!xml.isStartElement()) continue;
            const auto attributes = xml.attributes();
            if (xml.name() == QLatin1String("widget") && attributes.value("class") == QLatin1String("QMenu")) {
                menus.append(attributes.value("name").toString());
            }
            if (xml.name() == QLatin1String("addaction") && attributes.value("name") == QLatin1String("menuUpdates")) {
                ++updatesReferences;
            }
            QVERIFY(attributes.value("name") != QLatin1String("btnOpenWebsite"));
            QVERIFY(attributes.value("name") != QLatin1String("btnUpdate"));
        }
        QVERIFY(!xml.hasError());
        QCOMPARE(menus, QStringList({"menuF_ile", "menuUpdates"}));
        QCOMPARE(updatesReferences, 1);
    }

    void portableCopiesStayPortable() {
        QVERIFY(QFile::remove(temp->path() + "/ifw-installation.json"));
        QVERIFY(!IfwInstallation::detect(executable).isManaged());
        QMenu menu("Updates(&U)");
        UpdateChecker checker(&menu, QVersionNumber(2, 2, 4), executable);
        QCOMPARE(menu.title(), QString("Updates(&U)"));
        QCOMPARE(menu.actions().size(), 3);
        QCOMPARE(menu.actions()[0]->text(), QString("Check for Updates..."));
        QCOMPARE(menu.actions()[1]->text(), QString("Download Releases..."));
        QVERIFY(menu.actions()[0]->isEnabled());
        QVERIFY(menu.actions()[1]->isEnabled());
        QVERIFY(!menu.actions()[2]->isVisible());
    }

    void manualCheckKeepsMenuTitle() {
        QVERIFY(QFile::remove(temp->path() + "/ifw-installation.json"));
        QMenu menu("Updates(&U)");
        UpdateChecker checker(&menu, QVersionNumber(2, 2, 4), executable, false);
        auto *check = menu.actions()[0];
        auto *status = menu.actions()[2];
        QVERIFY(checker.findChildren<QNetworkReply *>().isEmpty());
        check->trigger();
        QCOMPARE(menu.title(), QString("Updates(&U)"));
        QCOMPARE(check->text(), QString("Check for Updates..."));
        QVERIFY(!check->isEnabled());
        QVERIFY(status->isVisible());
        QVERIFY(!status->isEnabled());
        QCOMPARE(status->text(), QString("Checking for updates..."));

        auto *reply = checker.findChild<QNetworkReply *>();
        QVERIFY(reply);
        reply->abort();
        QTRY_VERIFY(check->isEnabled());
        QCOMPARE(status->text(), QString("Could not check for updates"));
        QCOMPARE(menu.title(), QString("Updates(&U)"));
        QCOMPARE(check->text(), QString("Check for Updates..."));
    }

    void disablingAutomaticChecksKeepsManualActions() {
        QVERIFY(QFile::remove(temp->path() + "/ifw-installation.json"));
        QMenu menu("Updates(&U)");
        UpdateChecker checker(&menu, QVersionNumber(2, 2, 4), executable, false);
        auto *network = checker.findChild<QNetworkAccessManager *>();
        QVERIFY(network);
        QSignalSpy requests(network, &QNetworkAccessManager::finished);
        QTest::qWait(10500);
        QCOMPARE(requests.size(), 0);
        QVERIFY(checker.findChildren<QNetworkReply *>().isEmpty());
        QVERIFY(menu.actions()[0]->isEnabled());
        QVERIFY(menu.actions()[1]->isEnabled());
        QVERIFY(!menu.actions()[2]->isVisible());
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
        QMenu menu("Updates(&U)");
        UpdateChecker checker(&menu, QVersionNumber(2, 2, 4), executable);
        QCOMPARE(menu.title(), QString("Updates(&U)"));
        QCOMPARE(menu.actions().size(), 3);
        QCOMPARE(menu.actions()[0]->text(), QString("Update Application..."));
        QVERIFY(!menu.actions()[0]->isEnabled());
        QVERIFY(menu.actions()[1]->isEnabled());
        QCOMPARE(menu.actions()[2]->text(), QString("Online updates not configured"));
        QVERIFY(!menu.actions()[2]->isEnabled());
        QVERIFY(!checker.findChild<QNetworkAccessManager *>());
    }

    void menuDelegatesToFramework() {
        QMenu menu("Updates(&U)");
        UpdateChecker checker(&menu, QVersionNumber(2, 2, 4), executable);
        QCOMPARE(menu.title(), QString("Updates(&U)"));
        QCOMPARE(menu.actions().size(), 2);
        QCOMPARE(menu.actions()[0]->text(), QString("Update Application..."));
        QCOMPARE(menu.actions()[1]->text(), QString("Manage Installation..."));
        QVERIFY(!checker.findChild<QNetworkAccessManager *>());
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
        QMenu menu("Updates(&U)");
        UpdateChecker checker(&menu, QVersionNumber(2, 2, 4), executable);
        QCOMPARE(menu.title(), QString("Updates(&U)"));
        QCOMPARE(menu.actions().size(), 3);
        QVERIFY(!menu.actions()[0]->isEnabled());
        QVERIFY(!menu.actions()[1]->isEnabled());
        QCOMPARE(menu.actions()[2]->text(), QString("Maintenance Tool not found"));
        QVERIFY(!menu.actions()[2]->isEnabled());
        QVERIFY(!checker.findChild<QNetworkAccessManager *>());
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
