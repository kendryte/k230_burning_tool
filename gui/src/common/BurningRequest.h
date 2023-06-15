#pragma once

#include <public/canaan-burn.h>
#include <QString>

#include "common/BurnImageItem.h"

class BurningRequest {
  public:
	virtual ~BurningRequest(){};

	// QString systemImageFile;
	bool isAutoCreate = false;
	QList<struct BurnImageItem>	imageList;

	enum DeviceKind
	{ K230, };

	virtual QString getIdentity() const = 0;
	static class BurningProcess *reqeustFactory(KBMonCTX scope, const BurningRequest *request);

  protected:
	virtual enum DeviceKind getKind() const = 0;
};

class K230BurningRequest : public BurningRequest {
  protected:
	enum DeviceKind getKind() const { return K230; }

  public:
	QString usbPath;
	QString getIdentity() const { return usbPath + "!" + typeid(*this).name(); }
};
