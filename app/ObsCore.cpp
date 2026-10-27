#include "ObsCore.h"

#include "AudioPresets.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QSettings>

#include <graphics/matrix4.h>

#include <algorithm>

ObsCore &ObsCore::instance()
{
	static ObsCore core;
	return core;
}

static QFile *g_log = nullptr;

static void logHandler(int level, const char *fmt, va_list args, void *)
{
	if (!g_log)
		return;
	char buf[4096];
	vsnprintf(buf, sizeof(buf), fmt, args);
	g_log->write(buf);
	g_log->write("\n");
	g_log->flush();
}

bool ObsCore::applyVideo(int baseW, int baseH, int outW, int outH, int fps)
{
	obs_video_info vi = {};
	vi.graphics_module = "libobs-d3d11";
	vi.fps_num = fps;
	vi.fps_den = 1;
	vi.base_width = baseW;
	vi.base_height = baseH;
	vi.output_width = outW; // encode resolution; below canvas size = less encoder work
	vi.output_height = outH;
	vi.output_format = VIDEO_FORMAT_NV12;
	vi.adapter = 0;
	vi.gpu_conversion = true;
	vi.colorspace = VIDEO_CS_709;
	vi.range = VIDEO_RANGE_PARTIAL;
	vi.scale_type = OBS_SCALE_BILINEAR;
	if (const int rc = obs_reset_video(&vi); rc != OBS_VIDEO_SUCCESS) {
		m_error = QString("Video init failed (code %1)").arg(rc);
		return false;
	}
	m_w = baseW;
	m_h = baseH;
	return true;
}

bool ObsCore::start(const QString &root, int baseW, int baseH, int outW, int outH, int fps)
{
	if (m_running)
		return true;

	const QString logDir = qEnvironmentVariable("APPDATA") + "/RareStudio";
	QDir().mkpath(logDir);
	g_log = new QFile(logDir + "/latest.log");
	g_log->open(QIODevice::WriteOnly | QIODevice::Truncate);
	base_set_log_handler(logHandler, nullptr);

	m_error.clear();
	if (!obs_startup("en-US", nullptr, nullptr)) {
		m_error = "obs_startup failed";
		return false;
	}

	// libobs' effect parser resolves #includes by splitting on "/" only, so never pass backslashes.
	const QByteArray dataPath = QDir::fromNativeSeparators(root + "/data/libobs/").toUtf8();
	obs_add_data_path(dataPath.constData());

	obs_audio_info ai = {};
	ai.samples_per_sec = 48000;
	ai.speakers = SPEAKERS_STEREO;
	if (!obs_reset_audio(&ai)) {
		m_error = "Audio init failed";
		return false;
	}

	if (!applyVideo(baseW, baseH, outW, outH, fps))
		return false;

	const QByteArray bin = QDir::fromNativeSeparators(root + "/obs-plugins").toUtf8();
	const QByteArray data = QDir::fromNativeSeparators(root + "/data/obs-plugins/%module%").toUtf8();
	obs_add_module_path(bin.constData(), data.constData());
	obs_load_all_modules();
	obs_post_load_modules();

	m_running = true;
	return true;
}

void ObsCore::shutdown()
{
	if (!m_running)
		return;
	for (uint32_t i = 0; i < MAX_CHANNELS; ++i)
		obs_set_output_source(i, nullptr);
	if (m_transition)
		obs_source_release(m_transition);
	m_transition = nullptr;
	for (obs_source_t *s : m_heldScenes)
		obs_source_release(s);
	m_heldScenes.clear();
	if (m_desktop)
		obs_source_release(m_desktop);
	if (m_mic)
		obs_source_release(m_mic);
	m_scene = m_program = nullptr;
	m_desktop = m_mic = nullptr;

	// obs_shutdown() unloads every plugin DLL *before* it frees whatever sources are still alive, and it
	// waits on the destroy queue only once. Destroying a scene releases its items, which queues their
	// destruction behind that single wait: a media source still tearing down its network threads then ran
	// (or crashed, in the audio thread) after its plugin was gone. Clear everything first and keep draining
	// until nothing more gets destroyed, the way OBS itself does.
	auto remove = [](void *, obs_source_t *src) {
		obs_source_remove(src);
		return true;
	};
	obs_enum_scenes(remove, nullptr);
	obs_enum_sources(remove, nullptr);
	QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
	do {
		QCoreApplication::sendPostedEvents(nullptr);
	} while (obs_wait_for_destroy_queue());
	obs_enum_sources(
		[](void *, obs_source_t *src) {
			blog(LOG_WARNING, "shutdown: source '%s' is still referenced", obs_source_get_name(src));
			return true;
		},
		nullptr);

	obs_shutdown();
	m_running = false;
}

obs_scene_t *ObsCore::createScene(const QString &name)
{
	obs_scene_t *scene = obs_scene_create(name.toUtf8().constData());
	if (scene)
		m_heldScenes.append(obs_scene_get_source(scene)); // keeps the creation reference
	return scene;
}

obs_scene_t *ObsCore::duplicateScene(obs_scene_t *scene, const QString &name)
{
	obs_scene_t *copy = obs_scene_duplicate(scene, name.toUtf8().constData(), OBS_SCENE_DUP_REFS);
	if (copy)
		m_heldScenes.append(obs_scene_get_source(copy)); // keeps the creation reference
	return copy;
}

void ObsCore::removeScene(obs_scene_t *scene)
{
	obs_source_t *src = obs_scene_get_source(scene);
	if (scene == m_scene || scene == m_program)
		return;
	obs_source_remove(src);
	for (int i = m_heldScenes.size() - 1; i >= 0; --i)
		if (m_heldScenes[i] == src) {
			obs_source_release(src);
			m_heldScenes.removeAt(i);
		}
}

void ObsCore::holdScene(obs_source_t *scene)
{
	if (obs_source_t *ref = obs_source_get_ref(scene))
		m_heldScenes.append(ref);
}

void ObsCore::setTransition(const QString &typeId, const QByteArray &settingsJson, int durationMs)
{
	m_transitionMs = durationMs;
	if (m_transition && typeId == m_transitionId && settingsJson == m_transitionSettings)
		return;

	obs_data_t *settings = settingsJson.isEmpty() ? nullptr : obs_data_create_from_json(settingsJson.constData());
	obs_source_t *next = obs_source_create_private(typeId.toUtf8().constData(), "RareTransition", settings);
	if (settings)
		obs_data_release(settings);
	if (!next)
		return;
	m_transitionId = typeId;
	m_transitionSettings = settingsJson;
	if (m_transition) {
		// Hand the current picture to the new transition so there is no flash on the swap.
		obs_transition_swap_begin(next, m_transition);
		obs_set_output_source(0, next);
		obs_transition_swap_end(next, m_transition);
		obs_source_release(m_transition);
	} else {
		obs_set_output_source(0, next);
	}
	m_transition = next;
}
void ObsCore::goLive(obs_scene_t *scene)
{
	if (scene && scene == m_program && m_transition)
		return; // already on air
	obs_scene_t *previous = m_program;
	m_program = scene;
	if (m_programHook)
		m_programHook(scene);
	if (m_transitionHook)
		m_transitionHook(scene); // per-scene override may swap the transition and its length
	if (!m_transition)
		setTransition(m_transitionId, m_transitionSettings, m_transitionMs);
	if (!m_transition)
		return;

	obs_source_t *src = scene ? obs_scene_get_source(scene) : nullptr;
	if (previous && src && m_transitionId != "cut_transition" && (m_transitionMs > 0 || m_transitionId == "stinger_transition"))
		obs_transition_start(m_transition, OBS_TRANSITION_MODE_AUTO, uint32_t(m_transitionMs), src);
	else
		obs_transition_set(m_transition, src);
}

void ObsCore::setCurrentScene(obs_scene_t *scene)
{
	m_scene = scene;
	if (!m_studio)
		goLive(scene);
}

void ObsCore::setStudioMode(bool on)
{
	m_studio = on;
	if (!on && m_scene && m_scene != m_program)
		m_scene = m_program; // back to one view: edit what is on air
}

void ObsCore::commitToProgram()
{
	if (m_scene && m_scene != m_program)
		goLive(m_scene);
}
obs_source_t *ObsCore::addSource(const char *typeId, const QString &name)
{
	if (!m_scene)
		return nullptr;
	obs_source_t *src = obs_source_create(typeId, name.toUtf8().constData(), nullptr, nullptr);
	if (!src)
		return nullptr;

	obs_sceneitem_t *item = obs_scene_add(m_scene, src);
	if (item && (obs_source_get_output_flags(src) & OBS_SOURCE_VIDEO)) {
		// Every visual item lives in a top-left-aligned bounding box so the preview can
		// move/resize it by editing a plain rectangle.
		obs_transform_info t = {};
		t.pos = {0.0f, 0.0f};
		t.scale = {1.0f, 1.0f};
		t.alignment = OBS_ALIGN_LEFT | OBS_ALIGN_TOP;
		t.bounds_type = OBS_BOUNDS_SCALE_INNER;
		t.bounds_alignment = OBS_ALIGN_CENTER;
		t.bounds = {(float)m_w, (float)m_h};
		obs_sceneitem_set_info2(item, &t);
	}
	return src; // caller releases
}

// ---- Scene item geometry ---------------------------------------------------------------------

bool ObsCore::contentMatrix(obs_sceneitem_t *item, matrix4 *out)
{
	// libobs' draw transform works in the source's own pixel space (after crop), so scale the
	// unit square up to the visible pixel size first.
	obs_source_t *src = obs_sceneitem_get_source(item);
	obs_sceneitem_crop crop;
	obs_sceneitem_get_crop(item, &crop);
	const float sw = float(obs_source_get_width(src)) - crop.left - crop.right;
	const float sh = float(obs_source_get_height(src)) - crop.top - crop.bottom;
	if (sw <= 0.0f || sh <= 0.0f)
		return false;

	matrix4 draw, scale;
	obs_sceneitem_get_draw_transform(item, &draw);
	matrix4_identity(&scale);
	matrix4_scale3f(&scale, &scale, sw, sh, 1.0f);
	matrix4_mul(out, &scale, &draw);
	return true;
}

QRectF ObsCore::itemRect(obs_sceneitem_t *item)
{
	matrix4 m;
	if (!contentMatrix(item, &m))
		return {};
	const float xs[] = {0, 1, 0, 1}, ys[] = {0, 0, 1, 1};
	float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
	for (int i = 0; i < 4; ++i) {
		vec3 p, o;
		vec3_set(&p, xs[i], ys[i], 0.0f);
		vec3_transform(&o, &p, &m);
		x0 = std::min(x0, o.x);
		x1 = std::max(x1, o.x);
		y0 = std::min(y0, o.y);
		y1 = std::max(y1, o.y);
	}
	return QRectF(QPointF(x0, y0), QPointF(x1, y1));
}
// Visible (cropped) source size in source pixels.
static bool visibleSize(obs_sceneitem_t *item, float *w, float *h)
{
	obs_source_t *src = obs_sceneitem_get_source(item);
	obs_sceneitem_crop crop;
	obs_sceneitem_get_crop(item, &crop);
	*w = float(obs_source_get_width(src)) - crop.left - crop.right;
	*h = float(obs_source_get_height(src)) - crop.top - crop.bottom;
	return *w > 0.0f && *h > 0.0f;
}

void ObsCore::normalizeItem(obs_sceneitem_t *item)
{
	obs_transform_info t;
	obs_sceneitem_get_info2(item, &t);
	float sw, sh;
	if (t.rot != 0.0f || !visibleSize(item, &sw, &sh))
		return; // rotated items are only moved, never box-edited
	const QRectF r = itemRect(item); // what is visible right now (honours any fit-to-box letterboxing)
	t.bounds_type = OBS_BOUNDS_NONE;
	t.crop_to_bounds = false;
	t.alignment = OBS_ALIGN_LEFT | OBS_ALIGN_TOP;
	t.pos = {(float)r.left(), (float)r.top()};
	t.scale = {float(r.width()) / sw, float(r.height()) / sh};
	obs_sceneitem_set_info2(item, &t);
}

void ObsCore::setItemRect(obs_sceneitem_t *item, const QRectF &r)
{
	float sw, sh;
	if (!visibleSize(item, &sw, &sh))
		return;
	obs_transform_info t;
	obs_sceneitem_get_info2(item, &t);
	t.pos = {(float)r.left(), (float)r.top()};
	t.scale = {float(std::max(4.0, r.width())) / sw, float(std::max(4.0, r.height())) / sh};
	obs_sceneitem_set_info2(item, &t);
}
void ObsCore::fitToCanvas(obs_sceneitem_t *item)
{
	obs_transform_info t;
	obs_sceneitem_get_info2(item, &t);
	t.rot = 0.0f;
	t.alignment = OBS_ALIGN_LEFT | OBS_ALIGN_TOP;
	t.bounds_type = OBS_BOUNDS_SCALE_INNER;
	t.bounds_alignment = OBS_ALIGN_CENTER;
	t.pos = {0.0f, 0.0f};
	t.bounds = {(float)instance().m_w, (float)instance().m_h};
	obs_sceneitem_set_info2(item, &t);
}

void ObsCore::stretchToCanvas(obs_sceneitem_t *item)
{
	fitToCanvas(item);
	obs_transform_info t;
	obs_sceneitem_get_info2(item, &t);
	t.bounds_type = OBS_BOUNDS_STRETCH;
	obs_sceneitem_set_info2(item, &t);
}

void ObsCore::centerOnCanvas(obs_sceneitem_t *item, bool horizontal, bool vertical)
{
	const QRectF r = itemRect(item);
	vec2 pos;
	obs_sceneitem_get_pos(item, &pos);
	if (horizontal)
		pos.x += float(instance().m_w / 2.0 - r.center().x());
	if (vertical)
		pos.y += float(instance().m_h / 2.0 - r.center().y());
	obs_sceneitem_set_pos(item, &pos);
}

// Reuses the source restored from the saved collection if there is one; `fresh` says it was created now.
static obs_source_t *ensureAudioSource(const char *type, const char *name, bool *fresh)
{
	if (obs_source_t *existing = obs_get_source_by_name(name)) {
		*fresh = false;
		return existing;
	}
	*fresh = true;
	obs_data_t *s = obs_data_create();
	obs_data_set_string(s, "device_id", "default");
	obs_source_t *src = obs_source_create(type, name, s, nullptr);
	obs_data_release(s);
	return src;
}

void ObsCore::applyMonitoringDevice()
{
	if (!obs_audio_monitoring_available())
		return;
	QSettings s;
	const QByteArray name = s.value("audio/monitor_name", "Default").toString().toUtf8();
	const QByteArray id = s.value("audio/monitor_id", "default").toString().toUtf8();
	obs_set_audio_monitoring_device(name.constData(), id.constData());
}

// First run: a fresh mic gets the voice chain and desktop audio sits under it with a limiter. Collections
// saved before these defaults existed get the same filters once, but only on sources with no filters.
void ObsCore::setupDefaultAudio()
{
	bool freshDesktop = false, freshMic = false;
	if (!m_desktop) {
		m_desktop = ensureAudioSource("wasapi_output_capture", "Desktop Audio", &freshDesktop);
		if (m_desktop)
			obs_set_output_source(1, m_desktop);
	}
	if (!m_mic) {
		m_mic = ensureAudioSource("wasapi_input_capture", "Mic", &freshMic);
		if (m_mic)
			obs_set_output_source(2, m_mic);
	}

	QSettings s;
	if (!s.value("audio/defaults_v1", false).toBool()) {
		if (freshMic || freshDesktop)
			AudioPresets::resetLevels(freshMic ? m_mic : nullptr, freshDesktop ? m_desktop : nullptr);
		if (AudioPresets::filterCount(m_mic) == 0)
			AudioPresets::applyVoiceChain(m_mic);
		if (AudioPresets::filterCount(m_desktop) == 0)
			AudioPresets::applyDesktopChain(m_desktop);
		s.setValue("audio/defaults_v1", true);
	}
	applyMonitoringDevice();
}

const char *ObsCore::firstAvailableEncoder(std::initializer_list<const char *> candidates)
{
	for (const char *want : candidates) {
		const char *id = nullptr;
		for (size_t i = 0; obs_enum_encoder_types(i, &id); ++i) {
			if (strcmp(id, want) == 0)
				return want;
		}
	}
	return nullptr;
}
