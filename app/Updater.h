#pragma once

#include <QNetworkAccessManager>
#include <QObject>
#include <QString>

// Self-updater. Downloads a new version into %LOCALAPPDATA%\RareStudio\updates\<version>\ and
// marks it ready; the launcher (the desktop shortcut target) picks the newest ready version on
// the next start. The launcher itself never changes, so the shortcut never needs replacing.
//
// Feed format (JSON at the URL stored in settings key "update/url"):
//   { "version": "0.2.0", "url": "https://.../RareStudio-0.2.0.zip", "sha256": "<hex>" }
class Updater : public QObject {
	Q_OBJECT
public:
	explicit Updater(QObject *parent = nullptr);

	// manual = the user asked, so say something even when there is nothing new.
	void checkNow(bool manual = false);
	// Deletes downloaded versions older than the one that is running.
	void cleanOldVersions();
	static bool isNewer(const QString &candidate, const QString &current);

signals:
	void updateReady(const QString &version); // downloaded and verified; applies on next launch
	void status(const QString &message);

private:
	void download(const QString &version, const QUrl &url, const QString &sha256);
	void install(const QString &version, const QString &zipPath);

	QNetworkAccessManager m_net;
};
