#include "SourcePicker.h"

#include <obs.h>

#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QStyledItemDelegate>
#include <QVBoxLayout>

#include <cmath>
#include <vector>

namespace {

enum class Icon { Monitor, Window, Game, Webcam, Media, Image, Slides, Text, Color, Mic, Speaker, AppAudio, Browser };

struct SourceType {
	const char *id;
	const char *blurb;
	Icon icon;
};

struct Group {
	const char *title;
	std::vector<SourceType> types;
};

const std::vector<Group> kGroups = {
	{"SCREEN & GAMES",
	 {{"monitor_capture", "Your whole monitor", Icon::Monitor},
	  {"window_capture", "One app's window", Icon::Window},
	  {"game_capture", "A game, full-screen or windowed (lowest overhead)", Icon::Game}}},
	{"CAMERA & MEDIA",
	 {{"dshow_input", "Webcam or capture card", Icon::Webcam},
	  {"ffmpeg_source", "A video or audio file, or a stream URL", Icon::Media},
	  {"browser_source", "A web page: alerts, chat, widgets, overlays", Icon::Browser}}},
	{"GRAPHICS",
	 {{"image_source", "A picture or overlay", Icon::Image},
	  {"slideshow", "Several pictures in rotation", Icon::Slides},
	  {"text_gdiplus", "Text that updates as you type", Icon::Text},
	  {"color_source_v3", "A solid colour", Icon::Color}}},
	{"AUDIO",
	 {{"wasapi_input_capture", "A microphone or line-in", Icon::Mic},
	  {"wasapi_output_capture", "Everything a playback device plays", Icon::Speaker},
	  {"wasapi_process_output_capture", "Sound from a single app", Icon::AppAudio}}},
};

enum Role { IdRole = Qt::UserRole, NameRole, BlurbRole, IconRole, HeaderRole };

// A small line icon, drawn in the given colour inside r.
void paintIcon(QPainter &p, Icon icon, const QRectF &r, const QColor &c)
{
	p.save();
	p.setRenderHint(QPainter::Antialiasing);
	QPen pen(c, 1.7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
	p.setPen(pen);
	p.setBrush(Qt::NoBrush);
	const QRectF b = r.adjusted(r.width() * 0.22, r.height() * 0.22, -r.width() * 0.22, -r.height() * 0.22);
	const qreal w = b.width(), h = b.height();
	auto pt = [&](qreal x, qreal y) { return QPointF(b.left() + x * w, b.top() + y * h); };

	switch (icon) {
	case Icon::Monitor:
		p.drawRoundedRect(QRectF(pt(0, 0.05), pt(1, 0.72)), 2, 2);
		p.drawLine(pt(0.5, 0.72), pt(0.5, 0.92));
		p.drawLine(pt(0.28, 0.94), pt(0.72, 0.94));
		break;
	case Icon::Window:
		p.drawRoundedRect(QRectF(pt(0, 0.1), pt(1, 0.9)), 2, 2);
		p.drawLine(pt(0, 0.34), pt(1, 0.34));
		p.drawPoint(pt(0.14, 0.22));
		break;
	case Icon::Game: {
		QPainterPath body;
		body.addRoundedRect(QRectF(pt(0, 0.22), pt(1, 0.8)), h * 0.28, h * 0.28);
		p.drawPath(body);
		p.drawLine(pt(0.24, 0.42), pt(0.24, 0.62));
		p.drawLine(pt(0.14, 0.52), pt(0.34, 0.52));
		p.drawPoint(pt(0.68, 0.46));
		p.drawPoint(pt(0.8, 0.58));
		break;
	}
	case Icon::Webcam:
		p.drawEllipse(QRectF(pt(0.08, 0), pt(0.92, 0.84)));
		p.drawEllipse(QRectF(pt(0.34, 0.26), pt(0.66, 0.58)));
		p.drawLine(pt(0.5, 0.84), pt(0.5, 0.98));
		p.drawLine(pt(0.28, 0.98), pt(0.72, 0.98));
		break;
	case Icon::Media: {
		p.drawRoundedRect(QRectF(pt(0, 0.1), pt(1, 0.9)), 3, 3);
		QPainterPath tri;
		tri.moveTo(pt(0.4, 0.3));
		tri.lineTo(pt(0.4, 0.7));
		tri.lineTo(pt(0.72, 0.5));
		tri.closeSubpath();
		p.setBrush(c);
		p.drawPath(tri);
		break;
	}
	case Icon::Image: {
		p.drawRoundedRect(QRectF(pt(0, 0.1), pt(1, 0.9)), 3, 3);
		QPainterPath hills;
		hills.moveTo(pt(0.06, 0.82));
		hills.lineTo(pt(0.36, 0.46));
		hills.lineTo(pt(0.56, 0.68));
		hills.lineTo(pt(0.7, 0.54));
		hills.lineTo(pt(0.94, 0.82));
		p.drawPath(hills);
		p.drawEllipse(pt(0.74, 0.28), 2.0, 2.0);
		break;
	}
	case Icon::Slides:
		p.drawRoundedRect(QRectF(pt(0.14, 0), pt(1, 0.66)), 2, 2);
		p.drawRoundedRect(QRectF(pt(0, 0.3), pt(0.86, 1)), 2, 2);
		break;
	case Icon::Text: {
		QFont f = p.font();
		f.setPixelSize(int(h * 1.15));
		f.setBold(true);
		p.setFont(f);
		p.setPen(c);
		p.drawText(b, Qt::AlignCenter, "T");
		break;
	}
	case Icon::Color: {
		QLinearGradient g(b.topLeft(), b.bottomRight());
		g.setColorAt(0, QColor("#7c3aed"));
		g.setColorAt(1, QColor("#ec4899"));
		p.setPen(Qt::NoPen);
		p.setBrush(g);
		p.drawRoundedRect(QRectF(pt(0.04, 0.04), pt(0.96, 0.96)), 4, 4);
		break;
	}
	case Icon::Mic:
		p.drawRoundedRect(QRectF(pt(0.34, 0), pt(0.66, 0.58)), w * 0.16, w * 0.16);
		p.drawArc(QRectF(pt(0.14, 0.2), pt(0.86, 0.8)), 180 * 16, 180 * 16);
		p.drawLine(pt(0.5, 0.8), pt(0.5, 1));
		p.drawLine(pt(0.32, 1), pt(0.68, 1));
		break;
	case Icon::Speaker:
	case Icon::AppAudio: {
		QPainterPath sp;
		sp.moveTo(pt(0, 0.36));
		sp.lineTo(pt(0.26, 0.36));
		sp.lineTo(pt(0.5, 0.14));
		sp.lineTo(pt(0.5, 0.86));
		sp.lineTo(pt(0.26, 0.64));
		sp.lineTo(pt(0, 0.64));
		sp.closeSubpath();
		p.drawPath(sp);
		p.drawArc(QRectF(pt(0.36, 0.3), pt(0.8, 0.7)), -50 * 16, 100 * 16);
		p.drawArc(QRectF(pt(0.36, 0.1), pt(1.1, 0.9)), -50 * 16, 100 * 16);
		if (icon == Icon::AppAudio) {
			p.setBrush(c);
			p.drawRoundedRect(QRectF(pt(0.64, 0.78), pt(1, 1)), 1.5, 1.5);
		}
		break;
	}
	case Icon::Browser:
		p.drawEllipse(QRectF(pt(0, 0), pt(1, 1)));
		p.drawEllipse(QRectF(pt(0.3, 0), pt(0.7, 1)));
		p.drawLine(pt(0, 0.5), pt(1, 0.5));
		p.drawLine(pt(0.1, 0.25), pt(0.9, 0.25));
		p.drawLine(pt(0.1, 0.75), pt(0.9, 0.75));
		break;
	}
	p.restore();
}

class PickerDelegate : public QStyledItemDelegate {
public:
	using QStyledItemDelegate::QStyledItemDelegate;

	QSize sizeHint(const QStyleOptionViewItem &, const QModelIndex &i) const override
	{
		return {0, i.data(HeaderRole).isValid() ? 30 : 54};
	}

	void paint(QPainter *p, const QStyleOptionViewItem &opt, const QModelIndex &i) const override
	{
		p->save();
		p->setRenderHint(QPainter::Antialiasing);
		const QRect r = opt.rect.adjusted(2, 1, -2, -1);

		if (i.data(HeaderRole).isValid()) {
			QFont f = opt.font;
			f.setPixelSize(10);
			f.setBold(true);
			f.setLetterSpacing(QFont::AbsoluteSpacing, 1.4);
			p->setFont(f);
			p->setPen(QColor("#8a7bc8"));
			p->drawText(r.adjusted(8, 8, 0, 0), Qt::AlignLeft | Qt::AlignVCenter, i.data(HeaderRole).toString());
			p->restore();
			return;
		}

		const bool sel = opt.state & QStyle::State_Selected;
		const bool hot = opt.state & QStyle::State_MouseOver;
		if (sel) {
			QLinearGradient g(r.topLeft(), r.topRight());
			g.setColorAt(0, QColor("#5b21b6"));
			g.setColorAt(1, QColor("#7c3aed"));
			p->setPen(Qt::NoPen);
			p->setBrush(g);
			p->drawRoundedRect(r, 9, 9);
		} else if (hot) {
			p->setPen(Qt::NoPen);
			p->setBrush(QColor("#1d1d28"));
			p->drawRoundedRect(r, 9, 9);
		}

		const QRect tile(r.left() + 8, r.top() + (r.height() - 36) / 2, 36, 36);
		p->setPen(sel ? QColor(255, 255, 255, 50) : QColor("#2f2f3d"));
		p->setBrush(sel ? QColor(255, 255, 255, 30) : QColor("#1f1f2a"));
		p->drawRoundedRect(tile, 10, 10);
		paintIcon(*p, Icon(i.data(IconRole).toInt()), tile, sel ? QColor("#ffffff") : QColor("#c4b5fd"));

		const int tx = tile.right() + 14;
		QFont title = opt.font;
		title.setBold(true);
		title.setPixelSize(13);
		p->setFont(title);
		p->setPen(QColor("#ececf4"));
		p->drawText(QRect(tx, r.top() + 7, r.right() - tx - 8, 18), Qt::AlignLeft | Qt::AlignVCenter,
			    i.data(NameRole).toString());
		QFont sub = opt.font;
		sub.setPixelSize(11);
		p->setFont(sub);
		p->setPen(sel ? QColor("#e4dcff") : QColor("#8f8fa6"));
		const QString blurb = QFontMetrics(sub).elidedText(i.data(BlurbRole).toString(), Qt::ElideRight, r.right() - tx - 8);
		p->drawText(QRect(tx, r.top() + 26, r.right() - tx - 8, 16), Qt::AlignLeft | Qt::AlignVCenter, blurb);
		p->restore();
	}
};

} // namespace

bool pickSource(QWidget *parent, const std::function<QString(const QString &)> &makeUnique, QByteArray *id, QString *name)
{
	QDialog dlg(parent);
	dlg.setWindowTitle("Add source");
	dlg.resize(500, 600);

	auto *types = new QListWidget;
	types->setObjectName("typeList");
	types->setMouseTracking(true);
	types->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
	types->setItemDelegate(new PickerDelegate(types));
	types->setSpacing(1);
	for (const Group &g : kGroups) {
		bool headed = false;
		for (const SourceType &t : g.types) {
			const char *dn = obs_source_get_display_name(t.id);
			if (!dn)
				continue; // plugin not available on this machine
			if (!headed) {
				auto *h = new QListWidgetItem;
				h->setData(HeaderRole, QString::fromUtf8(g.title));
				h->setFlags(Qt::NoItemFlags);
				types->addItem(h);
				headed = true;
			}
			auto *it = new QListWidgetItem;
			it->setData(IdRole, QByteArray(t.id));
			it->setData(NameRole, QString::fromUtf8(dn));
			it->setData(BlurbRole, QString::fromUtf8(t.blurb));
			it->setData(IconRole, int(t.icon));
			it->setToolTip(QString::fromUtf8(t.blurb));
			types->addItem(it);
		}
	}
	for (int i = 0; i < types->count(); ++i)
		if (types->item(i)->flags() & Qt::ItemIsSelectable) {
			types->setCurrentRow(i);
			break;
		}

	auto *nameEdit = new QLineEdit;
	auto *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	box->button(QDialogButtonBox::Ok)->setText("Add");
	auto *form = new QFormLayout;
	form->addRow("Name", nameEdit);
	auto *lay = new QVBoxLayout(&dlg);
	lay->setContentsMargins(16, 16, 16, 12);
	lay->setSpacing(12);
	lay->addWidget(types, 1);
	lay->addLayout(form);
	lay->addWidget(box);

	// The name follows the type until you type your own.
	bool customName = false;
	auto suggest = [&] {
		if (!customName && types->currentItem())
			nameEdit->setText(makeUnique(types->currentItem()->data(NameRole).toString()));
	};
	suggest();
	QObject::connect(nameEdit, &QLineEdit::textEdited, &dlg, [&] { customName = true; });
	QObject::connect(types, &QListWidget::currentRowChanged, &dlg, suggest);
	QObject::connect(types, &QListWidget::itemDoubleClicked, &dlg, &QDialog::accept);
	QObject::connect(box, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
	QObject::connect(box, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
	if (dlg.exec() != QDialog::Accepted || !types->currentItem())
		return false;

	*id = types->currentItem()->data(IdRole).toByteArray();
	const QString wanted = nameEdit->text().trimmed();
	*name = makeUnique(wanted.isEmpty() ? types->currentItem()->data(NameRole).toString() : wanted);
	return true;
}
