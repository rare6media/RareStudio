#include "PreviewWidget.h"

#include "ObsCore.h"

#include <graphics/graphics.h>
#include <graphics/matrix4.h>
#include <graphics/vec4.h>

#include <QGuiApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QShowEvent>

#include <algorithm>
#include <cmath>

namespace {

constexpr double kHandlePx = 9.0;   // handle square, in screen pixels
constexpr double kHitPx = 8.0;      // grab tolerance, in screen pixels
constexpr double kSnapPx = 8.0;     // snap distance, in screen pixels
constexpr double kMinSize = 8.0;    // smallest box, canvas pixels

struct HitCtx {
	vec3 pt;
	obs_sceneitem_t *found = nullptr;
};

bool hitTest(obs_scene_t *, obs_sceneitem_t *item, void *p)
{
	auto *ctx = static_cast<HitCtx *>(p);
	if (!obs_sceneitem_visible(item) || obs_sceneitem_locked(item))
		return true;
	if (!(obs_source_get_output_flags(obs_sceneitem_get_source(item)) & OBS_SOURCE_VIDEO))
		return true;

	matrix4 box, inv;
	if (!ObsCore::contentMatrix(item, &box))
		return true;
	matrix4_inv(&inv, &box);
	vec3 local;
	vec3_transform(&local, &ctx->pt, &inv);
	if (local.x >= 0.0f && local.x <= 1.0f && local.y >= 0.0f && local.y <= 1.0f)
		ctx->found = item; // enumeration runs bottom to top, so the last hit is frontmost
	return true;
}

void drawUnitShape(gs_effect_t *solid, const vec4 &color, bool outline)
{
	gs_eparam_t *colorParam = gs_effect_get_param_by_name(solid, "color");
	gs_technique_t *tech = gs_effect_get_technique(solid, "Solid");
	gs_effect_set_vec4(colorParam, &color);

	gs_render_start(true);
	if (outline) {
		gs_vertex2f(0, 0);
		gs_vertex2f(1, 0);
		gs_vertex2f(1, 1);
		gs_vertex2f(0, 1);
		gs_vertex2f(0, 0);
	} else {
		gs_vertex2f(0, 0);
		gs_vertex2f(1, 0);
		gs_vertex2f(0, 1);
		gs_vertex2f(1, 1);
	}
	gs_vertbuffer_t *vb = gs_render_save();

	gs_technique_begin(tech);
	gs_technique_begin_pass(tech, 0);
	gs_load_vertexbuffer(vb);
	gs_draw(outline ? GS_LINESTRIP : GS_TRISTRIP, 0, 0);
	gs_technique_end_pass(tech);
	gs_technique_end(tech);
	gs_vertexbuffer_destroy(vb);
}

} // namespace

PreviewWidget::PreviewWidget(QWidget *parent) : QWidget(parent)
{
	setAttribute(Qt::WA_NativeWindow);
	setAttribute(Qt::WA_PaintOnScreen);
	setAttribute(Qt::WA_NoSystemBackground);
	setAttribute(Qt::WA_OpaquePaintEvent);
	setMouseTracking(true);
	setFocusPolicy(Qt::StrongFocus);
	setMinimumSize(320, 180);
}

PreviewWidget::~PreviewWidget()
{
	if (m_display)
		obs_display_destroy(m_display);
	setSelected(nullptr);
}

// ---- display ---------------------------------------------------------------------------------

void PreviewWidget::showEvent(QShowEvent *e)
{
	QWidget::showEvent(e);
	if (!m_display && QGuiApplication::platformName() != "offscreen") // headless UI tests have no real window
		createDisplay();
}

void PreviewWidget::createDisplay()
{
	const qreal dpr = devicePixelRatioF();
	gs_init_data gd = {};
	gd.window.hwnd = reinterpret_cast<void *>(winId());
	gd.cx = std::max(1, int(width() * dpr));
	gd.cy = std::max(1, int(height() * dpr));
	gd.format = GS_BGRA;
	gd.zsformat = GS_ZS_NONE;
	gd.adapter = 0;
	gd.num_backbuffers = 0;

	m_display = obs_display_create(&gd, 0xFF111114);
	if (m_display)
		obs_display_add_draw_callback(m_display, &PreviewWidget::draw, this);
}

void PreviewWidget::resizeEvent(QResizeEvent *e)
{
	QWidget::resizeEvent(e);
	if (m_display) {
		const qreal dpr = devicePixelRatioF();
		obs_display_resize(m_display, std::max(1, int(width() * dpr)), std::max(1, int(height() * dpr)));
	}
}

void PreviewWidget::setProgramOnly(bool on)
{
	m_programOnly = on;
	setAttribute(Qt::WA_TransparentForMouseEvents, on);
	setFocusPolicy(on ? Qt::NoFocus : Qt::StrongFocus);
}

void PreviewWidget::setRendering(bool on)
{
	if (m_display)
		obs_display_set_enabled(m_display, on);
}

void PreviewWidget::draw(void *param, uint32_t cx, uint32_t cy)
{
	auto *self = static_cast<PreviewWidget *>(param);
	obs_video_info ovi;
	if (!obs_get_video_info(&ovi) || !ovi.base_width || !ovi.base_height)
		return;

	// Letterbox the canvas into the widget.
	const float scale = std::min(float(cx) / ovi.base_width, float(cy) / ovi.base_height);
	const int w = int(ovi.base_width * scale);
	const int h = int(ovi.base_height * scale);
	const int x = (int(cx) - w) / 2;
	const int y = (int(cy) - h) / 2;

	gs_viewport_push();
	gs_projection_push();
	gs_ortho(0.0f, float(ovi.base_width), 0.0f, float(ovi.base_height), -100.0f, 100.0f);
	gs_set_viewport(x, y, w, h);
	if (self->m_editScene.load()) {
		// Studio Mode editor: show the scene being edited, not what is on air.
		gs_effect_t *flat = obs_get_base_effect(OBS_EFFECT_SOLID);
		vec4 black;
		vec4_set(&black, 0.0f, 0.0f, 0.0f, 1.0f);
		gs_matrix_push();
		gs_matrix_scale3f(float(ovi.base_width), float(ovi.base_height), 1.0f);
		drawUnitShape(flat, black, false);
		gs_matrix_pop();

		if (obs_scene_t *scene = ObsCore::instance().currentScene()) {
			gs_blend_state_push();
			gs_reset_blend_state();
			obs_source_video_render(obs_scene_get_source(scene));
			gs_blend_state_pop();
		}
	} else {
		obs_render_main_texture_src_color_only();
	}

	// Selection overlay: outline + eight handles. Costs nothing when nothing is selected.
	obs_sceneitem_t *sel = nullptr;
	{
		std::lock_guard lock(self->m_selMutex);
		if (self->m_sel && !self->m_programOnly)
			sel = retainItem(self->m_sel);
	}
	if (sel) {
		gs_effect_t *solid = obs_get_base_effect(OBS_EFFECT_SOLID);
		vec4 accent;
		vec4_set(&accent, 1.0f, 0.35f, 0.75f, 1.0f);

		matrix4 box;
		if (ObsCore::contentMatrix(sel, &box)) {
			gs_matrix_push();
			gs_matrix_mul(&box);
			drawUnitShape(solid, accent, true);
			gs_matrix_pop();
		}

		obs_transform_info t;
		obs_sceneitem_get_info2(sel, &t);
		if (t.rot == 0.0f) {
			const QRectF r = ObsCore::itemRect(sel);
			const float hs = float(kHandlePx / scale);
			const double xs[] = {r.left(), r.center().x(), r.right()};
			const double ys[] = {r.top(), r.center().y(), r.bottom()};
			for (int iy = 0; iy < 3; ++iy) {
				for (int ix = 0; ix < 3; ++ix) {
					if (ix == 1 && iy == 1)
						continue;
					gs_matrix_push();
					gs_matrix_translate3f(float(xs[ix]) - hs / 2, float(ys[iy]) - hs / 2, 0.0f);
					gs_matrix_scale3f(hs, hs, 1.0f);
					drawUnitShape(solid, accent, false);
					gs_matrix_pop();
				}
			}
		}
		obs_sceneitem_release(sel);
	}

	gs_projection_pop();
	gs_viewport_pop();
}

// ---- coordinates -----------------------------------------------------------------------------

double PreviewWidget::canvasScale() const
{
	obs_video_info ovi;
	if (!obs_get_video_info(&ovi) || !ovi.base_width || !ovi.base_height)
		return 1.0;
	const double dpr = devicePixelRatioF();
	return std::min(width() * dpr / ovi.base_width, height() * dpr / ovi.base_height);
}

QPointF PreviewWidget::toCanvas(const QPointF &p) const
{
	obs_video_info ovi;
	if (!obs_get_video_info(&ovi))
		return {};
	const double dpr = devicePixelRatioF();
	const double s = canvasScale();
	const double ox = (width() * dpr - ovi.base_width * s) / 2.0;
	const double oy = (height() * dpr - ovi.base_height * s) / 2.0;
	return {(p.x() * dpr - ox) / s, (p.y() * dpr - oy) / s};
}

// ---- selection -------------------------------------------------------------------------------

void PreviewWidget::setSelected(obs_sceneitem_t *item)
{
	obs_sceneitem_t *old;
	{
		std::lock_guard lock(m_selMutex);
		old = m_sel;
		m_sel = retainItem(item);
	}
	if (old)
		obs_sceneitem_release(old);
}

QString PreviewWidget::selectedName() const
{
	std::lock_guard lock(m_selMutex);
	return m_sel ? QString::fromUtf8(obs_source_get_name(obs_sceneitem_get_source(m_sel))) : QString();
}

obs_sceneitem_t *PreviewWidget::selectedItem() const
{
	std::lock_guard lock(m_selMutex);
	return retainItem(m_sel);
}

void PreviewWidget::selectByName(const QString &name)
{
	obs_scene_t *scene = ObsCore::instance().currentScene();
	obs_sceneitem_t *item = (scene && !name.isEmpty()) ? obs_scene_find_source(scene, name.toUtf8().constData()) : nullptr;
	setSelected(item);
}

void PreviewWidget::clearSelection()
{
	setSelected(nullptr);
}

obs_sceneitem_t *PreviewWidget::itemAt(const QPointF &pt) const
{
	obs_scene_t *scene = ObsCore::instance().currentScene();
	if (!scene)
		return nullptr;
	HitCtx ctx;
	vec3_set(&ctx.pt, float(pt.x()), float(pt.y()), 0.0f);
	obs_scene_enum_items(scene, hitTest, &ctx);
	return ctx.found;
}

int PreviewWidget::handleAt(const QPointF &pt) const
{
	obs_sceneitem_t *sel = selectedItem();
	if (!sel)
		return 0;
	int hit = 0;
	obs_transform_info t;
	obs_sceneitem_get_info2(sel, &t);
	if (t.rot == 0.0f) {
		const QRectF r = ObsCore::itemRect(sel);
		const double tol = kHitPx / canvasScale();
		const double xs[] = {r.left(), r.center().x(), r.right()};
		const double ys[] = {r.top(), r.center().y(), r.bottom()};
		const int hx[] = {HL, 0, HR};
		const int hy[] = {HT, 0, HB};
		for (int iy = 0; iy < 3 && !hit; ++iy)
			for (int ix = 0; ix < 3; ++ix)
				if ((ix != 1 || iy != 1) && std::abs(pt.x() - xs[ix]) <= tol && std::abs(pt.y() - ys[iy]) <= tol) {
					hit = hx[ix] | hy[iy];
					break;
				}
	}
	obs_sceneitem_release(sel);
	return hit;
}

// ---- mouse / keyboard ------------------------------------------------------------------------

double PreviewWidget::snap(double v, const double *targets, int n, double threshold) const
{
	for (int i = 0; i < n; ++i)
		if (std::abs(v - targets[i]) <= threshold)
			return targets[i];
	return v;
}

void PreviewWidget::updateCursor(const QPointF &pt)
{
	const int h = handleAt(pt);
	if (h == (HL | HT) || h == (HR | HB))
		setCursor(Qt::SizeFDiagCursor);
	else if (h == (HR | HT) || h == (HL | HB))
		setCursor(Qt::SizeBDiagCursor);
	else if (h & (HL | HR))
		setCursor(Qt::SizeHorCursor);
	else if (h & (HT | HB))
		setCursor(Qt::SizeVerCursor);
	else if (itemAt(pt))
		setCursor(Qt::SizeAllCursor);
	else
		unsetCursor();
}

void PreviewWidget::mousePressEvent(QMouseEvent *e)
{
	setFocus();
	const QPointF pt = toCanvas(e->position());

	if (e->button() == Qt::RightButton) {
		if (obs_sceneitem_t *hit = itemAt(pt)) {
			setSelected(hit);
			emit selectionChanged(selectedName());
		}
		emit contextRequested(e->globalPosition().toPoint());
		return;
	}
	if (e->button() != Qt::LeftButton)
		return;

	m_handle = handleAt(pt);
	obs_sceneitem_t *target = nullptr;
	if (m_handle) {
		target = selectedItem();
		m_drag = Drag::Resize;
	} else if (obs_sceneitem_t *hit = itemAt(pt)) {
		setSelected(hit);
		emit selectionChanged(selectedName());
		target = selectedItem();
		m_drag = Drag::Move;
	} else {
		setSelected(nullptr);
		emit selectionChanged(QString());
		m_drag = Drag::None;
		return;
	}

	ObsCore::normalizeItem(target);
	m_startRect = ObsCore::itemRect(target);
	m_pressPt = pt;
	obs_sceneitem_release(target);
}

void PreviewWidget::mouseMoveEvent(QMouseEvent *e)
{
	const QPointF pt = toCanvas(e->position());
	if (m_drag == Drag::None || !(e->buttons() & Qt::LeftButton)) {
		updateCursor(pt);
		return;
	}
	obs_sceneitem_t *sel = selectedItem();
	if (!sel)
		return;

	const int cw = ObsCore::instance().canvasWidth();
	const int ch = ObsCore::instance().canvasHeight();
	const double thr = kSnapPx / canvasScale();
	const double xt[] = {0.0, cw / 2.0, double(cw)};
	const double yt[] = {0.0, ch / 2.0, double(ch)};
	const bool noSnap = e->modifiers() & Qt::ControlModifier;
	const QPointF d = pt - m_pressPt;
	QRectF r = m_startRect;

	if (m_drag == Drag::Move) {
		r.translate(d);
		if (!noSnap) {
			// Snap whichever of left/center/right (and top/middle/bottom) is nearest a guide.
			auto nearest = [thr](std::initializer_list<double> edges, const double *guides) {
				double best = 0.0, bestAbs = thr + 1.0;
				for (double edge : edges)
					for (int i = 0; i < 3; ++i) {
						const double delta = guides[i] - edge;
						if (std::abs(delta) < bestAbs) {
							bestAbs = std::abs(delta);
							best = delta;
						}
					}
				return bestAbs <= thr ? best : 0.0;
			};
			r.translate(nearest({r.left(), r.center().x(), r.right()}, xt),
				    nearest({r.top(), r.center().y(), r.bottom()}, yt));
		}
	} else {
		double x0 = r.left(), x1 = r.right(), y0 = r.top(), y1 = r.bottom();
		if (m_handle & HL)
			x0 = noSnap ? x0 + d.x() : snap(x0 + d.x(), xt, 3, thr);
		if (m_handle & HR)
			x1 = noSnap ? x1 + d.x() : snap(x1 + d.x(), xt, 3, thr);
		if (m_handle & HT)
			y0 = noSnap ? y0 + d.y() : snap(y0 + d.y(), yt, 3, thr);
		if (m_handle & HB)
			y1 = noSnap ? y1 + d.y() : snap(y1 + d.y(), yt, 3, thr);

		if (!(e->modifiers() & Qt::ShiftModifier) && (m_handle & (HL | HR)) && (m_handle & (HT | HB))) {
			// Corner drags keep the aspect ratio; hold Shift to distort freely.
			const double aspect = m_startRect.width() / std::max(1.0, m_startRect.height());
			const double w = std::max(kMinSize, x1 - x0);
			const double h = w / aspect;
			if (m_handle & HT)
				y0 = y1 - h;
			else
				y1 = y0 + h;
		}
		if (x1 - x0 < kMinSize) {
			if (m_handle & HL) x0 = x1 - kMinSize; else x1 = x0 + kMinSize;
		}
		if (y1 - y0 < kMinSize) {
			if (m_handle & HT) y0 = y1 - kMinSize; else y1 = y0 + kMinSize;
		}
		r = QRectF(QPointF(x0, y0), QPointF(x1, y1));
	}

	ObsCore::setItemRect(sel, r);
	obs_sceneitem_release(sel);
	emit itemEdited();
}

void PreviewWidget::mouseReleaseEvent(QMouseEvent *e)
{
	if (e->button() == Qt::LeftButton)
		m_drag = Drag::None;
}

void PreviewWidget::keyPressEvent(QKeyEvent *e)
{
	obs_sceneitem_t *sel = selectedItem();
	if (!sel) {
		QWidget::keyPressEvent(e);
		return;
	}
	const float step = (e->modifiers() & Qt::ShiftModifier) ? 10.0f : 1.0f;
	vec2 pos;
	obs_sceneitem_get_pos(sel, &pos);
	bool moved = true;
	switch (e->key()) {
	case Qt::Key_Left: pos.x -= step; break;
	case Qt::Key_Right: pos.x += step; break;
	case Qt::Key_Up: pos.y -= step; break;
	case Qt::Key_Down: pos.y += step; break;
	case Qt::Key_Delete:
		moved = false;
		obs_sceneitem_release(sel);
		emit deleteRequested();
		return;
	default: moved = false;
	}
	if (moved) {
		obs_sceneitem_set_pos(sel, &pos);
		emit itemEdited();
	} else {
		QWidget::keyPressEvent(e);
	}
	obs_sceneitem_release(sel);
}
