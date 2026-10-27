#include "PropertiesDialog.h"

#include "Autosave.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFontDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSlider>
#include <QSpinBox>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include <cmath>

namespace {

QColor fromAbgr(quint32 v)
{
	return QColor(int(v & 0xFF), int((v >> 8) & 0xFF), int((v >> 16) & 0xFF), int((v >> 24) & 0xFF));
}

quint32 toAbgr(const QColor &c)
{
	return (quint32(c.alpha()) << 24) | (quint32(c.blue()) << 16) | (quint32(c.green()) << 8) | quint32(c.red());
}

QString fontLabel(obs_data_t *font)
{
	if (!font)
		return "Choose font...";
	return QString("%1, %2 pt").arg(QString::fromUtf8(obs_data_get_string(font, "face"))).arg(obs_data_get_int(font, "size"));
}

// "Images (*.png *.jpg);;All files (*.*)": libobs filters use Qt's syntax already.
QString startDir(const QString &current, const char *fallback)
{
	if (!current.isEmpty())
		return current;
	return fallback ? QString::fromUtf8(fallback) : QString();
}

QWidget *row(std::initializer_list<QWidget *> widgets, int stretchIndex = 0)
{
	auto *w = new QWidget;
	auto *h = new QHBoxLayout(w);
	h->setContentsMargins(0, 0, 0, 0);
	h->setSpacing(6);
	int i = 0;
	for (QWidget *c : widgets)
		h->addWidget(c, i++ == stretchIndex ? 1 : 0);
	return w;
}

} // namespace

PropertiesDialog::PropertiesDialog(obs_source_t *source, QWidget *parent)
	: QDialog(parent), m_source(obs_source_get_ref(source))
{
	setWindowTitle(QString("Properties - %1").arg(QString::fromUtf8(obs_source_get_name(source))));
	resize(540, 0);

	m_settings = obs_source_get_settings(source);
	m_original = obs_data_create();
	obs_data_apply(m_original, m_settings); // what Cancel goes back to
	m_props = obs_source_properties(source);
	if (m_props)
		obs_properties_apply_settings(m_props, m_settings); // runs the modified callbacks: correct fields shown

	// Edits are applied shortly after they stop, so dragging a slider doesn't re-init the source on every tick.
	m_live = new QTimer(this);
	m_live->setSingleShot(true);
	m_live->setInterval(120);
	connect(m_live, &QTimer::timeout, this, &PropertiesDialog::apply);

	m_scroll = new QScrollArea;
	m_scroll->setWidgetResizable(true);
	m_scroll->setFrameShape(QFrame::NoFrame);
	m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

	auto *outer = new QVBoxLayout(this);
	outer->setContentsMargins(16, 16, 16, 12);
	outer->setSpacing(10);
	outer->addWidget(m_scroll, 1);
	auto *hint = new QLabel("Changes apply live. Cancel puts everything back.");
	hint->setObjectName("hint");
	outer->addWidget(hint);
	auto *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	outer->addWidget(box);
	connect(box, &QDialogButtonBox::accepted, this, [this] {
		m_live->stop();
		apply();
		Autosave::touch();
		accept();
	});
	connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
	connect(this, &QDialog::rejected, this, &PropertiesDialog::restoreOriginal); // also covers the window's X

	rebuild();
	// Fit the form when it is short; long forms (filters, window capture) scroll instead of growing off screen.
	if (QWidget *body = m_scroll->widget())
		resize(width(), std::min(720, body->sizeHint().height() + 120));
}

PropertiesDialog::~PropertiesDialog()
{
	obs_properties_destroy(m_props);
	obs_data_release(m_settings);
	obs_data_release(m_original);
	obs_source_release(m_source);
}

void PropertiesDialog::restoreOriginal()
{
	m_live->stop();
	obs_data_clear(m_settings);
	obs_data_apply(m_settings, m_original);
	obs_source_update(m_source, m_settings);
}

void PropertiesDialog::apply()
{
	obs_source_update(m_source, m_settings);
}

void PropertiesDialog::edited(const QByteArray &name, bool applyNow)
{
	// A field can show/hide or refill others (media source: Local File <-> URL); lay the form out again then.
	if (obs_property_t *p = m_props ? obs_properties_get(m_props, name.constData()) : nullptr) {
		if (obs_property_modified(p, m_settings) && !m_rebuildQueued) {
			m_rebuildQueued = true;
			QTimer::singleShot(0, this, [this] { // not from inside the signal of a widget we are about to delete
				m_rebuildQueued = false;
				rebuild();
			});
		}
	}
	if (applyNow)
		m_live->start();
}

void PropertiesDialog::rebuild()
{
	const int scrollPos = m_scroll->verticalScrollBar()->value();
	if (QWidget *old = m_scroll->takeWidget())
		old->deleteLater();

	auto *body = new QWidget;
	auto *form = new QFormLayout(body);
	form->setContentsMargins(0, 0, 6, 0);
	form->setHorizontalSpacing(14);
	form->setVerticalSpacing(10);
	form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
	if (m_props)
		addProperties(m_props, form);
	if (form->rowCount() == 0)
		form->addRow(new QLabel("This source has no settings."));
	m_scroll->setWidget(body);
	QTimer::singleShot(0, this, [this, scrollPos] { m_scroll->verticalScrollBar()->setValue(scrollPos); });
}

void PropertiesDialog::addProperties(obs_properties_t *props, QFormLayout *form)
{
	for (obs_property_t *p = obs_properties_first(props); p; obs_property_next(&p)) {
		if (!obs_property_visible(p))
			continue;
		const QString label = QString::fromUtf8(obs_property_description(p));

		if (obs_property_get_type(p) == OBS_PROPERTY_GROUP) {
			const QByteArray name = obs_property_name(p);
			auto *box = new QGroupBox(label);
			auto *inner = new QFormLayout(box);
			inner->setHorizontalSpacing(14);
			inner->setVerticalSpacing(10);
			if (obs_property_group_type(p) == OBS_GROUP_CHECKABLE) {
				box->setCheckable(true);
				box->setChecked(obs_data_get_bool(m_settings, name.constData()));
				connect(box, &QGroupBox::toggled, this, [this, name](bool on) {
					obs_data_set_bool(m_settings, name.constData(), on);
					edited(name);
				});
			}
			addProperties(obs_property_group_content(p), inner);
			box->setEnabled(obs_property_enabled(p));
			form->addRow(box);
			continue;
		}

		bool ownRow = false;
		QWidget *w = makeWidget(p, &ownRow);
		if (!w)
			continue;
		if (const char *tip = obs_property_long_description(p); tip && *tip)
			w->setToolTip(QString::fromUtf8(tip));
		if (!obs_property_enabled(p))
			w->setEnabled(false);
		if (ownRow)
			form->addRow(w);
		else
			form->addRow(label, w);
	}
}

QWidget *PropertiesDialog::makeWidget(obs_property_t *p, bool *ownRow)
{
	const QByteArray name = obs_property_name(p);
	const char *key = name.constData();
	const QString label = QString::fromUtf8(obs_property_description(p));

	switch (obs_property_get_type(p)) {
	case OBS_PROPERTY_BOOL: {
		auto *c = new QCheckBox(label);
		c->setChecked(obs_data_get_bool(m_settings, key));
		connect(c, &QCheckBox::toggled, this, [this, name](bool on) {
			obs_data_set_bool(m_settings, name.constData(), on);
			edited(name);
		});
		*ownRow = true;
		return c;
	}
	case OBS_PROPERTY_INT: {
		auto *s = new QSpinBox;
		s->setRange(obs_property_int_min(p), obs_property_int_max(p));
		s->setSingleStep(std::max(1, obs_property_int_step(p)));
		s->setSuffix(QString::fromUtf8(obs_property_int_suffix(p)));
		s->setValue(int(obs_data_get_int(m_settings, key)));
		connect(s, &QSpinBox::valueChanged, this, [this, name](int v) {
			obs_data_set_int(m_settings, name.constData(), v);
			edited(name);
		});
		if (obs_property_int_type(p) != OBS_NUMBER_SLIDER)
			return s;
		auto *sl = new QSlider(Qt::Horizontal);
		sl->setRange(s->minimum(), s->maximum());
		sl->setSingleStep(s->singleStep());
		sl->setValue(s->value());
		connect(sl, &QSlider::valueChanged, s, &QSpinBox::setValue);
		connect(s, &QSpinBox::valueChanged, sl, [sl](int v) {
			QSignalBlocker b(sl);
			sl->setValue(v);
		});
		s->setFixedWidth(110);
		return row({sl, s});
	}
	case OBS_PROPERTY_FLOAT: {
		auto *s = new QDoubleSpinBox;
		const double step = obs_property_float_step(p) > 0 ? obs_property_float_step(p) : 0.01;
		s->setDecimals(step >= 1.0 ? 0 : step >= 0.1 ? 1 : step >= 0.01 ? 2 : 3);
		s->setRange(obs_property_float_min(p), obs_property_float_max(p));
		s->setSingleStep(step);
		s->setSuffix(QString::fromUtf8(obs_property_float_suffix(p)));
		s->setValue(obs_data_get_double(m_settings, key));
		connect(s, &QDoubleSpinBox::valueChanged, this, [this, name](double v) {
			obs_data_set_double(m_settings, name.constData(), v);
			edited(name);
		});
		if (obs_property_float_type(p) != OBS_NUMBER_SLIDER)
			return s;
		// Slider positions are whole steps from the minimum.
		const double lo = s->minimum();
		const int steps = int(std::lround((s->maximum() - lo) / step));
		auto *sl = new QSlider(Qt::Horizontal);
		sl->setRange(0, std::max(1, steps));
		sl->setValue(int(std::lround((s->value() - lo) / step)));
		connect(sl, &QSlider::valueChanged, s, [s, lo, step](int pos) { s->setValue(lo + pos * step); });
		connect(s, &QDoubleSpinBox::valueChanged, sl, [sl, lo, step](double v) {
			QSignalBlocker b(sl);
			sl->setValue(int(std::lround((v - lo) / step)));
		});
		s->setFixedWidth(110);
		return row({sl, s});
	}
	case OBS_PROPERTY_COLOR:
	case OBS_PROPERTY_COLOR_ALPHA: {
		// obs_data stores colours as 0xAABBGGRR.
		const bool alpha = obs_property_get_type(p) == OBS_PROPERTY_COLOR_ALPHA;
		auto *b = new QPushButton;
		auto paint = [b](quint32 v) {
			const QColor c = fromAbgr(v);
			b->setText(c.name().toUpper());
			b->setStyleSheet(QString("background:%1; color:%2;").arg(c.name(), c.lightness() > 128 ? "#000" : "#fff"));
		};
		paint(quint32(obs_data_get_int(m_settings, key)) | (alpha ? 0 : 0xFF000000u));
		connect(b, &QPushButton::clicked, this, [this, b, paint, name, alpha] {
			const QColor start = fromAbgr(quint32(obs_data_get_int(m_settings, name.constData())));
			const QColor c = QColorDialog::getColor(start, this, "Choose colour",
								alpha ? QColorDialog::ShowAlphaChannel : QColorDialog::ColorDialogOptions());
			if (!c.isValid())
				return;
			QColor out = c;
			if (!alpha)
				out.setAlpha(255);
			obs_data_set_int(m_settings, name.constData(), toAbgr(out));
			paint(toAbgr(out));
			edited(name);
		});
		return b;
	}
	case OBS_PROPERTY_TEXT: {
		const QString value = QString::fromUtf8(obs_data_get_string(m_settings, key));
		switch (obs_property_text_type(p)) {
		case OBS_TEXT_INFO: {
			auto *l = new QLabel(value.isEmpty() ? label : label + "  " + value);
			l->setWordWrap(true);
			l->setTextFormat(Qt::PlainText);
			const obs_text_info_type t = obs_property_text_info_type(p);
			l->setObjectName(t == OBS_TEXT_INFO_ERROR ? "error" : t == OBS_TEXT_INFO_WARNING ? "warning" : "hint");
			*ownRow = true;
			return l;
		}
		case OBS_TEXT_MULTILINE: {
			// Text sources: edits show on the canvas as you type.
			auto *e = new QPlainTextEdit(value);
			e->setMinimumHeight(90);
			e->setTabChangesFocus(true);
			connect(e, &QPlainTextEdit::textChanged, this, [this, e, name] {
				obs_data_set_string(m_settings, name.constData(), e->toPlainText().toUtf8().constData());
				edited(name);
			});
			return e;
		}
		default: {
			auto *e = new QLineEdit(value);
			// Written as you type, applied when you finish: a URL or path would otherwise reload per keystroke.
			connect(e, &QLineEdit::textEdited, this, [this, name](const QString &t) {
				obs_data_set_string(m_settings, name.constData(), t.toUtf8().constData());
			});
			connect(e, &QLineEdit::editingFinished, this, [this, name] { edited(name); });
			if (obs_property_text_type(p) != OBS_TEXT_PASSWORD)
				return e;
			e->setEchoMode(QLineEdit::Password);
			auto *show = new QPushButton("Show");
			show->setCheckable(true);
			connect(show, &QPushButton::toggled, e, [e, show](bool on) {
				e->setEchoMode(on ? QLineEdit::Normal : QLineEdit::Password);
				show->setText(on ? "Hide" : "Show");
			});
			return row({e, show});
		}
		}
	}
	case OBS_PROPERTY_PATH: {
		auto *e = new QLineEdit(QString::fromUtf8(obs_data_get_string(m_settings, key)));
		auto *b = new QPushButton("Browse...");
		const obs_path_type type = obs_property_path_type(p);
		const QString filter = QString::fromUtf8(obs_property_path_filter(p));
		const QByteArray def = obs_property_path_default_path(p);
		connect(e, &QLineEdit::textEdited, this, [this, name](const QString &t) {
			obs_data_set_string(m_settings, name.constData(), t.toUtf8().constData());
		});
		connect(e, &QLineEdit::editingFinished, this, [this, name] { edited(name); });
		connect(b, &QPushButton::clicked, this, [this, e, type, filter, def, name] {
			const QString dir = startDir(e->text(), def.isEmpty() ? nullptr : def.constData());
			QString f;
			if (type == OBS_PATH_DIRECTORY)
				f = QFileDialog::getExistingDirectory(this, "Choose folder", dir);
			else if (type == OBS_PATH_FILE_SAVE)
				f = QFileDialog::getSaveFileName(this, "Save as", dir, filter);
			else
				f = QFileDialog::getOpenFileName(this, "Choose file", dir, filter);
			if (f.isEmpty())
				return;
			e->setText(f);
			obs_data_set_string(m_settings, name.constData(), f.toUtf8().constData());
			edited(name);
		});
		return row({e, b});
	}
	case OBS_PROPERTY_LIST: {
		auto *c = new QComboBox;
		const obs_combo_format fmt = obs_property_list_format(p);
		const bool editable = obs_property_list_type(p) == OBS_COMBO_TYPE_EDITABLE;
		c->setEditable(editable);
		const size_t n = obs_property_list_item_count(p);
		int current = -1;
		for (size_t i = 0; i < n; ++i) {
			QVariant val;
			bool match = false;
			switch (fmt) {
			case OBS_COMBO_FORMAT_INT:
				val = qlonglong(obs_property_list_item_int(p, i));
				match = obs_data_get_int(m_settings, key) == val.toLongLong();
				break;
			case OBS_COMBO_FORMAT_FLOAT:
				val = obs_property_list_item_float(p, i);
				match = std::abs(obs_data_get_double(m_settings, key) - val.toDouble()) < 1e-6;
				break;
			case OBS_COMBO_FORMAT_BOOL:
				val = obs_property_list_item_bool(p, i);
				match = obs_data_get_bool(m_settings, key) == val.toBool();
				break;
			default:
				val = QString::fromUtf8(obs_property_list_item_string(p, i));
				match = QString::fromUtf8(obs_data_get_string(m_settings, key)) == val.toString();
			}
			c->addItem(QString::fromUtf8(obs_property_list_item_name(p, i)), val);
			if (obs_property_list_item_disabled(p, i))
				c->setItemData(c->count() - 1, 0, Qt::UserRole - 1); // greyed out, like OBS
			if (match)
				current = int(i);
		}
		if (current >= 0)
			c->setCurrentIndex(current);
		else if (editable)
			c->setEditText(QString::fromUtf8(obs_data_get_string(m_settings, key)));
		else
			c->setCurrentIndex(-1); // saved value not offered (device unplugged...): show nothing rather than lie

		auto store = [this, c, fmt, name] {
			const QVariant v = c->currentData();
			switch (fmt) {
			case OBS_COMBO_FORMAT_INT: obs_data_set_int(m_settings, name.constData(), v.toLongLong()); break;
			case OBS_COMBO_FORMAT_FLOAT: obs_data_set_double(m_settings, name.constData(), v.toDouble()); break;
			case OBS_COMBO_FORMAT_BOOL: obs_data_set_bool(m_settings, name.constData(), v.toBool()); break;
			default:
				obs_data_set_string(m_settings, name.constData(),
						    (v.isValid() ? v.toString() : c->currentText()).toUtf8().constData());
			}
			edited(name);
		};
		connect(c, &QComboBox::activated, this, store);
		if (editable && c->lineEdit())
			connect(c->lineEdit(), &QLineEdit::editingFinished, this, [this, c, name] {
				obs_data_set_string(m_settings, name.constData(), c->currentText().toUtf8().constData());
				edited(name);
			});
		return c;
	}
	case OBS_PROPERTY_BUTTON: {
		auto *b = new QPushButton(label);
		const bool isUrl = obs_property_button_type(p) == OBS_BUTTON_URL;
		const QString url = isUrl ? QString::fromUtf8(obs_property_button_url(p)) : QString();
		connect(b, &QPushButton::clicked, this, [this, name, isUrl, url] {
			if (isUrl) {
				QDesktopServices::openUrl(QUrl(url));
				return;
			}
			apply(); // the button acts on the source, so it should see what is on screen now
			obs_property_t *prop = obs_properties_get(m_props, name.constData());
			if (prop && obs_property_button_clicked(prop, m_source)) {
				// The source changed its own properties (refreshed a device list...): fetch them again.
				obs_properties_destroy(m_props);
				m_props = obs_source_properties(m_source);
				if (m_props)
					obs_properties_apply_settings(m_props, m_settings);
				QTimer::singleShot(0, this, &PropertiesDialog::rebuild);
			}
		});
		*ownRow = true;
		return b;
	}
	case OBS_PROPERTY_FONT: {
		obs_data_t *font = obs_data_get_obj(m_settings, key);
		auto *b = new QPushButton(fontLabel(font));
		obs_data_release(font);
		connect(b, &QPushButton::clicked, this, [this, b, name] {
			obs_data_t *cur = obs_data_get_obj(m_settings, name.constData());
			QFont start;
			if (cur) {
				start.setFamily(QString::fromUtf8(obs_data_get_string(cur, "face")));
				start.setPointSize(std::max(1, int(obs_data_get_int(cur, "size"))));
				const int flags = int(obs_data_get_int(cur, "flags"));
				start.setBold(flags & OBS_FONT_BOLD);
				start.setItalic(flags & OBS_FONT_ITALIC);
				start.setUnderline(flags & OBS_FONT_UNDERLINE);
				start.setStrikeOut(flags & OBS_FONT_STRIKEOUT);
				obs_data_release(cur);
			}
			bool ok = false;
			const QFont f = QFontDialog::getFont(&ok, start, this, "Choose font");
			if (!ok)
				return;
			obs_data_t *out = obs_data_create();
			obs_data_set_string(out, "face", f.family().toUtf8().constData());
			obs_data_set_string(out, "style", f.styleName().toUtf8().constData());
			obs_data_set_int(out, "size", f.pointSize());
			obs_data_set_int(out, "flags", (f.bold() ? OBS_FONT_BOLD : 0) | (f.italic() ? OBS_FONT_ITALIC : 0) |
							       (f.underline() ? OBS_FONT_UNDERLINE : 0) |
							       (f.strikeOut() ? OBS_FONT_STRIKEOUT : 0));
			obs_data_set_obj(m_settings, name.constData(), out);
			b->setText(fontLabel(out));
			obs_data_release(out);
			edited(name);
		});
		return b;
	}
	case OBS_PROPERTY_EDITABLE_LIST: {
		// Slideshow images, VLC playlists...: a list of strings, files or URLs.
		const obs_editable_list_type type = obs_property_editable_list_type(p);
		const QString filter = QString::fromUtf8(obs_property_editable_list_filter(p));
		const QByteArray def = obs_property_editable_list_default_path(p);
		auto *list = new QListWidget;
		list->setMinimumHeight(110);
		list->setSelectionMode(QAbstractItemView::ExtendedSelection);
		if (obs_data_array_t *arr = obs_data_get_array(m_settings, key)) {
			for (size_t i = 0, n = obs_data_array_count(arr); i < n; ++i) {
				obs_data_t *it = obs_data_array_item(arr, i);
				list->addItem(QString::fromUtf8(obs_data_get_string(it, "value")));
				obs_data_release(it);
			}
			obs_data_array_release(arr);
		}
		auto store = [this, list, name] {
			obs_data_array_t *arr = obs_data_array_create();
			for (int i = 0; i < list->count(); ++i) {
				obs_data_t *it = obs_data_create();
				obs_data_set_string(it, "value", list->item(i)->text().toUtf8().constData());
				obs_data_set_bool(it, "selected", false);
				obs_data_set_bool(it, "hidden", false);
				obs_data_array_push_back(arr, it);
				obs_data_release(it);
			}
			obs_data_set_array(m_settings, name.constData(), arr);
			obs_data_array_release(arr);
			edited(name);
		};
		auto *add = new QPushButton(type == OBS_EDITABLE_LIST_TYPE_STRINGS ? "Add..." : "Add files...");
		auto *addUrl = new QPushButton("Add URL...");
		auto *remove = new QPushButton("Remove");
		addUrl->setVisible(type == OBS_EDITABLE_LIST_TYPE_FILES_AND_URLS);
		connect(add, &QPushButton::clicked, this, [this, list, type, filter, def, store] {
			if (type == OBS_EDITABLE_LIST_TYPE_STRINGS) {
				bool ok = false;
				const QString t = QInputDialog::getText(this, "Add", "Value:", QLineEdit::Normal, {}, &ok);
				if (ok && !t.isEmpty())
					list->addItem(t);
			} else {
				list->addItems(QFileDialog::getOpenFileNames(this, "Add files", QString::fromUtf8(def), filter));
			}
			store();
		});
		connect(addUrl, &QPushButton::clicked, this, [this, list, store] {
			bool ok = false;
			const QString t = QInputDialog::getText(this, "Add URL", "URL:", QLineEdit::Normal, {}, &ok);
			if (ok && !t.trimmed().isEmpty()) {
				list->addItem(t.trimmed());
				store();
			}
		});
		connect(remove, &QPushButton::clicked, this, [list, store] {
			qDeleteAll(list->selectedItems());
			store();
		});
		auto *box = new QWidget;
		auto *v = new QVBoxLayout(box);
		v->setContentsMargins(0, 0, 0, 0);
		v->setSpacing(6);
		v->addWidget(list);
		auto *buttons = new QHBoxLayout;
		buttons->addWidget(add);
		buttons->addWidget(addUrl);
		buttons->addWidget(remove);
		buttons->addStretch();
		v->addLayout(buttons);
		return box;
	}
	default:
		return nullptr; // frame-rate pickers and the like keep their defaults
	}
}
