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
#include <QVersionNumber>

static QString updatesRoot()
{
	return qEnvironmentVariable("LOCALAPPDATA") + "/RareStudio/updates"; // same dir the launcher scans
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

void Updater::checkNow(bool manual)
{
	QNetworkRequest req{QUrl(feedUrl())};
	req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy); // GitHub redirects to its CDN
	QNetworkReply *reply = m_net.get(req);
	connect(reply, &QNetworkReply::finished, this, [this, reply, manual] {
		reply->deleteLater();
		if (reply->error() != QNetworkReply::NoError) {
			if (manual)
				emit status("Update check failed: " + reply->errorString());
			return;
		}
		const QJsonObject o = QJsonDocument::fromJson(reply->readAll()).object();
		const QString ver = o["version"].toString();
		const QUrl url(o["url"].toString());
		if (ver.isEmpty() || !url.isValid()) {
			if (manual)
				emit status("Update check failed: the feed is not in the expected format.");
			return;
		}
		if (!isNewer(ver, RARESTUDIO_VERSION)) {
			if (manual)
				emit status(QString("RareStudio %1 is up to date.").arg(RARESTUDIO_VERSION));
			return;
		}
		if (QDir(updatesRoot() + "/" + ver).exists("ok")) {
			emit updateReady(ver); // already downloaded, waiting for a restart
			return;
		}
		download(ver, url, o["sha256"].toString());
	});
}

void Updater::cleanOldVersions()
{
	const QDir root(updatesRoot());
	for (const QString &name : root.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
		if (QVersionNumber::fromString(name) < QVersionNumber::fromString(RARESTUDIO_VERSION))
			QDir(root.filePath(name)).removeRecursively();
	}
}

void Updater::download(const QString &version, const QUrl &url, const QString &sha256)
{
	emit status("Downloading update " + version + "...");
	QNetworkRequest req(url);
	req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
	QNetworkReply *reply = m_net.get(req);
	connect(reply, &QNetworkReply::finished, this, [this, reply, version, sha256] {
		reply->deleteLater();
		if (reply->error() != QNetworkReply::NoError) {
			emit status("Update download failed: " + reply->errorString());
			return;
		}
		const QByteArray data = reply->readAll();
		if (sha256.isEmpty() ||
		    QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex().compare(sha256.toLatin1(), Qt::CaseInsensitive) != 0) {
			emit status("Update rejected: checksum missing or mismatch.");
			return;
		}
		QDir().mkpath(updatesRoot());
		const QString zip = updatesRoot() + "/" + version + ".zip";
		QFile f(zip);
		if (!f.open(QIODevice::WriteOnly) || f.write(data) != data.size()) {
			emit status("Could not save update.");
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
			QDir(dest).removeRecursively();
			emit status("Update extraction failed.");
			return;
		}
		QFile ok(dest + "/ok"); // written last: marks the version complete for the launcher
		ok.open(QIODevice::WriteOnly);
		emit updateReady(version);
	});
	tar->start("tar.exe", {"-xf", QDir::toNativeSeparators(zipPath), "-C", QDir::toNativeSeparators(dest)});
}
