#ifndef BURINGCONTROL_H
#define BURINGCONTROL_H

#include <QGroupBox>
#include <QMap>
#include <QFile>
#include <QSettings>
#include <QStandardItemModel>
#include <QMenu>
#include <QPoint>
#include <QString>

#include "common/BurnImageItem.h"
#include "common/KdImageParser.h"
#include "common/CustomTableView.h"

namespace Ui {
class BurningControlWindow;
}

class BurningControlWindow : public QGroupBox {
	Q_OBJECT

  public:
	explicit BurningControlWindow(QWidget *parent = nullptr);
	~BurningControlWindow();

	// QList<struct BurnImageItem> getImageList() { return imageList; }
	QList<struct BurnImageItem> getImageList() { return getImageListFromTableView(); }

private:
	Ui::BurningControlWindow *ui;
    TableHeaderView        	*tableHeader;
    QStandardItemModel     	*tableModel;

	QSettings settings;
	bool autoBurningEnabled = false;
	QList<struct BurnImageItem>	imageList;

    struct kd_img_hdr_t lastKdImageHdr;
	QList<struct kd_img_part_t> lastKdImageParts;

private:
	void initTableView(void);
	// void readSettings(void);
	// void saveSettings(void);

	bool parseImage();
	bool parseKdimageToImageList(QString &imagePath);

	bool applyImageListToTableView();
	QList<struct BurnImageItem> getImageListFromTableView();

  signals:
	void newProcessRequested(class BurningRequest *partialRequest);
	void showSettingRequested();
	void updateBurnLibAutoBurnFlag(bool isAuto);

  private slots:
	void handleSettingsWindowButtonState();
	void on_btnStartBurn_clicked();
	void handleOpenDevice(QString path);
	void on_btnOpenSettings_clicked() { emit showSettingRequested(); }
    void on_buttonStartAuto_clicked(bool checked);
    void on_btnSelectImage_clicked();

    void tableviewHeaderStateChangedSlot(int state);
    void tableviewItemChangedSlot(QStandardItem* item);
};

#endif // BURINGCONTROL_H
