#include "Changelog.h"

#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QTextBrowser>
#include <QVBoxLayout>
#include <QVersionNumber>

#include <algorithm>

namespace Changelog {

QList<Entry> entries()
{
	QFile f(":/CHANGELOG.md");
	if (!f.open(QIODevice::ReadOnly))
		return {};
	static const QRegularExpression heading(R"(^##\s+(\d+(?:\.\d+)*)\s*(?:[-\x{2013}\x{2014}]\s*(\S+))?\s*$)");
	QList<Entry> out;
	for (const QString &line : QString::fromUtf8(f.readAll()).split('\n')) {
		const auto m = heading.match(line.trimmed());
		if (m.hasMatch())
			out.append({m.captured(1), m.captured(2), {}});
		else if (!out.isEmpty())
			out.last().markdown += line + "\n";
	}
	for (Entry &e : out)
		e.markdown = e.markdown.trimmed();
	return out;
}

void show(QWidget *parent, const QString &since)
{
	const QString current = RARESTUDIO_VERSION;
	const QVersionNumber seen = QVersionNumber::fromString(since);
	QString md;
	for (const Entry &e : entries()) {
		const QVersionNumber v = QVersionNumber::fromString(e.version);
		if (v > QVersionNumber::fromString(current))
			continue; // notes for a release you are not running
		if (!since.isEmpty() && v <= seen)
			continue;
		md += QString("## %1%2\n\n%3\n\n").arg(e.version, e.date.isEmpty() ? QString() : "  (" + e.date + ")", e.markdown);
	}
	if (md.isEmpty()) {
		const auto all = entries();
		md = all.isEmpty() ? QString("No release notes are available for this build.")
				   : QString("## %1\n\n%2").arg(all.first().version, all.first().markdown);
	}

	auto *dlg = new QDialog(parent);
	dlg->setAttribute(Qt::WA_DeleteOnClose);
	dlg->setWindowTitle("What's new in RareStudio");
	dlg->resize(560, 520);

	auto *title = new QLabel(QString("What's new in RareStudio %1").arg(current));
	title->setObjectName("dialogTitle");
	auto *body = new QTextBrowser;
	body->setOpenExternalLinks(true);
	body->setMarkdown(md);

	QSettings s;
	auto *when = new QComboBox;
	when->addItem("Show after each update", "update");
	when->addItem("Show every time RareStudio starts", "always");
	when->addItem("Never show automatically", "never");
	when->setCurrentIndex(std::max(0, when->findData(s.value("ui/changelog", "update").toString())));
	QObject::connect(when, &QComboBox::currentIndexChanged, dlg, [when] { QSettings().setValue("ui/changelog", when->currentData()); });

	auto *close = new QPushButton("Continue");
	close->setDefault(true);
	QObject::connect(close, &QPushButton::clicked, dlg, &QDialog::accept);
	auto *row = new QHBoxLayout;
	row->addWidget(when);
	row->addStretch();
	row->addWidget(close);

	auto *lay = new QVBoxLayout(dlg);
	lay->setContentsMargins(18, 16, 18, 14);
	lay->setSpacing(12);
	lay->addWidget(title);
	lay->addWidget(body, 1);
	lay->addLayout(row);
	dlg->open();
}

void showIfDue(QWidget *parent)
{
	QSettings s;
	const QString mode = s.value("ui/changelog", "update").toString();
	const QString seen = s.value("ui/lastSeenVersion").toString();
	const bool isNew = seen != RARESTUDIO_VERSION;
	s.setValue("ui/lastSeenVersion", RARESTUDIO_VERSION);
	if (mode == "never" || (mode == "update" && !isNew))
		return;
	// A newer-than-seen range only makes sense for an upgrade; "every launch" repeats the current notes.
	const bool upgrade = isNew && QVersionNumber::fromString(seen) < QVersionNumber::fromString(RARESTUDIO_VERSION);
	show(parent, upgrade ? seen : QString());
}

} // namespace Changelog
