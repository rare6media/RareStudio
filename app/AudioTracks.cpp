#include "AudioTracks.h"

#include <callback/signal.h>

#include <cstring>

namespace AudioTracks {

QString name(int track)
{
	static const char *kNames[kCount] = {"Mix", "Microphone", "Desktop", "Apps & media"};
	return track >= 0 && track < kCount ? QString::fromUtf8(kNames[track]) : QString("Track %1").arg(track + 1);
}

uint32_t mixersFor(obs_source_t *source)
{
	const char *id = obs_source_get_id(source);
	if (id && !strcmp(id, "wasapi_input_capture"))
		return 0b0011; // Mix + Microphone
	if (id && !strcmp(id, "wasapi_output_capture"))
		return 0b0101; // Mix + Desktop
	return 0b1001;     // Mix + Apps & media
}

void route(obs_source_t *source)
{
	if (!source || !(obs_source_get_output_flags(source) & OBS_SOURCE_AUDIO))
		return;
	// Not for scenes and transitions: they carry other sources' audio on whatever tracks those have.
	if (obs_source_get_type(source) != OBS_SOURCE_TYPE_INPUT)
		return;
	obs_source_set_audio_mixers(source, mixersFor(source));
}

void routeAll()
{
	obs_enum_sources(
		[](void *, obs_source_t *s) {
			route(s);
			return true;
		},
		nullptr);
}

void install()
{
	signal_handler_connect(
		obs_get_signal_handler(), "source_create",
		[](void *, calldata_t *cd) { route(static_cast<obs_source_t *>(calldata_ptr(cd, "source"))); }, nullptr);
}

} // namespace AudioTracks
