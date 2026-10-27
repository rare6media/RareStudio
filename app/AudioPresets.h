#pragma once

#include <obs.h>

#include <QList>
#include <QString>

// One place that knows which audio filters RareStudio recommends, how they are grouped in the UI and
// what "good defaults" look like for a microphone and for desktop audio.
namespace AudioPresets {

struct FilterInfo {
	QString id;    // libobs filter type id
	QString name;  // friendly name, also the default instance name
	QString group; // menu section
	QString blurb; // one-line description (tooltip)
};

// Curated audio filters that this build of libobs actually provides, in menu order.
QList<FilterInfo> audioFilters();

// Section a video filter type belongs in ("Color", "Keying", ...), by id.
QString videoGroup(const QString &typeId);

// Adds a filter with a unique name; returns it addref'd (caller releases) or null.
obs_source_t *addFilter(obs_source_t *source, const QString &typeId, const QString &baseName, obs_data_t *settings = nullptr);

// Recommended chains. Idempotent: a filter whose name already exists on the source is left alone.
// Mic: noise suppression -> noise gate -> compressor -> limiter. Desktop: limiter only.
void applyVoiceChain(obs_source_t *mic);
void applyDesktopChain(obs_source_t *desktop);

// Removes every filter from the source (used by "reset").
void clearFilters(obs_source_t *source);

// Sensible levels: desktop sits under the voice, nothing muted, no sync offset.
void resetLevels(obs_source_t *mic, obs_source_t *desktop);

int filterCount(obs_source_t *source);

} // namespace AudioPresets
