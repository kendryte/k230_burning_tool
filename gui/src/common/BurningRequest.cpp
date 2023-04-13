#include "BurningRequest.h"
#include "K230BurningProcess.h"

class BurningProcess *BurningRequest::reqeustFactory(KBMonCTX scope, const BurningRequest *request) {
	switch (request->getKind()) {
	case K230:
		return new K230BurningProcess(scope, reinterpret_cast<const K230BurningRequest *>(request));
	default:
		qFatal("invalid device kind argument.");
	}
}
