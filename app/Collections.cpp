#include "Collections.h"

#include <QDir>
#include <QFile>
#include <QSettings>

namespace Collections {

namespace {

QString root()
{
	return qEnvironmentVariable("APPDATA") + "/RareStudio";
}

QString dir()
{
	const QString d = root() + "/collections";
	QDir().mkpath(d);
	return d;
}

} // namespace

QString sanitize(const QString &name)
{
	QString out = name;
	for (QChar &c : out)
		if (QString("\\/:*?\"<>|").contains(c) || c.unicode() < 32)
			c = ' ';
	out = out.simplified();
	while (out.endsWith('.'))
		out.chop(1);
	return out.left(60).trimmed();
}

QString fileFor(const QString &name)
{
	return dir() + "/" + sanitize(name) + ".json";
}

bool exists(const QString &name)
{
	const QString clean = sanitize(name);
	for (const QString &n : list())
		if (n.compare(clean, Qt::CaseInsensitive) == 0)
			return true;
	return false;
}

QStringList list()
{
	QStringList out;
	for (const QFileInfo &f : QDir(dir()).entryInfoList({"*.json"}, QDir::Files, QDir::Name | QDir::IgnoreCase))
		out << f.completeBaseName();
	return out;
}

QString active()
{
	const QString name = sanitize(QSettings().value("collections/active", "Default").toString());
	return name.isEmpty() ? "Default" : name;
}

QString activeFile()
{
	const QString file = fileFor(active());
	// First run of a version with collections: the scenes you already have become "Default".
	const QString legacy = root() + "/scenes.json";
	if (!QFile::exists(file) && active() == "Default" && QFile::exists(legacy))
		QFile::copy(legacy, file); // the old file stays put as a spare copy
	return file;
}

void setActive(const QString &name)
{
	QSettings().setValue("collections/active", sanitize(name));
}

static QString check(const QString &name)
{
	if (sanitize(name).isEmpty())
		return "Give the collection a name.";
	if (exists(name))
		return QString("A collection called \"%1\" already exists.").arg(sanitize(name));
	return {};
}

QString create(const QString &name)
{
	if (const QString bad = check(name); !bad.isEmpty())
		return bad;
	QFile f(fileFor(name));
	if (!f.open(QIODevice::WriteOnly))
		return "Could not create the collection: " + f.errorString();
	f.write("{}"); // no sources: the app starts it with one empty scene
	return {};
}

QString duplicate(const QString &from, const QString &name)
{
	if (const QString bad = check(name); !bad.isEmpty())
		return bad;
	if (!QFile::copy(fileFor(from), fileFor(name)))
		return "Could not copy the collection.";
	return {};
}

QString rename(const QString &from, const QString &to)
{
	if (sanitize(from).compare(sanitize(to), Qt::CaseInsensitive) != 0)
		if (const QString bad = check(to); !bad.isEmpty())
			return bad;
	if (sanitize(to).isEmpty())
		return "Give the collection a name.";
	if (!QFile::rename(fileFor(from), fileFor(to)))
		return "Could not rename the collection.";
	QFile::rename(fileFor(from) + ".bak", fileFor(to) + ".bak");
	if (active().compare(sanitize(from), Qt::CaseInsensitive) == 0)
		setActive(to);
	return {};
}

QString remove(const QString &name)
{
	if (active().compare(sanitize(name), Qt::CaseInsensitive) == 0)
		return "Switch to another collection before deleting this one.";
	if (!QFile::remove(fileFor(name)))
		return "Could not delete the collection.";
	QFile::remove(fileFor(name) + ".bak");
	return {};
}

} // namespace Collections
