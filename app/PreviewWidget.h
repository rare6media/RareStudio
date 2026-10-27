#pragma once

#include <obs.h>

#include <QPointF>
#include <QRectF>
#include <QWidget>

#include <atomic>
#include <mutex>

// Native child window that libobs renders into directly (no frame copies through Qt).
// Also the editing surface: click to select a source, drag to move, drag handles to resize.
class PreviewWidget : public QWidget {
	Q_OBJECT
public:
	explicit PreviewWidget(QWidget *parent = nullptr);
	~PreviewWidget() override;

	QPaintEngine *paintEngine() const override { return nullptr; }

	// Pause rendering (minimized / hidden) so the preview costs nothing.
	void setRendering(bool on);

	// Read-only view of what is on air (used next to the editor in Studio Mode).
	void setProgramOnly(bool on);
	// Studio Mode editor: draw the edit scene directly instead of the on-air picture.
	void setShowsEditScene(bool on) { m_editScene = on; }

	// Selection is tracked by source name (unique inside a scene).
	void selectByName(const QString &name);
	void clearSelection();
	QString selectedName() const;
	obs_sceneitem_t *selectedItem() const; // addref'd copy, caller releases

signals:
	void selectionChanged(const QString &name); // empty = nothing selected
	void contextRequested(const QPoint &globalPos);
	void deleteRequested();
	void itemEdited(); // geometry changed by a drag/nudge

protected:
	void resizeEvent(QResizeEvent *e) override;
	void showEvent(QShowEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void keyPressEvent(QKeyEvent *e) override;

private:
	enum Handle { HL = 1, HR = 2, HT = 4, HB = 8 };
	enum class Drag { None, Move, Resize };

	void createDisplay();
	static void draw(void *param, uint32_t cx, uint32_t cy);

	void setSelected(obs_sceneitem_t *item); // takes its own reference
	obs_sceneitem_t *itemAt(const QPointF &canvasPt) const;
	int handleAt(const QPointF &canvasPt) const;
	QPointF toCanvas(const QPointF &widgetPt) const;
	double canvasScale() const; // widget physical px per canvas px
	double snap(double v, const double *targets, int n, double threshold) const;
	void updateCursor(const QPointF &canvasPt);

	obs_display_t *m_display = nullptr;

	mutable std::mutex m_selMutex; // the draw callback runs on the graphics thread
	obs_sceneitem_t *m_sel = nullptr;

	std::atomic<bool> m_editScene{false};
	bool m_programOnly = false;

	Drag m_drag = Drag::None;
	int m_handle = 0;
	QPointF m_pressPt;
	QRectF m_startRect;
};
