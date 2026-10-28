#pragma once

#include <QString>

// Writes a crash report (version, build, exception, call stack as module+offset) to
// %APPDATA%\RareStudio\crash.log when the process dies from an unhandled exception, abort or
// std::terminate. The release exe keeps its PDB in the build tree, so the offsets resolve to source
// lines. The next start tells the user and offers the logs folder.
namespace CrashReport {

void install(); // call first thing in main()
QString logsDir();
// Path of the crash report left by the previous run, once; empty if it ended normally.
QString takePending();

} // namespace CrashReport
