#pragma once

// "Browser" source: renders a web page (alerts, chat, widgets, overlays) with the Qt WebEngine that
// already powers the dashboards, so no separate Chromium build is needed. It registers under OBS's own
// id (browser_source) and uses its setting names, so a scene saved by OBS loads with the page intact.
namespace BrowserSource {

void registerSource(); // after the plugins are loaded, on the UI thread

} // namespace BrowserSource
