#pragma once

#include <QDialog>
#include <QMap>

class QCheckBox;
class QComboBox;
class QKeySequenceEdit;
class QLabel;
class QLineEdit;
class QSpinBox;

// Settings: video canvas, outputs (encoder / recording / clips) and global hotkeys.
class SettingsDialog : public QDialog {
	Q_OBJECT
public:
	explicit SettingsDialog(QWidget *parent = nullptr);

	struct Video {
		int baseW, baseH, outW, outH, fps;
	};
	static Video load(); // saved values (defaults 1080p30)

private:
	QWidget *buildVideoTab();
	QWidget *buildOutputTab();
	QWidget *buildAudioTab();
	QWidget *buildHotkeysTab();
	QWidget *buildUpdatesTab();
	QWidget *buildAccountsTab();
	QWidget *buildClipsTab();
	void refreshAccountStatus();
	bool apply();

	QComboBox *m_base, *m_out, *m_fps;
	QComboBox *m_encoder, *m_format;
	QSpinBox *m_videoKbps, *m_audioKbps, *m_replaySecs;
	QComboBox *m_monitor;
	QLineEdit *m_dir, *m_feed;
	QCheckBox *m_autoUpdate;

	// Accounts
	QLineEdit *m_twitchId, *m_googleId, *m_googleSecret;
	QLabel *m_twitchStatus, *m_googleStatus;
	QCheckBox *m_autoKeys;

	// Clips
	QCheckBox *m_localClip, *m_twitchClip, *m_makeShort, *m_autoUpload;
	QSpinBox *m_twitchClipSecs, *m_shortSecs;
	QComboBox *m_shortStyle, *m_privacy;
	QLineEdit *m_shortTitle;
	QMap<int, QKeySequenceEdit *> m_keys;
};
