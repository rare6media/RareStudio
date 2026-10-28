#include "Updater.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <QVersionNumber>

#include <algorithm>

static QString updatesRoot()
{
	return qEnvironmentVariable("LOCALAPPDATA") + "/RareStudio/updates"; // same dir the launcher scans
}

// Folders under updates\ that hold a complete version (the "ok" marker is written last), oldest first.
static QStringList readyVersions()
{
	QStringList out;
	const QDir root(updatesRoot());
	for (const QString &name : root.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
		if (root.exists(name + "/ok") && !QVersionNumber::fromString(name).isNull())
			out << name;
	std::sort(out.begin(), out.end(), [](const QString &a, const QString &b) {
		return QVersionNumber::fromString(a) < QVersionNumber::fromString(b);
	});
	return out;
}

// The copy that shipped with the installer; the launcher falls back to it when no update folder is left.
static QString shippedVersion()
{
	QFile f(qEnvironmentVariable("ProgramFiles") + "/RareStudio/app/version.txt");
	return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readLine()).trimmed() : QString();
}

Updater::Updater(QObject *parent) : QObject(parent) {}

QString Updater::defaultFeed()
{
	return "https://github.com/rare6media/RareStudio/releases/latest/download/latest.json";
}

QString Updater::feedUrl()
{
	const QString custom = QSettings().value("update/url").toString().trimmed();
	return custom.isEmpty() ? defaultFeed() : custom;
}

bool Updater::isNewer(const QString &candidate, const QString &current)
{
	return QVersionNumber::fromString(candidate) > QVersionNumber::fromString(current);
}

QString Updater::skipped()
{
	return QSettings().value("update/skip").toString();
}

void Updater::setSkipped(const QString &version)
{
	QSettings().setValue("update/skip", version);
}

int Updater::keepCount()
{
	return std::clamp(QSettings().value("update/keep", 2).toInt(), 0, 5);
}

QStringList Updater::rollbackTargets()
{
	const QVersionNumber now = QVersionNumber::fromString(RARESTUDIO_VERSION);
	QStringList out;
	for (const QString &v : readyVersions())
		if (QVersionNumber::fromString(v) < now)
			out.prepend(v);
	const QString shipped = shippedVersion();
	if (!shipped.isEmpty() && QVersionNumber::fromString(shipped) < now && !out.contains(shipped))
		out << shipped;
	return out;
}

bool Updater::rollbackTo(const QString &version, QString *error)
{
	if (!rollbackTargets().contains(version)) {
		*error = "That version is no longer on this computer.";
		return false;
	}
	const QVersionNumber target = QVersionNumber::fromString(version);
	QStringList doomed;
	QString newest;
	for (const QString &v : readyVersions()) {
		if (QVersionNumber::fromString(v) > target) {
			doomed << QDir::toNativeSeparators(updatesRoot() + "/" + v);
			newest = v;
		}
	}
	if (doomed.isEmpty()) {
		*error = "Nothing newer than that version is installed.";
		return false;
	}

	// The running copy cannot delete its own folder, so a script does it after this process has exited,
	// then starts the launcher again. Removing the newer folders is what makes the launcher pick `version`.
	QString script = "@echo off\r\nping -n 4 127.0.0.1 >nul\r\n";
	for (const QString &d : doomed)
		script += QString("rmdir /s /q \"%1\"\r\n").arg(d);
	const QString launcher = QDir::toNativeSeparators(qEnvironmentVariable("ProgramFiles") + "/RareStudio/RareStudio.exe");
	script += QString("if exist \"%1\" start \"\" \"%1\"\r\ndel \"%~f0\"\r\n").arg(launcher);

	const QString path = QDir::tempPath() + "/rarestudio-rollback.bat";
	QFile f(path);
	if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate) || f.write(script.toLocal8Bit()) < 0) {
		*error = "Could not write the rollback script: " + f.errorString();
		return false;
	}
	f.close();

	QProcess p;
	p.setProgram("cmd.exe");
	p.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *a) { a->flags |= 0x08000000; }); // no console window
	p.setArguments({"/c", QDir::toNativeSeparators(path)});
	if (!p.startDetached()) {
		*error = "Could not start the rollback script.";
		return false;
	}
	setSkipped(newest); // don't pull the release you just left straight back down
	qWarning("Updater: rolling back to %s (removing %d newer versions)", qPrintable(version), int(doomed.size()));
	return true;
}

void Updater::checkNow(bool manual)
{
	fetchFeed(manual, 0);
}

void Updater::fetchFeed(bool manual, int attempt)
{
	QNetworkRequest req{QUrl(feedUrl())};
	req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy); // GitHub redirects to its CDN
	req.setTransferTimeout(20000); // never leave "Checking..." hanging on a dead connection
	QNetworkReply *reply = m_net.get(req);
	connect(reply, &QNetworkReply::finished, this, [this, reply, manual, attempt] {
		reply->deleteLater();
		if (reply->error() != QNetworkReply::NoError) {
			qWarning("Updater: feed check failed (attempt %d): %s", attempt + 1, qPrintable(reply->errorString()));
			if (attempt == 0) { // one quiet retry covers a flaky connection at startup
				QTimer::singleShot(4000, this, [this, manual] { fetchFeed(manual, 1); });
				return;
			}
			if (manual)
				emit status("Update check failed: " + reply->errorString());
			emit checkFailed(reply->errorString());
			return;
		}
		const QJsonObject o = QJsonDocument::fromJson(reply->readAll()).object();
		const QString ver = o["version"].toString();
		const QUrl url(o["url"].toString());
		if (ver.isEmpty() || QVersionNumber::fromString(ver).isNull() || !url.isValid() || url.scheme() != "https") {
			qWarning("Updater: feed is not in the expected format (version '%s')", qPrintable(ver));
			if (manual)
				emit status("Update check failed: the feed is not in the expected format.");
			emit checkFailed("the feed is not in the expected format");
			return;
		}
		const bool newer = isNewer(ver, RARESTUDIO_VERSION);
		const bool skip = newer && !manual && skipped() == ver;
		emit checked(ver, o["notes"].toString(), newer && !skip);
		if (!newer) {
			if (manual)
				emit status(QString("RareStudio %1 is up to date.").arg(RARESTUDIO_VERSION));
			return;
		}
		if (skip) // asking by hand still installs it: that is a decision to take the release
			return;
		if (skipped() == ver)
			setSkipped({});
		if (QDir(updatesRoot() + "/" + ver).exists("ok")) {
			emit updateReady(ver); // already downloaded, waiting for a restart
			return;
		}
		download(ver, url, o["sha256"].toString());
	});
}

void Updater::cleanOldVersions()
{
	// Keep the newest few older versions for rollback; anything beyond that, and half-extracted
	// folders (no "ok" marker), is cleared.
	const QDir root(updatesRoot());
	const QVersionNumber now = QVersionNumber::fromString(RARESTUDIO_VERSION);
	const QStringList ready = readyVersions();
	int kept = 0;
	for (int i = int(ready.size()) - 1; i >= 0; --i)
		if (QVersionNumber::fromString(ready[i]) < now && ++kept > keepCount())
			QDir(root.filePath(ready[i])).removeRecursively();
	for (const QString &name : root.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
		if (!ready.contains(name) && QVersionNumber::fromString(name) < now)
			QDir(root.filePath(name)).removeRecursively();
}

void Updater::download(const QString &version, const QUrl &url, const QString &sha256, int attempt)
{
	emit status("Downloading update " + version + "...");
	QNetworkRequest req(url);
	req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
	req.setTransferTimeout(30000); // stalls, not total time: a big zip on a slow line is fine
	QNetworkReply *reply = m_net.get(req);
	connect(reply, &QNetworkReply::finished, this, [this, reply, version, sha256, url, attempt] {
		reply->deleteLater();
		if (reply->error() != QNetworkReply::NoError) {
			qWarning("Updater: download of %s failed (attempt %d): %s", qPrintable(version), attempt + 1, qPrintable(reply->errorString()));
			if (attempt == 0) {
				QTimer::singleShot(5000, this, [this, version, url, sha256] { download(version, url, sha256, 1); });
				return;
			}
			emit status("Update download failed: " + reply->errorString());
			return;
		}
		const QByteArray data = reply->readAll();
		if (sha256.isEmpty() ||
		    QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex().compare(sha256.toLatin1(), Qt::CaseInsensitive) != 0) {
			qWarning("Updater: checksum mismatch for %s", qPrintable(version));
			emit status("Update rejected: checksum missing or mismatch.");
			return;
		}
		QDir().mkpath(updatesRoot());
		const QString zip = updatesRoot() + "/" + version + ".zip";
		QFile f(zip);
		if (!f.open(QIODevice::WriteOnly) || f.write(data) != data.size()) {
			qWarning("Updater: could not write %s: %s", qPrintable(zip), qPrintable(f.errorString()));
			emit status("Could not save the update: " + f.errorString());
			return;
		}
		f.close();
		install(version, zip);
	});
}

void Updater::install(const QString &version, const QString &zipPath)
{
	const QString dest = updatesRoot() + "/" + version;
	QDir(dest).removeRecursively();
	QDir().mkpath(dest);

	// tar.exe ships with Windows 10+ and reads zip archives.
	auto *tar = new QProcess(this);
	connect(tar, &QProcess::finished, this, [this, tar, dest, zipPath, version](int code) {
		tar->deleteLater();
		QFile::remove(zipPath);
		if (code != 0 || !QFile::exists(dest + "/RareStudioApp.exe")) {
			qWarning("Updater: extracting %s failed (tar exit %d)", qPrintable(version), code);
			QDir(dest).removeRecursively();
			emit status("Update extraction failed.");
			return;
		}
		QFile ok(dest + "/ok"); // written last: marks the version complete for the launcher
		ok.open(QIODevice::WriteOnly);
		emit updateReady(version);
	});
	connect(tar, &QProcess::errorOccurred, this, [this, tar, dest](QProcess::ProcessError) {
		if (tar->state() != QProcess::NotRunning)
			return;
		qWarning("Updater: tar.exe could not start: %s", qPrintable(tar->errorString()));
		QDir(dest).removeRecursively();
		emit status("Could not unpack the update: " + tar->errorString());
	});
	tar->start("tar.exe", {"-xf", QDir::toNativeSeparators(zipPath), "-C", QDir::toNativeSeparators(dest)});
}
