#include "MultiStream.h"

#include "EncoderPool.h"

#include <QMetaObject>

bool MultiStream::start(const QList<Destination> &dests, QString *error)
{
	if (live())
		return true;

	QList<QPair<int, Destination>> active;
	for (int i = 0; i < dests.size(); ++i)
		if (dests[i].enabled && !dests[i].server.isEmpty())
			active.append({i, dests[i]});
	if (active.isEmpty()) {
		if (error)
			*error = "No enabled destinations.";
		return false;
	}

	EncoderPool &pool = EncoderPool::instance();
	if (!pool.acquire(error))
		return false;
	m_encoderName = pool.videoName();

	for (const auto &[index, d] : active) {
		Out o;
		o.index = index;

		obs_data_t *ss = obs_data_create();
		obs_data_set_string(ss, "server", d.server.toUtf8().constData());
		obs_data_set_string(ss, "key", d.key.toUtf8().constData());
		o.service = obs_service_create("rtmp_custom", d.name.toUtf8().constData(), ss, nullptr);
		obs_data_release(ss);

		o.output = obs_output_create("rtmp_output", d.name.toUtf8().constData(), nullptr, nullptr);
		if (!o.service || !o.output) {
			if (error)
				*error = "Could not create output for " + d.name;
			if (o.output)
				obs_output_release(o.output);
			if (o.service)
				obs_service_release(o.service);
			teardown();
			return false;
		}
		obs_output_set_service(o.output, o.service);
		obs_output_set_video_encoder(o.output, pool.video());
		obs_output_set_audio_encoder(o.output, pool.audio(), 0);

		signal_handler_t *sh = obs_output_get_signal_handler(o.output);
		auto *ctx = o.ctx = new SignalCtx{this, index};
		signal_handler_connect(sh, "start", onSignal, ctx);
		signal_handler_connect(sh, "stop", onSignal, ctx);
		signal_handler_connect(sh, "reconnect", onSignal, ctx);
		signal_handler_connect(sh, "reconnect_success", onSignal, ctx);

		m_outputs.append(o);
	}

	bool any = false;
	for (const Out &o : m_outputs) {
		const bool ok = obs_output_start(o.output);
		any |= ok;
		if (!ok) { // never connected, so no stop signal will come: say why now
			const char *err = obs_output_get_last_error(o.output);
			emit destinationState(o.index, err && *err ? QString("error: %1").arg(err) : "could not connect");
		} else {
			emit destinationState(o.index, "connecting...");
		}
	}
	if (!any) {
		if (error)
			*error = "No destination could start.";
		teardown();
		return false;
	}
	return true;
}

void MultiStream::onSignal(void *param, calldata_t *cd)
{
	auto *ctx = static_cast<SignalCtx *>(param);
	QString state;
	obs_output_t *out = (obs_output_t *)calldata_ptr(cd, "output");
	if (obs_output_active(out))
		state = obs_output_reconnecting(out) ? "reconnecting" : "live";
	else {
		const char *err = obs_output_get_last_error(out);
		state = err && *err ? QString("error: %1").arg(err) : "offline";
	}
	QMetaObject::invokeMethod(ctx->self, [self = ctx->self, i = ctx->index, state] {
		emit self->destinationState(i, state);
	}, Qt::QueuedConnection);
}

MultiStream::Stats MultiStream::stats() const
{
	Stats s;
	for (const Out &o : m_outputs) {
		s.dropped += obs_output_get_frames_dropped(o.output);
		s.total += obs_output_get_total_frames(o.output);
		s.bytes += obs_output_get_total_bytes(o.output);
	}
	return s;
}

void MultiStream::stop()
{
	for (const Out &o : m_outputs)
		obs_output_stop(o.output);
	teardown();
}

void MultiStream::teardown()
{
	if (m_outputs.isEmpty())
		return;
	for (Out &o : m_outputs) {
		if (obs_output_active(o.output))
			obs_output_force_stop(o.output);
		obs_output_release(o.output); // frees the signal handler, so ctx is safe to drop
		obs_service_release(o.service);
		delete o.ctx;
	}
	m_outputs.clear();
	EncoderPool::instance().release();
}
