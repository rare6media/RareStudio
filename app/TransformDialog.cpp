#include "TransformDialog.h"

#include "ObsCore.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>

namespace {

QDoubleSpinBox *makeSpin(double min, double max, const QString &suffix)
{
	auto *s = new QDoubleSpinBox;
	s->setRange(min, max);
	s->setDecimals(1);
	s->setSuffix(suffix);
	s->setKeyboardTracking(false);
	return s;
}

QSpinBox *makeCrop()
{
	auto *s = new QSpinBox;
	s->setRange(0, 16384);
	s->setSuffix(" px");
	s->setKeyboardTracking(false);
	return s;
}

} // namespace

TransformDialog::TransformDialog(obs_sceneitem_t *item, QWidget *parent)
	: QDialog(parent), m_item(retainItem(item))
{
	setWindowTitle(QString("Transform - %1").arg(obs_source_get_name(obs_sceneitem_get_source(item))));

	m_x = makeSpin(-32768, 32768, " px");
	m_y = makeSpin(-32768, 32768, " px");
	m_w = makeSpin(1, 32768, " px");
	m_h = makeSpin(1, 32768, " px");
	m_rot = makeSpin(-360, 360, "°");
	m_keep = new QCheckBox("Keep aspect ratio");
	m_keep->setChecked(true);
	m_cropL = makeCrop();
	m_cropT = makeCrop();
	m_cropR = makeCrop();
	m_cropB = makeCrop();

	auto *form = new QFormLayout;
	auto *pos = new QHBoxLayout;
	pos->addWidget(m_x);
	pos->addWidget(m_y);
	form->addRow("Position  X / Y", pos);
	auto *size = new QHBoxLayout;
	size->addWidget(m_w);
	size->addWidget(m_h);
	form->addRow("Size  W / H", size);
	form->addRow("", m_keep);
	form->addRow("Rotation", m_rot);
	auto *crop = new QGridLayout;
	crop->addWidget(m_cropL, 0, 0);
	crop->addWidget(m_cropT, 0, 1);
	crop->addWidget(m_cropR, 1, 0);
	crop->addWidget(m_cropB, 1, 1);
	form->addRow("Crop  L / T  R / B", crop);

	auto *quick = new QHBoxLayout;
	auto *fit = new QPushButton("Fit to screen");
	auto *stretch = new QPushButton("Stretch to screen");
	auto *cx = new QPushButton("Center");
	auto *reset = new QPushButton("Reset");
	for (auto *b : {fit, stretch, cx, reset})
		quick->addWidget(b);

	auto *box = new QDialogButtonBox(QDialogButtonBox::Close);
	auto *lay = new QVBoxLayout(this);
	lay->addLayout(form);
	lay->addLayout(quick);
	lay->addWidget(box);
	connect(box, &QDialogButtonBox::rejected, this, &QDialog::accept);

	connect(fit, &QPushButton::clicked, this, [this] { ObsCore::fitToCanvas(m_item); ObsCore::normalizeItem(m_item); load(); });
	connect(stretch, &QPushButton::clicked, this, [this] { ObsCore::stretchToCanvas(m_item); ObsCore::normalizeItem(m_item); load(); });
	connect(cx, &QPushButton::clicked, this, [this] { ObsCore::centerOnCanvas(m_item, true, true); load(); });
	connect(reset, &QPushButton::clicked, this, [this] {
		obs_sceneitem_crop c = {};
		obs_sceneitem_set_crop(m_item, &c);
		ObsCore::fitToCanvas(m_item);
		ObsCore::normalizeItem(m_item);
		load();
	});

	connect(m_x, &QDoubleSpinBox::valueChanged, this, [this] { applyPlacement(false, false); });
	connect(m_y, &QDoubleSpinBox::valueChanged, this, [this] { applyPlacement(false, false); });
	connect(m_rot, &QDoubleSpinBox::valueChanged, this, [this] { applyPlacement(false, false); });
	connect(m_w, &QDoubleSpinBox::valueChanged, this, [this] { applyPlacement(true, false); });
	connect(m_h, &QDoubleSpinBox::valueChanged, this, [this] { applyPlacement(false, true); });
	for (auto *s : {m_cropL, m_cropT, m_cropR, m_cropB})
		connect(s, &QSpinBox::valueChanged, this, [this] { applyCrop(); });

	ObsCore::normalizeItem(m_item);
	load();
}

TransformDialog::~TransformDialog()
{
	obs_sceneitem_release(m_item);
}

bool TransformDialog::visibleSize(float *w, float *h) const
{
	obs_source_t *src = obs_sceneitem_get_source(m_item);
	obs_sceneitem_crop c;
	obs_sceneitem_get_crop(m_item, &c);
	*w = float(obs_source_get_width(src)) - c.left - c.right;
	*h = float(obs_source_get_height(src)) - c.top - c.bottom;
	return *w > 0.0f && *h > 0.0f;
}

void TransformDialog::load()
{
	m_loading = true;
	obs_transform_info t;
	obs_sceneitem_get_info2(m_item, &t);
	obs_sceneitem_crop c;
	obs_sceneitem_get_crop(m_item, &c);

	float sw = 0, sh = 0;
	const bool sized = visibleSize(&sw, &sh);
	m_x->setValue(t.pos.x);
	m_y->setValue(t.pos.y);
	m_w->setValue(sized ? t.scale.x * sw : 0.0);
	m_h->setValue(sized ? t.scale.y * sh : 0.0);
	m_w->setEnabled(sized);
	m_h->setEnabled(sized);
	m_aspect = (sized && m_h->value() > 0.0) ? m_w->value() / m_h->value() : 1.0;
	m_rot->setValue(t.rot);
	m_cropL->setValue(c.left);
	m_cropT->setValue(c.top);
	m_cropR->setValue(c.right);
	m_cropB->setValue(c.bottom);
	m_loading = false;
}

void TransformDialog::applyPlacement(bool widthEdited, bool heightEdited)
{
	if (m_loading)
		return;
	float sw, sh;
	if (!visibleSize(&sw, &sh))
		return;

	if (m_keep->isChecked() && (widthEdited || heightEdited)) {
		m_loading = true; // set the partner field without recursing
		if (widthEdited)
			m_h->setValue(m_w->value() / m_aspect);
		else
			m_w->setValue(m_h->value() * m_aspect);
		m_loading = false;
	}

	obs_transform_info t;
	obs_sceneitem_get_info2(m_item, &t);
	t.pos = {(float)m_x->value(), (float)m_y->value()};
	t.rot = (float)m_rot->value();
	t.bounds_type = OBS_BOUNDS_NONE;
	t.alignment = OBS_ALIGN_LEFT | OBS_ALIGN_TOP;
	t.scale = {float(m_w->value()) / sw, float(m_h->value()) / sh};
	obs_sceneitem_set_info2(m_item, &t);

	if (!widthEdited && !heightEdited)
		return;
	m_aspect = m_w->value() / std::max(1.0, m_h->value());
}

void TransformDialog::applyCrop()
{
	if (m_loading)
		return;
	// Keep the on-screen size of what remains: crop eats into the picture, it doesn't rescale it.
	obs_transform_info t;
	obs_sceneitem_get_info2(m_item, &t);
	obs_sceneitem_crop c = {m_cropL->value(), m_cropT->value(), m_cropR->value(), m_cropB->value()};
	obs_sceneitem_set_crop(m_item, &c);
	obs_sceneitem_set_info2(m_item, &t);
	load();
}
