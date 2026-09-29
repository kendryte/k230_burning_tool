#include "EventStack.h"
#include <QDebug>
#include <QList>

EventStack::EventStack(int size) : list(QList<void *>(size, NULL)) {
}

void EventStack::set(unsigned int index, void *data) {
	Q_ASSERT(data != NULL);

	mutex.lock();
	if (canceled) {
		mutex.unlock();
		return;
	}
	list[index] = data;
	mutex.unlock();

	cond.wakeAll();
}

void EventStack::cancel() {
	mutex.lock();
	canceled = true;
	mutex.unlock();
	cond.wakeAll();
}

void *EventStack::pick(unsigned int index, int timeout_s) {
	int tmo = timeout_s;
	if(tmo > 100) {
		tmo = 100;
	}

	while (true) {
		mutex.lock();
		auto ret = list.at(index);
		if (!ret) {
			if (canceled) {
				mutex.unlock();
				return NULL;
			}

			QDeadlineTimer deadline(tmo * 1000);
			bool success = cond.wait(&mutex, deadline);
			if (!success) {
				mutex.unlock();

				qDebug() << "process wait condition timeout.";
				return NULL;
			}
		}
		mutex.unlock();

		if (ret) {
			return ret;
		}
	}
}
