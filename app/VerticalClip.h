#pragma once

#include <obs.h>

#include <QObject>
#include <QString>

// Always-ready 9:16 clips for YouTube Shorts.
//
// A second video canvas (1080x1920) is composed from whatever scene is live, with its own
// encoder and an in-memory replay buffer. Pressing Clip! writes the last N seconds as a
// ready-to-upload vertical MP4 instantly, with no re-encode or conversion step.
//
// Layouts: "blurred" = the live picture fitted in the middle over a blurred, zoomed copy of
// itself (the classic look); "fill" = centre crop; "fit" = fitted with black bars.
class VerticalClip : public QObject {
	Q_OBJECT
public:
	explicit VerticalClip(QObject *parent = nullptr) : QObject(parent) {}
	~VerticalClip() override { stop(); }

	static constexpr int kWidth = 1080;
	static constexpr int kHeight = 1920;

	bool start(QString *error);
	void stop();
	bool active() const { return m_output != nullptr; }
	bool saveClip();

	// Keep the vertical view in step with the scene that is on air.
	void programChanged(obs_scene_t *scene);

	video_t *video() const; // the vertical canvas output (also used by the self-test)

signals:
	void clipSaved(const QString &path);

private:
	static void onSaved(void *param, calldata_t *cd);
	void rebuildLayout();

	obs_canvas_t *m_canvas = nullptr;
	obs_scene_t *m_mirror = nullptr;     // what the vertical canvas shows
	obs_scene_t *m_backdrop = nullptr;   // wraps the live scene so the blur filter doesn't touch the real one
	obs_source_t *m_blur = nullptr;
	obs_scene_t *m_live = nullptr;       // the scene currently on air (not owned)
	obs_encoder_t *m_video = nullptr;
	obs_encoder_t *m_audio = nullptr;
	obs_encoder_t *m_trackAudio[3] = {}; // tracks 2-4, see AudioTracks.h
	obs_output_t *m_output = nullptr;
};
