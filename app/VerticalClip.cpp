#include "VerticalClip.h"

#include "EncoderPool.h"
#include "Recorder.h"

#include <util/platform.h>

#include <QDir>
#include <QMetaObject>
#include <QSettings>

namespace {

void clearScene(obs_scene_t *scene)
{
	obs_scene_enum_items(
		scene,
		[](obs_scene_t *, obs_sceneitem_t *item, void *) {
			obs_sceneitem_remove(item);
			return true;
		},
		nullptr);
}

// Places `source` in the 1080x1920 frame: scaled inside (fit) or covering (fill) and centred.
void place(obs_scene_t *scene, obs_source_t *source, obs_bounds_type mode)
{
	obs_sceneitem_t *item = obs_scene_add(scene, source);
	obs_transform_info t = {};
	t.scale = {1.0f, 1.0f};
	t.alignment = OBS_ALIGN_LEFT | OBS_ALIGN_TOP;
	t.bounds_type = mode;
	t.bounds_alignment = OBS_ALIGN_CENTER;
	t.bounds = {float(VerticalClip::kWidth), float(VerticalClip::kHeight)};
	t.crop_to_bounds = mode == OBS_BOUNDS_SCALE_OUTER;
	obs_sceneitem_set_info2(item, &t);
}

} // namespace

video_t *VerticalClip::video() const
{
	return m_canvas ? obs_canvas_get_video(m_canvas) : nullptr;
}

bool VerticalClip::start(QString *error)
{
	if (m_output)
		return true;

	obs_video_info ovi = {};
	if (!obs_get_video_info(&ovi)) {
		if (error)
			*error = "The video engine is not running.";
		return false;
	}
	ovi.base_width = ovi.output_width = kWidth;
	ovi.base_height = ovi.output_height = kHeight;
	m_canvas = obs_canvas_create_private("RareVertical", &ovi, uint32_t(ACTIVATE));
	if (!m_canvas || !obs_canvas_has_video(m_canvas)) {
		if (error)
			*error = "Could not create the vertical canvas.";
		stop();
		return false;
	}

	// Scenes that belong to the vertical canvas (so they never appear in your scene list).
	m_mirror = obs_canvas_scene_create(m_canvas, "RareVerticalMirror");
	m_backdrop = obs_canvas_scene_create(m_canvas, "RareVerticalBackdrop");
	obs_canvas_set_channel(m_canvas, 0, obs_scene_get_source(m_mirror));

	m_blur = obs_source_create_private("rare_bg_blur", "RareBlur", nullptr);
	if (m_blur && qEnvironmentVariableIsEmpty("RARESTUDIO_NOBLUR")) // env switch is for diagnostics only
		obs_source_filter_add(obs_scene_get_source(m_backdrop), m_blur);

	// Encoders: same hardware encoder family as the main stream, a little more bitrate for 1080x1920.
	QSettings s;
	const QString enc = EncoderPool::preferredVideoEncoder();
	obs_data_t *vs = obs_data_create();
	obs_data_set_string(vs, "rate_control", "CBR");
	obs_data_set_int(vs, "bitrate", s.value("shorts/video_kbps", 8000).toInt());
	obs_data_set_int(vs, "keyint_sec", 2);
	if (enc == "obs_x264")
		obs_data_set_string(vs, "preset", "veryfast");
	m_video = obs_video_encoder_create(enc.toUtf8().constData(), "rare_vertical_video", vs, nullptr);
	obs_data_release(vs);
	obs_data_t *as = obs_data_create();
	obs_data_set_int(as, "bitrate", 160);
	m_audio = obs_audio_encoder_create("ffmpeg_aac", "rare_vertical_audio", as, 0, nullptr);
	obs_data_release(as);
	if (!m_video || !m_audio) {
		if (error)
			*error = "Could not create the vertical encoders.";
		stop();
		return false;
	}
	obs_encoder_set_video(m_video, obs_canvas_get_video(m_canvas));
	obs_encoder_set_audio(m_audio, obs_get_audio());

	const QString dir = s.value("record/dir", Recorder::defaultDir()).toString();
	QDir().mkpath(dir);
	obs_data_t *rs = obs_data_create();
	obs_data_set_string(rs, "directory", dir.toUtf8().constData());
	obs_data_set_string(rs, "format", "Short_%CCYY-%MM-%DD_%hh-%mm-%ss");
	obs_data_set_string(rs, "extension", "mp4"); // YouTube-friendly
	obs_data_set_int(rs, "max_time_sec", s.value("shorts/seconds", 30).toInt());
	obs_data_set_int(rs, "max_size_mb", 512);
	m_output = obs_output_create("replay_buffer", "rare_vertical_replay", rs, nullptr);
	obs_data_release(rs);
	if (!m_output) {
		if (error)
			*error = "Could not create the vertical replay buffer.";
		stop();
		return false;
	}
	obs_output_set_video_encoder(m_output, m_video);
	obs_output_set_audio_encoder(m_output, m_audio, 0);
	signal_handler_connect(obs_output_get_signal_handler(m_output), "saved", onSaved, this);

	rebuildLayout();
	if (!obs_output_start(m_output)) {
		if (error)
			*error = QString::fromUtf8(obs_output_get_last_error(m_output));
		stop();
		return false;
	}
	return true;
}

void VerticalClip::stop()
{
	if (m_output) {
		obs_output_stop(m_output);
		for (int i = 0; i < 60 && obs_output_active(m_output); ++i)
			os_sleep_ms(50);
		if (obs_output_active(m_output))
			obs_output_force_stop(m_output);
		obs_output_release(m_output);
		m_output = nullptr;
	}
	if (m_video) {
		obs_encoder_release(m_video);
		m_video = nullptr;
	}
	if (m_audio) {
		obs_encoder_release(m_audio);
		m_audio = nullptr;
	}
	// Unwind from the inside out so nothing is released twice.
	if (m_canvas)
		obs_canvas_set_channel(m_canvas, 0, nullptr);
	if (m_mirror)
		clearScene(m_mirror);
	if (m_backdrop)
		clearScene(m_backdrop);
	if (m_blur) {
		if (m_backdrop)
			obs_source_filter_remove(obs_scene_get_source(m_backdrop), m_blur);
		obs_source_release(m_blur);
		m_blur = nullptr;
	}
	if (m_mirror) {
		obs_canvas_scene_remove(m_mirror);
		obs_scene_release(m_mirror);
		m_mirror = nullptr;
	}
	if (m_backdrop) {
		obs_canvas_scene_remove(m_backdrop);
		obs_scene_release(m_backdrop);
		m_backdrop = nullptr;
	}
	if (m_canvas) {
		obs_canvas_remove(m_canvas);
		obs_canvas_release(m_canvas);
		m_canvas = nullptr;
	}
}

void VerticalClip::programChanged(obs_scene_t *scene)
{
	m_live = scene;
	if (m_output)
		rebuildLayout();
}

// Re-points the vertical view at the live scene. The real scene is nested (never copied), so
// edits to it show up in the vertical view immediately.
void VerticalClip::rebuildLayout()
{
	if (!m_mirror || !m_backdrop)
		return;
	clearScene(m_mirror);
	clearScene(m_backdrop);
	if (!m_live)
		return;

	obs_source_t *live = obs_scene_get_source(m_live);
	const QString style = qEnvironmentVariable("RARESTUDIO_SHORTS_STYLE", QSettings().value("shorts/style", "blurred").toString());
	blog(LOG_INFO, "vertical layout [%s]: live %ux%u, backdrop %ux%u, mirror %ux%u", qUtf8Printable(style),
	     obs_source_get_width(live), obs_source_get_height(live), obs_source_get_width(obs_scene_get_source(m_backdrop)),
	     obs_source_get_height(obs_scene_get_source(m_backdrop)), obs_source_get_width(obs_scene_get_source(m_mirror)),
	     obs_source_get_height(obs_scene_get_source(m_mirror)));
	if (style == "fill") {
		place(m_mirror, live, OBS_BOUNDS_SCALE_OUTER);
	} else if (style == "fit") {
		place(m_mirror, live, OBS_BOUNDS_SCALE_INNER);
	} else {
		place(m_backdrop, live, OBS_BOUNDS_SCALE_OUTER); // zoomed to fill; the filter blurs this wrapper only
		place(m_mirror, obs_scene_get_source(m_backdrop), OBS_BOUNDS_SCALE_INNER);
		place(m_mirror, live, OBS_BOUNDS_SCALE_INNER);
	}
}

bool VerticalClip::saveClip()
{
	if (!m_output || !obs_output_active(m_output))
		return false;
	calldata_t cd;
	calldata_init(&cd);
	const bool ok = proc_handler_call(obs_output_get_proc_handler(m_output), "save", &cd);
	calldata_free(&cd);
	return ok;
}

void VerticalClip::onSaved(void *param, calldata_t *cd)
{
	Q_UNUSED(cd); // the "saved" signal carries no output pointer; we know which output this is
	auto *self = static_cast<VerticalClip *>(param);
	QString path;
	calldata_t pcd;
	calldata_init(&pcd);
	if (self->m_output && proc_handler_call(obs_output_get_proc_handler(self->m_output), "get_last_replay", &pcd))
		path = QString::fromUtf8(calldata_string(&pcd, "path"));
	calldata_free(&pcd);
	QMetaObject::invokeMethod(self, [self, path] { emit self->clipSaved(path); }, Qt::QueuedConnection);
}
