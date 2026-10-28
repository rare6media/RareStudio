#pragma once

#include <obs.h>

#include <QString>
#include <QStringList>

// Clip audio is saved as separate tracks so the clip editor can balance or mute each one:
//   1  Mix           everything (this is also what a stream carries)
//   2  Microphone    microphones and line-ins
//   3  Desktop       what the PC plays (the Desktop Audio source and other playback devices)
//   4  Apps & media  app/game audio, media files, browser sources: everything else
// Every audio source is routed to track 1 plus the one track for its kind. The streaming and
// recording encoders only read track 1, so nothing about them changes.
namespace AudioTracks {

constexpr int kCount = 4;

QString name(int track);        // "Mix", "Microphone", ...; "Track n" beyond the plan
uint32_t mixersFor(obs_source_t *source); // the mask a source should have
void route(obs_source_t *source);         // applies it (audio sources only)
void routeAll();                          // every existing source: after scenes are loaded
void install();                           // routes sources created from now on

} // namespace AudioTracks
