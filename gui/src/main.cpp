#include "main.h"
#include "MainWindow.h"
#include <QApplication>
#include <QLocale>
#include <QScreen>
#include <QSettings>
#include <QTranslator>

#define SETTING_WINDOW_SIZE "window-size"

static QApplication *a;
static QTranslator translator;
static MainWindow *w;
int main(int argc, char *argv[]) {
	a = new QApplication(argc, argv);

	if(QLocale::Chinese != QLocale::system().language()) {
		const QString baseName = "K230_qt_en";
		if (translator.load(":/translations/" + baseName)) {
			a->installTranslator(&translator);
		}
	}

	w = new MainWindow;

	QSettings settings(QSettings::Scope::UserScope, SETTINGS_CATEGORY, "ui");
	if (settings.contains(SETTING_WINDOW_SIZE)) {
		QSize size = settings.value(SETTING_WINDOW_SIZE).toSize();
		QSize container = w->screen()->size() * 0.9;
		if (size.height() > container.height() || size.width() > container.width()) {
			w->showMaximized();
		} else {
			w->resize(size);
			auto topleft = (w->screen()->size() - size) / 2;
			w->move(topleft.width(), topleft.height());
			w->showNormal();
		}
	} else {
		w->showNormal();
	}

	auto r = a->exec();

	qDebug("Application main() return.");

	delete w;

	QApplication::exit(r);

	return r;
}

void saveWindowSize() {
	QSettings settings(QSettings::Scope::UserScope, SETTINGS_CATEGORY, "ui");
	settings.setValue(SETTING_WINDOW_SIZE, w->size());
}

QString tr(const char *s, const char *c, int n) {
	return a->tr(s, c, n);
}
