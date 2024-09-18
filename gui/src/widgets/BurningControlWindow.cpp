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

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>
#include <QJsonParseError>

#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif

#define SETTING_SYSTEM_IMAGE_PATH 			"sys-image-path"
#define SETTING_BYTE_SIZE 					"byte-size"

#define TABWIDGET_IMAGE_BURN_INDEX			(0)
#define TABWIDGET_PARTITION_BURN_INDEX		(1)

#define TABLEVIEW_SELECT_COL				(0)
#define TABLEVIEW_ADDRESS_COL				(1)
#define TABLEVIEW_ALTNAME_COL				(2)
#define TABLEVIEW_FILE_PATH_COL				(3)
#define TABLEVIEW_BTN_OPEN_COL				(4)

BurningControlWindow::BurningControlWindow(QWidget *parent)
	: QGroupBox(parent), ui(new Ui::BurningControlWindow), settings(QSettings::Scope::UserScope, SETTINGS_CATEGORY, "burning") {
	ui->setupUi(this);

	initTableView();

    readSettings();

#if IS_AVALON_NANO3
    ui->tabWidget->setTabVisible(TABWIDGET_PARTITION_BURN_INDEX, false);
	ui->tabWidget->setCurrentIndex(TABWIDGET_IMAGE_BURN_INDEX);

	ui->inputTarget->hide();
	ui->label_15->hide();
	ui->buttonStartAuto->hide();
	ui->btnOpenSettings->hide();
#else
	ui->label_tips->hide();
#endif

	// auto select loader
	QModelIndex index = tableModel->index(0, 0, QModelIndex());
	tableModel->setData(index, true, Qt::UserRole);

	GlobalSetting::flashTarget.connectCombobox(ui->inputTarget, true);

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
		QMessageBox::critical(Q_NULLPTR, QString(), tr("Parse Download Configure Failed."));

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
		QMessageBox::critical(Q_NULLPTR, QString(), tr("Parse Download Configure Failed."));
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
	auto str = QFileDialog::getOpenFileName(this, tr("Open Image File"), ui->inputSysImage->text(), tr("Image(*.img);;All Files(*.*)"), nullptr, QFileDialog::ReadOnly);
	if (str.isEmpty()) {
		return;
	}
	ui->inputSysImage->setText(str);

	checkSysImage();
}

void BurningControlWindow::handleSettingsWindowButtonState() {
	if (autoBurningEnabled) {
		ui->tabWidget->setEnabled(false);
		ui->inputTarget->setEnabled(false);
		ui->btnOpenSettings->setEnabled(false);
	} else if (BurnLibrary::instance()->getBurningJobCount() > 0) {
		ui->tabWidget->setEnabled(false);
		ui->inputTarget->setEnabled(false);
		ui->btnOpenSettings->setEnabled(false);
	} else {
		ui->tabWidget->setEnabled(true);
		ui->inputTarget->setEnabled(true);
		ui->btnOpenSettings->setEnabled(true);
	}
}

bool BurningControlWindow::checkSysImage() {
	QFile _fd;
	struct BurnImageItem item;

	imageList.clear();

	/* default loader */
	kburnUsbIspCommandTaget isp_target = (kburnUsbIspCommandTaget)GlobalSetting::flashTarget.getValue();

	qDebug() << __func__ << __LINE__ << isp_target;

	if(TABWIDGET_IMAGE_BURN_INDEX == ui->tabWidget->currentIndex()) {
		QString fPath = ui->inputSysImage->text();

		_fd.setFileName(fPath);

		if (_fd.exists()) {
			QString info = tr("File Size: ");

			QLocale locale = this->locale();
			info += locale.formattedDataSize(_fd.size());

			ui->txtImageInfo->setText(info);
			ui->txtImageInfo->setStyleSheet("");

			qint64 fSize = _fd.size();
			if(fSize > 4096) {
				fSize = (fSize + 4096 - 1) & (-4096);
			}

			item.address = 0;
			item.size = fSize;
			item.fileName = _fd.fileName();
			strncpy(item.altName, "image", 32);
			imageList.append(item);

			memset(&item, 0, sizeof(struct BurnImageItem));

			_fd.setFileName(QString(":/loader.bin"));
			item.address = 0;
			item.size = _fd.size();
			item.fileName = _fd.fileName();
			strncpy(item.altName, "loader", 32);
			imageList.append(item);

#if IS_AVALON_NANO3
			int taget_nand_index = ui->inputTarget->findText(QString("SPI NAND"));
			ui->inputTarget->setCurrentIndex(taget_nand_index);
#endif

			return true;
		} else {
			ui->txtImageInfo->setText(tr("Can't Find File"));
			ui->txtImageInfo->setStyleSheet("QLabel { color : red; }");

			return false;
		}
	} else if(TABWIDGET_PARTITION_BURN_INDEX == ui->tabWidget->currentIndex()) {
		/**
		 * 1. 遍历所有的item，将地址，altname，文件名放到对应的QVector，并要判断是否为空
		 * 2. 检查QVector
		 */

		bool selected = false;
		uint address = 0, itemCount = 0;
		QString  addressString, altNameString, filePathString;

		QVector<uint> addressVector;
		QVector<QString> altnameVector;
		QVector<QString> filepathVector;

		for(int row = 0; row < tableModel->rowCount(); row++) {
			bool selected = tableModel->index(row, 0).data(Qt::UserRole).toBool();

			if(selected) {
				itemCount++;

				/**
				 * check altname
				 */
				altNameString = tableModel->index(row, TABLEVIEW_ALTNAME_COL).data().toString();
				if(altNameString.isEmpty()) {
					QMessageBox::critical(Q_NULLPTR, QString(), tr("Row %1 altName \"%2\" is Empty").arg(row).arg(altNameString));
					return false;
				}
				// if(altnameVector.contains(altNameString)) {
				// 	QMessageBox::critical(Q_NULLPTR, QString(), tr("Row %1 altName \"%2\" is Repeated").arg(row).arg(altNameString));
				// 	return false;
				// }
				altnameVector.append(altNameString);

				address = -1;
				if(altNameString != QString("loader")) {
					/**
					 * check address
					 */
					addressString = tableModel->index(row, TABLEVIEW_ADDRESS_COL).data().toString();
					if(addressString.isEmpty()) {
						QMessageBox::critical(Q_NULLPTR, QString(), tr("Row %1 Convert address \"%2\" failed").arg(row).arg(addressString));
						return false;
					}
					if((addressString.startsWith(QString("0x"))) || (addressString.startsWith(QString("0X")))) {
						address = addressString.toUInt(&selected, 16);
					} else {
						address = addressString.toUInt(&selected, 10);
					}

					if(false == selected) {
						QMessageBox::critical(Q_NULLPTR, QString(), tr("Row %1 Convert address \"%2\" failed").arg(row).arg(addressString));
						return false;
					}

					if(addressVector.contains(address)) {
						QMessageBox::critical(Q_NULLPTR, QString(), tr("Row %1 address \"%2\" is Repeated").arg(row).arg(addressString));
						return false;
					}
					addressVector.append(address);
				}

				/**
				 * check filepath
				 */
				filePathString = tableModel->index(row, TABLEVIEW_FILE_PATH_COL).data().toString();
				if(filePathString.isEmpty()) {
					QMessageBox::critical(Q_NULLPTR, QString(), tr("Row %1 filepath \"%2\" is Empty").arg(row).arg(filePathString));
					return false;
				}

				/* TBD: check same file? */
				if(filepathVector.contains(filePathString)) {
					QMessageBox::critical(Q_NULLPTR, QString(), tr("Row %1 filepath \"%2\" is Repeated").arg(row).arg(filePathString));
					return false;
				}

				_fd.setFileName(filePathString);
				if(!_fd.exists()) {
					QMessageBox::critical(Q_NULLPTR, QString(), tr("Row %1 file \"%2\" not Exists").arg(row).arg(filePathString));
					return false;
				}
				filepathVector.append(filePathString);

				memset(&item, 0, sizeof(struct BurnImageItem));

				qint64 fSize = _fd.size();
				if(fSize > 4096) {
					fSize = (fSize + 4096 - 1) & (-4096);
				}

				item.address = address;
				item.size = fSize;
				item.fileName = filePathString;
				strncpy(item.altName, qPrintable(altNameString), 32);
				imageList.append(item);

				qDebug() << __func__ << __LINE__ << row << address << _fd.size() << addressString << fSize << altNameString << filePathString;
			}
		}

		QList<struct BurnImageItem>	sortList(imageList);
		std::sort(sortList.begin(), sortList.end());
		int listSize = sortList.size();
		for(int i = 0; i < (listSize - 1); i++) {
			struct BurnImageItem a, b;
			a = sortList[i];
			b = sortList[i + 1];

			if(0x00 == strncmp(a.altName, "loader", sizeof(a.altName)) || 0x00 == strncmp(b.altName, "loader", sizeof(a.altName))) {
				continue;
			}

			if((a.address + a.size) > (b.address)) {
				QMessageBox::critical(Q_NULLPTR, QString(), tr("File %1 Too Large").arg(a.fileName));
				return false;
			}
		}

		if(false == altnameVector.contains(QString("loader"))) {
			QMessageBox::critical(Q_NULLPTR, QString(), tr("Please Select a 'loader'"));
			return false;
		}

		/* just loader */
		if(0x01 >= itemCount) {
			QMessageBox::critical(Q_NULLPTR, QString(), tr("Please Select at least one Image File"));
			return false;
		}

		if(((itemCount - 1) != addressVector.size()) || (itemCount != altnameVector.size()) || (itemCount != filepathVector.size())) {
			QMessageBox::critical(Q_NULLPTR, QString(), tr("Unknown Error"));
			return false;
		}

		return true;
	}

	return false;
}

void BurningControlWindow::readSettings(void)
{
//    QDir appDir(QCoreApplication::applicationDirPath());

//    if (settings.contains(SETTING_SYSTEM_IMAGE_PATH)) {
//        QString saved = settings.value(SETTING_SYSTEM_IMAGE_PATH).toString();
//        if (QDir(saved).isRelative()) {
//            saved = QDir::cleanPath(appDir.absoluteFilePath(saved));
//        }
//        ui->inputSysImage->setText(saved);
//        ui->tabWidget->setCurrentIndex(TABWIDGET_IMAGE_BURN_INDEX);
//        checkSysImage();
//    }
}

void BurningControlWindow::saveSettings(void)
{
//    QString fPath = ui->inputSysImage->text();

//    if (!fPath.isEmpty()) {
//        QDir d(QCoreApplication::applicationDirPath());
//        QString relPath = d.relativeFilePath(fPath);

//        if (d.isRelativePath(relPath)) {
//            settings.setValue(SETTING_SYSTEM_IMAGE_PATH, relPath);
//        } else {
//            settings.setValue(SETTING_SYSTEM_IMAGE_PATH, fPath);
//        }
//    }
//    ISettingsBase::commitAllSettings();
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

    ui->tableView->setModel(tableModel);
	ui->tableView->setHorizontalHeader(tableHeader);
	ui->tableView->setCornerButtonEnabled(false);
    ui->tableView->setSelectionBehavior(QAbstractItemView::SelectRows);
	ui->tableView->setContextMenuPolicy(Qt::CustomContextMenu);

    connect(tableModel, &QStandardItemModel::itemChanged, this, &BurningControlWindow::tableviewItemChangedSlot);
    connect(tableHeader, &TableHeaderView::stateChanged, this, &BurningControlWindow::tableviewHeaderStateChangedSlot);

	QMenu *tableMenu = new QMenu(ui->tableView);

    QAction *addItem = tableMenu->addAction(tr("Add one Line"));
    QAction *delItem = tableMenu->addAction(tr("Del current Line"));
    QAction *importConfig = tableMenu->addAction(tr("Export Configure"));
    QAction *exportConfig = tableMenu->addAction(tr("Import Configure"));

    connect(addItem, &QAction::triggered, this, &BurningControlWindow::tableviewMenuAddItemSlot);
    connect(delItem, &QAction::triggered, this, &BurningControlWindow::tableviewMenuDelItemSlot);
    connect(importConfig, &QAction::triggered, this, &BurningControlWindow::tableviewMenuImportConfigmSlot);
    connect(exportConfig, &QAction::triggered, this, &BurningControlWindow::tableviewMenuExportConfigmSlot);

	connect(ui->tableView, &QTableView::customContextMenuRequested, this, [this, tableMenu] (const QPoint &pos) {
		menuPoint = pos;
		tableMenu->exec(mapToGlobal(pos));
	});

	tableModel->setHorizontalHeaderItem(TABLEVIEW_SELECT_COL, new QStandardItem());
	tableModel->setHorizontalHeaderItem(TABLEVIEW_ADDRESS_COL, new QStandardItem(tr("Target Address")));
	tableModel->setHorizontalHeaderItem(TABLEVIEW_ALTNAME_COL, new QStandardItem(tr("Target Type")));
	tableModel->setHorizontalHeaderItem(TABLEVIEW_FILE_PATH_COL, new QStandardItem(tr("File Path")));
	tableModel->setHorizontalHeaderItem(TABLEVIEW_BTN_OPEN_COL, new QStandardItem(tr("Open")));

	/**
	 * 选中
	 */
	CheckBoxDelegate *pCheckDelegate = new CheckBoxDelegate(this);
    ui->tableView->setItemDelegateForColumn(TABLEVIEW_SELECT_COL, pCheckDelegate);

	/**
	 * 目标介质
	 */
	// QStringList targetMedium;
	// targetMedium << "eMMC" << "Nor Flash";

	// ComboDelegate *pComboMedium = new ComboDelegate(this);
	// pComboMedium->setItems(targetMedium);
    // ui->tableView->setItemDelegateForColumn(1, pComboMedium);

	/**
	 * 目标地址
	 */
	for(int i = 0; i < 10; i++) {
		tableModel->setItem(i, TABLEVIEW_ADDRESS_COL, new QStandardItem(QString("0x00000000")));
	}

	/**
	 * 目标名称
	 */
    QStringList targetAltName;
    targetAltName << "loader" << "spl" << "uboot" << "rtt" << "kernel" << "rootfs" << "image";

	ComboDelegate *pComboAltName = new ComboDelegate(this);
	pComboAltName->setItems(targetAltName);
    ui->tableView->setItemDelegateForColumn(TABLEVIEW_ALTNAME_COL, pComboAltName);
	tableModel->setItem(0, TABLEVIEW_ALTNAME_COL, new QStandardItem(QString("loader")));
	for(int i = 1; i < 10;i++) {
		tableModel->setItem(i, TABLEVIEW_ALTNAME_COL, new QStandardItem(QString("image")));
	}
	/**
	 * 文件路径
	 */
	ui->tableView->horizontalHeader()->setSectionResizeMode(TABLEVIEW_FILE_PATH_COL, QHeaderView::Stretch);
	tableModel->setItem(0, TABLEVIEW_FILE_PATH_COL, new QStandardItem(QString(":/loader.bin")));

	/**
	 * Open
	 */
	PushButtonDelegate *pBtnOpen = new PushButtonDelegate(tr("..."), this);
    ui->tableView->setItemDelegateForColumn(TABLEVIEW_BTN_OPEN_COL, pBtnOpen);
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
	int row = index.row();
	if(row < 0) {
		row = 0;
	}

	QString filePath = tableModel->index(row, TABLEVIEW_FILE_PATH_COL).data().toString();

#if defined(Q_OS_LINUX)
	QString str = QFileDialog::getOpenFileName(this, tr("Select File"), filePath, tr(""), nullptr, QFileDialog::ReadOnly | QFileDialog::DontUseNativeDialog);
#else
	QString str = QFileDialog::getOpenFileName(this, tr("Select File"), filePath, tr(""), nullptr, QFileDialog::ReadOnly);
#endif

	if (str.isEmpty()) {
		return;
	}

	QModelIndex selIndex = tableModel->index(index.row(), 0, QModelIndex());
	tableModel->setData(selIndex, true, Qt::UserRole);

	QModelIndex fnameIndex = tableModel->index(index.row(), TABLEVIEW_FILE_PATH_COL, QModelIndex());
	tableModel->setData(fnameIndex, str);

	emit tableModel->dataChanged(fnameIndex, fnameIndex);
}

void BurningControlWindow::tableviewMenuAddItemSlot(bool checked)
{
	int row = ui->tableView->rowAt(menuPoint.y());
	tableModel->insertRows(row, 1);
	tableModel->setItem(row, TABLEVIEW_ADDRESS_COL, new QStandardItem(QString("0x00000000")));
}

void BurningControlWindow::tableviewMenuDelItemSlot(bool checked)
{
	int row = ui->tableView->rowAt(menuPoint.y());
	tableModel->removeRows(row, 1);
}

/**
{
	"base": "/home/user/xxx",
	"version": 1.0,
	"files": [{
		"addr": 1024,
		"alt": "uboot",
		"file": "1.bin"
	}, {
		"addr": 1024,
		"alt": "uboot",
		"file": "1.bin"
	}, {
		"addr": 1024,
		"alt": "uboot",
		"file": "1.bin"
	}]
}
*/

void BurningControlWindow::tableviewMenuImportConfigmSlot(bool checked)
{
	QString address, altName, filePath;

	QJsonArray RootJsonArr;
	QJsonObject RootJsonObj;
	QJsonDocument RootJsonDoc;
	QJsonParseError jsonErr;

#if defined(Q_OS_LINUX)
	QString openFileName = QFileDialog::getOpenFileName(this, tr("Select File"), filePath, tr(""), nullptr, QFileDialog::ReadOnly | QFileDialog::DontUseNativeDialog);
#else
	QString openFileName = QFileDialog::getOpenFileName(this, tr("Select File"), filePath, tr(""), nullptr, QFileDialog::ReadOnly);
#endif
	if (openFileName.isEmpty()) {
		return;
	}

	QFile openFile(openFileName);
	QFileInfo openFileInfo(openFileName);
	QString basePath = openFileInfo.absolutePath();

	if(false == openFile.open(QIODeviceBase::ReadOnly)) {
		QMessageBox::critical(Q_NULLPTR, QString(), tr("Open Configure Failed"));
		return;
	}

	QByteArray content = openFile.readAll();
	openFile.close();

	RootJsonDoc = QJsonDocument::fromJson(content, &jsonErr);
	if(jsonErr.error != QJsonParseError::NoError) {
		QMessageBox::critical(Q_NULLPTR, QString(), tr("Parse Configure Failed"));
		return;
	}

	if(false == RootJsonDoc.isObject()) {
		QMessageBox::critical(Q_NULLPTR, QString(), tr("Parse Configure Failed"));
		return;
	}
	RootJsonObj = RootJsonDoc.object();

	int rowCount = 0, version = RootJsonObj.value(QString("version")).toInt(-1);
	if(version == -1) {
		QMessageBox::critical(Q_NULLPTR, QString(), tr("Parse Configure Failed"));
		return;
	}

	switch(version) {
		case 1:
			RootJsonArr = RootJsonObj.value(QString("files")).toArray();
			if(RootJsonArr.count() > 0) {
				tableModel->removeRows(0, tableModel->rowCount());

				QJsonArray::const_iterator iter = RootJsonArr.begin();
				for(;iter != RootJsonArr.end(); iter++) {
					QJsonObject temp;
					QJsonValue  item = *iter;
					if(false == item.isObject()) {
						QMessageBox::critical(Q_NULLPTR, QString(), tr("Parse Configure Failed"));
						return;
					}
					temp = item.toObject();

					address = temp.value(QString("addr")).toString();
					altName = temp.value(QString("alt")).toString();
					filePath = basePath + "/" + temp.value(QString("path")).toString();

					qDebug() << __func__ << __LINE__  << address << altName << filePath;

					tableModel->insertRows(rowCount, 1);

					QModelIndex index = tableModel->index(rowCount, 0, QModelIndex());
            		tableModel->setData(index, true, Qt::UserRole);

					tableModel->setItem(rowCount, TABLEVIEW_ADDRESS_COL, new QStandardItem(address));
					tableModel->setItem(rowCount, TABLEVIEW_ADDRESS_COL, new QStandardItem(address));
					tableModel->setItem(rowCount, TABLEVIEW_ALTNAME_COL, new QStandardItem(altName));
					tableModel->setItem(rowCount, TABLEVIEW_FILE_PATH_COL, new QStandardItem(filePath));
					rowCount ++;
				} 
			}
			break;
		default:
			break;
	}
}

void BurningControlWindow::tableviewMenuExportConfigmSlot(bool checked)
{
	bool selected = false;
	QString address, altName, filePath, basePath;

	QJsonArray RootJsonArr;
	QJsonObject RootJsonObj;
	QJsonDocument RootJsonDoc;

	if(true == checkSysImage()) {
		for(int row = 0; row < tableModel->rowCount(); row++) {
			bool selected = tableModel->index(row, 0).data(Qt::UserRole).toBool();

			if(selected) {
				address = tableModel->index(row, TABLEVIEW_ADDRESS_COL).data().toString();
				altName = tableModel->index(row, TABLEVIEW_ALTNAME_COL).data().toString();
				filePath = tableModel->index(row, TABLEVIEW_FILE_PATH_COL).data().toString();

				qDebug() << __func__ << __LINE__ << row << "Selected" << address << altName << filePath;

				QFileInfo tempFileInfo(filePath);
				QString tempFilePath = tempFileInfo.absolutePath();
				if(basePath.isNull()) {
					basePath = tempFilePath;
				} else {
					if(basePath != tempFilePath) {
						QMessageBox::critical(Q_NULLPTR, QString(), tr("Files not in Same Folder"));
						return;
					}
				}

				QJsonObject tempObj;
				tempObj.insert("addr", 	QJsonValue(address));
				tempObj.insert("alt", 	QJsonValue(altName));
				tempObj.insert("path", 	QJsonValue(tempFileInfo.fileName()));

				RootJsonArr.push_back(QJsonValue(tempObj));
			} else {
				qDebug() << __func__ << __LINE__ << row << "Not Seleted";
			}
		}

		RootJsonObj.insert(QString("files"), 	QJsonValue(RootJsonArr));
		RootJsonObj.insert(QString("version"), 	QJsonValue(1.0));

		RootJsonDoc.setObject(RootJsonObj);

		QByteArray content = RootJsonDoc.toJson(QJsonDocument::Compact);
		qDebug() << __func__ << __LINE__ << content;

#if defined(Q_OS_LINUX)
		QString saveFileName = QFileDialog::getSaveFileName(this, tr("Save Configure"), QDir::homePath(), tr("Json(*.json)"), nullptr, QFileDialog::DontUseNativeDialog);
#else
		QString saveFileName = QFileDialog::getSaveFileName(this, tr("Save Configure"), QDir::homePath(), tr("Json(*.json)"), nullptr);
#endif
		if (saveFileName.isEmpty()) {
			return;
		}

		QFile saveFile(saveFileName);
		if(false == saveFile.open(QIODeviceBase::WriteOnly)) {
			QMessageBox::critical(Q_NULLPTR, QString(), tr("Save Configure Failed"));
			return;
		}

		saveFile.write(content);
		saveFile.close();
	} else {
		QMessageBox::critical(Q_NULLPTR, QString(), tr("Export Configure Failed"));
	}
}
