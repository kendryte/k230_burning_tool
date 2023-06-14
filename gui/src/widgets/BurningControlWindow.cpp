#include "BurningControlWindow.h"
#include "common/AppGlobalSetting.h"
#include "common/BurningRequest.h"
#include "common/BurnLibrary.h"
#include "ui_BurningControlWindow.h"

#include <QDir>
#include <QFileDialog>
#include <QMessageBox>
#include <QTimer>
#include <QAction>

#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif

#define SETTING_SYSTEM_IMAGE_PATH "sys-image-path"
#define SETTING_BYTE_SIZE "byte-size"

BurningControlWindow::BurningControlWindow(QWidget *parent)
	: QGroupBox(parent), ui(new Ui::BurningControlWindow), settings(QSettings::Scope::UserScope, SETTINGS_CATEGORY, "burning") {
	ui->setupUi(this);

	initTableView();

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
		QString info = tr("文件大小： ");

		QLocale locale = this->locale();
		info += locale.formattedDataSize(fd.size());

		ui->txtImageInfo->setText(info);
		ui->txtImageInfo->setStyleSheet("");

		return true;
	} else {
		fd.setFileName("");
		ui->txtImageInfo->setText(tr("未找到文件"));
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

/**
 * for tableView
 */
void BurningControlWindow::initTableView(void)
{
	/**
	 * tabview init start
	 */
    tableModel = new QStandardItemModel(ui->tableView);
	tableHeader = new TableHeaderView(Qt::Horizontal, ui->tableView);

    connect(tableModel, &QStandardItemModel::itemChanged, this, &BurningControlWindow::tableviewItemChangedSlot);
    connect(tableHeader, &TableHeaderView::stateChanged, this, &BurningControlWindow::tableviewHeaderStateChangedSlot);

	ui->tableView->setHorizontalHeader(tableHeader);
	ui->tableView->setCornerButtonEnabled(false);
    ui->tableView->setSelectionBehavior(QAbstractItemView::SelectRows);
	ui->tableView->setContextMenuPolicy(Qt::CustomContextMenu);

	QMenu *tableMenu = new QMenu(ui->tableView);

    QAction *addItem = tableMenu->addAction(tr("增加Item"));
    QAction *delItem = tableMenu->addAction(tr("删除Item"));
    QAction *importConfig = tableMenu->addAction(tr("导入配置"));
    QAction *exportConfig = tableMenu->addAction(tr("导出配置"));

    connect(addItem, &QAction::triggered, this, &BurningControlWindow::tableviewMenuAddItemSlot);
    connect(delItem, &QAction::triggered, this, &BurningControlWindow::tableviewMenuDelItemSlot);
    connect(importConfig, &QAction::triggered, this, &BurningControlWindow::tableviewMenuImportConfigmSlot);
    connect(exportConfig, &QAction::triggered, this, &BurningControlWindow::tableviewMenuExportConfigmSlot);

	connect(ui->tableView, &QTableView::customContextMenuRequested, this, [this, tableMenu] (const QPoint &pos) {
		menuPoint = pos;
		tableMenu->exec(mapToGlobal(pos));
	});

	tableModel->setHorizontalHeaderItem(1, new QStandardItem(tr("目标介质")));
	tableModel->setHorizontalHeaderItem(2, new QStandardItem(tr("目标地址")));
	tableModel->setHorizontalHeaderItem(3, new QStandardItem(tr("目标名称")));
	tableModel->setHorizontalHeaderItem(4, new QStandardItem(tr("文件路径")));
	tableModel->setHorizontalHeaderItem(5, new QStandardItem(tr("打开文件")));

	/**
	 * 选中
	 */
	CheckBoxDelegate *pCheckDelegate = new CheckBoxDelegate(this);
    ui->tableView->setItemDelegateForColumn(0, pCheckDelegate);

	/**
	 * 目标介质
	 */
	QStringList targetMedium;
	targetMedium << "eMMC" << "Nor Flash";

	ComboDelegate *pComboMedium = new ComboDelegate(this);
	pComboMedium->setItems(targetMedium);
    ui->tableView->setItemDelegateForColumn(1, pComboMedium);

	/**
	 * 目标地址
	 */
	for(int i = 0;i < 10;i++) {
		tableModel->setItem(i, 2, new QStandardItem(QString("0x00000000")));
	}

	/**
	 * 目标名称
	 */
	QStringList targetAltName;
	targetAltName << "uboot" << "rtt" << "kernel" << "rootfs";

	ComboDelegate *pComboAltName = new ComboDelegate(this);
	pComboAltName->setItems(targetAltName);
    ui->tableView->setItemDelegateForColumn(3, pComboAltName);

	/**
	 * 文件路径
	 */
    ui->tableView->setModel(tableModel);
	ui->tableView->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);

	/**
	 * Open
	 */
	PushButtonDelegate *pBtnOpen = new PushButtonDelegate(tr("..."), this);
    ui->tableView->setItemDelegateForColumn(5, pBtnOpen);
	connect(pBtnOpen, &PushButtonDelegate::btnClicked, this, &BurningControlWindow::tabviewBtnOpenClickedSlot);
	/*
	 * tabview init end
	 **/
}

void BurningControlWindow::tableviewHeaderStateChangedSlot(int state)
{
    int rowCount = tableModel->rowCount();

    if (state == 2)
    {
        for (int j = 0; j < rowCount; j++)
        {
            QModelIndex index = tableModel->index(j, 0, QModelIndex());
            tableModel->setData(index, true, Qt::UserRole);
        }
    }
    else if (state == 0)
    {
        for (int j = 0; j < rowCount; j++)
        {
            QModelIndex index = tableModel->index(j, 0, QModelIndex());
            tableModel->setData(index, false, Qt::UserRole);
        }
    }
}

void BurningControlWindow::tableviewItemChangedSlot(QStandardItem *item)
{
    int rowCount = tableModel->rowCount();
    int column = item->index().column();

    if (column == 0)
    {
        Qt::CheckState state = Qt::Unchecked;
        int nSelectedCount = 0;
        for (int i = 0; i < rowCount; i++)
        {
            if (tableModel->index(i, 0).data(Qt::UserRole).toBool())
            {
                nSelectedCount++;
            }
        }
        if (nSelectedCount >= rowCount)
        {
            state = Qt::Checked;
        }
        else if (nSelectedCount > 0)
        {
            state = Qt::PartiallyChecked;
        }
        tableHeader->onStateChanged(state);
    }
}

void BurningControlWindow::tabviewBtnOpenClickedSlot(const QModelIndex &index)
{
	auto str = QFileDialog::getOpenFileName(this, tr("选择文件"), ui->inputSysImage->text(), tr(""), nullptr, QFileDialog::ReadOnly);
	if (str.isEmpty()) {
		return;
	}

	QModelIndex fnameIndex = tableModel->index(index.row(), 4, QModelIndex());

	tableModel->setData(fnameIndex, str);
	emit tableModel->dataChanged(fnameIndex, fnameIndex);
}

void BurningControlWindow::tableviewMenuAddItemSlot(bool checked)
{
	int row = tableModel->rowCount();
	tableModel->insertRows(row, 1);
	tableModel->setItem(row, 2, new QStandardItem(QString("0x00000000")));
}

void BurningControlWindow::tableviewMenuDelItemSlot(bool checked)
{
	int row = ui->tableView->rowAt(menuPoint.y());
	tableModel->removeRows(row, 1);
}

void BurningControlWindow::tableviewMenuImportConfigmSlot(bool checked)
{
	qDebug() << __func__ << __LINE__ << menuPoint << ui->tableView->rowAt(menuPoint.y());
}

void BurningControlWindow::tableviewMenuExportConfigmSlot(bool checked)
{
	bool selected = false;
	QString medium, address, altName, filePath;
	
	for(int row = 0; row < tableModel->rowCount(); row++) {
		bool selected = tableModel->index(row, 0).data(Qt::UserRole).toBool();

		if(selected) {
			medium = tableModel->index(row, 1).data().toString();
			address = tableModel->index(row, 2).data().toString();
			altName = tableModel->index(row, 3).data().toString();
			filePath = tableModel->index(row, 4).data().toString();

			qDebug() << __func__ << __LINE__ << row << "Selected" << medium << address << altName << filePath;
		} else {
			qDebug() << __func__ << __LINE__ << row << "Not Seleted";
		}
	}
}
