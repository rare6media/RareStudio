#pragma once

#include <QString>

#include <functional>

class QWidget;

// Ready-made full-screen scenes: Starting soon, Be right back, Stream ending, Countdown. Each is a
// browser source showing the bundled overlay page, so there is nothing to install and the text,
// countdown and colours are chosen here.
namespace Overlays {

struct Spec {
	QString sceneName; // also the name of its browser source
	QString url;       // file:// address of the overlay page with the chosen options
};

// Writes the overlay page (and the logo it shows) under %APPDATA%\RareStudio\overlays and returns its folder.
QString prepare();
// Asks what to make. `taken` says whether a scene/source name is already in use. False if cancelled.
bool ask(QWidget *parent, const std::function<bool(const QString &)> &taken, Spec *out);

} // namespace Overlays
