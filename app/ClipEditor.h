#pragma once

#include "AudioOut.h"
#include "ClipMedia.h"

#include <QDialog>
#include <QElapsedTimer>
#include <QTimer>

#include <vector>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QSlider;
class QThread;
class QToolButton;
class TrackTimeline;

// Review a clip before it goes anywhere: watch it, trim it, and balance every audio track of the file
// (mute, solo, volume) while listening to exactly what the result will sound like. "Save" writes an
// edited MP4 next to the original; "Save and upload" hands it to YouTube Shorts.
class ClipEditor : public QDialog {
	Q_OBJECT
public:
	ClipEditor(const QString &path, const QString &title, const QString &privacy, bool canUpload, QWidget *parent = nullptr);
	~ClipEditor() override;

	bool valid() const { return m_info.ok; }

signals:
	// The edited file is written. `upload` = the user chose Save and upload.
	void exported(const QString &path, bool upload, const QString &title, const QString &privacy);

protected:
	void keyPressEvent(QKeyEvent *e) override;
	void closeEvent(QCloseEvent *e) override;

private:
	void buildUi(bool canUpload, const QString &title, const QString &privacy);
	void loadAudio();
	void togglePlay();
	void seek(double seconds);
	void tick();
	void showFrame(double seconds);
	void feedAudio();
	void rangeChanged(double in, double out);
	void updateRangeLabel();
	void updateEffectiveGains();
	void startExport(bool upload);
	QString outputPath() const;

	QString m_path;
	ClipMedia::Info m_info;
	ClipMedia::VideoReader m_reader;
	AudioOut m_out;
	QTimer m_timer;
	QElapsedTimer m_clock;

	std::vector<std::vector<float>> m_pcm;     // per track, interleaved stereo 48 kHz
	std::vector<float> m_eff;                  // effective gain per track (mute/solo applied)
	int m_tracks = 0;
	double m_in = 0, m_outPoint = 0, m_pos = 0;
	bool m_playing = false;
	qint64 m_writeSample = 0;                  // next sample to hand to the audio device
	double m_clockStart = 0;                   // playback start, for the silent fallback clock

	QLabel *m_video = nullptr, *m_time = nullptr, *m_range = nullptr, *m_status = nullptr;
	QPushButton *m_play = nullptr, *m_save = nullptr, *m_saveUpload = nullptr, *m_cancel = nullptr;
	QCheckBox *m_loop = nullptr;
	QLineEdit *m_title = nullptr;
	QComboBox *m_privacy = nullptr;
	QProgressBar *m_progress = nullptr;
	TrackTimeline *m_timeline = nullptr;
	std::vector<QToolButton *> m_mute, m_solo;
	std::vector<QSlider *> m_vol;
	std::vector<QLabel *> m_volLabel;

	QThread *m_worker = nullptr;
	bool m_exporting = false;
	bool m_cancelExport = false;
	bool m_wantUpload = false;
	QString m_exportPath;
};
