#pragma once

#include <QElapsedTimer>
#include <QImage>
#include <QPixmap>
#include <QString>
#include <QTimer>
#include <QWidget>

// Startup sequence: the RareStudio logo transmits like a broadcast antenna (signal arcs pulsing
// outward) over the app's dark base colour, with its name and version beneath. Time-driven (every frame is a pure
// function of the clock), so it looks the same however busy the startup is.
//
// Loading work blocks the UI thread, so call pump() from inside long steps to keep it moving.
class SplashScreen : public QWidget {
public:
	SplashScreen();
	~SplashScreen() override;

	void setStatus(const QString &text);
	// For screenshots in the self-test: freeze the sequence at `seconds` instead of following the clock.
	void freezeAt(double seconds) { m_frozen = seconds; }
	// Keeps the sequence playing until it has finished (or was skipped by a click/key).
	void waitUntilDone();
	// Fades out and deletes itself.
	void dismiss();

	// Repaints the active splash, at most about 60 times a second. A no-op when there is none.
	static void pump();

protected:
	void paintEvent(QPaintEvent *) override;
	void mousePressEvent(QMouseEvent *) override { m_skipped = true; }
	void keyPressEvent(QKeyEvent *) override { m_skipped = true; }

private:
	void paintSignal(QPainter &p, double t, const QPointF &centre);
	void paintBrand(QPainter &p, double t, const QPointF &centre);

	QElapsedTimer m_clock;
	QTimer m_frame;
	QPixmap m_logo;
	QString m_status = "Starting";
	bool m_skipped = false;
	double m_frozen = -1;
	QString m_version, m_build;
};
