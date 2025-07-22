#include "BurningControlWindow.h"
#include "common/AppGlobalSetting.h"
#include "common/BurningRequest.h"
#include "common/BurnLibrary.h"
#include "ui_BurningControlWindow.h"

#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QTimer>
#include <QAction>
#include <QtAlgorithms>
#include <QEventLoop>

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>
#include <QJsonParseError>

#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif

#define SETTING_SYSTEM_IMAGE_PATH 			"sys-image-path"

#define TABLEVIEW_COL_CHECKBOX				(0)
#define TABLEVIEW_COL_PART_NAME				(1)
#define TABLEVIEW_COL_PART_OFFSET			(2)
// #define TABLEVIEW_COL_PART_SIZE				(3)
// #define TABLEVIEW_COL_FILE_NAME				(4)
#define TABLEVIEW_COL_FILE_SIZE				(3)

BurningControlWindow::BurningControlWindow(QWidget *parent)
	: QGroupBox(parent), ui(new Ui::BurningControlWindow), settings(QSettings::Scope::UserScope, SETTINGS_CATEGORY, "burning") {
	ui->setupUi(this);

	initTableView();

	GlobalSetting::flashTarget.connectCombobox(ui->inputTarget, true);

#if IS_AVALON_NANO3
	ui->inputTarget->hide();
	ui->buttonStartAuto->hide();
	ui->btnOpenSettings->hide();
	ui->label_medium->hide();
	ui->tableView->hide();

	int taget_nand_index = ui->inputTarget->findText(QString("SPI NAND"));
	ui->inputTarget->setCurrentIndex(taget_nand_index);

	ui->groupBox_Image->setMinimumHeight(50);
	ui->groupBox_Image->setMaximumHeight(80);
	ui->groupBox_Image->updateGeometry();

	ui->groupBox_Util->setMinimumHeight(80);
	ui->groupBox_Util->setMaximumHeight(80);
	ui->groupBox_Util->updateGeometry();

	// Create "Tips:" label (left-aligned, bold)
	QLabel *labelTips = new QLabel(ui->groupBox_Util);
	labelTips->setObjectName("labelTips");
	labelTips->setText("<b>Tips:</b>");  // Bold formatting
	labelTips->setAlignment(Qt::AlignLeft | Qt::AlignTop);  // Align top to match content

	// Create content label (word-wrapped)
	QLabel *labelContent = new QLabel(ui->groupBox_Util);
	labelContent->setObjectName("labelContent");
	labelContent->setWordWrap(true);  // Enable auto-wrapping
	labelContent->setAlignment(Qt::AlignLeft);
	labelContent->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

	// Set content text
	labelContent->setText(QCoreApplication::translate("BurningControlWindow", 
		"To prevent upgrade failures or device issues, please confirm the firmware image is compatible with the product type prior to upgrade.", 
		nullptr));

	// Add both labels to the grid layout (same row, different columns)
	ui->gridLayout->addWidget(labelTips, 0, 1, 1, 1);    // Column 1
	ui->gridLayout->addWidget(labelContent, 0, 2, 1, 1);  // Column 2 (expands)
	ui->gridLayout->setColumnStretch(2, 1);  // Allow column 2 to expand
#endif

	auto instance = BurnLibrary::instance();
	connect(instance, &BurnLibrary::onBeforDeviceOpen, this, &BurningControlWindow::handleOpenDevice);
	connect(instance, &BurnLibrary::jobListChanged, this, &BurningControlWindow::handleSettingsWindowButtonState);
    connect(this, &BurningControlWindow::updateBurnLibAutoBurnFlag, instance, &BurnLibrary::setAutoBurnFlag);

    // readSettings();
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
	QList<struct BurnImageItem> list = getImageList();

	if((true == checked) && (0x01 >= list.size())) {
		QMessageBox::critical(Q_NULLPTR, QString(), tr("Parse Download Configure Failed."));

		QTimer::singleShot(0, ui->buttonStartAuto, &AnimatedButton::click);
		return;
	}

	// saveSettings();

	emit updateBurnLibAutoBurnFlag(checked);

	autoBurningEnabled = checked;
	handleSettingsWindowButtonState();
}

void BurningControlWindow::on_btnStartBurn_clicked() {
	QString currentImagePath = ui->inputSysImage->text();

	QFileInfo imageFileInfo(currentImagePath);
	QDateTime imageFileLastModified = imageFileInfo.lastModified();

	if(lastKdImageModifiedTime != imageFileLastModified) {
		BurnLibrary::instance()->localLog(QStringLiteral("Image (%1) changed").arg(currentImagePath));

		parseImage();
	}

	QList<struct BurnImageItem> list = getImageList();

	if(0x01 >= list.size()) {
		QMessageBox::critical(Q_NULLPTR, QString(), tr("Parse Download Configure Failed."));
		return;
	}

	// saveSettings();

	auto request = new K230BurningRequest();

	request->usbPath = QString("WaitingConnect");
	request->isAutoCreate = false;

	emit newProcessRequested(request);

	kburnMonitorManualTrig(BurnLibrary::instance()->context());
}

void BurningControlWindow::on_btnSelectImage_clicked() {
	auto str = QFileDialog::getOpenFileName(this, tr("Open Image File"), ui->inputSysImage->text(), tr("Image(*.img *.kdimg);;All Files(*.*)"), nullptr, QFileDialog::ReadOnly);
	if (str.isEmpty()) {
		return;
	}
	ui->inputSysImage->setText(str);

	parseImage();
}

void BurningControlWindow::handleSettingsWindowButtonState() {
	if (autoBurningEnabled) {
		ui->groupBox_Image->setEnabled(false);
		ui->inputTarget->setEnabled(false);
		ui->btnOpenSettings->setEnabled(false);
	} else if (BurnLibrary::instance()->getBurningJobCount() > 0) {
		ui->groupBox_Image->setEnabled(false);
		ui->inputTarget->setEnabled(false);
		ui->btnOpenSettings->setEnabled(false);
	} else {
		ui->groupBox_Image->setEnabled(true);
		ui->inputTarget->setEnabled(true);
		ui->btnOpenSettings->setEnabled(true);
	}
}

// void BurningControlWindow::readSettings(void)
// {
// 	QDir appDir(QCoreApplication::applicationDirPath());

// 	if (settings.contains(SETTING_SYSTEM_IMAGE_PATH)) {
// 		QString saved = settings.value(SETTING_SYSTEM_IMAGE_PATH).toString();
// 		if (QDir(saved).isRelative()) {
// 			saved = QDir::cleanPath(appDir.absoluteFilePath(saved));
// 		}
// 		ui->inputSysImage->setText(saved);
// 	}
// }

// void BurningControlWindow::saveSettings(void)
// {
// 	QString fPath = ui->inputSysImage->text();

// 	if (!fPath.isEmpty()) {
// 		QDir d(QCoreApplication::applicationDirPath());
// 		QString relPath = d.relativeFilePath(fPath);

// 		if (d.isRelativePath(relPath)) {
// 			settings.setValue(SETTING_SYSTEM_IMAGE_PATH, relPath);
// 		} else {
// 			settings.setValue(SETTING_SYSTEM_IMAGE_PATH, fPath);
// 		}
// 	}

// 	ISettingsBase::commitAllSettings();
// }

/**
 * for tableView
 */
void BurningControlWindow::initTableView(void)
{
    tableModel = new QStandardItemModel(ui->tableView);
	tableHeader = new TableHeaderView(Qt::Horizontal, ui->tableView);

    ui->tableView->setModel(tableModel);
	ui->tableView->setHorizontalHeader(tableHeader);
	ui->tableView->setCornerButtonEnabled(false);
    ui->tableView->setSelectionBehavior(QAbstractItemView::SelectRows);
	// ui->tableView->setContextMenuPolicy(Qt::CustomContextMenu);
	ui->tableView->verticalHeader()->setVisible(false);
	ui->tableView->setShowGrid(false);
	ui->tableView->setEditTriggers(QAbstractItemView::NoEditTriggers);

    connect(tableModel, &QStandardItemModel::itemChanged, this, &BurningControlWindow::tableviewItemChangedSlot);
    connect(tableHeader, &TableHeaderView::stateChanged, this, &BurningControlWindow::tableviewHeaderStateChangedSlot);

	tableModel->setHorizontalHeaderItem(TABLEVIEW_COL_CHECKBOX, new QStandardItem());
	tableModel->setHorizontalHeaderItem(TABLEVIEW_COL_PART_NAME, new QStandardItem(tr("Part Name")));
	tableModel->setHorizontalHeaderItem(TABLEVIEW_COL_PART_OFFSET, new QStandardItem(tr("Part Address")));
	tableModel->setHorizontalHeaderItem(TABLEVIEW_COL_FILE_SIZE, new QStandardItem(tr("File Size")));

	ui->tableView->horizontalHeader()->setSectionResizeMode(TABLEVIEW_COL_PART_NAME, QHeaderView::Stretch);

	CheckBoxDelegate *pCheckDelegate = new CheckBoxDelegate(this);
    ui->tableView->setItemDelegateForColumn(TABLEVIEW_COL_CHECKBOX, pCheckDelegate);
}

void BurningControlWindow::tableviewHeaderStateChangedSlot(int state)
{
    int rowCount = tableModel->rowCount();

    if (state == 2) {
        for (int j = 0; j < rowCount; j++) {
            QModelIndex index = tableModel->index(j, 0, QModelIndex());
            tableModel->setData(index, true, Qt::UserRole);
        }
    }
    else if (state == 0) {
        for (int j = 0; j < rowCount; j++) {
            QModelIndex index = tableModel->index(j, 0, QModelIndex());
            tableModel->setData(index, false, Qt::UserRole);
        }
    }
}

void BurningControlWindow::tableviewItemChangedSlot(QStandardItem *item)
{
    int rowCount = tableModel->rowCount();
    int column = item->index().column();

    if (TABLEVIEW_COL_CHECKBOX == column) {
        Qt::CheckState state = Qt::Unchecked;
        int nSelectedCount = 0;
        for (int i = 0; i < rowCount; i++) {
            if (tableModel->index(i, 0).data(Qt::UserRole).toBool()) {
                nSelectedCount++;
            }
        }
        if (nSelectedCount >= rowCount) {
            state = Qt::Checked;
        }
        else if (nSelectedCount > 0) {
            state = Qt::PartiallyChecked;
        }
        tableHeader->onStateChanged(state);
    }
}

static QString getDefaultLoader(QString &target)
{
	if(target == QString("EMMC")) {
		return QString(":/loader/loader_mmc.bin");
	}
	else if(target == QString("SD Card")) {
		return QString(":/loader/loader_mmc.bin");
	}
	else if(target == QString("OTP")) {
		return QString(":/loader/loader_spi_nor.bin");
	}
	else if(target == QString("SPI NAND")) {
		return QString(":/loader/loader_spi_nand.bin");
	}
	else if(target == QString("SPI NOR")) {
		return QString(":/loader/loader_spi_nor.bin");
	}
	else {
		return QString();
	}
}

static QString convertFileSize(qint64 size) {
    if (size < 1024) {
        return QString::number(size) + " bytes";
    } else if (size < 1024 * 1024) {
        return QString::number(size / 1024.0, 'f', 2) + " KiB";
    } else if (size < 1024 * 1024 * 1024) {
        return QString::number(size / (1024.0 * 1024), 'f', 2) + " MiB";
    } else {
        return QString::number(size / (1024.0 * 1024 * 1024), 'f', 2) + " GiB";
    }
}

QList<struct BurnImageItem> BurningControlWindow::getImageListFromTableView() {
	QString partName;
	QList<QString> selectedParts;
	QList<struct BurnImageItem> itemList;

	bool haveLoaderPart = false;

    int rowCount = tableModel->rowCount();

	QString currentTargetText = ui->inputTarget->currentText();
	QString dft_loader = getDefaultLoader(currentTargetText);

	for (int index = 0; index < rowCount; index++) {
		if (tableModel->index(index, 0).data(Qt::UserRole).toBool()) {
			partName = tableModel->index(index, TABLEVIEW_COL_PART_NAME).data().toString();

			if(partName == QString("loader")) {
				haveLoaderPart = true;
			}

			selectedParts.append(partName);
		}
	}

	itemList.clear();

	if(!haveLoaderPart) {
		struct BurnImageItem item;

		QFile imageFile(dft_loader);

		item.partName = QString("loader");
		item.partOffset = 0;
		item.partSize = 0; // no limit
		item.fileName = imageFile.fileName();
		item.fileSize = imageFile.size();

		itemList.append(item);
	}

	for (auto& item : imageList) {
		partName = item.partName;

		if(selectedParts.contains(partName)) {
			itemList.append(item);
		}
	}

	BurnLibrary::instance()->localLog(QString("Image list:"));

    for (auto& item : itemList) {
        QString logMessage = QStringLiteral("\tPart Name: %1, Part Offset: %2, Part Size: %3, File Name: %4, File Size: %5")
                             .arg(item.partName)
                             .arg(item.partOffset)
                             .arg(item.partSize)
                             .arg(item.fileName)
                             .arg(item.fileSize);

        BurnLibrary::instance()->localLog(logMessage);
    }

	return itemList;
}

bool BurningControlWindow::applyImageListToTableView() {
	int rowCount = 0;

	if(0 >= imageList.size()) {
		return false;
	}

    std::sort(imageList.begin(), imageList.end()); // Uses operator<

	for (auto& item : imageList) {
		if ((item.partName == QString("loader")) &&
			item.fileName.startsWith(":/loader/")) {
			continue;
		}

		tableModel->insertRows(rowCount, 1);

		QModelIndex index = tableModel->index(rowCount, 0, QModelIndex());
		tableModel->setData(index, true, Qt::UserRole);

		tableModel->setItem(rowCount, TABLEVIEW_COL_PART_NAME, new QStandardItem(item.partName));
		tableModel->setItem(rowCount, TABLEVIEW_COL_PART_OFFSET, new QStandardItem(QString("0x") + QString("%1").arg(item.partOffset, 8, 16, QLatin1Char('0')).toUpper()));
		tableModel->setItem(rowCount, TABLEVIEW_COL_FILE_SIZE, new QStandardItem(convertFileSize(item.fileSize)));

		rowCount++;
	}

	return true;
}

bool BurningControlWindow::parseImage() {
	QFile imageFile;
	QFileInfo imageFileInfo;
	QString imageFileSuffix;
	struct BurnImageItem item;

	QString currentImagePath = ui->inputSysImage->text();

	tableModel->removeRows(0, tableModel->rowCount());

	if(!QFile::exists(currentImagePath)) {
		BurnLibrary::instance()->localLog(QStringLiteral("Image (%1) not exists").arg(currentImagePath));

		return false;
	}

	imageFileInfo.setFile(currentImagePath);
	imageFileSuffix = imageFileInfo.suffix().toLower();

	if(imageFileSuffix == QString("kdimg")) {
		lastKdImageModifiedTime = imageFileInfo.lastModified();

		return parseKdimageToImageList(currentImagePath);
	} else {
		imageList.clear();

		// add image
		imageFile.setFileName(currentImagePath);

		item.partName = QString("image");
		item.partOffset = 0x00;
		item.partSize = imageFile.size();
        item.partEraseSize = 0x00;
		item.fileName = imageFile.fileName();
		item.fileSize = imageFile.size();

		imageList.append(item);

		return applyImageListToTableView();
	}

	return false;
}

static bool copyPartOfFile(const QString& sourceFilePath, const QString& destinationFilePath, qint64 offset, qint64 size) {
    QFile sourceFile(sourceFilePath);
    QFile destinationFile(destinationFilePath);

    // Open the source file in read-only mode
    if (!sourceFile.open(QIODevice::ReadOnly)) {
		BurnLibrary::instance()->localLog(QStringLiteral("Could not open source file: %1").arg(sourceFilePath));
        return false;
    }

    // Seek to the specified offset in the source file
    if (!sourceFile.seek(offset)) {
		BurnLibrary::instance()->localLog(QStringLiteral("Could not seek to offset %1 in source file.").arg(offset));
        return false;
    }

    // Open the destination file in write-only mode
    if (!destinationFile.open(QIODevice::WriteOnly)) {
		BurnLibrary::instance()->localLog(QStringLiteral("Could not open destination file: %1").arg(destinationFilePath));
        return false;
    }

    // Read the required data from the source file
    QByteArray data = sourceFile.read(size);
    if (data.size() != size) {
		BurnLibrary::instance()->localLog(QStringLiteral("Could not read enough data. Expected %1 bytes, but got %2.").arg(size).arg(data.size()));
        return false;
    }

    // Write the data to the destination file
    qint64 bytesWritten = destinationFile.write(data);
    if (bytesWritten != size) {
		BurnLibrary::instance()->localLog(QStringLiteral("Error writing to destination file. Expected %1 bytes, but wrote %2.").arg(size).arg(bytesWritten));
        return false;
    }

	BurnLibrary::instance()->localLog(QStringLiteral("Successfully extract %1 bytes from %2 to %3.").arg(size).arg(sourceFilePath).arg(destinationFilePath));

    // Close both files
    sourceFile.close();
    destinationFile.close();

    return true;
}

bool BurningControlWindow::parseKdimageToImageList(QString &imagePath) {
    struct kd_img_hdr_t hdr;
	QList<struct kd_img_part_t> parts;
	bool haveLoaderPart = false;

	QFile imageFile(imagePath);

    if (!imageFile.open(QIODevice::ReadOnly | QIODevice::Unbuffered)) {
		BurnLibrary::instance()->localLog(QStringLiteral("Could not open kdimage file: %1").arg(imagePath));
        return false;
    }

	if(0x00 != parseKdImage(imageFile, hdr, parts)) {
		imageFile.close();
		BurnLibrary::instance()->localLog(QStringLiteral("Could not parse kdimage header: %1").arg(imagePath));
        return false;
	}

	if(compareKdImage(hdr, parts, lastKdImageHdr, lastKdImageParts)) {
		imageFile.close();
		BurnLibrary::instance()->localLog(QStringLiteral("Same KdImage %1").arg(imagePath));

		imageList = lastKdImageList;
		return applyImageListToTableView();
	}

	{
		bool result = false;

		// Create a tips message box
		QMessageBox* msgBox = new QMessageBox(QMessageBox::Information, 
                                      "Tips", 
                                      "Processing the image, please wait...", 
                                      QMessageBox::NoButton, this);
		msgBox->setModal(true); // Make sure the dialog is modal
		msgBox->setStandardButtons(QMessageBox::NoButton);  
		msgBox->show();         // Show the dialog

		// Create a worker object and a thread to run it in
		ExtractKdImageWorker* worker = new ExtractKdImageWorker();
		QThread* workerThread = new QThread();

		// Move the worker object to the new thread
		worker->moveToThread(workerThread);

		// Connect signals and slots
		QObject::connect(workerThread, &QThread::started, [this, worker, &imageFile, &parts]() {
			// Start the long-running task once the thread is started
			worker->extractKdImage(imageFile, parts, lastKdImageParts, imageList, lastKdImageList);
		});

		// Connect the taskCompleted signal to handle the completion
		QObject::connect(worker, &ExtractKdImageWorker::taskCompleted, [worker, &msgBox, &result, &workerThread](bool taskResult) {
			result = taskResult;  // Capture the result

			// Close the message box once the task is complete
			msgBox->accept();
			msgBox->deleteLater();

			// Cleanup: quit the worker thread
			workerThread->quit();
			workerThread->wait();  // Wait until the thread has finished processing
			worker->deleteLater();
			workerThread->deleteLater();
		});

	    QEventLoop loop;
    	QObject::connect(worker, &ExtractKdImageWorker::taskCompleted, &loop, &QEventLoop::quit);

		// Start the worker thread
		workerThread->start();

		loop.exec();

		if(false == result) {
			imageFile.close();
			BurnLibrary::instance()->localLog(QStringLiteral("Can not extract KdImage %1").arg(imagePath));
			return false;
		}
	}

	imageFile.close();

	lastKdImageHdr = hdr;
	lastKdImageParts = parts;
	lastKdImageList = imageList;

	return applyImageListToTableView();
}
