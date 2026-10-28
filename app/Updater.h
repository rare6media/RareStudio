#pragma once

#include <QNetworkAccessManager>
#include <QObject>
#include <QString>
#include <QStringList>

// Self-updater. Downloads a new version into %LOCALAPPDATA%\RareStudio\updates\<version>\ and
// marks it ready; the launcher (the desktop shortcut target) picks the newest ready version on
// the next start. The launcher itself never changes, so the shortcut never needs replacing.
//
// Feed format (JSON at the URL stored in settings key "update/url"):
//   { "version": "0.2.0", "url": "https://.../RareStudio-0.2.0.zip", "sha256": "<hex>", "notes": "optional" }
//
// Version control: older versions stay on disk (Settings > Updates), and rollbackTo() removes
// every version newer than the one you pick so the launcher falls back to it. The release you rolled
// back from is remembered as skipped, so it is not downloaded again until you stop skipping it.
class Updater : public QObject {
	Q_OBJECT
public:
	explicit Updater(QObject *parent = nullptr);

	// manual = the user asked, so say something even when there is nothing new.
	void checkNow(bool manual = false);
	// Deletes downloaded versions older than the one that is running.
	void cleanOldVersions();
	// The built-in GitHub releases feed, unless "update/url" overrides it.
	static QString defaultFeed();
	static QString feedUrl();
	static bool isNewer(const QString &candidate, const QString &current);

	// A release the user does not want offered ("update/skip"); automatic checks ignore it.
	static QString skipped();
	static void setSkipped(const QString &version);
	// How many older versions to keep on disk for rollback ("update/keep", default 2).
	static int keepCount();
	// Versions you can go back to, newest first: kept downloads plus the one that shipped with the install.
	static QStringList rollbackTargets();
	// Removes everything newer than `version` (once this process has exited) and starts the launcher
	// again. Returns false with a reason in `error` if it cannot.
	static bool rollbackTo(const QString &version, QString *error);

signals:
	void updateReady(const QString &version); // downloaded and verified; applies on next launch
	void status(const QString &message);
	// A feed check finished. `newer` is false for the current release and for a skipped one.
	void checked(const QString &latest, const QString &notes, bool newer);
	void checkFailed(const QString &reason);

private:
	void fetchFeed(bool manual, int attempt);
	void download(const QString &version, const QUrl &url, const QString &sha256, int attempt = 0);
	void install(const QString &version, const QString &zipPath);

	QNetworkAccessManager m_net;
};
