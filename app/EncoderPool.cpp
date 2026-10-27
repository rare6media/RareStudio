#include "EncoderPool.h"

#include "ObsCore.h"

#include <QSettings>

namespace {

// Hardware encoders first: they leave the CPU free for your game and the rest of your PC.
constexpr const char *kPreferred[] = {"obs_nvenc_h264_tex", "jim_nvenc", "h264_texture_amf",
				      "obs_qsv11_v2", "obs_qsv11", "obs_x264"};

} // namespace

EncoderPool &EncoderPool::instance()
{
	static EncoderPool pool;
	return pool;
}

QList<QPair<QString, QString>> EncoderPool::availableVideoEncoders()
{
	QList<QPair<QString, QString>> out;
	for (const char *id : kPreferred) {
		if (ObsCore::firstAvailableEncoder({id}))
			out.append({id, QString::fromUtf8(obs_encoder_get_display_name(id))});
	}
	return out;
}

QString EncoderPool::preferredVideoEncoder()
{
	// "auto" (or an encoder that is no longer available) = the first one in preference order.
	const QString wanted = QSettings().value("output/encoder", "auto").toString();
	const auto available = availableVideoEncoders();
	for (const auto &e : available)
		if (e.first == wanted)
			return e.first;
	return available.isEmpty() ? QString() : available.first().first;
}

bool EncoderPool::acquire(QString *error)
{
	if (m_refs > 0) {
		++m_refs;
		return true;
	}

	QSettings s;
	m_videoName = preferredVideoEncoder();
	if (m_videoName.isEmpty()) {
		if (error)
			*error = "No H.264 encoder available.";
		return false;
	}

	const int videoKbps = s.value("stream/video_kbps", 6000).toInt();
	const int audioKbps = s.value("stream/audio_kbps", 160).toInt();

	obs_data_t *vs = obs_data_create();
	obs_data_set_string(vs, "rate_control", "CBR");
	obs_data_set_int(vs, "bitrate", videoKbps);
	obs_data_set_int(vs, "keyint_sec", 2);
	if (m_videoName == "obs_x264")
		obs_data_set_string(vs, "preset", "veryfast");
	m_video = obs_video_encoder_create(m_videoName.toUtf8().constData(), "rare_video", vs, nullptr);
	obs_data_release(vs);

	obs_data_t *as = obs_data_create();
	obs_data_set_int(as, "bitrate", audioKbps);
	m_audio = obs_audio_encoder_create("ffmpeg_aac", "rare_audio", as, 0, nullptr);
	obs_data_release(as);

	if (!m_video || !m_audio) {
		if (error)
			*error = "Could not create encoders.";
		if (m_video)
			obs_encoder_release(m_video);
		if (m_audio)
			obs_encoder_release(m_audio);
		m_video = m_audio = nullptr;
		return false;
	}
	obs_encoder_set_video(m_video, obs_get_video());
	obs_encoder_set_audio(m_audio, obs_get_audio());
	m_refs = 1;
	return true;
}

void EncoderPool::release()
{
	if (m_refs <= 0 || --m_refs > 0)
		return;
	obs_encoder_release(m_video);
	obs_encoder_release(m_audio);
	m_video = m_audio = nullptr;
}
