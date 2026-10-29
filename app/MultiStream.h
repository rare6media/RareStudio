#pragma once

#include <obs.h>

#include <QList>
#include <QObject>
#include <QString>

struct Destination {
	QString name;
	QString server;
	QString key;
	bool enabled = true;
};

struct SignalCtx {
	class MultiStream *self;
	int index; // position in the list handed to start(), so two "Twitch" rows never get each other's status
};

// One encode, many RTMP outputs: a single video and audio encoder are shared by
// every destination, so adding platforms costs upload bandwidth, not CPU/GPU.
class MultiStream : public QObject {
	Q_OBJECT
public:
	explicit MultiStream(QObject *parent = nullptr) : QObject(parent) {}
	~MultiStream() override { stop(); }

	bool start(const QList<Destination> &dests, QString *error);
	void stop();
	bool live() const { return !m_outputs.isEmpty(); }

	QString encoderName() const { return m_encoderName; }

	struct Stats {
		int dropped = 0;
		int total = 0;
		quint64 bytes = 0; // all destinations combined
	};
	Stats stats() const;

	// Per destination, for the Stream Health panel.
	struct DestStats {
		int index = -1; // position in the list handed to start()
		QString name;
		bool active = false;
		bool reconnecting = false;
		int dropped = 0;
		int total = 0;
		quint64 bytes = 0;
		double congestion = 0.0; // 0 = clear, 1 = the connection cannot keep up
		int connectMs = 0;
	};
	QList<DestStats> destStats() const;

signals:
	void destinationState(int index, const QString &state);

private:
	struct Out {
		int index = -1;
		QString name;
		SignalCtx *ctx = nullptr;
		obs_output_t *output = nullptr;
		obs_service_t *service = nullptr;
	};

	static void onSignal(void *param, calldata_t *cd);
	void teardown();

	QList<Out> m_outputs;
	QString m_encoderName;
};
