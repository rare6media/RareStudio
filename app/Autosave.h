#pragma once

#include <QObject>
#include <QTimer>

#include <functional>

// "Something changed, get it onto disk": every edit to scenes, sources, filters, audio or stream
// destinations calls touch(), and shortly after the edits stop everything is saved. libobs' global
// source signals are hooked too, so changes made anywhere (dialogs, mixer, plugins) are caught.
// A crash therefore costs at most the last fraction of a second of work.
class Autosave : public QObject {
	Q_OBJECT
public:
	static Autosave *instance();
	static void touch(); // safe from any thread, and a no-op once the app is shutting down

	void setSaver(std::function<void()> save);
	void hookObs();
	void unhookObs();
	void flush(); // save now if anything is pending

private:
	explicit Autosave(QObject *parent);
	void run();
	static void onObsSignal(void *, struct calldata *);

	QTimer m_debounce;
	std::function<void()> m_save;
	bool m_saving = false;
	bool m_hooked = false;
};
