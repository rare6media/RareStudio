#pragma once

#include <QString>
#include <QWidget>

class QListWidget;

// Running log of clips: local saves, Twitch clips and YouTube Shorts uploads, with live status.
// Double-click an entry to open its link or file.
class ClipsPanel : public QWidget {
	Q_OBJECT
public:
	explicit ClipsPanel(QWidget *parent = nullptr);

	// Adds a line and returns its id. `target` is a URL or a local file path to open on double-click.
	int add(const QString &text, const QString &target = QString());
	void update(int id, const QString &text, const QString &target = QString());

private:
	QListWidget *m_list;
	int m_next = 0;
};
