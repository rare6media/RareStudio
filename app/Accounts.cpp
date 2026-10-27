#include "Accounts.h"

#include "Secrets.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDesktopServices>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QRandomGenerator>
#include <QSettings>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUrlQuery>

namespace {

const char *kTwitchScopes = "channel:read:stream_key clips:edit channel:manage:broadcast";
const char *kGoogleScope = "https://www.googleapis.com/auth/youtube";

QByteArray form(const QList<QPair<QString, QString>> &items)
{
	QUrlQuery q;
	for (const auto &i : items)
		q.addQueryItem(i.first, i.second);
	return q.toString(QUrl::FullyEncoded).toUtf8();
}

QJsonObject parse(QNetworkReply *r)
{
	return QJsonDocument::fromJson(r->readAll()).object();
}

QString randomString(int bytes)
{
	QByteArray b;
	for (int i = 0; i < bytes; ++i)
		b.append(char(QRandomGenerator::system()->bounded(256)));
	return QString::fromLatin1(b.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

QString errorText(const QJsonObject &o, const QString &fallback)
{
	// Twitch: {"message":...}  Google: {"error":{"message":...}} or {"error":"...","error_description":...}
	if (o.contains("message"))
		return o["message"].toString();
	if (o["error"].isObject())
		return o["error"].toObject()["message"].toString(fallback);
	if (o.contains("error_description"))
		return o["error_description"].toString();
	if (o.contains("error"))
		return o["error"].toString();
	return fallback;
}

QSettings &settings()
{
	static QSettings s;
	return s;
}

} // namespace

Accounts &Accounts::instance()
{
	static Accounts a;
	return a;
}

// ---- storage ---------------------------------------------------------------------------------

QString Accounts::twitchClientId() const { return settings().value("twitch/client_id").toString(); }
void Accounts::setTwitchClientId(const QString &id) { settings().setValue("twitch/client_id", id.trimmed()); }
bool Accounts::twitchConnected() const { return !settings().value("twitch/refresh").toString().isEmpty(); }
QString Accounts::twitchLogin() const { return settings().value("twitch/login").toString(); }

QString Accounts::googleClientId() const { return settings().value("google/client_id").toString(); }
QString Accounts::googleClientSecret() const { return Secrets::unprotect(settings().value("google/client_secret").toString()); }
void Accounts::setGoogleCredentials(const QString &id, const QString &secret)
{
	settings().setValue("google/client_id", id.trimmed());
	if (!secret.isEmpty())
		settings().setValue("google/client_secret", Secrets::protect(secret.trimmed()));
}
bool Accounts::googleConnected() const { return !settings().value("google/refresh").toString().isEmpty(); }
QString Accounts::googleChannel() const { return settings().value("google/channel").toString(); }

void Accounts::storeTokens(bool twitch, const QJsonObject &r)
{
	const QString p = twitch ? "twitch" : "google";
	settings().setValue(p + "/token", Secrets::protect(r["access_token"].toString()));
	if (!r["refresh_token"].toString().isEmpty()) // refresh replies from Google omit it; keep the old one
		settings().setValue(p + "/refresh", Secrets::protect(r["refresh_token"].toString()));
	settings().setValue(p + "/expires", QDateTime::currentSecsSinceEpoch() + r["expires_in"].toInt(3600));
}

// Returns a valid access token, refreshing it first if it is about to expire.
void Accounts::withToken(bool twitch, std::function<void(const QString &, const QString &)> cb)
{
	const QString p = twitch ? "twitch" : "google";
	const QString token = Secrets::unprotect(settings().value(p + "/token").toString());
	const QString refresh = Secrets::unprotect(settings().value(p + "/refresh").toString());
	if (refresh.isEmpty()) {
		cb({}, twitch ? "Twitch is not connected." : "Google is not connected.");
		return;
	}
	if (!token.isEmpty() && settings().value(p + "/expires").toLongLong() - QDateTime::currentSecsSinceEpoch() > 60) {
		cb(token, {});
		return;
	}

	QNetworkRequest req(QUrl(twitch ? "https://id.twitch.tv/oauth2/token" : "https://oauth2.googleapis.com/token"));
	req.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
	QList<QPair<QString, QString>> f = {{"grant_type", "refresh_token"}, {"refresh_token", refresh}};
	f.append({"client_id", twitch ? twitchClientId() : googleClientId()});
	if (!twitch)
		f.append({"client_secret", googleClientSecret()});
	QNetworkReply *reply = m_net.post(req, form(f));
	connect(reply, &QNetworkReply::finished, this, [this, reply, twitch, cb] {
		reply->deleteLater();
		const QJsonObject o = parse(reply);
		if (o["access_token"].toString().isEmpty()) {
			cb({}, "Sign-in expired: " + errorText(o, reply->errorString()) + " (reconnect in Settings > Accounts)");
			return;
		}
		storeTokens(twitch, o);
		cb(o["access_token"].toString(), {});
	});
}

// ---- Twitch ----------------------------------------------------------------------------------

void Accounts::helix(const QString &method, const QString &path, const QString &token,
		     std::function<void(int, const QJsonObject &)> cb)
{
	QNetworkRequest req(QUrl("https://api.twitch.tv/helix/" + path));
	req.setRawHeader("Authorization", "Bearer " + token.toUtf8());
	req.setRawHeader("Client-Id", twitchClientId().toUtf8());
	QNetworkReply *reply = method == "POST" ? m_net.post(req, QByteArray()) : m_net.get(req);
	connect(reply, &QNetworkReply::finished, this, [reply, cb] {
		reply->deleteLater();
		cb(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(), parse(reply));
	});
}

void Accounts::connectTwitch()
{
	if (twitchClientId().isEmpty()) {
		emit failed("Enter your Twitch Client ID first.");
		return;
	}
	const int gen = ++m_twitchGeneration;
	QNetworkRequest req(QUrl("https://id.twitch.tv/oauth2/device"));
	req.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
	QNetworkReply *reply = m_net.post(req, form({{"client_id", twitchClientId()}, {"scopes", kTwitchScopes}}));
	connect(reply, &QNetworkReply::finished, this, [this, reply, gen] {
		reply->deleteLater();
		const QJsonObject o = parse(reply);
		if (o["device_code"].toString().isEmpty()) {
			emit failed("Twitch: " + errorText(o, reply->errorString()) +
				    "\n\nCheck the Client ID, and that the app is registered as a Public client.");
			return;
		}
		emit twitchDeviceCode(o["user_code"].toString(), QUrl(o["verification_uri"].toString()));
		const int interval = std::max(2, o["interval"].toInt(5));
		pollTwitch(o["device_code"].toString(), interval,
			   QDateTime::currentMSecsSinceEpoch() + qint64(o["expires_in"].toInt(600)) * 1000, gen);
	});
}

void Accounts::pollTwitch(const QString &deviceCode, int intervalSec, qint64 deadlineMs, int gen)
{
	QTimer::singleShot(intervalSec * 1000, this, [this, deviceCode, intervalSec, deadlineMs, gen] {
		if (gen != m_twitchGeneration)
			return; // cancelled
		if (QDateTime::currentMSecsSinceEpoch() > deadlineMs) {
			emit failed("Twitch sign-in timed out. Try Connect again.");
			return;
		}
		QNetworkRequest req(QUrl("https://id.twitch.tv/oauth2/token"));
		req.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
		QNetworkReply *reply = m_net.post(req, form({{"client_id", twitchClientId()},
							     {"scopes", kTwitchScopes},
							     {"device_code", deviceCode},
							     {"grant_type", "urn:ietf:params:oauth:grant-type:device_code"}}));
		connect(reply, &QNetworkReply::finished, this, [this, reply, deviceCode, intervalSec, deadlineMs, gen] {
			reply->deleteLater();
			const QJsonObject o = parse(reply);
			if (!o["access_token"].toString().isEmpty()) {
				storeTokens(true, o);
				fetchTwitchUser(o["access_token"].toString());
				return;
			}
			const QString msg = o["message"].toString();
			if (msg == "authorization_pending" || msg == "slow_down" || msg.isEmpty())
				pollTwitch(deviceCode, intervalSec, deadlineMs, gen); // still waiting for you to approve
			else
				emit failed("Twitch sign-in failed: " + msg);
		});
	});
}

void Accounts::fetchTwitchUser(const QString &token)
{
	helix("GET", "users", token, [this](int, const QJsonObject &o) {
		const QJsonObject u = o["data"].toArray().first().toObject();
		settings().setValue("twitch/user_id", u["id"].toString());
		settings().setValue("twitch/login", u["display_name"].toString(u["login"].toString()));
		emit changed();
	});
}

void Accounts::disconnectTwitch()
{
	++m_twitchGeneration;
	for (const char *k : {"token", "refresh", "expires", "user_id", "login"})
		settings().remove(QString("twitch/") + k);
	emit changed();
}

void Accounts::twitchStreamKey(KeyCb cb)
{
	withToken(true, [this, cb](const QString &token, const QString &err) {
		if (!err.isEmpty())
			return cb({}, err);
		const QString id = settings().value("twitch/user_id").toString();
		helix("GET", "streams/key?broadcaster_id=" + id, token, [cb](int status, const QJsonObject &o) {
			const QString key = o["data"].toArray().first().toObject()["stream_key"].toString();
			if (key.isEmpty())
				cb({}, QString("Twitch did not return a stream key (%1): %2").arg(status).arg(errorText(o, "unknown error")));
			else
				cb(key, {});
		});
	});
}

void Accounts::twitchClip(int seconds, KeyCb cb)
{
	withToken(true, [this, seconds, cb](const QString &token, const QString &err) {
		if (!err.isEmpty())
			return cb({}, err);
		const QString id = settings().value("twitch/user_id").toString();
		helix("POST", QString("clips?broadcaster_id=%1&duration=%2").arg(id).arg(seconds), token,
		      [cb](int status, const QJsonObject &o) {
			      const QJsonObject c = o["data"].toArray().first().toObject();
			      if (c["id"].toString().isEmpty())
				      cb({}, QString("Twitch clip failed (%1): %2").arg(status).arg(errorText(o, "is the stream live?")));
			      else
				      cb("https://clips.twitch.tv/" + c["id"].toString(), {});
		      });
	});
}

// ---- Google / YouTube ------------------------------------------------------------------------

void Accounts::connectGoogle()
{
	if (googleClientId().isEmpty() || googleClientSecret().isEmpty()) {
		emit failed("Enter your Google OAuth Client ID and Client secret first.");
		return;
	}
	auto *server = new QTcpServer(this);
	if (!server->listen(QHostAddress::LocalHost, 0)) {
		server->deleteLater();
		emit failed("Could not open a local port for the Google sign-in.");
		return;
	}
	const QString redirect = QString("http://127.0.0.1:%1").arg(server->serverPort());
	const QString verifier = randomString(48);
	const QString challenge = QString::fromLatin1(
		QCryptographicHash::hash(verifier.toLatin1(), QCryptographicHash::Sha256)
			.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
	const QString state = randomString(16);

	QUrl url("https://accounts.google.com/o/oauth2/v2/auth");
	QUrlQuery q;
	q.addQueryItem("client_id", googleClientId());
	q.addQueryItem("redirect_uri", redirect);
	q.addQueryItem("response_type", "code");
	q.addQueryItem("scope", kGoogleScope);
	q.addQueryItem("code_challenge", challenge);
	q.addQueryItem("code_challenge_method", "S256");
	q.addQueryItem("access_type", "offline");
	q.addQueryItem("prompt", "consent");
	q.addQueryItem("state", state);
	url.setQuery(q);
	QDesktopServices::openUrl(url);

	QTimer::singleShot(5 * 60 * 1000, server, [server] { server->close(); server->deleteLater(); });
	connect(server, &QTcpServer::newConnection, this, [this, server, redirect, verifier, state] {
		QTcpSocket *sock = server->nextPendingConnection();
		connect(sock, &QTcpSocket::readyRead, this, [this, sock, server, redirect, verifier, state] {
			const QByteArray request = sock->readAll();
			const QString line = QString::fromLatin1(request.left(request.indexOf("\r\n")));
			const QUrlQuery rq(QUrl("http://x" + line.section(' ', 1, 1)));
			if (!line.startsWith("GET /?") || rq.queryItemValue("state") != state) {
				sock->close(); // a stray request (favicon etc.); keep waiting
				return;
			}
			const bool ok = !rq.queryItemValue("code").isEmpty();
			const QByteArray body = ok ? "<html><body style='font-family:sans-serif;background:#1b1b22;color:#eee;text-align:center;padding-top:80px'>"
						     "<h2>RareStudio is connected to Google.</h2><p>You can close this tab.</p></body></html>"
						   : "<html><body>Sign-in was cancelled.</body></html>";
			sock->write("HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nConnection: close\r\nContent-Length: " +
				    QByteArray::number(body.size()) + "\r\n\r\n" + body);
			sock->disconnectFromHost();
			server->close();
			server->deleteLater();
			if (!ok) {
				emit failed("Google sign-in was cancelled.");
				return;
			}

			QNetworkRequest req(QUrl("https://oauth2.googleapis.com/token"));
			req.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
			QNetworkReply *reply = m_net.post(req, form({{"code", rq.queryItemValue("code")},
								      {"client_id", googleClientId()},
								      {"client_secret", googleClientSecret()},
								      {"redirect_uri", redirect},
								      {"grant_type", "authorization_code"},
								      {"code_verifier", verifier}}));
			connect(reply, &QNetworkReply::finished, this, [this, reply] {
				reply->deleteLater();
				const QJsonObject o = parse(reply);
				if (o["access_token"].toString().isEmpty()) {
					emit failed("Google sign-in failed: " + errorText(o, reply->errorString()));
					return;
				}
				storeTokens(false, o);
				// Name the channel so the settings page can show who is connected.
				QNetworkRequest cr(QUrl("https://www.googleapis.com/youtube/v3/channels?part=snippet&mine=true"));
				cr.setRawHeader("Authorization", "Bearer " + o["access_token"].toString().toUtf8());
				QNetworkReply *cReply = m_net.get(cr);
				connect(cReply, &QNetworkReply::finished, this, [this, cReply] {
					cReply->deleteLater();
					const QJsonObject c = parse(cReply)["items"].toArray().first().toObject();
					settings().setValue("google/channel", c["snippet"].toObject()["title"].toString("(no YouTube channel)"));
					emit changed();
				});
			});
		});
	});
}

void Accounts::disconnectGoogle()
{
	for (const char *k : {"token", "refresh", "expires", "channel"})
		settings().remove(QString("google/") + k);
	emit changed();
}

void Accounts::youtubeIngest(std::function<void(const QString &, const QString &, const QString &)> cb)
{
	withToken(false, [this, cb](const QString &token, const QString &err) {
		if (!err.isEmpty())
			return cb({}, {}, err);
		QNetworkRequest req(QUrl("https://www.googleapis.com/youtube/v3/liveStreams?part=snippet,cdn&mine=true&maxResults=50"));
		req.setRawHeader("Authorization", "Bearer " + token.toUtf8());
		QNetworkReply *reply = m_net.get(req);
		connect(reply, &QNetworkReply::finished, this, [reply, cb] {
			reply->deleteLater();
			const QJsonObject o = parse(reply);
			const QJsonArray items = o["items"].toArray();
			if (items.isEmpty()) {
				cb({}, {}, "YouTube returned no stream key: " +
						   errorText(o, "live streaming may not be enabled on this channel yet (YouTube Studio > Go live; "
								"it can take up to 24 hours after you first enable it)."));
				return;
			}
			// Prefer the channel's default reusable stream.
			QJsonObject best = items.first().toObject();
			for (const QJsonValue &v : items)
				if (v.toObject()["snippet"].toObject()["isDefaultStream"].toBool())
					best = v.toObject();
			const QJsonObject ing = best["cdn"].toObject()["ingestionInfo"].toObject();
			cb(ing["ingestionAddress"].toString(), ing["streamName"].toString(), {});
		});
	});
}

UploadJob *Accounts::uploadShort(const QString &file, const QString &title, const QString &description,
				 const QString &privacy)
{
	auto *job = new UploadJob(this);
	withToken(false, [this, job, file, title, description, privacy](const QString &token, const QString &err) {
		if (!err.isEmpty()) {
			emit job->finished({}, err);
			return;
		}
		const qint64 size = QFileInfo(file).size();
		if (size <= 0) {
			emit job->finished({}, "The clip file is empty or missing.");
			return;
		}

		const QJsonObject meta{
			{"snippet", QJsonObject{{"title", title.left(100)},
						{"description", description},
						{"tags", QJsonArray{"Shorts"}},
						{"categoryId", "20"}}}, // Gaming
			{"status", QJsonObject{{"privacyStatus", privacy}, {"selfDeclaredMadeForKids", false}}}};

		// Step 1: start a resumable upload session.
		QNetworkRequest init(QUrl("https://www.googleapis.com/upload/youtube/v3/videos?uploadType=resumable&part=snippet,status"));
		init.setRawHeader("Authorization", "Bearer " + token.toUtf8());
		init.setHeader(QNetworkRequest::ContentTypeHeader, "application/json; charset=UTF-8");
		init.setRawHeader("X-Upload-Content-Length", QByteArray::number(size));
		init.setRawHeader("X-Upload-Content-Type", "video/mp4");
		QNetworkReply *start = m_net.post(init, QJsonDocument(meta).toJson(QJsonDocument::Compact));
		connect(start, &QNetworkReply::finished, this, [this, start, job, file, size] {
			start->deleteLater();
			const QUrl location(QString::fromLatin1(start->rawHeader("Location")));
			if (!location.isValid() || location.isEmpty()) {
				emit job->finished({}, "YouTube refused the upload: " + errorText(parse(start), start->errorString()));
				return;
			}

			// Step 2: send the file.
			auto *f = new QFile(file);
			if (!f->open(QIODevice::ReadOnly)) {
				f->deleteLater();
				emit job->finished({}, "Could not read the clip file.");
				return;
			}
			QNetworkRequest put(location);
			put.setHeader(QNetworkRequest::ContentTypeHeader, "video/mp4");
			put.setHeader(QNetworkRequest::ContentLengthHeader, size);
			QNetworkReply *up = m_net.put(put, f);
			f->setParent(up); // closes with the request
			connect(up, &QNetworkReply::uploadProgress, job, &UploadJob::progress);
			connect(up, &QNetworkReply::finished, this, [up, job] {
				up->deleteLater();
				const QJsonObject o = parse(up);
				if (o["id"].toString().isEmpty())
					emit job->finished({}, "Upload failed: " + errorText(o, up->errorString()));
				else
					emit job->finished("https://youtube.com/shorts/" + o["id"].toString(), {});
			});
		});
	});
	return job;
}
