#include "main.h"
#include "MainWindow.h"
#include <QApplication>
#include <QLocale>
#include <QScreen>
#include <QSettings>
#include <QTranslator>

#include <QPalette>
#include <QStyleFactory>

#define SETTING_WINDOW_SIZE "window-size"

static QApplication *a;
static QTranslator translator;
static MainWindow *w;
int main(int argc, char *argv[]) {
	a = new QApplication(argc, argv);

#if IS_AVALON_NANO3
	// Avalon Nano3 not load translate
#else
	if(QLocale::Chinese == QLocale::system().language()) {
		const QString baseName = "K230_qt_zh_CN";
		if (translator.load(":/i18n/" + baseName)) {
			a->installTranslator(&translator);
		}
	}
#endif

	// Force light theme
    QPalette lightPalette;
    lightPalette.setColor(QPalette::Window, Qt::white);
    lightPalette.setColor(QPalette::WindowText, Qt::black);
    lightPalette.setColor(QPalette::Base, Qt::white);
    lightPalette.setColor(QPalette::AlternateBase, Qt::lightGray);
    lightPalette.setColor(QPalette::ToolTipBase, Qt::white);
    lightPalette.setColor(QPalette::ToolTipText, Qt::black);
    lightPalette.setColor(QPalette::Text, Qt::black);
    lightPalette.setColor(QPalette::Button, Qt::lightGray);
    lightPalette.setColor(QPalette::ButtonText, Qt::black);
    lightPalette.setColor(QPalette::BrightText, Qt::red);
    lightPalette.setColor(QPalette::Link, QColor(42, 130, 218));

    a->setPalette(lightPalette);
    a->setStyle(QStyleFactory::create("Fusion"));

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
