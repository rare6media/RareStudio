#pragma once

#include <QNetworkAccessManager>
#include <QObject>
#include <QString>
#include <QUrl>

#include <functional>

// Progress/result of one YouTube upload.
class UploadJob : public QObject {
	Q_OBJECT
public:
	using QObject::QObject;
signals:
	void progress(qint64 sent, qint64 total);
	void finished(const QString &url, const QString &error); // exactly one of the two is non-empty
};

// Twitch and Google/YouTube accounts: sign-in, stream keys, Twitch clips, YouTube Shorts uploads.
//
// You need to provide your own API credentials (a Twitch app Client ID, and a Google OAuth
// client ID + secret). Those are what let *your* account sign in without anyone else's quota
// or approval. Tokens are stored encrypted (Windows DPAPI).
class Accounts : public QObject {
	Q_OBJECT
public:
	static Accounts &instance();

	using KeyCb = std::function<void(const QString &value, const QString &error)>;

	// ---- Twitch ----
	QString twitchClientId() const;
	void setTwitchClientId(const QString &id);
	bool twitchConnected() const;
	QString twitchLogin() const;
	void connectTwitch();            // device-code flow: emits twitchDeviceCode, then changed()
	void disconnectTwitch();
	void twitchStreamKey(KeyCb cb);  // value = stream key
	void twitchClip(int seconds, KeyCb cb); // value = clip URL (stream must be live)

	// ---- Google / YouTube ----
	QString googleClientId() const;
	QString googleClientSecret() const;
	void setGoogleCredentials(const QString &id, const QString &secret);
	bool googleConnected() const;
	QString googleChannel() const;
	void connectGoogle();            // opens your browser; completes on the loopback redirect
	void disconnectGoogle();
	// Calls back with the ingest server and key of your default live stream.
	void youtubeIngest(std::function<void(const QString &server, const QString &key, const QString &error)> cb);
	UploadJob *uploadShort(const QString &file, const QString &title, const QString &description,
			       const QString &privacy); // privacy: private | unlisted | public

signals:
	void twitchDeviceCode(const QString &userCode, const QUrl &verificationUrl);
	void changed();                  // a connection was made or removed
	void failed(const QString &message);

private:
	Accounts() = default;

	void withToken(bool twitch, std::function<void(const QString &token, const QString &error)> cb);
	void storeTokens(bool twitch, const QJsonObject &reply);
	void fetchTwitchUser(const QString &token);
	void pollTwitch(const QString &deviceCode, int intervalSec, qint64 deadlineMs, int generation);
	void helix(const QString &method, const QString &path, const QString &token,
		   std::function<void(int status, const QJsonObject &json)> cb);

	QNetworkAccessManager m_net;
	int m_twitchGeneration = 0; // bumped to cancel an in-flight device-code poll
};
