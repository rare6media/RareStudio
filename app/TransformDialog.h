#pragma once

#include <obs.h>

#include <QDialog>

class QCheckBox;
class QDoubleSpinBox;
class QSpinBox;

// Numeric position / size / rotation / crop editor for one scene item. Edits apply live.
class TransformDialog : public QDialog {
	Q_OBJECT
public:
	TransformDialog(obs_sceneitem_t *item, QWidget *parent = nullptr);
	~TransformDialog() override;

private:
	void load();
	void applyPlacement(bool widthEdited, bool heightEdited);
	void applyCrop();
	bool visibleSize(float *w, float *h) const;

	obs_sceneitem_t *m_item;
	bool m_loading = false;
	double m_aspect = 1.0; // width / height, used when "keep aspect" is ticked

	QDoubleSpinBox *m_x, *m_y, *m_w, *m_h, *m_rot;
	QCheckBox *m_keep;
	QSpinBox *m_cropL, *m_cropT, *m_cropR, *m_cropB;
};
