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
constexpr int kSkyW = 190, kSkyH = 110; // aurora raster; the widget scales it 4x
constexpr double kReveal = 1.55;        // seconds: logo arrives
constexpr double kName = 2.05;          // name letters start
constexpr double kVersion = 2.85;       // version line fades in
constexpr double kTotal = 3.9;          // sequence length

SplashScreen *g_active = nullptr;

double clamp01(double v) { return std::clamp(v, 0.0, 1.0); }
double ease(double v) { v = clamp01(v); return 1.0 - std::pow(1.0 - v, 3.0); }
double easeBack(double v) // overshoots slightly, settles
{
	v = clamp01(v);
	const double c = 1.70158;
	return 1.0 + (c + 1.0) * std::pow(v - 1.0, 3.0) + c * std::pow(v - 1.0, 2.0);
}
double hash(int n) // stable pseudo-random in [0,1)
{
	const double s = std::sin(n * 127.1 + 311.7) * 43758.5453;
	return s - std::floor(s);
}

struct Rgb {
	double r, g, b;
};
// Aurora palette, bottom ribbon to top: mint, cyan, brand violet, orchid, ice blue.
constexpr Rgb kRibbon[] = {{0.20, 0.95, 0.62}, {0.13, 0.78, 0.95}, {0.49, 0.23, 0.93}, {0.91, 0.47, 0.98}, {0.38, 0.62, 1.0}};
constexpr int kRibbons = 5;

} // namespace

SplashScreen::SplashScreen()
	: QWidget(nullptr, Qt::SplashScreen | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint),
	  m_sky(kSkyW, kSkyH, QImage::Format_RGB32),
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
	QLinearGradient bg(0, 0, 0, kH);
	bg.setColorAt(0, QColor(8, 8, 16));
	bg.setColorAt(1, QColor(15, 12, 28));
	p.fillPath(card, bg);

	// Stars, twinkling in the upper sky.
	p.setPen(Qt::NoPen);
	for (int i = 0; i < 70; ++i) {
		const double tw = 0.35 + 0.65 * (0.5 + 0.5 * std::sin(t * (0.8 + hash(i + 900) * 2.2) + hash(i) * 6.28));
		p.setBrush(QColor(210, 220, 255, int(110 * tw * ease(t / 1.2))));
		p.drawEllipse(QPointF(hash(i + 1) * kW, hash(i + 2) * kH * 0.72), 0.7 + hash(i + 3) * 0.9, 0.7 + hash(i + 3) * 0.9);
	}

	paintAurora(p, t);
	const QPointF centre(kW / 2.0, kH * 0.43);
	paintPackets(p, t, centre);
	paintBrand(p, t, centre);

	// Footer: what is loading, and a hairline that fills as the sequence plays.
	p.setPen(QColor(150, 150, 185, int(200 * ease((t - 0.4) / 0.6))));
	QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
	mono.setPixelSize(11);
	p.setFont(mono);
	p.drawText(QRectF(26, kH - 38, kW - 52, 16), Qt::AlignLeft | Qt::AlignVCenter, m_status.toUpper() + QString(int(t * 2.5) % 4, '.'));
	const double fill = clamp01(t / kTotal);
	p.fillRect(QRectF(0, kH - 3, kW, 3), QColor(255, 255, 255, 14));
	QLinearGradient bar(0, 0, kW, 0);
	bar.setColorAt(0, QColor(52, 211, 153));
	bar.setColorAt(0.5, QColor(124, 58, 237));
	bar.setColorAt(1, QColor(232, 121, 249));
	p.fillRect(QRectF(0, kH - 3, kW * fill, 3), bar);

	p.setClipping(false);
	p.setPen(QPen(QColor(255, 255, 255, 26), 1));
	p.setBrush(Qt::NoBrush);
	p.drawPath(card);
}

void SplashScreen::paintAurora(QPainter &p, double t)
{
	const double fadeIn = ease(t / 1.4);
	// Curve for each ribbon at each column, computed once per frame.
	double centreY[kRibbons][kSkyW];
	for (int r = 0; r < kRibbons; ++r) {
		const double base = kSkyH * (0.50 + 0.07 * r) - r * 3;
		for (int x = 0; x < kSkyW; ++x) {
			const double u = x / double(kSkyW);
			centreY[r][x] = base + 9.0 * std::sin(u * 5.2 + t * (0.55 + 0.12 * r) + r * 1.7) +
					5.0 * std::sin(u * 11.0 - t * (0.9 - 0.1 * r) + r * 0.6) + 3.0 * std::sin(u * 2.1 + t * 0.3);
		}
	}
	for (int y = 0; y < kSkyH; ++y) {
		QRgb *line = reinterpret_cast<QRgb *>(m_sky.scanLine(y));
		for (int x = 0; x < kSkyW; ++x) {
			const double u = x / double(kSkyW);
			double R = 8, G = 8, B = 16; // card background, so the raster blends seamlessly
			for (int r = 0; r < kRibbons; ++r) {
				const double d = (y - centreY[r][x]) / (6.0 + 2.0 * r);
				// Bright, sharp lower edge; a long glow fading upward: that is the curtain.
				double a = d > 0 ? std::exp(-d * d * 6.0) : std::exp(d * 0.28);
				// Vertical rays inside the curtain.
				a *= 0.62 + 0.38 * std::sin(u * (38.0 + 9.0 * r) + std::sin(u * 7.0 + t * 0.9 + r) * 2.2 + t * 0.7);
				a *= 0.30 * fadeIn * (0.75 + 0.25 * std::sin(t * 0.8 + r * 1.3 + u * 3.0));
				R += kRibbon[r].r * a * 255;
				G += kRibbon[r].g * a * 255;
				B += kRibbon[r].b * a * 255;
			}
			// Keep the curtain in the upper two thirds; the lower third stays calm for the footer.
			const double keep = 1.0 - clamp01((y - kSkyH * 0.72) / (kSkyH * 0.28));
			R = 8 + (R - 8) * keep;
			G = 8 + (G - 8) * keep;
			B = 16 + (B - 16) * keep;
			line[x] = qRgb(int(std::min(R, 255.0)), int(std::min(G, 255.0)), int(std::min(B, 255.0)));
		}
	}
	p.setCompositionMode(QPainter::CompositionMode_Plus);
	p.drawImage(QRectF(0, 0, kW, kH), m_sky, QRectF(0, 0, kSkyW, kSkyH));
	p.setCompositionMode(QPainter::CompositionMode_SourceOver);
}

void SplashScreen::paintPackets(QPainter &p, double t, const QPointF &centre)
{
	// Data converging on the logo from both edges. Each packet is a pure function of the clock:
	// a glowing head with a fading trail, some carrying a tag (what a real startup is moving around).
	static const char *kTags[] = {"0x7C3A", "d3d11", "NV12", "48kHz", "x264", "RTMP", "SRT", "OBS 31", "0xA1F4", "1080p60", "AAC", "0xE879"};
	QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
	mono.setPixelSize(10);
	p.setFont(mono);

	const double gate = ease((t - 0.35) / 0.9); // packets switch on after the sky has risen
	for (int i = 0; i < 44; ++i) {
		const double dur = 1.5 + hash(i + 40) * 1.4;
		const double ph = (t - 0.35 - hash(i + 10) * dur) / dur;
		if (ph < 0)
			continue;
		const double k = ph - std::floor(ph); // 0..1 along its path, repeating
		const bool left = i % 2 == 0;
		const double lane = (hash(i + 20) - 0.5);
		const QPointF from(left ? -10 : kW + 10, kH * (0.30 + hash(i + 30) * 0.5));
		const QPointF ctrl(left ? kW * 0.22 : kW * 0.78, centre.y() + lane * 220);
		auto at = [&](double s) { // quadratic Bezier from the edge, through ctrl, into the logo
			const double m = 1 - s;
			return QPointF(m * m * from.x() + 2 * m * s * ctrl.x() + s * s * centre.x(),
				       m * m * from.y() + 2 * m * s * ctrl.y() + s * s * centre.y());
		};
		const double fade = std::sin(k * M_PI) * (k > 0.86 ? (1 - k) / 0.14 : 1.0) * gate;
		const QColor head = i % 3 == 0 ? QColor(52, 211, 153) : i % 3 == 1 ? QColor(34, 211, 238) : QColor(196, 140, 255);
		for (int s = 0; s < 9; ++s) { // trail
			const double kk = std::max(0.0, k - s * 0.012);
			QColor c = head;
			c.setAlphaF(clamp01(fade * (1.0 - s / 9.0) * 0.85));
			p.setPen(Qt::NoPen);
			p.setBrush(c);
			p.drawEllipse(at(kk), 2.6 - s * 0.22, 2.6 - s * 0.22);
		}
		if (i % 4 == 0) {
			QColor c = head.lighter(140);
			c.setAlphaF(clamp01(fade * 0.8));
			p.setPen(c);
			const QPointF pos = at(k);
			p.drawText(pos + QPointF(left ? 8 : -62, -6), QString::fromLatin1(kTags[(i / 4) % 12]));
		}
	}
}

void SplashScreen::paintBrand(QPainter &p, double t, const QPointF &centre)
{
	const double rev = t - kReveal;
	if (rev < 0) {
		return;
	}
	const double a = ease(rev / 0.7);
	const double pulse = 0.5 + 0.5 * std::sin(t * 2.4);

	// Glow behind the mark, plus a ring that leaves it when it arrives.
	QRadialGradient glow(centre, 150);
	glow.setColorAt(0, QColor(124, 58, 237, int((110 + 40 * pulse) * a)));
	glow.setColorAt(0.45, QColor(34, 211, 238, int(34 * a)));
	glow.setColorAt(1, QColor(124, 58, 237, 0));
	p.setPen(Qt::NoPen);
	p.setBrush(glow);
	p.drawEllipse(centre, 150, 150);
	if (rev < 1.1) {
		const double rk = ease(rev / 1.1);
		p.setBrush(Qt::NoBrush);
		p.setPen(QPen(QColor(150, 220, 255, int(170 * (1 - rk))), 2));
		p.drawEllipse(centre, 40 + rk * 260, 40 + rk * 260);
	}

	const double size = 118 * (0.82 + 0.18 * easeBack(rev / 0.8));
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
		const double la = ease((t - kName - i * 0.055) / 0.45);
		const double y = baseY + (1 - la) * 12;
		p.setPen(QColor(34, 211, 238, int(70 * la)));
		p.drawText(QPointF(x + 1.5, y + 1.5), QString(name[i]));
		p.setPen(QColor(244, 244, 252, int(255 * la)));
		p.drawText(QPointF(x, y), QString(name[i]));
		x += fm.horizontalAdvance(name[i]) + 5;
	}

	// Identity line: version and the commit it was built from.
	const double va = ease((t - kVersion) / 0.6);
	QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
	mono.setPixelSize(12);
	mono.setLetterSpacing(QFont::AbsoluteSpacing, 2);
	p.setFont(mono);
	p.setPen(QColor(170, 150, 235, int(235 * va)));
	p.drawText(QRectF(0, baseY + 14 + (1 - va) * 6, kW, 18), Qt::AlignHCenter | Qt::AlignVCenter,
		   QString("VERSION %1  ·  BUILD %2").arg(m_version, m_build.toUpper()));
}
