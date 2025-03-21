#include "SettingsWindow.h"
#include "../common/BurnLibrary.h"
#include "common/AppGlobalSetting.h"
#include "ui_SettingsWindow.h"
#include <public/canaan-burn.h>
#include <QDir>
#include <QFileDialog>
#include <QPushButton>


#define SETTING_SYSTEM_IMAGE_PATH "sys-image-path"
#define SETTING_BYTE_SIZE "byte-size"


SettingsWindow::SettingsWindow(QWidget *parent)
	: QWidget(parent), ui(new Ui::SettingsWindow), settings(QSettings::Scope::UserScope, SETTINGS_CATEGORY, "burning") {
	ui->setupUi(this);

	// ui->advanceView1->setVisible(false);
	// ui->advanceView2->setVisible(false);
	// ui->advanceView3->setVisible(false);

	GlobalSetting::autoConfirm.connectCheckBox(ui->inputAutoConfirm);
	connectCheckboxEnable(GlobalSetting::autoConfirm, ui->inputAutoConfirm, {ui->inputAutoConfirmTimeout});
	GlobalSetting::autoConfirmTimeout.connectSpinBox(ui->inputAutoConfirmTimeout);
	connect(ui->inputAutoConfirm, &QCheckBox::toggled, this, [=](bool enable) {
		ui->inputAutoConfirmManualJob->setEnabled(enable);
		ui->inputAutoConfirmEvenError->setEnabled(enable);
	});

	GlobalSetting::autoConfirmManualJob.depend(GlobalSetting::autoConfirm);
	GlobalSetting::autoConfirmManualJob.connectCheckBox(ui->inputAutoConfirmManualJob);
	connectCheckboxEnable(GlobalSetting::autoConfirmManualJob, ui->inputAutoConfirmManualJob, {ui->inputAutoConfirmManualJobTimeout});
	GlobalSetting::autoConfirmManualJobTimeout.connectSpinBox(ui->inputAutoConfirmManualJobTimeout);

	GlobalSetting::autoConfirmEvenError.depend(GlobalSetting::autoConfirm);
	GlobalSetting::autoConfirmEvenError.connectCheckBox(ui->inputAutoConfirmEvenError);
	connectCheckboxEnable(GlobalSetting::autoConfirmEvenError, ui->inputAutoConfirmEvenError, {ui->inputAutoConfirmEvenErrorTimeout});
	GlobalSetting::autoConfirmEvenErrorTimeout.connectSpinBox(ui->inputAutoConfirmEvenErrorTimeout);

	GlobalSetting::autoResetChipAfterBurn.connectCheckBox(ui->inputEnableAutoResetChip);
	GlobalSetting::disableUpdate.connectCheckBox(ui->inputDisableUpdate);
	// GlobalSetting::watchVid.connectSpinBox(ui->inputWatchVid);
	// GlobalSetting::watchPid.connectSpinBox(ui->inputWatchPid);
	GlobalSetting::appBurnThread.connectSpinBox(ui->inputAppBurnThread);
	// GlobalSetting::usbLedPin.connectSpinBox(ui->inputUsbLedPin);
	// GlobalSetting::usbLedLevel.connectSpinBox(ui->inputUsbLedLevel);

	if (!GlobalSetting::autoConfirm.getValue()) {
		ui->inputAutoConfirmManualJob->setEnabled(false);
		ui->inputAutoConfirmEvenError->setEnabled(false);
	}
}

SettingsWindow::~SettingsWindow() {
	delete ui;
}

void SettingsWindow::connectCheckboxEnable(SettingsBool &setting, QCheckBox *source, QWidget *target) {
	setting.connectWidgetEnabled({target});
	connect(source, &QCheckBox::toggled, this, [=] { emit updateCheckboxState(); });
	connect(
		this, &SettingsWindow::updateCheckboxState, target, [=] { target->setEnabled(source->isChecked() && ui->inputAutoConfirm->isChecked()); });
}

void SettingsWindow::showEvent(QShowEvent *event) {
	QWidget::showEvent(event);

	if (shown)
		return;
	shown = true;

	reloadSettings();
}

void SettingsWindow::checkAndSetInt(const QString &setting, const QString &input) {
	bool ok = false;
	uint32_t value = input.toInt(&ok);
	if (ok) {
		settings.setValue(setting, value);
	}
}

template <typename keyT, typename valueT>
void SettingsWindow::checkAndSetMap(const QMap<keyT, valueT> &map, const char *config, keyT current) {
	if (map.contains(current)) {
		settings.setValue(config, current);
	}
}

void SettingsWindow::acceptSave() {
	// QString fPath = fd.fileName();
	// if (!fPath.isEmpty()) {
	// 	QDir d(QCoreApplication::applicationDirPath());
	// 	QString relPath = d.relativeFilePath(fPath);

	// 	if (d.isRelativePath(relPath)) {
	// 		settings.setValue(SETTING_SYSTEM_IMAGE_PATH, relPath);
	// 	} else {
	// 		settings.setValue(SETTING_SYSTEM_IMAGE_PATH, fPath);
	// 	}
	// }

	// checkAndSetInt(SETTING_BAUD_INIT, ui->inputBaudrateInit->currentText());
	// checkAndSetInt(SETTING_BAUD_HIGH, ui->inputBaudrateHigh->currentText());

	// checkAndSetMap(bsMap, SETTING_BYTE_SIZE, (uint8_t)ui->inputByteSize->value());
	// checkAndSetMap(parMap, SETTING_PARITY, ui->inputParity->currentText());
	// checkAndSetMap(sbMap, SETTING_STOP_BITS, (float)ui->inputStopBits->value());

	reloadSettings();
	ISettingsBase::commitAllSettings();

	emit settingsCommited();
}

void SettingsWindow::restoreDefaults() {
	ISettingsBase::resetEverythingToDefaults();
	ISettingsBase::commitAllSettings();

	settings.clear();
	reloadSettings();
}

void SettingsWindow::reloadSettings() {
	// auto context = BurnLibrary::context();

	// QDir appDir(QCoreApplication::applicationDirPath());
	// if (settings.contains(SETTING_SYSTEM_IMAGE_PATH)) {
	// 	QString saved = settings.value(SETTING_SYSTEM_IMAGE_PATH).toString();
	// 	if (QDir(saved).isRelative()) {
	// 		saved = QDir::cleanPath(appDir.absoluteFilePath(saved));
	// 	}
	// 	ui->inputSysImage->setText(saved);
	// 	checkSysImage();
	// }

	// ui->inputBaudrateHigh->clear();
	// ui->inputBaudrateInit->clear();
	// for (auto v : baudrates) {
	// 	ui->inputBaudrateHigh->addItem(QString::number(v), v);
	// 	ui->inputBaudrateInit->addItem(QString::number(v), v);
	// }
	// {
	// 	auto v = settings.value(SETTING_BAUD_HIGH, 921600).toUInt();
	// 	ui->inputBaudrateHigh->setCurrentText(QString::number(v));
	// 	kburnSetSerialBaudrate(context, v);
	// }
	// {
	// 	auto v = settings.value(SETTING_BAUD_INIT, 115200).toUInt();
	// 	ui->inputBaudrateInit->setCurrentText(QString::number(v));
	// 	kburnSetSerialFastbootBaudrate(context, v);
	// }

	// {
	// 	auto v = settings.value(SETTING_BYTE_SIZE, 8).toUInt();
	// 	ui->inputByteSize->setValue(v);
	// 	kburnSetSerialByteSize(context, bsMap.value(v));
	// }

	// {
	// 	ui->inputParity->clear();
	// 	for (auto v : parMap.keys()) {
	// 		ui->inputParity->addItem(v);
	// 	}

	// 	auto v = settings.value(SETTING_PARITY, "None").toString();
	// 	ui->inputParity->setCurrentText(v);
	// 	kburnSetSerialParity(context, parMap.value(v));
	// }

	// {
	// 	auto v = settings.value(SETTING_STOP_BITS, 2).toFloat();
	// 	ui->inputStopBits->setValue(v);
	// 	kburnSetSerialStopBits(context, sbMap.value(v));
	// }

	// {
	// 	auto v = settings.value(SETTING_SERIAL_READ_TIMEOUT, 1000).toUInt();
	// 	ui->inputSerialReadTimeout->setValue(v);
	// 	kburnSetSerialReadTimeout(context, v);
	// }

	// {
	// 	auto v = settings.value(SETTING_SERIAL_WRITE_TIMEOUT, 1000).toUInt();
	// 	ui->inputSerialWriteTimeout->setValue(v);
	// 	kburnSetSerialWriteTimeout(context, v);
	// }

	// {
	// 	auto v = settings.value(SETTING_SERIAL_RETRY, 6).toUInt();
	// 	ui->inputSerialRetry->setValue(v);
	// 	kburnSetSerialFailRetry(context, v);
	// }

	emit settingsUnsaved(false);
}
