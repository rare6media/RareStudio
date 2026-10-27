#include "MixerPanel.h"

#include "AudioPresets.h"
#include "Autosave.h"

#include <QAction>
#include <QActionGroup>
#include <QDoubleSpinBox>
#include <QFontMetrics>
#include <QFrame>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace {

constexpr double kMinDb = -60.0; // bottom of the fader and the entry box = silence
constexpr double kMaxDb = 12.0;
constexpr int kFaderSteps = 1000;
constexpr float kClipDb = -0.5f;
constexpr qint64 kPeakHoldMs = 1200;
constexpr float kFallDbPerSec = 24.0f;

struct Strong {
	obs_source_t *s;
	explicit Strong(obs_weak_source_t *w) : s(obs_weak_source_get_source(w)) {}
	~Strong() { obs_source_release(s); }
	Strong(const Strong &) = delete;
	Strong &operator=(const Strong &) = delete;
};

float sanitize(float db)
{
	return std::isfinite(db) ? std::clamp(db, LevelMeter::kFloor, 6.0f) : LevelMeter::kFloor;
}

int dbToPos(double db)
{
	return int(std::lround(std::clamp((db - kMinDb) / (kMaxDb - kMinDb), 0.0, 1.0) * kFaderSteps));
}

double posToDb(int pos)
{
	return kMinDb + double(pos) / kFaderSteps * (kMaxDb - kMinDb);
}

double mulToDb(float mul)
{
	return mul <= 0.001f ? kMinDb : std::clamp(20.0 * std::log10(double(mul)), kMinDb, kMaxDb);
}

} // namespace

// ---- LevelMeter -------------------------------------------------------------------------------

LevelMeter::LevelMeter(QWidget *parent) : QWidget(parent)
{
	setFixedHeight(32);
	setMinimumWidth(120);
	setToolTip("Click to clear the clip indicator");
	m_clock.start();
}

void LevelMeter::setLevels(const float mag[2], const float peak[2])
{
	for (int c = 0; c < 2; ++c) {
		m_in[c] = sanitize(mag[c]);
		m_inPeak[c] = sanitize(peak[c]);
		if (m_inPeak[c] >= kClipDb)
			m_clip = true;
	}
}

void LevelMeter::advance()
{
	const qint64 now = m_clock.elapsed();
	const float dt = float(std::min<qint64>(now - m_last, 250)) / 1000.0f;
	m_last = now;
	const float before[4] = {m_shown[0], m_shown[1], m_hold[0], m_hold[1]};

	for (int c = 0; c < 2; ++c) {
		const float target = m_muted ? kFloor : m_in[c];
		m_shown[c] = target >= m_shown[c] ? target : std::max(target, m_shown[c] - kFallDbPerSec * dt);

		const float p = m_muted ? kFloor : m_inPeak[c];
		if (p >= m_hold[c]) {
			m_hold[c] = p;
			m_holdSince[c] = now;
		} else if (now - m_holdSince[c] > kPeakHoldMs) {
			m_hold[c] = std::max(p, m_hold[c] - kFallDbPerSec * dt);
		}
	}
	if (before[0] != m_shown[0] || before[1] != m_shown[1] || before[2] != m_hold[0] || before[3] != m_hold[1])
		update();
}

void LevelMeter::setMuted(bool muted)
{
	if (m_muted == muted)
		return;
	m_muted = muted;
	update();
}

void LevelMeter::mousePressEvent(QMouseEvent *)
{
	m_clip = false;
	update();
}

void LevelMeter::paintEvent(QPaintEvent *)
{
	QPainter p(this);
	const int ledW = 9, gap = 4;
	const int barW = width() - ledW - gap;
	const int barH = 8;
	auto xFor = [&](float db) { return int((std::clamp(db, kFloor, 0.0f) - kFloor) / -kFloor * barW); };

	struct Zone {
		float from, to;
		QColor on;
	};
	const Zone zones[] = {{kFloor, -18.0f, QColor("#22c55e")}, {-18.0f, -6.0f, QColor("#eab308")},
			      {-6.0f, 0.0f, QColor("#ef4444")}};

	for (int c = 0; c < 2; ++c) {
		const int y = 1 + c * (barH + 2);
		for (const Zone &z : zones) {
			const int x0 = xFor(z.from), x1 = xFor(z.to);
			QColor off = z.on;
			off.setAlpha(m_muted ? 18 : 45);
			p.fillRect(x0, y, x1 - x0, barH, off);
			const int fill = std::min(x1, xFor(m_shown[c]));
			if (fill > x0) {
				QColor on = m_muted ? QColor("#55556a") : z.on;
				p.fillRect(x0, y, fill - x0, barH, on);
			}
		}
		if (!m_muted && m_hold[c] > kFloor + 1.0f) {
			const int hx = std::min(barW - 2, xFor(m_hold[c]));
			p.fillRect(hx, y, 2, barH, m_hold[c] >= -6.0f ? QColor("#fecaca") : QColor("#f0f0ff"));
		}
	}

	// Clip latch
	const QRect led(barW + gap, 1, ledW, 2 * barH + 2);
	p.fillRect(led, m_clip ? QColor("#ef4444") : QColor("#2c2c38"));

	// Scale
	QFont f = font();
	f.setPixelSize(9);
	p.setFont(f);
	p.setPen(QColor("#7a7a90"));
	const int tickY = 2 * barH + 4;
	for (int db : {-60, -50, -40, -30, -20, -10, -6, -3, 0}) {
		const int x = xFor(float(db));
		p.drawLine(x, tickY, x, tickY + 2);
		const QString t = QString::number(db);
		const int tw = p.fontMetrics().horizontalAdvance(t);
		// Centred under the tick, except at the ends where it sits beside it (not on top of it).
		const int tx = x - tw / 2 < 0 ? x + 3 : x + tw / 2 > barW ? x - tw - 3 : x - tw / 2;
		p.drawText(tx, tickY + 3, tw + 2, 10, Qt::AlignLeft | Qt::AlignTop, t);
	}
}

// ---- MixerStrip -------------------------------------------------------------------------------

MixerStrip::MixerStrip(const QString &label, obs_source_t *source, QWidget *parent)
	: QWidget(parent), m_ptr(source), m_weak(obs_source_get_weak_source(source))
{
	auto *lay = new QVBoxLayout(this);
	lay->setContentsMargins(8, 6, 8, 6);
	lay->setSpacing(3);

	auto *top = new QHBoxLayout;
	top->setSpacing(4);
	m_name = new QLabel(label);
	m_name->setStyleSheet("font-weight: 600; color: #e8e8f0;");
	m_name->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	m_menuBtn = new QToolButton;
	m_menuBtn->setText(QString::fromUtf8("\xE2\x8B\xAF")); // ⋯
	m_menuBtn->setToolTip("Filters, device, monitoring, sync offset");
	m_menuBtn->setPopupMode(QToolButton::InstantPopup);
	m_menuBtn->setStyleSheet("QToolButton { background: #2c2c38; border: 1px solid #353544; border-radius: 6px; "
				 "color: #e8e8f0; padding: 1px 8px; font-weight: 700; }"
				 "QToolButton:hover { border-color: #7c3aed; }"
				 "QToolButton::menu-indicator { image: none; }");
	m_mute = new QPushButton("M");
	m_mute->setCheckable(true);
	m_mute->setToolTip("Mute");
	m_mute->setFixedWidth(30);
	m_mute->setStyleSheet("QPushButton { padding: 2px 0; font-weight: 700; }"
			      "QPushButton:checked { background: #dc2626; border-color: #dc2626; color: white; }");
	top->addWidget(m_name, 1);
	top->addWidget(m_menuBtn);
	top->addWidget(m_mute);
	lay->addLayout(top);

	m_levels = new LevelMeter;
	lay->addWidget(m_levels);

	auto *bottom = new QHBoxLayout;
	bottom->setSpacing(8);
	m_fader = new Fader(Qt::Horizontal);
	m_fader->setRange(0, kFaderSteps);
	m_fader->setToolTip("Volume (double-click for 0 dB)");
	m_db = new QDoubleSpinBox;
	m_db->setRange(kMinDb, kMaxDb);
	m_db->setDecimals(1);
	m_db->setSingleStep(0.5);
	m_db->setSuffix(" dB");
	m_db->setSpecialValueText("-inf dB");
	m_db->setKeyboardTracking(false);
	m_db->setFixedWidth(104);
	bottom->addWidget(m_fader, 1);
	bottom->addWidget(m_db);
	lay->addLayout(bottom);

	connect(m_fader, &QSlider::valueChanged, this, [this](int pos) { setDb(posToDb(pos), true); });
	connect(m_fader, &Fader::resetRequested, this, [this] { setDb(0.0, true); });
	connect(m_db, &QDoubleSpinBox::valueChanged, this, [this](double db) { setDb(db, true); });
	connect(m_mute, &QPushButton::toggled, this, [this](bool on) {
		Strong s(m_weak);
		if (s.s)
			obs_source_set_muted(s.s, on);
		Autosave::touch();
		m_levels->setMuted(on);
		m_lastMuted = on;
	});

	buildMenu();
	syncFromSource();

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
	obs_weak_source_release(m_weak);
}

void MixerStrip::buildMenu()
{
	m_menu = new QMenu(this);
	m_filtersAct = m_menu->addAction("Filters...", this, [this] {
		Strong s(m_weak);
		if (s.s)
			emit filtersRequested(s.s);
	});
	m_menu->addAction("Device / properties...", this, [this] {
		Strong s(m_weak);
		if (s.s)
			emit propertiesRequested(s.s);
	});
	m_menu->addSeparator();

	if (obs_audio_monitoring_available()) {
		QMenu *mon = m_menu->addMenu("Monitoring");
		auto *group = new QActionGroup(mon);
		const struct {
			const char *text;
			obs_monitoring_type type;
		} kinds[] = {{"Off", OBS_MONITORING_TYPE_NONE},
			     {"Monitor only (not in stream)", OBS_MONITORING_TYPE_MONITOR_ONLY},
			     {"Monitor and output", OBS_MONITORING_TYPE_MONITOR_AND_OUTPUT}};
		for (const auto &k : kinds) {
			QAction *a = mon->addAction(k.text);
			a->setCheckable(true);
			a->setData(int(k.type));
			group->addAction(a);
			connect(a, &QAction::triggered, this, [this, t = k.type] {
				Strong s(m_weak);
				if (s.s)
					obs_source_set_monitoring_type(s.s, t);
				Autosave::touch();
			});
		}
		connect(mon, &QMenu::aboutToShow, this, [this, mon] {
			Strong s(m_weak);
			if (!s.s)
				return;
			const int cur = int(obs_source_get_monitoring_type(s.s));
			for (QAction *a : mon->actions())
				a->setChecked(a->data().toInt() == cur);
		});
	}

	m_syncAct = m_menu->addAction("Sync offset...", this, [this] {
		Strong s(m_weak);
		if (!s.s)
			return;
		const int cur = int(obs_source_get_sync_offset(s.s) / 1000000);
		bool ok = false;
		const int ms = QInputDialog::getInt(this, "Sync offset",
						    "Delay this source by (ms). Negative values play it earlier:", cur,
						    -950, 20000, 10, &ok);
		if (ok) {
			obs_source_set_sync_offset(s.s, int64_t(ms) * 1000000);
			Autosave::touch();
		}
	});
	m_menu->addAction("Reset volume to 0 dB", this, [this] { setDb(0.0, true); });

	connect(m_menu, &QMenu::aboutToShow, this, [this] {
		Strong s(m_weak);
		if (!s.s)
			return;
		const int n = AudioPresets::filterCount(s.s);
		m_filtersAct->setText(n ? QString("Filters (%1)...").arg(n) : QString("Filters..."));
		m_syncAct->setText(QString("Sync offset (%1 ms)...").arg(int(obs_source_get_sync_offset(s.s) / 1000000)));
	});
	m_menuBtn->setMenu(m_menu);
}

void MixerStrip::setDb(double db, bool fromUser)
{
	db = std::clamp(db, kMinDb, kMaxDb);
	{
		QSignalBlocker b1(m_fader), b2(m_db);
		m_fader->setValue(dbToPos(db));
		m_db->setValue(db);
	}
	const float mul = db <= kMinDb ? 0.0f : float(std::pow(10.0, db / 20.0));
	m_lastMul = mul;
	if (fromUser) {
		Strong s(m_weak);
		if (s.s)
			obs_source_set_volume(s.s, mul);
	}
}

void MixerStrip::syncFromSource()
{
	Strong s(m_weak);
	if (!s.s)
		return;
	const float mul = obs_source_get_volume(s.s);
	if (std::fabs(mul - m_lastMul) > 1e-4f)
		setDb(mulToDb(mul), false);
	const bool muted = obs_source_muted(s.s);
	if (muted != m_lastMuted || muted != m_mute->isChecked()) {
		QSignalBlocker b(m_mute);
		m_mute->setChecked(muted);
		m_lastMuted = muted;
		m_levels->setMuted(muted);
	}
	if (m_name->toolTip().isEmpty())
		m_name->setToolTip(QString::fromUtf8(obs_source_get_name(s.s)));
}

bool MixerStrip::alive() const
{
	Strong s(m_weak);
	return s.s && !obs_source_removed(s.s);
}

void MixerStrip::onLevels(void *param, const float magnitude[MAX_AUDIO_CHANNELS], const float peak[MAX_AUDIO_CHANNELS],
			  const float[MAX_AUDIO_CHANNELS])
{
	auto *self = static_cast<MixerStrip *>(param);
	// Mono sources report one channel: show it on both bars.
	const int right = obs_volmeter_get_nr_channels(self->m_meter) > 1 ? 1 : 0;
	self->m_mag[0].store(magnitude[0], std::memory_order_relaxed);
	self->m_mag[1].store(magnitude[right], std::memory_order_relaxed);
	self->m_peak[0].store(peak[0], std::memory_order_relaxed);
	self->m_peak[1].store(peak[right], std::memory_order_relaxed);
}

void MixerStrip::refresh()
{
	const float mag[2] = {m_mag[0].load(std::memory_order_relaxed), m_mag[1].load(std::memory_order_relaxed)};
	const float peak[2] = {m_peak[0].load(std::memory_order_relaxed), m_peak[1].load(std::memory_order_relaxed)};
	m_levels->setLevels(mag, peak);
	m_levels->advance();
	syncFromSource();
}

// ---- MixerPanel -------------------------------------------------------------------------------

MixerPanel::MixerPanel(QWidget *parent) : QWidget(parent)
{
	auto *outer = new QVBoxLayout(this);
	outer->setContentsMargins(0, 0, 0, 0);
	auto *scroll = new QScrollArea;
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);
	scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	auto *inner = new QWidget;
	m_layout = new QVBoxLayout(inner);
	m_layout->setContentsMargins(0, 0, 0, 0);
	m_layout->setSpacing(0);
	m_layout->addStretch();
	scroll->setWidget(inner);
	outer->addWidget(scroll);

	m_timer.setInterval(40); // 25 Hz: smooth meters, still cheap
	connect(&m_timer, &QTimer::timeout, this, [this] {
		if (++m_ticks % 50 == 0) // every ~2 s
			syncSources();
		for (MixerStrip *s : m_strips)
			s->refresh();
	});
}

void MixerPanel::showEvent(QShowEvent *)
{
	syncSources();
	m_timer.start();
}

void MixerPanel::addStrip(const QString &label, obs_source_t *source)
{
	if (!source)
		return;
	for (MixerStrip *s : m_strips)
		if (s->identity() == source)
			return;
	auto *strip = new MixerStrip(label, source);
	m_strips.append(strip);
	connect(strip, &MixerStrip::filtersRequested, this, &MixerPanel::filtersRequested);
	connect(strip, &MixerStrip::propertiesRequested, this, &MixerPanel::propertiesRequested);
	m_layout->insertWidget(m_layout->count() - 1, strip);

	// Hairline between strips
	auto *line = new QFrame;
	line->setFrameShape(QFrame::HLine);
	line->setStyleSheet("color: #2a2a35;");
	strip->setProperty("divider", QVariant::fromValue<QObject *>(line));
	m_layout->insertWidget(m_layout->count() - 1, line);
}

void MixerPanel::syncSources()
{
	// Drop strips whose source is gone (also removes its divider).
	for (int i = int(m_strips.size()) - 1; i >= 0; --i) {
		MixerStrip *s = m_strips[i];
		if (s->alive())
			continue;
		if (auto *line = s->property("divider").value<QObject *>())
			line->deleteLater();
		m_strips.removeAt(i);
		s->deleteLater();
	}

	// Pick up any other source that produces audio (application capture, media, browser...).
	QList<obs_source_t *> found;
	obs_enum_sources(
		[](void *p, obs_source_t *src) {
			if (obs_source_get_type(src) == OBS_SOURCE_TYPE_INPUT &&
			    (obs_source_get_output_flags(src) & OBS_SOURCE_AUDIO))
				if (obs_source_t *ref = obs_source_get_ref(src)) // null if it is being destroyed right now
					static_cast<QList<obs_source_t *> *>(p)->append(ref);
			return true;
		},
		&found);
	for (obs_source_t *src : found) {
		if (!obs_source_removed(src))
			addStrip(QString::fromUtf8(obs_source_get_name(src)), src);
		obs_source_release(src);
	}
}
