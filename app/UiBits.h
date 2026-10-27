#pragma once

#include <QWidget>

class QApplication;
class QLabel;
class QVariantAnimation;

// Small look-and-feel pieces shared by the main window and dialogs. Everything animated here is
// cheap (a few dozen pixels repainted, or a window's opacity) and can be switched off in
// Settings > Interface.
namespace UiBits {

double scaleFactor();      // Settings > Interface > size (1.0 = 100%)
bool animationsEnabled();
void installEffects(QApplication &app); // menu/combo/tooltip fades + dialogs fading in

} // namespace UiBits

// "● LIVE 01:02:03" pill: a softly pulsing dot plus a label. Idle (hidden) costs nothing.
class StatusChip : public QWidget {
public:
	StatusChip(const QString &objectName, const QColor &dot, QWidget *parent = nullptr);
	void setText(const QString &text);
	void setActive(bool on); // shows + starts pulsing, or hides + stops

protected:
	void paintEvent(QPaintEvent *) override;

private:
	QColor m_dot;
	QLabel *m_label;
	QVariantAnimation *m_pulse;
	qreal m_glow = 1.0;
};
