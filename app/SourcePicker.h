#pragma once

#include <QByteArray>
#include <QString>

#include <functional>

class QWidget;

// The "Add source" dialog: sources grouped by kind, each with a drawn icon and a one-line description.
// Returns false if cancelled; otherwise the libobs source id and the (already unique) name to give it.
// makeUnique turns a wanted name into one no existing source uses.
bool pickSource(QWidget *parent, const std::function<QString(const QString &)> &makeUnique, QByteArray *id, QString *name);
