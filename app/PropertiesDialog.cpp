#include "PropertiesDialog.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QTimer>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

PropertiesDialog::PropertiesDialog(obs_source_t *source, QWidget *parent) : QDialog(parent), m_source(source)
{
	setWindowTitle(QString("Properties - %1").arg(obs_source_get_name(source)));
	resize(460, 0);

	m_settings = obs_source_get_settings(source);
	m_original = obs_data_create();
	obs_data_apply(m_original, m_settings); // what Cancel goes back to
	m_props = obs_source_properties(source);

	// Edits are applied shortly after they stop, so dragging a slider doesn't re-init the source on every tick.
	m_live = new QTimer(this);
	m_live->setSingleShot(true);
	m_live->setInterval(90);
	connect(m_live, &QTimer::timeout, this, &PropertiesDialog::apply);

	auto *outer = new QVBoxLayout(this);
	m_form = new QFormLayout;
	outer->addLayout(m_form);
	auto *hint = new QLabel("Changes apply live. Cancel puts everything back.");
	hint->setStyleSheet("color: #9a9ab0;");
	outer->addWidget(hint);
	auto *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	outer->addWidget(box);
	connect(box, &QDialogButtonBox::accepted, this, [this] {
		m_live->stop();
		apply();
		accept();
	});
	connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
	connect(this, &QDialog::rejected, this, &PropertiesDialog::restoreOriginal); // also covers the window's X

	rebuild();
}

PropertiesDialog::~PropertiesDialog()
{
	obs_properties_destroy(m_props);
	obs_data_release(m_settings);
	obs_data_release(m_original);
}

void PropertiesDialog::restoreOriginal()
{
	m_live->stop();
	obs_data_clear(m_settings);
	obs_data_apply(m_settings, m_original);
	obs_source_update(m_source, m_settings);
}

void PropertiesDialog::hook(QWidget *w)
{
	auto go = [this] { m_live->start(); };
	if (auto *c = qobject_cast<QCheckBox *>(w))
		connect(c, &QCheckBox::toggled, this, go);
	else if (auto *s = qobject_cast<QSpinBox *>(w))
		connect(s, &QSpinBox::valueChanged, this, go);
	else if (auto *d = qobject_cast<QDoubleSpinBox *>(w))
		connect(d, &QDoubleSpinBox::valueChanged, this, go);
	else if (auto *cb = qobject_cast<QComboBox *>(w))
		connect(cb, &QComboBox::currentIndexChanged, this, go);
	else if (auto *e = qobject_cast<QLineEdit *>(w))
		connect(e, &QLineEdit::editingFinished, this, go); // not per keystroke: URLs/paths would reload each time
}

void PropertiesDialog::rebuild()
{
	for (obs_property_t *p = obs_properties_first(m_props); p; obs_property_next(&p)) {
		if (!obs_property_visible(p))
			continue;
		const char *name = obs_property_name(p);
		const QString label = QString::fromUtf8(obs_property_description(p));
		QWidget *w = nullptr;

		switch (obs_property_get_type(p)) {
		case OBS_PROPERTY_BOOL: {
			auto *c = new QCheckBox;
			c->setChecked(obs_data_get_bool(m_settings, name));
			w = c;
			break;
		}
		case OBS_PROPERTY_INT: {
			auto *s = new QSpinBox;
			s->setRange(obs_property_int_min(p), obs_property_int_max(p));
			s->setValue(int(obs_data_get_int(m_settings, name)));
			w = s;
			break;
		}
		case OBS_PROPERTY_FLOAT: {
			auto *s = new QDoubleSpinBox;
			s->setDecimals(2);
			s->setRange(obs_property_float_min(p), obs_property_float_max(p));
			s->setSingleStep(obs_property_float_step(p));
			s->setValue(obs_data_get_double(m_settings, name));
			w = s;
			break;
		}
		case OBS_PROPERTY_COLOR:
		case OBS_PROPERTY_COLOR_ALPHA: {
			// obs_data stores colours as 0xAABBGGRR.
			const quint32 abgr = quint32(obs_data_get_int(m_settings, name));
			auto *b = new QPushButton;
			b->setProperty("abgr", abgr);
			auto paint = [b](quint32 v) {
				const QColor c(int(v & 0xFF), int((v >> 8) & 0xFF), int((v >> 16) & 0xFF));
				b->setText(c.name().toUpper());
				b->setStyleSheet(QString("background:%1; color:%2;").arg(c.name(), c.lightness() > 128 ? "#000" : "#fff"));
			};
			paint(abgr);
			connect(b, &QPushButton::clicked, this, [this, b, paint] {
				const quint32 cur = b->property("abgr").toUInt();
				const QColor start(int(cur & 0xFF), int((cur >> 8) & 0xFF), int((cur >> 16) & 0xFF));
				const QColor c = QColorDialog::getColor(start, this, "Choose colour");
				if (!c.isValid())
					return;
				const quint32 v = 0xFF000000u | (quint32(c.blue()) << 16) | (quint32(c.green()) << 8) | quint32(c.red());
				b->setProperty("abgr", v);
				paint(v);
				m_live->start();
			});
			w = b;
			break;
		}
		case OBS_PROPERTY_TEXT:
		case OBS_PROPERTY_PATH: {
			auto *e = new QLineEdit(QString::fromUtf8(obs_data_get_string(m_settings, name)));
			w = e;
			if (obs_property_get_type(p) == OBS_PROPERTY_PATH) {
				auto *row = new QWidget;
				auto *h = new QHBoxLayout(row);
				h->setContentsMargins(0, 0, 0, 0);
				auto *b = new QPushButton("...");
				h->addWidget(e);
				h->addWidget(b);
				connect(b, &QPushButton::clicked, this, [this, e] {
					const QString f = QFileDialog::getOpenFileName(this, "Choose file");
					if (!f.isEmpty())
						e->setText(f);
				});
				hook(e);
				m_widgets.insert(name, e);
				m_form->addRow(label, row);
				continue;
			}
			break;
		}
		case OBS_PROPERTY_LIST: {
			auto *c = new QComboBox;
			const obs_combo_format fmt = obs_property_list_format(p);
			const size_t n = obs_property_list_item_count(p);
			int current = -1;
			for (size_t i = 0; i < n; ++i) {
				const QString text = QString::fromUtf8(obs_property_list_item_name(p, i));
				QVariant val;
				if (fmt == OBS_COMBO_FORMAT_INT)
					val = qlonglong(obs_property_list_item_int(p, i));
				else if (fmt == OBS_COMBO_FORMAT_FLOAT)
					val = obs_property_list_item_float(p, i);
				else
					val = QString::fromUtf8(obs_property_list_item_string(p, i));
				c->addItem(text, val);

				bool match = false;
				if (fmt == OBS_COMBO_FORMAT_INT)
					match = obs_data_get_int(m_settings, name) == val.toLongLong();
				else if (fmt == OBS_COMBO_FORMAT_STRING)
					match = QString::fromUtf8(obs_data_get_string(m_settings, name)) == val.toString();
				if (match)
					current = int(i);
			}
			if (current >= 0)
				c->setCurrentIndex(current);
			c->setProperty("fmt", int(fmt));
			w = c;
			break;
		}
		default:
			continue; // groups, buttons, color pickers etc. are left at defaults for now
		}

		if (!obs_property_enabled(p))
			w->setEnabled(false);
		hook(w);
		m_widgets.insert(name, w);
		m_form->addRow(label, w);
	}
}

void PropertiesDialog::apply()
{
	collect();
	obs_source_update(m_source, m_settings);
}

void PropertiesDialog::collect()
{
	for (auto it = m_widgets.cbegin(); it != m_widgets.cend(); ++it) {
		const QByteArray key = it.key().toUtf8();
		QWidget *w = it.value();
		if (auto *c = qobject_cast<QCheckBox *>(w)) {
			obs_data_set_bool(m_settings, key.constData(), c->isChecked());
		} else if (auto *s = qobject_cast<QSpinBox *>(w)) {
			obs_data_set_int(m_settings, key.constData(), s->value());
		} else if (auto *ds = qobject_cast<QDoubleSpinBox *>(w)) {
			obs_data_set_double(m_settings, key.constData(), ds->value());
		} else if (auto *pb = qobject_cast<QPushButton *>(w); pb && pb->property("abgr").isValid()) {
			obs_data_set_int(m_settings, key.constData(), pb->property("abgr").toUInt());
		} else if (auto *e = qobject_cast<QLineEdit *>(w)) {
			obs_data_set_string(m_settings, key.constData(), e->text().toUtf8().constData());
		} else if (auto *cb = qobject_cast<QComboBox *>(w)) {
			const QVariant v = cb->currentData();
			switch (obs_combo_format(cb->property("fmt").toInt())) {
			case OBS_COMBO_FORMAT_INT:
				obs_data_set_int(m_settings, key.constData(), v.toLongLong());
				break;
			case OBS_COMBO_FORMAT_FLOAT:
				obs_data_set_double(m_settings, key.constData(), v.toDouble());
				break;
			default:
				obs_data_set_string(m_settings, key.constData(), v.toString().toUtf8().constData());
			}
		}
	}
}
