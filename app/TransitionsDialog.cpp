#include "TransitionsDialog.h"

#include "ObsCore.h"
#include "PropertiesDialog.h"
#include "TransitionManager.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

TransitionsDialog::TransitionsDialog(QWidget *parent) : QDialog(parent)
{
	setWindowTitle("Transitions & Stingers");
	resize(720, 520);

	m_list = new QListWidget;
	auto *add = new QPushButton("Add transition...");
	auto *stinger = new QPushButton("Add stinger from video...");
	auto *config = new QPushButton("Configure...");
	auto *use = new QPushButton("Use now");
	auto *dup = new QPushButton("Duplicate");
	auto *ren = new QPushButton("Rename");
	auto *rem = new QPushButton("Remove");

	auto *buttons = new QVBoxLayout;
	for (auto *b : {use, config, add, stinger, dup, ren, rem})
		buttons->addWidget(b);
	buttons->addStretch();

	auto *top = new QHBoxLayout;
	top->addWidget(m_list, 1);
	top->addLayout(buttons);

	m_overrides = new QTableWidget(0, 2);
	m_overrides->setHorizontalHeaderLabels({"Scene", "Transition when switching TO it"});
	m_overrides->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
	m_overrides->verticalHeader()->hide();
	m_overrides->setSelectionMode(QAbstractItemView::NoSelection);

	auto *box = new QDialogButtonBox(QDialogButtonBox::Close);
	auto *lay = new QVBoxLayout(this);
	lay->addWidget(new QLabel("Transitions (the one in the toolbar is used unless a scene overrides it):"));
	lay->addLayout(top, 2);
	lay->addWidget(new QLabel("Per-scene overrides:"));
	lay->addWidget(m_overrides, 1);
	lay->addWidget(box);
	connect(box, &QDialogButtonBox::rejected, this, &QDialog::accept);

	connect(add, &QPushButton::clicked, this, &TransitionsDialog::addTransition);
	connect(stinger, &QPushButton::clicked, this, &TransitionsDialog::addStinger);
	connect(dup, &QPushButton::clicked, this, &TransitionsDialog::duplicate);
	connect(ren, &QPushButton::clicked, this, &TransitionsDialog::rename);
	connect(rem, &QPushButton::clicked, this, &TransitionsDialog::remove);
	connect(config, &QPushButton::clicked, this, &TransitionsDialog::configure);
	connect(m_list, &QListWidget::itemDoubleClicked, this, &TransitionsDialog::configure);
	connect(use, &QPushButton::clicked, this, &TransitionsDialog::makeCurrent);

	refresh();
	buildOverrides();
}

QString TransitionsDialog::selectedName() const
{
	return m_list->currentItem() ? m_list->currentItem()->data(Qt::UserRole).toString() : QString();
}

void TransitionsDialog::refresh()
{
	auto &tm = TransitionManager::instance();
	const QString keep = selectedName().isEmpty() ? tm.current() : selectedName();
	m_list->clear();
	for (const TransitionDef &d : tm.defs()) {
		const char *type = obs_source_get_display_name(d.type.toUtf8().constData());
		auto *it = new QListWidgetItem(QString("%1%2   — %3").arg(d.name == tm.current() ? "▶ " : "", d.name,
										 QString::fromUtf8(type ? type : d.type.toUtf8().constData())));
		it->setData(Qt::UserRole, d.name);
		m_list->addItem(it);
		if (d.name == keep)
			m_list->setCurrentItem(it);
	}
}

void TransitionsDialog::buildOverrides()
{
	auto &tm = TransitionManager::instance();
	m_overrides->setRowCount(0);
	obs_enum_scenes(
		[](void *p, obs_source_t *src) {
			auto *self = static_cast<TransitionsDialog *>(p);
			auto &tm = TransitionManager::instance();
			const QString scene = QString::fromUtf8(obs_source_get_name(src));
			const int r = self->m_overrides->rowCount();
			self->m_overrides->insertRow(r);
			self->m_overrides->setItem(r, 0, new QTableWidgetItem(scene));
			auto *combo = new QComboBox;
			combo->addItem("(use the toolbar transition)", QString());
			for (const QString &n : tm.names())
				combo->addItem(n, n);
			combo->setCurrentIndex(std::max(0, combo->findData(tm.overrideFor(scene))));
			connect(combo, &QComboBox::currentIndexChanged, self, [scene, combo] {
				TransitionManager::instance().setOverride(scene, combo->currentData().toString());
			});
			self->m_overrides->setCellWidget(r, 1, combo);
			return true;
		},
		this);
}

void TransitionsDialog::addTransition()
{
	// Every transition type libobs knows, with its display name.
	QStringList labels;
	QStringList ids;
	const char *id = nullptr;
	for (size_t i = 0; obs_enum_transition_types(i, &id); ++i) {
		const char *dn = obs_source_get_display_name(id);
		labels << QString::fromUtf8(dn ? dn : id);
		ids << QString::fromUtf8(id);
	}
	bool ok = false;
	const QString label = QInputDialog::getItem(this, "Add transition", "Type:", labels, 0, false, &ok);
	if (!ok)
		return;
	const QString name = QInputDialog::getText(this, "Add transition", "Name:", QLineEdit::Normal, label, &ok);
	if (!ok || name.isEmpty())
		return;
	if (!TransitionManager::instance().add({name, ids[labels.indexOf(label)], {}, 600}))
		QMessageBox::warning(this, "RareStudio", "A transition with that name already exists.");
	refresh();
	emit changed();
}

void TransitionsDialog::addStinger()
{
	const QString file = QFileDialog::getOpenFileName(this, "Choose stinger video", QString(),
							  "Video (*.mp4 *.mov *.webm *.mkv *.avi *.gif);;All files (*)");
	if (file.isEmpty())
		return;
	bool ok = false;
	const QString name = QInputDialog::getText(this, "Add stinger", "Name:", QLineEdit::Normal,
						   "Stinger: " + QFileInfo(file).completeBaseName(), &ok);
	if (!ok || name.isEmpty())
		return;

	obs_data_t *s = obs_data_create();
	obs_data_set_string(s, "path", file.toUtf8().constData());
	obs_data_set_int(s, "transition_point", 500);
	obs_data_set_string(s, "tp_type", "time"); // switch scenes 0.5s into the video; edit under Configure
	const QByteArray json(obs_data_get_json(s));
	obs_data_release(s);

	if (!TransitionManager::instance().add({name, "stinger_transition", json, 1500}))
		QMessageBox::warning(this, "RareStudio", "A transition with that name already exists.");
	else
		QMessageBox::information(this, "Stinger added",
					 "Choose Configure to set the transition point, which is the moment in the video "
					 "when the scene swaps (usually when the video fully covers the screen).");
	refresh();
	emit changed();
}

void TransitionsDialog::duplicate()
{
	auto &tm = TransitionManager::instance();
	if (TransitionDef *d = tm.find(selectedName())) {
		TransitionDef copy = *d;
		for (int n = 2; tm.find(copy.name = QString("%1 %2").arg(d->name).arg(n)); ++n) {}
		tm.add(copy);
		refresh();
		emit changed();
	}
}

void TransitionsDialog::rename()
{
	auto &tm = TransitionManager::instance();
	const QString from = selectedName();
	if (from.isEmpty())
		return;
	bool ok = false;
	const QString to = QInputDialog::getText(this, "Rename", "Name:", QLineEdit::Normal, from, &ok);
	if (ok && !to.isEmpty() && !tm.rename(from, to))
		QMessageBox::warning(this, "RareStudio", "That name is already used.");
	refresh();
	buildOverrides();
	emit changed();
}

void TransitionsDialog::remove()
{
	TransitionManager::instance().remove(selectedName());
	refresh();
	buildOverrides();
	emit changed();
}

void TransitionsDialog::makeCurrent()
{
	if (!selectedName().isEmpty()) {
		TransitionManager::instance().setCurrent(selectedName());
		refresh();
		emit changed();
	}
}

void TransitionsDialog::configure()
{
	auto &tm = TransitionManager::instance();
	const QString name = selectedName();
	if (name.isEmpty())
		return;
	tm.setCurrent(name); // the live transition source is what the dialog edits
	if (obs_source_t *src = ObsCore::instance().transitionSource()) {
		PropertiesDialog(src, this).exec();
		tm.captureCurrentSettings();
	}
	refresh();
	emit changed();
}
