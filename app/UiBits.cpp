#include "UiBits.h"

#include <QApplication>
#include <QDialog>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPointer>
#include <QPropertyAnimation>
#include <QSettings>
#include <QVariantAnimation>

#include <algorithm>

namespace UiBits {

double scaleFactor()
{
	return std::clamp(QSettings().value("ui/scale", 1.0).toDouble(), 0.75, 2.0);
}

bool animationsEnabled()
{
	return QSettings().value("ui/animations", true).toBool();
}

namespace {

// Dialogs fade in over a few frames. Only dialogs: the main window holds the D3D preview, and a
// layered (translucent) window under a swap chain is exactly what you don't want.
class DialogFader : public QObject {
public:
	using QObject::QObject;
	bool eventFilter(QObject *o, QEvent *e) override
	{
		if (e->type() == QEvent::Show && m_enabled) {
			auto *d = qobject_cast<QDialog *>(o);
			if (d && d->isWindow() && !d->property("rsFaded").toBool()) {
				d->setProperty("rsFaded", true); // once per dialog, not every re-show
				d->setWindowOpacity(0.0);
				auto *a = new QPropertyAnimation(d, "windowOpacity", d);
				a->setDuration(140);
				a->setStartValue(0.0);
				a->setEndValue(1.0);
				a->setEasingCurve(QEasingCurve::OutCubic);
				a->start(QAbstractAnimation::DeleteWhenStopped);
			}
		}
		return false;
	}
	bool m_enabled = true;
};

QPointer<DialogFader> g_fader;

} // namespace

void installEffects(QApplication &app)
{
	const bool on = animationsEnabled();
	QApplication::setEffectEnabled(Qt::UI_General, on);
	QApplication::setEffectEnabled(Qt::UI_FadeMenu, on);
	QApplication::setEffectEnabled(Qt::UI_AnimateCombo, on);
	QApplication::setEffectEnabled(Qt::UI_FadeTooltip, on);
	if (!g_fader) {
		g_fader = new DialogFader(&app);
		app.installEventFilter(g_fader);
	}
	g_fader->m_enabled = on;
}

} // namespace UiBits

StatusChip::StatusChip(const QString &objectName, const QColor &dot, QWidget *parent) : QWidget(parent), m_dot(dot)
{
	setObjectName(objectName);
	setAttribute(Qt::WA_StyledBackground);
	auto *lay = new QHBoxLayout(this);
	lay->setContentsMargins(24, 3, 10, 3); // room for the dot on the left
	m_label = new QLabel;
	lay->addWidget(m_label);

	m_pulse = new QVariantAnimation(this);
	m_pulse->setStartValue(1.0);
	m_pulse->setKeyValueAt(0.5, 0.35);
	m_pulse->setEndValue(1.0);
	m_pulse->setDuration(1600);
	m_pulse->setLoopCount(-1);
	m_pulse->setEasingCurve(QEasingCurve::InOutSine);
	connect(m_pulse, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) {
		m_glow = v.toReal();
		update(QRect(0, 0, 24, height())); // only the dot
	});
	hide();
}

void StatusChip::setText(const QString &text)
{
	if (m_label->text() != text)
		m_label->setText(text);
}

void StatusChip::setActive(bool on)
{
	if (on == isVisible())
		return;
	setVisible(on);
	if (on && UiBits::animationsEnabled())
		m_pulse->start();
	else {
		m_pulse->stop();
		m_glow = 1.0;
	}
}

void StatusChip::paintEvent(QPaintEvent *e)
{
	QWidget::paintEvent(e); // stylesheet pill
	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing);
	const QPointF c(13, height() / 2.0);
	QColor halo = m_dot;
	halo.setAlphaF(0.30 * m_glow);
	p.setPen(Qt::NoPen);
	p.setBrush(halo);
	p.drawEllipse(c, 4.0 + 3.0 * m_glow, 4.0 + 3.0 * m_glow);
	QColor core = m_dot;
	core.setAlphaF(0.55 + 0.45 * m_glow);
	p.setBrush(core);
	p.drawEllipse(c, 4.0, 4.0);
}
