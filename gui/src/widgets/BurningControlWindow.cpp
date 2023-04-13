#include "BurningControlWindow.h"
#include "common/AppGlobalSetting.h"
#include "common/BurningRequest.h"
#include "common/BurnLibrary.h"
#include "ui_BurningControlWindow.h"

#include <QDir>
#include <QFileDialog>
#include <QMessageBox>
#include <QTimer>

#define SETTING_SYSTEM_IMAGE_PATH "sys-image-path"
#define SETTING_BYTE_SIZE "byte-size"

BurningControlWindow::BurningControlWindow(QWidget *parent)
	: QGroupBox(parent), ui(new Ui::BurningControlWindow), settings(QSettings::Scope::UserScope, SETTINGS_CATEGORY, "burning") {
	ui->setupUi(this);

	readSettings();
	GlobalSetting::flashTarget.connectCombobox(ui->inputTarget);

	auto instance = BurnLibrary::instance();
	connect(instance, &BurnLibrary::onBeforDeviceOpen, this, &BurningControlWindow::handleOpenDevice);
	connect(instance, &BurnLibrary::jobListChanged, this, &BurningControlWindow::handleSettingsWindowButtonState);
	connect(this, &BurningControlWindow::updateBurnLibAutoBurnFlag, instance, &BurnLibrary::setAutoBurnFlag);
}

BurningControlWindow::~BurningControlWindow() {
	delete ui;
}

void BurningControlWindow::handleOpenDevice(QString path) {
	if (autoBurningEnabled) {
		auto request = new K230BurningRequest();
		request->usbPath = path;
		request->isAutoCreate = true;

		if (!BurnLibrary::instance()->hasBurning(request)) {

			emit newProcessRequested(request);
		}
	}
}

void BurningControlWindow::on_buttonStartAuto_clicked(bool checked) {
	if((true == checked) && (false == checkSysImage())) {
		QMessageBox::critical(Q_NULLPTR, QString(), tr("请先打开系统镜像！！！"));

		QTimer::singleShot(0, ui->buttonStartAuto, &AnimatedButton::click);
		return;
	}

	saveSettings();

	emit updateBurnLibAutoBurnFlag(checked);

	autoBurningEnabled = checked;
	handleSettingsWindowButtonState();
}

void BurningControlWindow::on_btnStartBurn_clicked() {
	if(false == checkSysImage()) {
		QMessageBox::critical(Q_NULLPTR, QString(), tr("请先打开系统镜像！！！"));
		return;
	}

	saveSettings();

	auto request = new K230BurningRequest();

	request->usbPath = QString("WaitingConnect");
	request->isAutoCreate = false;

	emit newProcessRequested(request);

	kburnMonitorManualTrig(BurnLibrary::instance()->context());
}

void BurningControlWindow::on_btnSelectImage_clicked()
{
	auto str = QFileDialog::getOpenFileName(this, tr("打开系统镜像"), ui->inputSysImage->text(), tr(""), nullptr, QFileDialog::ReadOnly);
	if (str.isEmpty()) {
		return;
	}
	ui->inputSysImage->setText(str);

	checkSysImage();
}

void BurningControlWindow::handleSettingsWindowButtonState() {
	if (autoBurningEnabled) {
		ui->groupBox_Image->setEnabled(false);
		ui->groupBox_Target->setEnabled(false);
		ui->btnOpenSettings->setEnabled(false);
	} else if (BurnLibrary::instance()->getBurningJobCount() > 0) {
		ui->groupBox_Image->setEnabled(false);
		ui->groupBox_Target->setEnabled(false);
		ui->btnOpenSettings->setEnabled(false);
	} else {
		ui->groupBox_Image->setEnabled(true);
		ui->groupBox_Target->setEnabled(true);
		ui->btnOpenSettings->setEnabled(true);
	}
}

bool BurningControlWindow::checkSysImage() {
	QString fPath = ui->inputSysImage->text();

	if (fd.fileName() == fPath) {
		return !fd.fileName().isEmpty();
	}

	fd.setFileName(fPath);
	if (fd.exists()) {
		QString info = tr("file size: ");

		QLocale locale = this->locale();
		info += locale.formattedDataSize(fd.size());

		ui->txtImageInfo->setText(info);
		ui->txtImageInfo->setStyleSheet("");

		return true;
	} else {
		fd.setFileName("");
		ui->txtImageInfo->setText(tr("file not found"));
		ui->txtImageInfo->setStyleSheet("QLabel { color : red; }");

		return false;
	}
}

void BurningControlWindow::readSettings(void)
{
	QDir appDir(QCoreApplication::applicationDirPath());

	if (settings.contains(SETTING_SYSTEM_IMAGE_PATH)) {
		QString saved = settings.value(SETTING_SYSTEM_IMAGE_PATH).toString();
		if (QDir(saved).isRelative()) {
			saved = QDir::cleanPath(appDir.absoluteFilePath(saved));
		}
		ui->inputSysImage->setText(saved);
		checkSysImage();
	}
}

void BurningControlWindow::saveSettings(void)
{
	QString fPath = ui->inputSysImage->text();

	if (!fPath.isEmpty()) {
		QDir d(QCoreApplication::applicationDirPath());
		QString relPath = d.relativeFilePath(fPath);

		if (d.isRelativePath(relPath)) {
			settings.setValue(SETTING_SYSTEM_IMAGE_PATH, relPath);
		} else {
			settings.setValue(SETTING_SYSTEM_IMAGE_PATH, fPath);
		}
	}
	ISettingsBase::commitAllSettings();
}
