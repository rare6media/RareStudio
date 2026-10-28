#include "SplashScreen.h"

#include <QApplication>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QIcon>
#include <QThread>
#include <QPainter>
#include <QPainterPath>
#include <QScreen>
#include <QVariantAnimation>

#include <algorithm>
#include <cmath>

#ifndef RARESTUDIO_BUILD
#define RARESTUDIO_BUILD "dev"
#endif

namespace {

constexpr int kW = 760, kH = 440;
constexpr double kReveal = 0.35;  // seconds: logo arrives
constexpr double kName = 0.85;   // name letters start
constexpr double kVersion = 1.5; // version line fades in
constexpr double kTotal = 2.6;   // sequence length

SplashScreen *g_active = nullptr;

double clamp01(double v) { return std::clamp(v, 0.0, 1.0); }
double ease(double v) { v = clamp01(v); return 1.0 - std::pow(1.0 - v, 3.0); }
double easeBack(double v) // overshoots slightly, settles
{
	v = clamp01(v);
	const double c = 1.70158;
	return 1.0 + (c + 1.0) * std::pow(v - 1.0, 3.0) + c * std::pow(v - 1.0, 2.0);
}
} // namespace

SplashScreen::SplashScreen()
	: QWidget(nullptr, Qt::SplashScreen | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint),
	  m_logo(QIcon(":/logo").pixmap(256, 256)),
	  m_version(RARESTUDIO_VERSION),
	  m_build(RARESTUDIO_BUILD)
{
	setAttribute(Qt::WA_TranslucentBackground);
	setFixedSize(kW, kH);
	if (const QScreen *s = QGuiApplication::primaryScreen())
		move(s->availableGeometry().center() - QPoint(kW / 2, kH / 2));
	g_active = this;
	m_clock.start();
	m_frame.setInterval(16);
	QObject::connect(&m_frame, &QTimer::timeout, this, [this] { update(); });
	m_frame.start();
	show();
	QApplication::processEvents();
}

SplashScreen::~SplashScreen()
{
	if (g_active == this)
		g_active = nullptr;
}

void SplashScreen::pump()
{
	if (!g_active || QThread::currentThread() != qApp->thread())
		return;
	static QElapsedTimer last;
	if (last.isValid() && last.elapsed() < 14)
		return;
	last.start();
	g_active->repaint(); // paint now: the event loop is not running while startup work blocks
	QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
}

void SplashScreen::setStatus(const QString &text)
{
	m_status = text;
	pump();
}

void SplashScreen::waitUntilDone()
{
	while (!m_skipped && m_clock.elapsed() < kTotal * 1000) {
		QApplication::processEvents(QEventLoop::AllEvents, 16);
		QThread::msleep(4);
	}
}

void SplashScreen::dismiss()
{
	m_frame.stop();
	auto *fade = new QVariantAnimation(this);
	fade->setDuration(380);
	fade->setStartValue(1.0);
	fade->setEndValue(0.0);
	connect(fade, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) { setWindowOpacity(v.toDouble()); });
	connect(fade, &QVariantAnimation::finished, this, [this] { delete this; });
	fade->start();
}

// ---- Painting -------------------------------------------------------------------------------

void SplashScreen::paintEvent(QPaintEvent *)
{
	const double t = m_frozen >= 0 ? m_frozen : m_clock.elapsed() / 1000.0;
	QPainter p(this);
	p.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform | QPainter::TextAntialiasing);

	QPainterPath card;
	card.addRoundedRect(QRectF(0.5, 0.5, kW - 1, kH - 1), 20, 20);
	p.setClipPath(card);
	p.fillPath(card, QColor(0x11, 0x11, 0x17)); // the app's base colour (style.qss)

	const QPointF centre(kW / 2.0, kH * 0.43);
	paintSignal(p, t, centre);
	paintBrand(p, t, centre);

	// Footer: what is loading, and a hairline that fills as the sequence plays.
	p.setPen(QColor(154, 154, 176, int(220 * ease((t - 0.2) / 0.4))));
	QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
	mono.setPixelSize(11);
	p.setFont(mono);
	p.drawText(QRectF(26, kH - 38, kW - 52, 16), Qt::AlignLeft | Qt::AlignVCenter, m_status.toUpper() + QString(int(t * 2.5) % 4, '.'));
	const double fill = clamp01(t / kTotal);
	p.fillRect(QRectF(0, kH - 3, kW, 3), QColor(255, 255, 255, 14));
	QLinearGradient bar(0, 0, kW, 0);
	bar.setColorAt(0, QColor(0x8b, 0x5c, 0xf6));
	bar.setColorAt(1, QColor(0xec, 0x48, 0x99));
	p.fillRect(QRectF(0, kH - 3, kW * fill, 3), bar);

	p.setClipping(false);
	p.setPen(QPen(QColor(255, 255, 255, 26), 1));
	p.setBrush(Qt::NoBrush);
	p.drawPath(card);
}

// Broadcast antenna: the logo transmits. Arc pairs pulse outward on both sides of it, like the
// classic ((( )))  symbol, and a soft ring leaves the mark each time a pulse starts.
void SplashScreen::paintSignal(QPainter &p, double t, const QPointF &centre)
{
	const double on = t - kReveal;
	if (on < 0)
		return;
	constexpr double kPeriod = 1.3;
	constexpr int kArcs = 4;
	const double fade = ease(on / 0.5);
	p.setBrush(Qt::NoBrush);
	for (int i = 0; i < kArcs; ++i) {
		const double r = 78 + i * 30;
		// A pulse travels outward: each arc lights up in turn, then dims until the next one.
		const double ph = std::fmod(on / kPeriod, 1.0) * (kArcs + 1.5) - i;
		const double lit = ph < 0 ? 0 : std::exp(-ph * 1.6);
		const double alpha = fade * (0.24 + 0.76 * lit) * (1.0 - i * 0.12);
		QColor c = QColor(0x8b, 0x5c, 0xf6).toRgb();
		const QColor pink(0xec, 0x48, 0x99);
		c.setRgbF(c.redF() + (pink.redF() - c.redF()) * i / (kArcs - 1), c.greenF() + (pink.greenF() - c.greenF()) * i / (kArcs - 1),
			  c.blueF() + (pink.blueF() - c.blueF()) * i / (kArcs - 1), clamp01(alpha));
		p.setPen(QPen(c, 3.2, Qt::SolidLine, Qt::RoundCap));
		const QRectF box(centre.x() - r, centre.y() - r, 2 * r, 2 * r);
		p.drawArc(box, -38 * 16, 76 * 16);          // right side
		p.drawArc(box, (180 - 38) * 16, 76 * 16);   // left side
	}
	// Ring leaving the mark as each pulse starts.
	const double rk = std::fmod(on / kPeriod, 1.0);
	p.setPen(QPen(QColor(0x8b, 0x5c, 0xf6, int(90 * (1 - rk) * fade)), 2));
	p.drawEllipse(centre, 50 + rk * 120, 50 + rk * 120);
}

void SplashScreen::paintBrand(QPainter &p, double t, const QPointF &centre)
{
	const double rev = t - kReveal;
	if (rev < 0)
		return;
	const double a = ease(rev / 0.5);

	const double size = 118 * (0.82 + 0.18 * easeBack(rev / 0.6));
	p.setOpacity(a);
	p.drawPixmap(QRectF(centre.x() - size / 2, centre.y() - size / 2, size, size), m_logo, m_logo.rect());
	p.setOpacity(1);

	// RareStudio, letter by letter, rising into place.
	QFont f = font();
	f.setPixelSize(34);
	f.setWeight(QFont::DemiBold);
	f.setLetterSpacing(QFont::AbsoluteSpacing, 5);
	p.setFont(f);
	const QString name = "RareStudio";
	const QFontMetricsF fm(f);
	double total = 0;
	for (QChar c : name)
		total += fm.horizontalAdvance(c) + 5;
	double x = centre.x() - total / 2;
	const double baseY = centre.y() + 118;
	for (int i = 0; i < name.size(); ++i) {
		const double la = ease((t - kName - i * 0.04) / 0.35);
		p.setPen(QColor(236, 236, 244, int(255 * la)));
		p.drawText(QPointF(x, baseY + (1 - la) * 10), QString(name[i]));
		x += fm.horizontalAdvance(name[i]) + 5;
	}

	// Identity line: version and the commit it was built from.
	const double va = ease((t - kVersion) / 0.5);
	QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
	mono.setPixelSize(12);
	mono.setLetterSpacing(QFont::AbsoluteSpacing, 2);
	p.setFont(mono);
	p.setPen(QColor(154, 154, 176, int(235 * va)));
	p.drawText(QRectF(0, baseY + 14 + (1 - va) * 6, kW, 18), Qt::AlignHCenter | Qt::AlignVCenter,
		   QString("VERSION %1  ·  BUILD %2").arg(m_version, m_build.toUpper()));
}
