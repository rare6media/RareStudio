#pragma once

#include <QList>
#include <QString>

// RareStudio's own scene transitions. They are real libobs transition sources (so they work
// with everything else: scene overrides, studio mode, audio crossfade) drawn by one GPU shader.
// Compared with OBS's built-ins they add selectable easing curves (including overshoot),
// motion blur / glitch / zoom / ripple effects, and a procedural branded stinger that needs no
// video file.
namespace RareTransitions {

// Registers every rare_* transition with libobs. Call once, after the engine has started.
void registerAll();

struct Info {
	QString id;
	QString name;
};
QList<Info> list();

} // namespace RareTransitions
