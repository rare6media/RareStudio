#include "MixerPanel.h"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QSlider>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

MixerStrip::MixerStrip(const QString &label, obs_source_t *source, QWidget *parent)
	: QWidget(parent), m_source(source)
{
	auto *lay = new QVBoxLayout(this);
	lay->setContentsMargins(6, 4, 6, 4);
	lay->setSpacing(2);

	auto *top = new QHBoxLayout;
	top->addWidget(new QLabel(label));
	m_mute = new QCheckBox("Mute");
	auto *filters = new QPushButton("Filters");
	filters->setToolTip("Noise suppression, gain, compressor, noise gate...");
	top->addStretch();
	top->addWidget(filters);
	top->addWidget(m_mute);
	connect(filters, &QPushButton::clicked, this, [this] { emit filtersRequested(m_source); });
	lay->addLayout(top);

	m_bar = new QProgressBar;
	m_bar->setRange(-60, 0);
	m_bar->setValue(-60);
	m_bar->setTextVisible(false);
	m_bar->setFixedHeight(8);
	lay->addWidget(m_bar);

	m_fader = new QSlider(Qt::Horizontal);
	m_fader->setRange(0, 100);
	m_fader->setValue(int(std::lround(std::cbrt(obs_source_get_volume(source)) * 100.0f)));
	lay->addWidget(m_fader);

	connect(m_fader, &QSlider::valueChanged, this, [this](int v) {
		const float f = v / 100.0f;
		obs_source_set_volume(m_source, f * f * f); // perceptual curve
	});
	connect(m_mute, &QCheckBox::toggled, this, [this](bool on) { obs_source_set_muted(m_source, on); });
	m_mute->setChecked(obs_source_muted(source));

	m_meter = obs_volmeter_create(OBS_FADER_LOG);
	obs_volmeter_attach_source(m_meter, source);
	obs_volmeter_add_callback(m_meter, &MixerStrip::onLevels, this);
}

MixerStrip::~MixerStrip()
{
	if (m_meter) {
		obs_volmeter_remove_callback(m_meter, &MixerStrip::onLevels, this);
		obs_volmeter_destroy(m_meter);
	}
}

void MixerStrip::onLevels(void *param, const float[MAX_AUDIO_CHANNELS], const float peak[MAX_AUDIO_CHANNELS],
			  const float[MAX_AUDIO_CHANNELS])
{
	auto *self = static_cast<MixerStrip *>(param);
	self->m_level.store(std::max(peak[0], peak[1]), std::memory_order_relaxed);
}

void MixerStrip::refreshMeter()
{
	const float db = std::clamp(m_level.load(std::memory_order_relaxed), -60.0f, 0.0f);
	m_bar->setValue(int(db));
}

MixerPanel::MixerPanel(QWidget *parent) : QWidget(parent)
{
	m_layout = new QVBoxLayout(this);
	m_layout->setContentsMargins(0, 0, 0, 0);
	m_layout->addStretch();
	m_timer.setInterval(66); // ~15 Hz is plenty for meters
	connect(&m_timer, &QTimer::timeout, this, [this] {
		for (MixerStrip *s : m_strips)
			s->refreshMeter();
	});
}

void MixerPanel::addStrip(const QString &label, obs_source_t *source)
{
	if (!source)
		return;
	auto *strip = new MixerStrip(label, source, this);
	m_strips.append(strip);
	connect(strip, &MixerStrip::filtersRequested, this, &MixerPanel::filtersRequested);
	m_layout->insertWidget(m_layout->count() - 1, strip);
}
