#pragma once

#include <obs.h>

#include <QElapsedTimer>
#include <QObject>
#include <QString>

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
	qint64 recordingSeconds() const { return m_record ? m_clock.elapsed() / 1000 : 0; }

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

private:
	enum Kind { RecordStop, ReplayStop, ReplaySaved };
	struct Ctx {
		Recorder *self;
		Kind kind;
	};

	static void onSignal(void *param, calldata_t *cd);
	void handle(Kind kind, const QString &text);
	void finishRecord();
	void finishReplay();

	obs_output_t *m_record = nullptr;
	obs_output_t *m_replay = nullptr;
	QString m_recordPath;
	QElapsedTimer m_clock;
	Ctx m_ctx[3] = {{this, RecordStop}, {this, ReplayStop}, {this, ReplaySaved}};
};
