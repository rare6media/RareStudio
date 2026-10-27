#include "FiltersDialog.h"

#include "PropertiesDialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QLineEdit>
#include <QVBoxLayout>

#include <algorithm>

namespace {

// Filters that make sense for this source: video filters need a video source, audio filters an
// audio source.
QList<QPair<QString, QString>> filtersFor(obs_source_t *source)
{
	const uint32_t flags = obs_source_get_output_flags(source);
	QList<QPair<QString, QString>> out;
	const char *id = nullptr;
	for (size_t i = 0; obs_enum_filter_types(i, &id); ++i) {
		const uint32_t f = obs_get_source_output_flags(id);
		const bool video = (f & OBS_SOURCE_VIDEO) && (flags & (OBS_SOURCE_VIDEO | OBS_SOURCE_ASYNC));
		const bool audio = (f & OBS_SOURCE_AUDIO) && (flags & OBS_SOURCE_AUDIO);
		if (!video && !audio)
			continue;
		const char *dn = obs_source_get_display_name(id);
		out.append({QString::fromUtf8(dn ? dn : id), QString::fromUtf8(id)});
	}
	std::sort(out.begin(), out.end());
	return out;
}

} // namespace

FiltersDialog::FiltersDialog(obs_source_t *source, QWidget *parent)
	: QDialog(parent), m_source(obs_source_get_ref(source))
{
	setWindowTitle(QString("Filters - %1").arg(obs_source_get_name(source)));
	resize(460, 420);

	m_list = new QListWidget;
	m_types = new QComboBox;
	for (const auto &f : filtersFor(source))
		m_types->addItem(f.first, f.second);

	auto *add = new QPushButton("Add");
	auto *props = new QPushButton("Properties");
	auto *up = new QPushButton("▲");
	auto *down = new QPushButton("▼");
	auto *rem = new QPushButton("✕");
	up->setToolTip("Move up (applied earlier)");
	down->setToolTip("Move down (applied later)");
	rem->setToolTip("Remove filter");
	for (auto *b : {up, down, rem})
		b->setFixedWidth(34);

	auto *addRow = new QHBoxLayout;
	addRow->addWidget(m_types, 1);
	addRow->addWidget(add);
	auto *btnRow = new QHBoxLayout;
	btnRow->addWidget(props);
	btnRow->addStretch();
	btnRow->addWidget(up);
	btnRow->addWidget(down);
	btnRow->addWidget(rem);

	auto *box = new QDialogButtonBox(QDialogButtonBox::Close);
	auto *lay = new QVBoxLayout(this);
	lay->addWidget(new QLabel("Filters run top to bottom. Untick to disable one without removing it."));
	lay->addWidget(m_list, 1);
	lay->addLayout(btnRow);
	lay->addLayout(addRow);
	lay->addWidget(box);

	connect(box, &QDialogButtonBox::rejected, this, &QDialog::accept);
	connect(add, &QPushButton::clicked, this, &FiltersDialog::addFilter);
	connect(rem, &QPushButton::clicked, this, &FiltersDialog::removeFilter);
	connect(up, &QPushButton::clicked, this, [this] { moveFilter(OBS_ORDER_MOVE_UP); });
	connect(down, &QPushButton::clicked, this, [this] { moveFilter(OBS_ORDER_MOVE_DOWN); });
	connect(props, &QPushButton::clicked, this, &FiltersDialog::editFilter);
	connect(m_list, &QListWidget::itemDoubleClicked, this, &FiltersDialog::editFilter);
	connect(m_list, &QListWidget::itemChanged, this, [this](QListWidgetItem *it) {
		if (obs_source_t *f = obs_source_get_filter_by_name(m_source, it->text().toUtf8().constData())) {
			obs_source_set_enabled(f, it->checkState() == Qt::Checked);
			obs_source_release(f);
		}
	});

	refresh();
}

FiltersDialog::~FiltersDialog()
{
	obs_source_release(m_source);
}

void FiltersDialog::refresh(const QString &select)
{
	QSignalBlocker block(m_list);
	m_list->clear();
	obs_source_enum_filters(
		m_source,
		[](obs_source_t *, obs_source_t *filter, void *p) {
			auto *list = static_cast<QListWidget *>(p);
			auto *it = new QListWidgetItem(QString::fromUtf8(obs_source_get_name(filter)));
			it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
			it->setCheckState(obs_source_enabled(filter) ? Qt::Checked : Qt::Unchecked);
			list->addItem(it);
		},
		m_list);
	if (!select.isEmpty()) {
		const auto found = m_list->findItems(select, Qt::MatchExactly);
		if (!found.isEmpty())
			m_list->setCurrentItem(found.first());
	}
}

obs_source_t *FiltersDialog::currentFilter() const
{
	QListWidgetItem *it = m_list->currentItem();
	return it ? obs_source_get_filter_by_name(m_source, it->text().toUtf8().constData()) : nullptr;
}

void FiltersDialog::addFilter()
{
	if (m_types->count() == 0)
		return;
	const QString id = m_types->currentData().toString();
	bool ok = false;
	QString name = QInputDialog::getText(this, "Add filter", "Name:", QLineEdit::Normal, m_types->currentText(), &ok);
	if (!ok || name.isEmpty())
		return;
	// Names are unique per source; add a number if needed.
	const QString base = name;
	for (int n = 2; obs_source_t *dup = obs_source_get_filter_by_name(m_source, name.toUtf8().constData()); ++n) {
		obs_source_release(dup);
		name = QString("%1 %2").arg(base).arg(n);
	}
	obs_source_t *filter = obs_source_create(id.toUtf8().constData(), name.toUtf8().constData(), nullptr, nullptr);
	if (!filter)
		return;
	obs_source_filter_add(m_source, filter);
	refresh(name);
	PropertiesDialog(filter, this).exec();
	obs_source_release(filter);
}

void FiltersDialog::removeFilter()
{
	if (obs_source_t *f = currentFilter()) {
		obs_source_filter_remove(m_source, f);
		obs_source_release(f);
		refresh();
	}
}

void FiltersDialog::moveFilter(obs_order_movement dir)
{
	if (obs_source_t *f = currentFilter()) {
		const QString name = QString::fromUtf8(obs_source_get_name(f));
		obs_source_filter_set_order(m_source, f, dir);
		obs_source_release(f);
		refresh(name);
	}
}

void FiltersDialog::editFilter()
{
	if (obs_source_t *f = currentFilter()) {
		PropertiesDialog(f, this).exec();
		obs_source_release(f);
	}
}
