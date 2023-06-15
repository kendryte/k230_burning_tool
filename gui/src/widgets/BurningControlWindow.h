#ifndef BURINGCONTROL_H
#define BURINGCONTROL_H

#include <QGroupBox>
#include <QMap>
#include <QFile>
#include <QSettings>
#include <QStandardItemModel>
#include <QMenu>
#include <QPoint>

#include "common/BurnImageItem.h"
#include "common/CustomTableView.h"

namespace Ui {
class BurningControlWindow;
}

class BurningControlWindow : public QGroupBox {
	Q_OBJECT

	Ui::BurningControlWindow *ui;

	// QFile fd;
	QSettings settings;

	bool autoBurningEnabled = false;

  public:
	explicit BurningControlWindow(QWidget *parent = nullptr);
	~BurningControlWindow();

	// QString getFile() { return fd.fileName(); }
	QList<struct BurnImageItem> getImageList() { return imageList; }

private:
	QPoint 					menuPoint;
    TableHeaderView        	*tableHeader;
    QStandardItemModel     	*tableModel;
	QList<struct BurnImageItem>	imageList;

  private:
	void initTableView(void);
	void readSettings(void);
	void saveSettings(void);

  signals:
	void newProcessRequested(class BurningRequest *partialRequest);
	void showSettingRequested();
	void updateBurnLibAutoBurnFlag(bool isAuto);

  public slots:
	bool checkSysImage();

  private slots:
	void handleSettingsWindowButtonState();
	void on_btnStartBurn_clicked();
	void handleOpenDevice(QString path);
	void on_btnOpenSettings_clicked() { emit showSettingRequested(); }
    void on_buttonStartAuto_clicked(bool checked);
    void on_btnSelectImage_clicked();

    void tableviewHeaderStateChangedSlot(int state);
    void tableviewItemChangedSlot(QStandardItem* item);
	void tabviewBtnOpenClickedSlot(const QModelIndex &index);

	void tableviewMenuAddItemSlot(bool checked);
	void tableviewMenuDelItemSlot(bool checked);
	void tableviewMenuImportConfigmSlot(bool checked);
	void tableviewMenuExportConfigmSlot(bool checked);
};

#endif // BURINGCONTROL_H
