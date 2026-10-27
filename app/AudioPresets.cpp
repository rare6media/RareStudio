#include "AudioPresets.h"

#include <cmath>
#include <cstring>

namespace AudioPresets {

namespace {

bool registered(const char *id)
{
	const char *t = nullptr;
	for (size_t i = 0; obs_enum_filter_types(i, &t); ++i)
		if (strcmp(t, id) == 0)
			return true;
	return false;
}

// Noise suppression v2 (RNNoise / Speex / NVIDIA) supersedes the original filter.
const char *noiseSuppressId()
{
	return registered("noise_suppress_filter_v2") ? "noise_suppress_filter_v2" : "noise_suppress_filter";
}

bool hasFilterNamed(obs_source_t *source, const QString &name)
{
	if (obs_source_t *f = obs_source_get_filter_by_name(source, name.toUtf8().constData())) {
		obs_source_release(f);
		return true;
	}
	return false;
}

void addIfMissing(obs_source_t *source, const char *id, const QString &name, obs_data_t *settings)
{
	if (!registered(id) || hasFilterNamed(source, name)) {
		obs_data_release(settings);
		return;
	}
	if (obs_source_t *f = addFilter(source, id, name, settings))
		obs_source_release(f);
	obs_data_release(settings);
}

} // namespace

QList<FilterInfo> audioFilters()
{
	const QList<FilterInfo> all = {
		{noiseSuppressId(), "Noise Suppression", "Cleanup", "Removes fans, keyboard and room hiss (RNNoise)."},
		{"noise_gate_filter", "Noise Gate", "Cleanup", "Silences the mic when you are not talking."},
		{"expander_filter", "Expander", "Cleanup", "A gentler gate: turns quiet noise down instead of cutting it."},
		{"compressor_filter", "Compressor", "Dynamics", "Evens out loud and quiet speech for a steady voice."},
		{"upward_compressor_filter", "Upward Compressor", "Dynamics", "Lifts quiet parts without touching loud ones."},
		{"limiter_filter", "Limiter", "Dynamics", "A ceiling that stops sudden shouts and explosions from clipping."},
		{"gain_filter", "Gain", "Level & Tone", "Raises or lowers the level by a fixed number of dB."},
		{"basic_eq_filter", "Equalizer (3-band)", "Level & Tone", "Shape low, mid and high frequencies."},
		{"invert_polarity_filter", "Invert Polarity", "Level & Tone", "Fixes phase cancellation between two mics."},
		{"async_delay_filter", "Delay", "Level & Tone", "Delays this source to line it up with video."},
	};
	QList<FilterInfo> out;
	for (const FilterInfo &f : all)
		if (registered(f.id.toUtf8().constData()))
			out.append(f);
	return out;
}

QString videoGroup(const QString &id)
{
	if (id.contains("key"))
		return "Keying";
	if (id.contains("color") || id.contains("lut") || id.contains("hdr") || id.contains("sdr"))
		return "Color";
	if (id.contains("crop") || id.contains("scale") || id.contains("scroll") || id.contains("mask") ||
	    id.contains("delay"))
		return "Layout & Timing";
	if (id.contains("blur") || id.contains("sharp") || id.contains("blend") || id.contains("rare_"))
		return "Effects";
	return "Other";
}

obs_source_t *addFilter(obs_source_t *source, const QString &typeId, const QString &baseName, obs_data_t *settings)
{
	QString name = baseName;
	for (int n = 2; obs_source_t *dup = obs_source_get_filter_by_name(source, name.toUtf8().constData()); ++n) {
		obs_source_release(dup);
		name = QString("%1 %2").arg(baseName).arg(n);
	}
	obs_source_t *f = obs_source_create(typeId.toUtf8().constData(), name.toUtf8().constData(), settings, nullptr);
	if (!f)
		return nullptr;
	obs_source_filter_add(source, f);
	return f;
}

void applyVoiceChain(obs_source_t *mic)
{
	if (!mic)
		return;

	obs_data_t *ns = obs_data_create();
	obs_data_set_string(ns, "method", "rnnoise");
	addIfMissing(mic, noiseSuppressId(), "Noise Suppression", ns);

	obs_data_t *gate = obs_data_create();
	obs_data_set_double(gate, "open_threshold", -30.0);
	obs_data_set_double(gate, "close_threshold", -36.0);
	obs_data_set_int(gate, "attack_time", 25);
	obs_data_set_int(gate, "hold_time", 200);
	obs_data_set_int(gate, "release_time", 150);
	addIfMissing(mic, "noise_gate_filter", "Noise Gate", gate);

	obs_data_t *comp = obs_data_create();
	obs_data_set_double(comp, "ratio", 4.0);
	obs_data_set_double(comp, "threshold", -18.0);
	obs_data_set_int(comp, "attack_time", 6);
	obs_data_set_int(comp, "release_time", 60);
	obs_data_set_double(comp, "output_gain", 0.0);
	addIfMissing(mic, "compressor_filter", "Compressor", comp);

	obs_data_t *lim = obs_data_create();
	obs_data_set_double(lim, "threshold", -3.0);
	obs_data_set_int(lim, "release_time", 60);
	addIfMissing(mic, "limiter_filter", "Limiter", lim);
}

void applyDesktopChain(obs_source_t *desktop)
{
	if (!desktop)
		return;
	obs_data_t *lim = obs_data_create();
	obs_data_set_double(lim, "threshold", -2.0);
	obs_data_set_int(lim, "release_time", 60);
	addIfMissing(desktop, "limiter_filter", "Limiter", lim);
}

void clearFilters(obs_source_t *source)
{
	if (!source)
		return;
	QList<obs_source_t *> filters;
	obs_source_enum_filters(
		source,
		[](obs_source_t *, obs_source_t *f, void *p) {
			static_cast<QList<obs_source_t *> *>(p)->append(obs_source_get_ref(f));
		},
		&filters);
	for (obs_source_t *f : filters) {
		obs_source_filter_remove(source, f);
		obs_source_release(f);
	}
}

void resetLevels(obs_source_t *mic, obs_source_t *desktop)
{
	if (mic) {
		obs_source_set_volume(mic, 1.0f);
		obs_source_set_muted(mic, false);
		obs_source_set_sync_offset(mic, 0);
	}
	if (desktop) {
		obs_source_set_volume(desktop, std::pow(10.0f, -8.0f / 20.0f));
		obs_source_set_muted(desktop, false);
		obs_source_set_sync_offset(desktop, 0);
	}
}

int filterCount(obs_source_t *source)
{
	int n = 0;
	if (source)
		obs_source_enum_filters(
			source, [](obs_source_t *, obs_source_t *, void *p) { ++*static_cast<int *>(p); }, &n);
	return n;
}

} // namespace AudioPresets
