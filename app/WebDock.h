#pragma once

#include <QDockWidget>
#include <QList>
#include <QTimer>
#include <QUrl>

class QTabWidget;
class QWebEngineView;

// One dock holding the channel dashboards (YouTube Studio, Twitch Dashboard...) as tabs.
//
// Each browser view is created the first time its tab is shown and then kept for the life of
// the app (destroying web views inside docks causes ghost images and crashes in Qt). To keep
// RAM low, a view that is no longer visible is frozen and then discarded, which makes Chromium
// release its renderer; it reloads when you come back to the tab.
class WebDock : public QDockWidget {
	Q_OBJECT
public:
	struct Site {
		QString title;
		QUrl home;
	};

	WebDock(const QString &title, const QList<Site> &sites, QWidget *parent = nullptr);

private:
	struct Tab {
		QWidget *container = nullptr;
		QWebEngineView *view = nullptr;
		QUrl home;
		QTimer *sleep = nullptr;
	};

	void sync();
	void wake(int index);
	void scheduleSleep(int index);
	void ensureView(int index);

	QTabWidget *m_tabs = nullptr;
	QList<Tab> m_data;
};
