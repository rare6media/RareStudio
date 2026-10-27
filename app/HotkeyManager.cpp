#include "HotkeyManager.h"

#include <QCoreApplication>
#include <QSettings>

#include <windows.h>

namespace {

// Qt key -> Win32 virtual key. Letters/digits share their ASCII codes.
UINT toVirtualKey(int qtKey)
{
	if (qtKey >= Qt::Key_A && qtKey <= Qt::Key_Z)
		return UINT('A' + (qtKey - Qt::Key_A));
	if (qtKey >= Qt::Key_0 && qtKey <= Qt::Key_9)
		return UINT('0' + (qtKey - Qt::Key_0));
	if (qtKey >= Qt::Key_F1 && qtKey <= Qt::Key_F24)
		return VK_F1 + UINT(qtKey - Qt::Key_F1);
	switch (qtKey) {
	case Qt::Key_Space: return VK_SPACE;
	case Qt::Key_Return:
	case Qt::Key_Enter: return VK_RETURN;
	case Qt::Key_Tab: return VK_TAB;
	case Qt::Key_Home: return VK_HOME;
	case Qt::Key_End: return VK_END;
	case Qt::Key_PageUp: return VK_PRIOR;
	case Qt::Key_PageDown: return VK_NEXT;
	case Qt::Key_Insert: return VK_INSERT;
	case Qt::Key_Delete: return VK_DELETE;
	case Qt::Key_Left: return VK_LEFT;
	case Qt::Key_Right: return VK_RIGHT;
	case Qt::Key_Up: return VK_UP;
	case Qt::Key_Down: return VK_DOWN;
	case Qt::Key_Pause: return VK_PAUSE;
	default: return 0;
	}
}

UINT toModifiers(Qt::KeyboardModifiers m)
{
	UINT out = MOD_NOREPEAT;
	if (m & Qt::ControlModifier) out |= MOD_CONTROL;
	if (m & Qt::AltModifier) out |= MOD_ALT;
	if (m & Qt::ShiftModifier) out |= MOD_SHIFT;
	if (m & Qt::MetaModifier) out |= MOD_WIN;
	return out;
}

} // namespace

HotkeyManager::HotkeyManager(QObject *parent) : QObject(parent)
{
	QCoreApplication::instance()->installNativeEventFilter(this);
}

HotkeyManager::~HotkeyManager()
{
	unregisterAll();
	if (QCoreApplication::instance())
		QCoreApplication::instance()->removeNativeEventFilter(this);
}

QList<HotkeyManager::Action> HotkeyManager::configurable()
{
	return {ToggleStream, ToggleRecord, PauseRecord, ToggleReplay, SaveClip, Screenshot, MuteMic, NextScene, PrevScene};
}

QString HotkeyManager::settingsKey(Action a)
{
	switch (a) {
	case ToggleStream: return "hotkeys/stream";
	case ToggleRecord: return "hotkeys/record";
	case ToggleReplay: return "hotkeys/replay";
	case SaveClip: return "hotkeys/clip";
	case MuteMic: return "hotkeys/mute_mic";
	case NextScene: return "hotkeys/next_scene";
	case PrevScene: return "hotkeys/prev_scene";
	case PauseRecord: return "hotkeys/pause_record";
	case Screenshot: return "hotkeys/screenshot";
	default: return {};
	}
}

QString HotkeyManager::label(Action a)
{
	switch (a) {
	case ToggleStream: return "Start / stop streaming";
	case ToggleRecord: return "Start / stop recording";
	case ToggleReplay: return "Arm / disarm replay buffer";
	case SaveClip: return "Save clip (last seconds)";
	case MuteMic: return "Mute / unmute microphone";
	case NextScene: return "Next scene";
	case PrevScene: return "Previous scene";
	case PauseRecord: return "Pause / resume recording";
	case Screenshot: return "Screenshot of the output";
	default: return {};
	}
}

bool HotkeyManager::registerOne(int id, const QKeySequence &seq)
{
	if (seq.isEmpty())
		return false;
	const QKeyCombination kc = seq[0];
	const UINT vk = toVirtualKey(kc.key());
	if (!vk)
		return false;
	if (!RegisterHotKey(nullptr, id, toModifiers(kc.keyboardModifiers()), vk))
		return false; // already taken by another app
	m_ids.append(id);
	return true;
}

void HotkeyManager::unregisterAll()
{
	for (int id : m_ids)
		UnregisterHotKey(nullptr, id);
	m_ids.clear();
}

void HotkeyManager::reload()
{
	unregisterAll();
	QSettings s;
	for (Action a : configurable())
		registerOne(a, QKeySequence(s.value(settingsKey(a)).toString()));
	// Ctrl+Alt+1..9 jump straight to the Nth scene.
	for (int n = 1; n <= 9; ++n)
		registerOne(SceneBase + n, QKeySequence(Qt::CTRL | Qt::ALT | (Qt::Key_0 + n)));
}

bool HotkeyManager::nativeEventFilter(const QByteArray &type, void *message, qintptr *)
{
	if (type != "windows_generic_MSG")
		return false;
	const MSG *msg = static_cast<MSG *>(message);
	if (msg->message != WM_HOTKEY)
		return false;
	emit triggered(int(msg->wParam));
	return true;
}
