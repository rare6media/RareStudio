#pragma once

#include <obs.h>

#include <QElapsedTimer>
#include <QObject>
#include <QString>

#include <atomic>

// Local recording and the instant-replay ("clip") buffer. Both ride the shared encoders, so
// recording while streaming costs no extra encode.
class Recorder : public QObject {
	Q_OBJECT
public:
	explicit Recorder(QObject *parent = nullptr) : QObject(parent) {}
	~Recorder() override { shutdown(); }

	bool startRecording(QString *error);
	void stopRecording();
	bool recording() const { return m_record != nullptr; }
	qint64 recordingSeconds() const; // time actually recorded: paused stretches don't count

	// Pausing pauses the encoders themselves, so it is only offered while the recording is their only
	// user (pausing them under a live stream or the replay buffer would freeze those too).
	bool paused() const { return m_pausedSince >= 0; }
	bool setPaused(bool pause);

	// Saves the program output (what viewers see) as a PNG in the recordings folder, at the output
	// resolution. Asynchronous: screenshotSaved() reports the file, or an empty path on failure.
	void screenshot();

	bool startReplay(QString *error);
	void stopReplay();
	bool replayActive() const { return m_replay != nullptr; }
	bool saveClip(); // asks the buffer to write the last N seconds to disk

	void shutdown(); // synchronous; used on exit

	static QString defaultDir();

signals:
	void recordingStopped(const QString &path, const QString &error);
	void clipSaved(const QString &path);
	void replayStopped();
	void screenshotSaved(const QString &path);

private:
	enum Kind { RecordStop, ReplayStop, ReplaySaved };
	struct Ctx {
		Recorder *self;
		Kind kind;
	};

	struct Shot {
		Recorder *self;
		QString path;
		int w, h;
		std::atomic<bool> taken{false};
	};

	static void onSignal(void *param, calldata_t *cd);
	static void onShotFrame(void *param, video_data *frame);
	void handle(Kind kind, const QString &text);
	void finishRecord();
	void finishReplay();

	obs_output_t *m_record = nullptr;
	obs_output_t *m_replay = nullptr;
	QString m_recordPath;
	QElapsedTimer m_clock;
	qint64 m_pausedMs = 0;     // total paused time so far
	qint64 m_pausedSince = -1; // m_clock time the current pause began, -1 when not paused
	Ctx m_ctx[3] = {{this, RecordStop}, {this, ReplayStop}, {this, ReplaySaved}};
};
