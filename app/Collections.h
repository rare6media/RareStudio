#pragma once

#include <QString>
#include <QStringList>

// Scene collections: separate sets of scenes and sources (a "Just Chatting" setup, a "Gaming" setup,
// a "Podcast" one), each its own file under %APPDATA%\RareStudio\collections\<name>.json. Exactly one
// is active. Switching restarts RareStudio onto the other one: libobs cannot safely swap every source
// of a running studio, and a restart costs a few seconds, not a crash.
namespace Collections {

QStringList list();         // names, alphabetical
QString active();           // "Default" until you choose another
QString activeFile();       // the active collection's file; the very first call adopts the old scenes.json
QString fileFor(const QString &name);
QString sanitize(const QString &name); // what the name becomes on disk: no \ / : * ? " < > |, trimmed
bool exists(const QString &name);

void setActive(const QString &name);
// Each returns an empty string on success, else a message to show the user.
QString create(const QString &name);                          // an empty collection
QString duplicate(const QString &from, const QString &name);  // copies what is on disk (save first)
QString rename(const QString &from, const QString &to);
QString remove(const QString &name);                          // never the active one

} // namespace Collections
