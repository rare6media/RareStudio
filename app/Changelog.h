#pragma once

#include <QList>
#include <QString>

class QWidget;

// Release notes, read from the CHANGELOG.md embedded in the exe, and the "What's new" window.
namespace Changelog {

struct Entry {
	QString version; // "0.9.0"
	QString date;    // "2026-10-28" (may be empty)
	QString markdown; // the bullets, without the heading
};

QList<Entry> entries(); // newest first
// Opens the window: every release newer than `since` (or just the current one when `since` is empty
// or nothing newer is listed), and a choice of when it should appear.
void show(QWidget *parent, const QString &since = {});
// Launch hook: shows the window when the installed version differs from the one last seen, or on every
// launch if Settings > Updates says so. Remembers the version as seen.
void showIfDue(QWidget *parent);

} // namespace Changelog
