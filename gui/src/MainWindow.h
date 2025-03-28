#pragma once

#include <public/canaan-burn.h>

#include "common/SettingsHelper.h"

#include <QMainWindow>
#include <QSettings>

#define SETTING_LOG_TRACE "log-trace"
#define SETTING_LOG_BUFFER "log-buffer"

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow {
	Q_OBJECT
	KBMonCTX context;
	bool closing = false;
	bool shown = false;
  bool logShown;

	QSettings settings;
	class UpdateChecker *updateChecker;

	SettingsBool traceSetting{"debug", SETTING_LOG_TRACE, IS_DEBUG};
	SettingsBool logBufferSetting{"debug", SETTING_LOG_BUFFER, false};

  protected:
  public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

    void resizeEvent(QResizeEvent *event) { onResized(); }

  private slots:
    void on_btnOpenWebsite_triggered();
    void on_btnSaveLog_triggered();
    // void on_btnOpenRelease_triggered();
    void startNewBurnJob(class BurningRequest *partialRequest);
    void onResized();

    void on_action_triggered();
    void splitterMovedSlot(int pos, int index);

  private:
    void closeEvent(QCloseEvent *ev);
    Ui::MainWindow *ui;
};
