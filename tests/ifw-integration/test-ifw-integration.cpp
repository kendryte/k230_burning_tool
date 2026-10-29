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
#include <QTimer>
#include <QXmlStreamReader>
#include <cstring>
#include <memory>

class TestReply : public QNetworkReply {
    QByteArray body;
    qint64 offset = 0;

    qint64 readData(char *data, qint64 size) override {
        const auto count = qMin(size, qint64(body.size()) - offset);
        if (!count) return -1;
        std::memcpy(data, body.constData() + offset, size_t(count));
        offset += count;
        return count;
    }

  public:
    TestReply(const QNetworkRequest &request, const QByteArray &contents, NetworkError error, QObject *parent)
        : QNetworkReply(parent), body(contents) {
        setRequest(request);
        setUrl(request.url());
        open(QIODevice::ReadOnly);
        QTimer::singleShot(0, this, [this, error] {
            if (isFinished()) return;
            emit readyRead();
            if (isFinished()) return;
            if (error != NoError) setError(error, "Test network error");
            setFinished(true);
            emit finished();
        });
    }

    qint64 bytesAvailable() const override { return body.size() - offset + QNetworkReply::bytesAvailable(); }
    void abort() override {
        if (isFinished()) return;
        setError(OperationCanceledError, "Aborted");
        setFinished(true);
        emit finished();
    }
};

class TestNetwork : public QNetworkAccessManager {
    QNetworkReply *createRequest(Operation, const QNetworkRequest &request, QIODevice *) override {
        requests.append(request);
        return new TestReply(request, body, error, this);
    }

  public:
    QByteArray body;
    QNetworkReply::NetworkError error = QNetworkReply::NoError;
    QList<QNetworkRequest> requests;
};

class IfwIntegrationTests : public QObject {
    Q_OBJECT
    std::unique_ptr<QTemporaryDir> temp;
    QString executable, tool;
    QJsonObject marker;

    static QString componentName() {
#ifdef Q_OS_WIN
        return "com.kendryte.k230burning.windows.x8664.normal";
#else
        return "com.kendryte.k230burning.linux.x8664.normal";
#endif
    }

    static QByteArray repositoryXml(const QString &version, const QString &component = componentName()) {
        return QString("<Updates><ApplicationVersion>1.0.0</ApplicationVersion><PackageUpdate>"
                       "<Name>%1</Name><Version>%2</Version></PackageUpdate></Updates>")
            .arg(component, version).toUtf8();
    }

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
        marker = {{"schema", 1}, {"platform", platform}, {"arch", "x86_64"}, {"variant", "normal"},
                  {"executable", "app/bin/" + filename}, {"repository", "https://updates.example.test/linux/x86_64/normal"}};
        saveMarker();
    }

    void detectsManagedInstallation() {
        const auto installed = IfwInstallation::detect(executable);
        QVERIFY(installed.isManaged());
        QVERIFY(installed.canUpdate());
        QCOMPARE(installed.root, temp->path());
        QCOMPARE(installed.maintenanceTool, tool);
        QCOMPARE(installed.component, componentName());
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
        TestNetwork installedNetwork;
        QMenu installedMenu("Updates(&U)");
        UpdateChecker installedChecker(&installedMenu, QVersionNumber(2, 2, 4), executable, false, &installedNetwork);
        QVERIFY(QFile::remove(temp->path() + "/ifw-installation.json"));
        TestNetwork portableNetwork;
        QMenu menu("Updates(&U)");
        UpdateChecker checker(&menu, QVersionNumber(2, 2, 4), executable, false, &portableNetwork);
        QTest::qWait(10500);
        QVERIFY(installedNetwork.requests.isEmpty());
        QVERIFY(portableNetwork.requests.isEmpty());
        QVERIFY(installedMenu.actions()[2]->isEnabled());
        QVERIFY(!installedMenu.actions()[3]->isVisible());
        QVERIFY(menu.actions()[0]->isEnabled());
        QVERIFY(menu.actions()[1]->isEnabled());
        QVERIFY(!menu.actions()[2]->isVisible());
    }

    void automaticChecksShowVersionsForBothEditions() {
        TestNetwork installedNetwork;
        installedNetwork.body = repositoryXml("2.2.8");
        QMenu installedMenu("Updates(&U)");
        UpdateChecker installedChecker(&installedMenu, QVersionNumber(2, 2, 5), executable, true, &installedNetwork);
        QSignalSpy installedStatus(&installedChecker, &UpdateChecker::statusChanged);
        QVERIFY(QFile::remove(temp->path() + "/ifw-installation.json"));
        TestNetwork portableNetwork;
        portableNetwork.body = R"({"version":2208})";
        QMenu portableMenu("Updates(&U)");
        UpdateChecker portableChecker(&portableMenu, QVersionNumber(2, 2, 5), executable, true, &portableNetwork);
        QSignalSpy portableStatus(&portableChecker, &UpdateChecker::statusChanged);

        QVERIFY(installedNetwork.requests.isEmpty());
        QVERIFY(portableNetwork.requests.isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(installedStatus.size(), 2, 12000);
        QTRY_COMPARE(portableStatus.size(), 2);
        const QString expected = "New version available: v2.2.8 (installed: v2.2.5)";
        QCOMPARE(installedStatus.last().at(0).toString(), expected);
        QCOMPARE(portableStatus.last().at(0).toString(), expected);
        QCOMPARE(installedMenu.actions().last()->text(), expected);
        QCOMPARE(portableMenu.actions().last()->text(), expected);
        QCOMPARE(installedMenu.title(), QString("Updates(&U) (v2.2.8 available)"));
        QCOMPARE(portableMenu.title(), QString("Updates(&U) (v2.2.8 available)"));
        QCOMPARE(installedNetwork.requests.size(), 1);
        QCOMPARE(portableNetwork.requests.size(), 1);
    }

    void updateResponses_data() {
        QTest::addColumn<bool>("managed");
        QTest::addColumn<QByteArray>("body");
        QTest::addColumn<QString>("expected");
        const QString newer = "New version available: v2.2.8 (installed: v2.2.5)";
        const QString current = "You are up to date (v2.2.5)";
        const QString failed = "Could not check for updates";
        QTest::newRow("portable-newer") << false << QByteArray(R"({"version":2208})") << newer;
        QTest::newRow("portable-same") << false << QByteArray(R"({"version":2205})") << current;
        QTest::newRow("portable-older") << false << QByteArray(R"({"version":2204})") << current;
        QTest::newRow("portable-invalid") << false << QByteArray("not json") << failed;
        QTest::newRow("portable-missing") << false << QByteArray("{}") << failed;
        QTest::newRow("portable-fraction") << false << QByteArray(R"({"version":2208.1})") << failed;
        QTest::newRow("portable-negative") << false << QByteArray(R"({"version":-1})") << failed;
        QTest::newRow("portable-string") << false << QByteArray(R"({"version":"2208"})") << failed;
        QTest::newRow("portable-oversized") << false << QByteArray(65537, 'x') << failed;
        QTest::newRow("installed-newer") << true << repositoryXml("2.2.8") << newer;
        QTest::newRow("installed-same") << true << repositoryXml("2.2.5") << current;
        QTest::newRow("installed-older") << true << repositoryXml("2.2.4") << current;
        QTest::newRow("installed-numeric-comparison") << true << repositoryXml("2.10.0")
            << QString("New version available: v2.10.0 (installed: v2.2.5)");
        QTest::newRow("installed-other-component") << true << repositoryXml("9.0.0", "other.application") << failed;
        QTest::newRow("installed-empty") << true << QByteArray("<Updates/>") << failed;
        QTest::newRow("installed-invalid") << true << QByteArray("<Updates><PackageUpdate>") << failed;
        QTest::newRow("installed-trailing-junk") << true << repositoryXml("2.2.8") + "junk" << failed;
        QTest::newRow("installed-invalid-version") << true << repositoryXml("2.2.8bad") << failed;
        QTest::newRow("installed-oversized") << true << QByteArray(65537, 'x') << failed;
    }

    void updateResponses() {
        QFETCH(bool, managed);
        QFETCH(QByteArray, body);
        QFETCH(QString, expected);
        if (!managed) QVERIFY(QFile::remove(temp->path() + "/ifw-installation.json"));
        TestNetwork network;
        network.body = body;
        QMenu menu("Updates(&U)");
        UpdateChecker checker(&menu, QVersionNumber(2, 2, 5), executable, false, &network);
        QSignalSpy status(&checker, &UpdateChecker::statusChanged);
        auto *check = menu.actions()[managed ? 2 : 0];
        check->trigger();
        QVERIFY(!check->isEnabled());
        QTRY_VERIFY(check->isEnabled());
        QCOMPARE(menu.actions().last()->text(), expected);
        QCOMPARE(status.size(), 2);
        QCOMPARE(status.last().at(0).toString(), expected);
        const QString expectedTitle = expected.startsWith("New version available:")
            ? QString("Updates(&U) (%1 available)").arg(expected.section(' ', 3, 3)) : QString("Updates(&U)");
        QCOMPARE(menu.title(), expectedTitle);
        QCOMPARE(network.requests.size(), 1);
        const QString url = managed ? "https://updates.example.test/linux/x86_64/normal/Updates.xml"
            : "https://download.kendryte.com/developer/tools/k230_burningtool/k230_burningtool_lastest.txt";
        QCOMPARE(network.requests.first().url(), QUrl(url));

        // A failed request must leave manual retries available.
        network.error = QNetworkReply::HostNotFoundError;
        check->trigger();
        QTRY_VERIFY(check->isEnabled());
        QCOMPARE(menu.actions().last()->text(), QString("Could not check for updates"));
        // A failed retry must not hide a previously discovered update.
        QCOMPARE(menu.title(), expectedTitle);
        network.error = QNetworkReply::NoError;
        check->trigger();
        QTRY_VERIFY(check->isEnabled());
        QCOMPARE(menu.actions().last()->text(), expected);
        QCOMPARE(menu.title(), expectedTitle);

        network.body = managed ? repositoryXml("2.2.5") : QByteArray(R"({"version":2205})");
        check->trigger();
        QTRY_VERIFY(check->isEnabled());
        QCOMPARE(menu.title(), QString("Updates(&U)"));
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
        QCOMPARE(menu.actions().size(), 4);
        QCOMPARE(menu.actions()[0]->text(), QString("Update Application..."));
        QCOMPARE(menu.actions()[1]->text(), QString("Manage Installation..."));
        QCOMPARE(menu.actions()[2]->text(), QString("Check for Updates..."));
        QVERIFY(checker.findChild<QNetworkAccessManager *>());
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
