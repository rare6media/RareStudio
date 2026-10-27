#pragma once

#include <obs.h>

#include <graphics/matrix4.h>

#include <QList>
#include <QRectF>
#include <QString>

#include <QByteArray>

#include <functional>
#include <initializer_list>

// obs_sceneitem_addref returns void in this libobs; this returns the item for convenient chaining.
inline obs_sceneitem_t *retainItem(obs_sceneitem_t *item)
{
	if (item)
		obs_sceneitem_addref(item);
	return item;
}

// Thin owner of the libobs runtime: startup/shutdown, canvas settings, scenes.
class ObsCore {
public:
	static ObsCore &instance();

	// root = folder holding data/ and obs-plugins/ (the app dir).
	bool start(const QString &root, int baseW, int baseH, int outW, int outH, int fps);
	// Change canvas/encode resolution and fps. Fails while streaming.
	bool applyVideo(int baseW, int baseH, int outW, int outH, int fps);
	void shutdown();
	bool running() const { return m_running; }
	QString error() const { return m_error; }

	// Scenes. libobs only weakly tracks sources, so we hold a reference to each scene.
	obs_scene_t *createScene(const QString &name);
	void holdScene(obs_source_t *scene);
	obs_scene_t *duplicateScene(obs_scene_t *scene, const QString &name);
	void removeScene(obs_scene_t *scene); // caller must have switched away from it first
	// The scene being edited. Outside Studio Mode it is also the live scene (animated by the
	// current transition); in Studio Mode it only changes what you are editing.
	void setCurrentScene(obs_scene_t *scene);
	// Studio Mode: edit one scene while another is live, then push the edit scene on air.
	void setStudioMode(bool on);
	bool studioMode() const { return m_studio; }
	void commitToProgram();
	obs_scene_t *programScene() const { return m_program; }
	// Installs a transition of `typeId` (cut_transition, fade_transition, rare_whip, stinger_transition...)
	// configured from `settingsJson`. Rebuilt only when type or settings actually change.
	void setTransition(const QString &typeId, const QByteArray &settingsJson, int durationMs);
	obs_source_t *transitionSource() const { return m_transition; } // for the properties dialog
	QString transitionId() const { return m_transitionId; }
	int transitionMs() const { return m_transitionMs; }
	// Called whenever a different scene is put on air (used to keep the vertical Shorts view in sync).
	void setProgramHook(std::function<void(obs_scene_t *)> hook) { m_programHook = std::move(hook); }
	// Called just before a scene goes on air so per-scene transition overrides can be applied.
	void setTransitionHook(std::function<void(obs_scene_t *)> hook) { m_transitionHook = std::move(hook); }
	obs_scene_t *currentScene() const { return m_scene; }

	// Adds a source of `typeId` to the current scene, scaled to fit the canvas.
	obs_source_t *addSource(const char *typeId, const QString &name);

	// Global audio (output channels 1 and 2).
	void setupDefaultAudio();
	obs_source_t *desktopAudio() const { return m_desktop; }
	obs_source_t *micAudio() const { return m_mic; }
	// Headphone device for per-source monitoring (settings audio/monitor_*; default = system default).
	static void applyMonitoringDevice();

	// First id from `candidates` that libobs has registered, else null.
	static const char *firstAvailableEncoder(std::initializer_list<const char *> candidates);

	int canvasWidth() const { return m_w; }
	int canvasHeight() const { return m_h; }

	// ---- Scene item geometry (all in canvas pixels) ----
	// Maps the unit square (0..1) onto the item's visible content on the canvas. False if it has no size.
	static bool contentMatrix(obs_sceneitem_t *item, matrix4 *out);
	// Axis-aligned bounds of the item on the canvas, including rotation and crop.
	static QRectF itemRect(obs_sceneitem_t *item);
	// Converts the item to top-left aligned, scale-based placement (no bounds box), keeping what
	// is currently visible exactly where it is.
	static void normalizeItem(obs_sceneitem_t *item);
	// Moves/resizes the item so its visible content fills `r`, by changing position and scale.
	// (Scale rather than a bounds box, so a resizing window source is not distorted afterwards.)
	static void setItemRect(obs_sceneitem_t *item, const QRectF &r);
	static void fitToCanvas(obs_sceneitem_t *item);     // keep aspect, fill as much as possible
	static void stretchToCanvas(obs_sceneitem_t *item); // ignore aspect, fill exactly
	static void centerOnCanvas(obs_sceneitem_t *item, bool horizontal, bool vertical);

private:
	ObsCore() = default;

	bool m_running = false;
	QString m_error;
	int m_w = 1920, m_h = 1080;
	void goLive(obs_scene_t *scene); // put a scene on air through the transition

	obs_scene_t *m_scene = nullptr;   // being edited
	obs_scene_t *m_program = nullptr; // on air
	bool m_studio = false;
	QList<obs_source_t *> m_heldScenes;
	obs_source_t *m_desktop = nullptr;
	obs_source_t *m_mic = nullptr;
	obs_source_t *m_transition = nullptr; // sits on output channel 0 and animates scene changes
	QString m_transitionId = "fade_transition";
	QByteArray m_transitionSettings;
	std::function<void(obs_scene_t *)> m_transitionHook;
	std::function<void(obs_scene_t *)> m_programHook;
	int m_transitionMs = 300;
};
