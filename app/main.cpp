#include "MainWindow.h"
#include "ObsCore.h"
#include "RareTransitions.h"
#include "SelfTest.h"
#include "SettingsDialog.h"
#include "TransitionManager.h"
#include "UiBits.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QIcon>
#include <QSettings>
#include <QMessageBox>
#include <QProcess>
#include <QStyleFactory>

#include <cmath>

int main(int argc, char **argv)
{

	// Keep embedded browser docks cheap: one shared renderer, no GPU raster of its own.
	// Dashboards are mostly text: render them in software so the browser never touches the GPU
	// (no GPU contention with your game/encoder) and holds far less memory.
	qputenv("QTWEBENGINE_CHROMIUM_FLAGS",
		"--disable-background-networking --disable-gpu --disable-gpu-compositing --disable-gpu-rasterization");

	// QtWebEngine is created lazily (when a dashboard dock is first shown). It needs this set
	// before the application object exists, otherwise creating it later crashes.
	QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);

	// Qt's warnings/errors go to a file: this is a GUI-subsystem exe with no console.
	static QFile qtLog(qEnvironmentVariable("APPDATA") + "/RareStudio/qt.log");
	QDir().mkpath(qEnvironmentVariable("APPDATA") + "/RareStudio");
	qtLog.open(QIODevice::WriteOnly | QIODevice::Truncate);
	qInstallMessageHandler([](QtMsgType, const QMessageLogContext &, const QString &msg) {
		qtLog.write(msg.toUtf8() + "\n");
		qtLog.flush();
	});
	qputenv("QT_LOGGING_RULES", "qt.webenginecontext.debug=true");

	// Automated tests run against a throw-away profile so they never touch real settings/scenes.
	if (const QByteArray prof = qgetenv("RARESTUDIO_TEST_PROFILE"); !prof.isEmpty()) {
		QSettings::setDefaultFormat(QSettings::IniFormat);
		QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, QString::fromLocal8Bit(prof));
		qputenv("APPDATA", prof);
	}

	// Settings > Interface > size: Qt scales every widget, font and the preview's pixel mapping.
	// It has to be known before the application object exists.
	QCoreApplication::setApplicationName("RareStudio");
	QCoreApplication::setOrganizationName("RareStudio");
	if (const double scale = UiBits::scaleFactor(); std::abs(scale - 1.0) > 0.01 && qEnvironmentVariableIsEmpty("QT_SCALE_FACTOR"))
		qputenv("QT_SCALE_FACTOR", QByteArray::number(scale, 'f', 2));

	QApplication app(argc, argv);
	app.setApplicationVersion(RARESTUDIO_VERSION);

	app.setStyle(QStyleFactory::create("Fusion"));
	QPalette p;
	p.setColor(QPalette::Window, QColor(17, 17, 23));
	p.setColor(QPalette::WindowText, Qt::white);
	p.setColor(QPalette::Base, QColor(18, 18, 25));
	p.setColor(QPalette::AlternateBase, QColor(23, 23, 31));
	p.setColor(QPalette::Text, Qt::white);
	p.setColor(QPalette::Button, QColor(31, 31, 42));
	p.setColor(QPalette::ButtonText, Qt::white);
	p.setColor(QPalette::Highlight, QColor(124, 58, 237));
	p.setColor(QPalette::PlaceholderText, QColor(110, 110, 132));
	p.setColor(QPalette::HighlightedText, Qt::white);
	app.setPalette(p);
	app.setWindowIcon(QIcon(":/logo"));
	if (QFile css(":/style.qss"); css.open(QIODevice::ReadOnly))
		app.setStyleSheet(QString::fromUtf8(css.readAll()));
	UiBits::installEffects(app);

	// libobs locates Game Capture's hook files relative to the working directory, assuming OBS's
	// bin\64bit layout. Mirror it so "..\..\data\obs-plugins\win-capture" resolves to our data dir.
	const QString appDir = QCoreApplication::applicationDirPath();
	QDir().mkpath(appDir + "/bin/64bit");
	QDir::setCurrent(appDir + "/bin/64bit");

	const SettingsDialog::Video v = SettingsDialog::load();
	if (!ObsCore::instance().start(QCoreApplication::applicationDirPath(), v.baseW, v.baseH, v.outW, v.outH, v.fps)) {
		QMessageBox::critical(nullptr, "RareStudio",
				      "Could not start the video engine:\n" + ObsCore::instance().error() +
					      "\n\nLog: %APPDATA%\\RareStudio\\latest.log");
		return 1;
	}

	RareTransitions::registerAll(); // our own shader transitions + brand stinger
	TransitionManager::instance().load();

	// Headless verification modes (no window): see SelfTest.h.
	{
		const QStringList args = app.arguments();
		if (const int i = args.indexOf("--selftest-ui"); i >= 0) {
			const int rc = SelfTest::ui(args.value(i + 1, QDir::tempPath() + "/rarestudio-selftest"));
			ObsCore::instance().shutdown();
			return rc;
		}
		if (const int i = args.indexOf("--selftest-video"); i >= 0) {
			const int rc = SelfTest::video(args.value(i + 1, QDir::tempPath() + "/rarestudio-selftest"));
			ObsCore::instance().shutdown();
			return rc;
		}
		if (const int i = args.indexOf("--selftest-vertical"); i >= 0) {
			const int rc = SelfTest::vertical(args.value(i + 1, QDir::tempPath() + "/rarestudio-selftest"));
			ObsCore::instance().shutdown();
			return rc;
		}
		if (const int i = args.indexOf("--selftest-stability"); i >= 0) {
			const int rc = SelfTest::stability(args.value(i + 1, QDir::tempPath() + "/rarestudio-selftest"));
			ObsCore::instance().shutdown();
			return rc;
		}
		if (const int i = args.indexOf("--selftest-transitions"); i >= 0) {
			const int rc = SelfTest::transitions(args.value(i + 1, QDir::tempPath() + "/rarestudio-selftest"));
			ObsCore::instance().shutdown();
			return rc;
		}
	}

	int rc;
	{
		MainWindow w;
		w.show();
		rc = app.exec();
	}
	ObsCore::instance().shutdown();
	// Settings > Interface size asks for a restart; start the new copy only once this one has let go
	// of the webcam, capture hooks and encoders.
	if (app.property("rsRestart").toBool())
		QProcess::startDetached(QCoreApplication::applicationFilePath(), app.arguments().mid(1));
	return rc;
}
