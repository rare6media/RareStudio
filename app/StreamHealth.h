#pragma once

#include "MultiStream.h"

#include <QList>
#include <QMap>
#include <QSet>
#include <QWidget>

class QLabel;
class QVBoxLayout;
class HealthSpark;

// Live view of how the stream is doing: one verdict, the machine's own load, and a row per
// destination with bitrate history, dropped frames and congestion. Fed once a second by MainWindow.
class StreamHealthPanel : public QWidget {
	Q_OBJECT
public:
	enum class Level { Good, Fair, Poor };

	// What the encoder/renderer did since the previous tick (all zero when not measured).
	struct Machine {
		double cpu = 0;      // percent
		double fps = 0;
		double lagPct = 0;   // frames the renderer missed, percent
		double skipPct = 0;  // frames the encoder skipped, percent
	};

	// The thresholds, kept pure so the self-test can pin them down.
	static Level assessDestination(double dropPct, double congestion, bool reconnecting);
	static Level assessMachine(const Machine &m);

	explicit StreamHealthPanel(QWidget *parent = nullptr);

	void tick(const QList<MultiStream::DestStats> &dests, const Machine &machine);
	void reset(); // not live: back to the idle look and forget history

	Level level() const { return m_level; }
	QString verdict() const { return m_verdict; }

signals:
	// Emitted when the overall level changes while live. `text` says what is wrong (or "healthy").
	void levelChanged(StreamHealthPanel::Level level, const QString &text);

private:
	struct Row {
		QWidget *box = nullptr;
		QLabel *title = nullptr;
		QLabel *detail = nullptr;
		HealthSpark *spark = nullptr;
		QList<QPair<int, int>> window; // (total, dropped) at each recent tick
		quint64 lastBytes = 0;
		bool haveLast = false;
	};

	void setLevel(Level l, const QString &text);

	QLabel *m_banner = nullptr;
	QLabel *m_machine = nullptr;
	QLabel *m_empty = nullptr;
	QVBoxLayout *m_rows = nullptr;
	QMap<int, Row> m_byIndex;
	Level m_level = Level::Good;
	QString m_verdict;
	bool m_live = false;
};
