#include "StreamHealth.h"

#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QVBoxLayout>

#include <algorithm>

namespace {

constexpr int kWindow = 10;   // seconds of drop history a verdict looks at
constexpr int kHistory = 90;  // bitrate samples kept for the sparkline

const char *colourFor(StreamHealthPanel::Level l)
{
	return l == StreamHealthPanel::Level::Good ? "#4ade80" : l == StreamHealthPanel::Level::Fair ? "#fbbf24" : "#f87171";
}

QString wordFor(StreamHealthPanel::Level l)
{
	return l == StreamHealthPanel::Level::Good ? "Healthy" : l == StreamHealthPanel::Level::Fair ? "Fair" : "Poor";
}

} // namespace

// Bitrate history for one destination: a filled line scaled to its own recent peak.
class HealthSpark : public QWidget {
public:
	explicit HealthSpark(QWidget *parent = nullptr) : QWidget(parent) { setFixedHeight(34); }

	void push(double kbps)
	{
		m_values.append(kbps);
		while (m_values.size() > kHistory)
			m_values.removeFirst();
		update();
	}
	void setColour(const QColor &c)
	{
		m_colour = c;
		update();
	}

protected:
	void paintEvent(QPaintEvent *) override
	{
		QPainter p(this);
		p.setRenderHint(QPainter::Antialiasing);
		const QRectF r = QRectF(rect()).adjusted(0.5, 2, -0.5, -2);
		p.setPen(QPen(QColor(255, 255, 255, 16), 1));
		p.drawLine(r.bottomLeft(), r.bottomRight());
		if (m_values.size() < 2)
			return;
		const double peak = std::max(1.0, *std::max_element(m_values.begin(), m_values.end()) * 1.15);
		QPainterPath line;
		for (int i = 0; i < m_values.size(); ++i) {
			const QPointF pt(r.left() + r.width() * (kHistory - m_values.size() + i) / double(kHistory - 1),
					 r.bottom() - r.height() * (m_values[i] / peak));
			i ? line.lineTo(pt) : line.moveTo(pt);
		}
		QPainterPath fill = line;
		fill.lineTo(r.left() + r.width(), r.bottom());
		fill.lineTo(r.left() + r.width() * (kHistory - m_values.size()) / double(kHistory - 1), r.bottom());
		fill.closeSubpath();
		QColor f = m_colour;
		f.setAlpha(40);
		p.fillPath(fill, f);
		p.setPen(QPen(m_colour, 1.6));
		p.drawPath(line);
	}

private:
	QList<double> m_values;
	QColor m_colour = QColor(0x8b, 0x5c, 0xf6);
};

StreamHealthPanel::Level StreamHealthPanel::assessDestination(double dropPct, double congestion, bool reconnecting)
{
	if (reconnecting || dropPct >= 2.0 || congestion >= 0.8)
		return Level::Poor;
	if (dropPct >= 0.2 || congestion >= 0.5)
		return Level::Fair;
	return Level::Good;
}

StreamHealthPanel::Level StreamHealthPanel::assessMachine(const Machine &m)
{
	if (m.lagPct >= 10.0 || m.skipPct >= 5.0 || m.cpu >= 95.0)
		return Level::Poor;
	if (m.lagPct >= 2.0 || m.skipPct >= 0.5 || m.cpu >= 85.0)
		return Level::Fair;
	return Level::Good;
}

StreamHealthPanel::StreamHealthPanel(QWidget *parent) : QWidget(parent)
{
	auto *root = new QVBoxLayout(this);
	root->setContentsMargins(12, 12, 12, 12);
	root->setSpacing(10);

	m_banner = new QLabel;
	m_banner->setObjectName("healthBanner");
	m_banner->setWordWrap(true);
	m_banner->setTextFormat(Qt::RichText);
	root->addWidget(m_banner);

	m_machine = new QLabel;
	m_machine->setObjectName("hint");
	m_machine->setTextFormat(Qt::RichText);
	m_machine->setWordWrap(true);
	root->addWidget(m_machine);

	m_empty = new QLabel("Go live to see per-destination bitrate, dropped frames and congestion here.");
	m_empty->setObjectName("empty");
	m_empty->setWordWrap(true);
	root->addWidget(m_empty);

	auto *rowsHost = new QWidget;
	m_rows = new QVBoxLayout(rowsHost);
	m_rows->setContentsMargins(0, 0, 0, 0);
	m_rows->setSpacing(8);
	root->addWidget(rowsHost);
	root->addStretch(1);
	reset();
}

void StreamHealthPanel::reset()
{
	m_live = false;
	for (Row &r : m_byIndex)
		delete r.box;
	m_byIndex.clear();
	m_empty->show();
	m_machine->clear();
	m_level = Level::Good;
	m_verdict.clear();
	m_banner->setText("<span style='color:#8a8aa0'>●</span>&nbsp;&nbsp;<span style='color:#9a9ab0'>Not live</span>");
}

void StreamHealthPanel::setLevel(Level l, const QString &text)
{
	const bool changed = l != m_level || text != m_verdict;
	m_level = l;
	m_verdict = text;
	m_banner->setText(QString("<span style='color:%1;font-size:16px'>●</span>&nbsp;&nbsp;<b style='color:%1'>%2</b>"
				  "<span style='color:#9a9ab0'>&nbsp;&nbsp;%3</span>")
				  .arg(colourFor(l), wordFor(l), text.toHtmlEscaped()));
	if (changed)
		emit levelChanged(l, text);
}

void StreamHealthPanel::tick(const QList<MultiStream::DestStats> &dests, const Machine &machine)
{
	m_live = true;
	m_empty->hide();

	Level worst = Level::Good;
	QString reason;
	auto consider = [&](Level l, const QString &why) {
		if (l > worst || (l == worst && l != Level::Good && reason.isEmpty())) {
			worst = l;
			reason = why;
		}
	};

	QSet<int> seen;
	for (const MultiStream::DestStats &d : dests) {
		seen.insert(d.index);
		Row &row = m_byIndex[d.index];
		if (!row.box) {
			row.box = new QWidget;
			row.box->setObjectName("healthRow");
			row.box->setAttribute(Qt::WA_StyledBackground);
			auto *v = new QVBoxLayout(row.box);
			v->setContentsMargins(12, 10, 12, 8);
			v->setSpacing(4);
			row.title = new QLabel;
			row.title->setTextFormat(Qt::RichText);
			row.detail = new QLabel;
			row.detail->setObjectName("hint");
			row.detail->setTextFormat(Qt::RichText);
			row.spark = new HealthSpark;
			v->addWidget(row.title);
			v->addWidget(row.detail);
			v->addWidget(row.spark);
			m_rows->addWidget(row.box);
		}

		double kbps = 0;
		if (row.haveLast && d.bytes >= row.lastBytes)
			kbps = (d.bytes - row.lastBytes) * 8.0 / 1000.0; // one tick = one second
		row.lastBytes = d.bytes;
		row.haveLast = true;

		row.window.append({d.total, d.dropped});
		while (row.window.size() > kWindow + 1)
			row.window.removeFirst();
		const int frames = row.window.last().first - row.window.first().first;
		const int lost = row.window.last().second - row.window.first().second;
		const double dropPct = frames > 0 ? 100.0 * lost / frames : 0.0;

		const bool settled = d.active || d.reconnecting;
		const Level lvl = settled ? assessDestination(dropPct, d.congestion, d.reconnecting) : Level::Fair;
		QString why;
		if (!settled)
			why = d.name + " is not connected";
		else if (d.reconnecting)
			why = d.name + " lost its connection and is reconnecting";
		else if (dropPct >= 0.2)
			why = QString("%1 is dropping %2% of frames").arg(d.name).arg(dropPct, 0, 'f', 1);
		else if (d.congestion >= 0.5)
			why = QString("%1's connection is congested").arg(d.name);
		consider(lvl, why);

		const QString state = !d.active ? "connecting" : d.reconnecting ? "reconnecting" : "live";
		row.title->setText(QString("<b style='color:%1'>●</b>&nbsp;&nbsp;<b>%2</b><span style='color:#6f6f86'>&nbsp;&nbsp;%3</span>")
					   .arg(colourFor(lvl), d.name.toHtmlEscaped(), state));
		row.detail->setText(QString("%1 kbps &nbsp;·&nbsp; dropped %2 of %3 (%4% recent) &nbsp;·&nbsp; congestion %5%")
					    .arg(kbps, 0, 'f', 0).arg(d.dropped).arg(d.total).arg(dropPct, 0, 'f', 1)
					    .arg(int(d.congestion * 100)));
		row.spark->setColour(QColor(colourFor(lvl)));
		row.spark->push(kbps);
	}
	for (auto it = m_byIndex.begin(); it != m_byIndex.end();) { // destinations that went away
		if (seen.contains(it.key())) {
			++it;
			continue;
		}
		delete it->box;
		it = m_byIndex.erase(it);
	}

	const Level ml = assessMachine(machine);
	if (ml != Level::Good)
		consider(ml, machine.skipPct >= 0.5 ? "the encoder is skipping frames (try a faster preset or a lower resolution)"
			    : machine.lagPct >= 2.0 ? "the renderer is missing frames (the GPU is overloaded)"
						    : "the CPU is nearly maxed out");
	m_machine->setText(QString("CPU <b>%1%</b> &nbsp;·&nbsp; FPS <b>%2</b> &nbsp;·&nbsp; missed render frames <b>%3%</b> &nbsp;·&nbsp; "
				   "encoder skipped <b>%4%</b>")
				   .arg(machine.cpu, 0, 'f', 0).arg(machine.fps, 0, 'f', 0).arg(machine.lagPct, 0, 'f', 1).arg(machine.skipPct, 0, 'f', 1));

	setLevel(worst, worst == Level::Good ? "every destination is delivering cleanly" : reason);
}
