#include "ClipsPanel.h"

#include <QDateTime>
#include <QDesktopServices>
#include <QFileInfo>
#include <QListWidget>
#include <QMenu>
#include <QUrl>
#include <QVBoxLayout>

ClipsPanel::ClipsPanel(QWidget *parent) : QWidget(parent)
{
	m_list = new QListWidget;
	m_list->setWordWrap(true);
	m_list->setToolTip("Double-click an entry to open it");
	auto *lay = new QVBoxLayout(this);
	lay->setContentsMargins(6, 6, 6, 6);
	lay->addWidget(m_list);

	m_list->setContextMenuPolicy(Qt::CustomContextMenu);
	connect(m_list, &QWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
		QListWidgetItem *it = m_list->itemAt(pos);
		const QString target = it ? it->data(Qt::UserRole + 1).toString() : QString();
		if (target.isEmpty() || !QFileInfo::exists(target))
			return;
		static const QStringList kVideo = {"mp4", "mkv", "mov", "flv", "ts"};
		QMenu menu;
		if (kVideo.contains(QFileInfo(target).suffix().toLower()))
			menu.addAction("Edit / trim...", this, [this, target] { emit editRequested(target); });
		menu.addAction("Open", this, [target] { QDesktopServices::openUrl(QUrl::fromLocalFile(target)); });
		menu.addAction("Show in folder", this, [target] { QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(target).absolutePath())); });
		menu.exec(m_list->viewport()->mapToGlobal(pos));
	});
	connect(m_list, &QListWidget::itemDoubleClicked, this, [](QListWidgetItem *it) {
		const QString target = it->data(Qt::UserRole + 1).toString();
		if (target.isEmpty())
			return;
		if (QFileInfo::exists(target))
			QDesktopServices::openUrl(QUrl::fromLocalFile(target));
		else
			QDesktopServices::openUrl(QUrl(target));
	});
}

int ClipsPanel::add(const QString &text, const QString &target)
{
	auto *it = new QListWidgetItem;
	it->setData(Qt::UserRole, m_next);
	m_list->insertItem(0, it); // newest on top
	const int id = m_next++;
	update(id, text, target);
	return id;
}

void ClipsPanel::update(int id, const QString &text, const QString &target)
{
	for (int i = 0; i < m_list->count(); ++i) {
		QListWidgetItem *it = m_list->item(i);
		if (it->data(Qt::UserRole).toInt() != id)
			continue;
		if (!it->data(Qt::UserRole + 2).isValid())
			it->setData(Qt::UserRole + 2, QDateTime::currentDateTime().toString("HH:mm:ss"));
		it->setText(QString("%1   %2").arg(it->data(Qt::UserRole + 2).toString(), text));
		if (!target.isEmpty())
			it->setData(Qt::UserRole + 1, target);
		return;
	}
}
