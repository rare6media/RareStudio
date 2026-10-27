#include "FiltersDialog.h"

#include "AudioPresets.h"
#include "PropertiesDialog.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMap>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSet>
#include <QVBoxLayout>

#include <algorithm>

namespace {

struct Entry {
	QString id, name, blurb;
};

// Menu sections -> filter types that make sense for this source. Audio filters come from the curated
// list first; anything else libobs offers lands under "Other".
QMap<QString, QList<Entry>> menuFor(obs_source_t *source)
{
	const uint32_t flags = obs_source_get_output_flags(source);
	const bool hasAudio = flags & OBS_SOURCE_AUDIO;
	const bool hasVideo = flags & (OBS_SOURCE_VIDEO | OBS_SOURCE_ASYNC);
	QMap<QString, QList<Entry>> groups;
	QSet<QString> seen;

	if (hasAudio) {
		for (const auto &f : AudioPresets::audioFilters()) {
			groups[hasVideo ? "Audio: " + f.group : f.group].append({f.id, f.name, f.blurb});
			seen.insert(f.id);
		}
	}

	const char *id = nullptr;
	for (size_t i = 0; obs_enum_filter_types(i, &id); ++i) {
		const QString qid = QString::fromUtf8(id);
		if (seen.contains(qid))
			continue;
		const uint32_t f = obs_get_source_output_flags(id);
		const bool video = (f & OBS_SOURCE_VIDEO) && hasVideo;
		const bool audio = (f & OBS_SOURCE_AUDIO) && hasAudio;
		if (!video && !audio)
			continue;
		const char *dn = obs_source_get_display_name(id);
		const QString name = QString::fromUtf8(dn ? dn : id);
		const QString group = video ? AudioPresets::videoGroup(qid) : QString("Other");
		groups[group].append({qid, name, QString()});
		seen.insert(qid);
	}
	for (auto &list : groups)
		std::sort(list.begin(), list.end(), [](const Entry &a, const Entry &b) { return a.name < b.name; });
	return groups;
}

} // namespace

FiltersDialog::FiltersDialog(obs_source_t *source, QWidget *parent)
	: QDialog(parent), m_source(obs_source_get_ref(source)),
	  m_audio(obs_source_get_output_flags(source) & OBS_SOURCE_AUDIO)
{
	setWindowTitle(QString("Filters - %1").arg(obs_source_get_name(source)));
	resize(480, 460);

	m_list = new QListWidget;
	m_hint = new QLabel;
	m_hint->setWordWrap(true);
	m_hint->setStyleSheet("color: #9a9ab0;");

	auto *add = new QPushButton("Add filter");
	auto *addMenu = new QMenu(add);
	addMenu->setToolTipsVisible(true);
	const auto groups = menuFor(source);
	for (auto it = groups.cbegin(); it != groups.cend(); ++it) {
		QMenu *sub = addMenu->addMenu(it.key());
		sub->setToolTipsVisible(true);
		for (const Entry &e : it.value()) {
			QAction *a = sub->addAction(e.name, this, [this, e] { addFilter(e.id, e.name); });
			a->setToolTip(e.blurb);
		}
	}
	add->setMenu(addMenu);
	add->setEnabled(!groups.isEmpty());

	QPushButton *presets = nullptr;
	if (m_audio) {
		presets = new QPushButton("Presets");
		auto *pm = new QMenu(presets);
		pm->setToolTipsVisible(true);
		pm->addAction("Voice chain (noise suppression, gate, compressor, limiter)", this, [this] {
			AudioPresets::applyVoiceChain(m_source);
			refresh();
		})->setToolTip("Recommended for a microphone. Filters you already have are kept.");
		pm->addAction("Desktop limiter", this, [this] {
			AudioPresets::applyDesktopChain(m_source);
			refresh();
		})->setToolTip("Stops loud game or music peaks from clipping the stream.");
		pm->addSeparator();
		pm->addAction("Remove all filters", this, [this] {
			if (QMessageBox::question(this, "Remove all filters", "Remove every filter from this source?") !=
			    QMessageBox::Yes)
				return;
			AudioPresets::clearFilters(m_source);
			refresh();
		});
		presets->setMenu(pm);
	}

	auto *props = new QPushButton("Settings...");
	auto *up = new QPushButton(QString::fromUtf8("\xE2\x96\xB2"));
	auto *down = new QPushButton(QString::fromUtf8("\xE2\x96\xBC"));
	auto *rem = new QPushButton(QString::fromUtf8("\xE2\x9C\x95"));
	up->setToolTip("Move up (applied earlier)");
	down->setToolTip("Move down (applied later)");
	rem->setToolTip("Remove filter");
	for (auto *b : {up, down, rem})
		b->setFixedWidth(34);

	auto *btnRow = new QHBoxLayout;
	btnRow->addWidget(add);
	if (presets)
		btnRow->addWidget(presets);
	btnRow->addWidget(props);
	btnRow->addStretch();
	btnRow->addWidget(up);
	btnRow->addWidget(down);
	btnRow->addWidget(rem);

	auto *box = new QDialogButtonBox(QDialogButtonBox::Close);
	auto *lay = new QVBoxLayout(this);
	lay->addWidget(new QLabel(m_audio ? "Audio filters run top to bottom. Untick one to bypass it."
					  : "Filters run top to bottom. Untick one to bypass it."));
	lay->addWidget(m_list, 1);
	lay->addLayout(btnRow);
	lay->addWidget(m_hint);
	lay->addWidget(box);

	connect(box, &QDialogButtonBox::rejected, this, &QDialog::accept);
	connect(box, &QDialogButtonBox::accepted, this, &QDialog::accept);
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

void FiltersDialog::updateHint()
{
	if (m_list->count() > 0) {
		m_hint->setText("Double-click a filter to adjust it.");
	} else if (m_audio) {
		m_hint->setText("No filters yet. Try Presets > Voice chain for a clean, even microphone in one click.");
	} else {
		m_hint->setText("No filters yet. Use Add filter to start.");
	}
}

void FiltersDialog::refresh(const QString &select)
{
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
				const char *dn = obs_source_get_display_name(obs_source_get_id(filter));
				it->setToolTip(QString::fromUtf8(dn ? dn : ""));
				list->addItem(it);
			},
			m_list);
		if (!select.isEmpty()) {
			const auto found = m_list->findItems(select, Qt::MatchExactly);
			if (!found.isEmpty())
				m_list->setCurrentItem(found.first());
		}
	}
	updateHint();
}

obs_source_t *FiltersDialog::currentFilter() const
{
	QListWidgetItem *it = m_list->currentItem();
	return it ? obs_source_get_filter_by_name(m_source, it->text().toUtf8().constData()) : nullptr;
}

void FiltersDialog::addFilter(const QString &typeId, const QString &name)
{
	obs_source_t *filter = AudioPresets::addFilter(m_source, typeId, name);
	if (!filter)
		return;
	refresh(QString::fromUtf8(obs_source_get_name(filter)));
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
