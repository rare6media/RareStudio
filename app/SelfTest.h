#pragma once

#include <QString>

// Headless checks that render through the real engine and write images/files to disk, so
// features can be verified without a window (and without touching the user's screen or input).
// Run via command line flags handled in main.cpp.
namespace SelfTest {

// Switches between two test scenes with every transition and writes contact sheets
// (frames at 20/40/60/80% of each transition) into `outDir`.
int transitions(const QString &outDir);

// Builds the 9:16 Shorts canvas from a test scene, grabs a vertical frame, saves a clip and
// checks the file was written. Writes vertical.png and vertical.txt into `outDir`.
int vertical(const QString &outDir);

// Puts a webcam (async video source) on air with no window and renders for a few seconds.
// Writes video.txt into `outDir`; a crash here reproduces the render-thread crash.
int video(const QString &outDir);

// Builds the real main window and settings dialogs (run with QT_QPA_PLATFORM=offscreen) and
// renders them to PNGs in `outDir`, to catch construction crashes and layout problems.
int ui(const QString &outDir);

// Drives the real main window through the flows that used to crash or lose work: adding stream
// destinations, adding a media source and switching it to a URL/stream, and the live autosave.
// Writes stability.txt into `outDir` (PASS/FAIL per check); returns non-zero on any failure.
int stability(const QString &outDir);

} // namespace SelfTest
