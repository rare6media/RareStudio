#pragma once

#include <QAbstractNativeEventFilter>
#include <QKeySequence>
#include <QList>
#include <QObject>

// System-wide hotkeys (work while a game has focus). Uses RegisterHotKey, so there is no
// keyboard hook and no polling: idle cost is zero.
class HotkeyManager : public QObject, public QAbstractNativeEventFilter {
	Q_OBJECT
public:
	enum Action { ToggleStream = 1, ToggleRecord, SaveClip, ToggleReplay, MuteMic, NextScene, PrevScene, PauseRecord, Screenshot, SceneBase = 100 };

	explicit HotkeyManager(QObject *parent = nullptr);
	~HotkeyManager() override;

	// Re-reads "hotkeys/<name>" from settings and (re)registers everything.
	void reload();

	static QString settingsKey(Action a);
	static QString label(Action a);
	static QList<Action> configurable();

	bool nativeEventFilter(const QByteArray &type, void *message, qintptr *result) override;

signals:
	void triggered(int action); // an Action, or SceneBase + n (1-based) for Ctrl+Alt+n

private:
	bool registerOne(int id, const QKeySequence &seq);
	void unregisterAll();

	QList<int> m_ids;
};
