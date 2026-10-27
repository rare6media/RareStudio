#include "Recorder.h"

#include "EncoderPool.h"

#include <util/platform.h>

#include <QDateTime>
#include <QDir>
#include <QImage>
#include <QMetaObject>
#include <QSettings>
#include <QStandardPaths>
#include <QThreadPool>

#include <atomic>

QString Recorder::defaultDir()
{
	return QStandardPaths::writableLocation(QStandardPaths::MoviesLocation) + "/RareStudio";
}

bool Recorder::startRecording(QString *error)
{
	if (m_record)
		return true;
	EncoderPool &pool = EncoderPool::instance();
	if (!pool.acquire(error))
		return false;

	QSettings s;
	const QString dir = s.value("record/dir", defaultDir()).toString();
	const QString ext = s.value("record/format", "mkv").toString();
	QDir().mkpath(dir);
	m_recordPath = QDir(dir).filePath(QDateTime::currentDateTime().toString("yyyy-MM-dd_HH-mm-ss") + "." + ext);

	obs_data_t *settings = obs_data_create();
	obs_data_set_string(settings, "path", m_recordPath.toUtf8().constData());
	if (ext == "mp4" || ext == "mov") // fragmented, so a crash doesn't lose the whole file
		obs_data_set_string(settings, "muxer_settings", "movflags=frag_keyframe+empty_moov");
	m_record = obs_output_create("ffmpeg_muxer", "rare_record", settings, nullptr);
	obs_data_release(settings);
	if (!m_record) {
		if (error)
			*error = "Could not create the recording output.";
		pool.release();
		return false;
	}
	obs_output_set_video_encoder(m_record, pool.video());
	obs_output_set_audio_encoder(m_record, pool.audio(), 0);
	signal_handler_connect(obs_output_get_signal_handler(m_record), "stop", onSignal, &m_ctx[RecordStop]);

	if (!obs_output_start(m_record)) {
		if (error)
			*error = QString::fromUtf8(obs_output_get_last_error(m_record));
		obs_output_release(m_record);
		m_record = nullptr;
		pool.release();
		return false;
	}
	m_clock.start();
	m_pausedMs = 0;
	m_pausedSince = -1;
	return true;
}

void Recorder::stopRecording()
{
	if (!m_record)
		return;
	setPaused(false); // the muxer writes a cleaner file when it isn't stopped mid-pause
	obs_output_stop(m_record); // finishes asynchronously; the stop signal calls finishRecord()
}

qint64 Recorder::recordingSeconds() const
{
	if (!m_record)
		return 0;
	const qint64 now = m_clock.elapsed();
	return (now - m_pausedMs - (m_pausedSince >= 0 ? now - m_pausedSince : 0)) / 1000;
}

bool Recorder::setPaused(bool pause)
{
	if (!m_record || paused() == pause)
		return m_record != nullptr;
	if (!obs_output_pause(m_record, pause))
		return false;
	if (pause) {
		m_pausedSince = m_clock.elapsed();
	} else {
		m_pausedMs += m_clock.elapsed() - m_pausedSince;
		m_pausedSince = -1;
	}
	return true;
}

void Recorder::screenshot()
{
	obs_video_info ovi;
	if (!obs_get_video_info(&ovi)) {
		emit screenshotSaved(QString());
		return;
	}
	const QString dir = QSettings().value("record/dir", defaultDir()).toString();
	QDir().mkpath(dir);

	auto *shot = new Shot{this,
			      QDir(dir).filePath("Screenshot_" + QDateTime::currentDateTime().toString("yyyy-MM-dd_HH-mm-ss") + ".png"),
			      int(ovi.output_width), int(ovi.output_height)};
	video_scale_info conv = {};
	conv.format = VIDEO_FORMAT_BGRA;
	conv.width = ovi.output_width;
	conv.height = ovi.output_height;
	conv.range = VIDEO_RANGE_FULL;
	conv.colorspace = VIDEO_CS_SRGB;
	obs_add_raw_video_callback(&conv, &Recorder::onShotFrame, shot);
}

// One frame of the main output, converted to BGRA by libobs. This runs on the video thread: it only
// copies the pixels and hands everything else to the UI thread, which also disconnects the callback
// (disconnecting from inside it would deadlock).
void Recorder::onShotFrame(void *param, video_data *frame)
{
	auto *s = static_cast<Shot *>(param);
	if (s->taken.exchange(true))
		return;
	const QImage img = QImage(frame->data[0], s->w, s->h, int(frame->linesize[0]), QImage::Format_RGB32).copy();
	QMetaObject::invokeMethod(s->self, [s, img] {
		obs_remove_raw_video_callback(&Recorder::onShotFrame, s);
		// PNG encoding of a 1080p frame takes a moment: keep it off the UI thread.
		QThreadPool::globalInstance()->start([self = s->self, path = s->path, img] {
			const bool ok = img.save(path, "PNG");
			QMetaObject::invokeMethod(self, [self, path, ok] { emit self->screenshotSaved(ok ? path : QString()); },
						  Qt::QueuedConnection);
		});
		delete s;
	}, Qt::QueuedConnection);
}

bool Recorder::startReplay(QString *error)
{
	if (m_replay)
		return true;
	EncoderPool &pool = EncoderPool::instance();
	if (!pool.acquire(error))
		return false;

	QSettings s;
	const QString dir = s.value("record/dir", defaultDir()).toString();
	QDir().mkpath(dir);

	obs_data_t *settings = obs_data_create();
	obs_data_set_string(settings, "directory", dir.toUtf8().constData());
	obs_data_set_string(settings, "format", "Clip_%CCYY-%MM-%DD_%hh-%mm-%ss");
	obs_data_set_string(settings, "extension", s.value("record/format", "mkv").toString().toUtf8().constData());
	obs_data_set_int(settings, "max_time_sec", s.value("replay/seconds", 30).toInt());
	obs_data_set_int(settings, "max_size_mb", 512);
	m_replay = obs_output_create("replay_buffer", "rare_replay", settings, nullptr);
	obs_data_release(settings);
	if (!m_replay) {
		if (error)
			*error = "Could not create the replay buffer.";
		pool.release();
		return false;
	}
	obs_output_set_video_encoder(m_replay, pool.video());
	obs_output_set_audio_encoder(m_replay, pool.audio(), 0);
	signal_handler_t *sh = obs_output_get_signal_handler(m_replay);
	signal_handler_connect(sh, "stop", onSignal, &m_ctx[ReplayStop]);
	signal_handler_connect(sh, "saved", onSignal, &m_ctx[ReplaySaved]);

	if (!obs_output_start(m_replay)) {
		if (error)
			*error = QString::fromUtf8(obs_output_get_last_error(m_replay));
		obs_output_release(m_replay);
		m_replay = nullptr;
		pool.release();
		return false;
	}
	return true;
}

void Recorder::stopReplay()
{
	if (m_replay)
		obs_output_stop(m_replay);
}

bool Recorder::saveClip()
{
	if (!m_replay || !obs_output_active(m_replay))
		return false;
	calldata_t cd;
	calldata_init(&cd);
	const bool ok = proc_handler_call(obs_output_get_proc_handler(m_replay), "save", &cd);
	calldata_free(&cd);
	return ok;
}

// Runs on a libobs thread: gather what we need, then hop to the UI thread.
void Recorder::onSignal(void *param, calldata_t *cd)
{
	auto *ctx = static_cast<Ctx *>(param);
	obs_output_t *out = static_cast<obs_output_t *>(calldata_ptr(cd, "output"));
	QString text;
	if (ctx->kind == ReplaySaved) {
		calldata_t pcd;
		calldata_init(&pcd);
		if (proc_handler_call(obs_output_get_proc_handler(out), "get_last_replay", &pcd))
			text = QString::fromUtf8(calldata_string(&pcd, "path"));
		calldata_free(&pcd);
	} else {
		const char *err = obs_output_get_last_error(out);
		text = err ? QString::fromUtf8(err) : QString();
	}
	QMetaObject::invokeMethod(ctx->self, [self = ctx->self, kind = ctx->kind, text] { self->handle(kind, text); },
				  Qt::QueuedConnection);
}

void Recorder::handle(Kind kind, const QString &text)
{
	switch (kind) {
	case RecordStop: {
		const QString path = m_recordPath;
		finishRecord();
		emit recordingStopped(path, text);
		break;
	}
	case ReplayStop:
		finishReplay();
		emit replayStopped();
		break;
	case ReplaySaved:
		emit clipSaved(text);
		break;
	}
}

void Recorder::finishRecord()
{
	if (!m_record)
		return;
	obs_output_release(m_record);
	m_record = nullptr;
	EncoderPool::instance().release();
}

void Recorder::finishReplay()
{
	if (!m_replay)
		return;
	obs_output_release(m_replay);
	m_replay = nullptr;
	EncoderPool::instance().release();
}

// A clean stop lets the muxer write the file trailer, so wait (briefly) for it to finish.
static void stopAndWait(obs_output_t *out)
{
	obs_output_stop(out);
	for (int i = 0; i < 100 && obs_output_active(out); ++i)
		os_sleep_ms(50);
	if (obs_output_active(out))
		obs_output_force_stop(out);
}

void Recorder::shutdown()
{
	if (m_record) {
		stopAndWait(m_record);
		finishRecord();
	}
	if (m_replay) {
		stopAndWait(m_replay);
		finishReplay();
	}
}
