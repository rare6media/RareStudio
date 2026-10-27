# RareStudio

A lighter OBS-style streaming studio for Windows, built on the real OBS engine (libobs) with a
small native Qt UI instead of OBS's full frontend.

- **Stream to several platforms at once** from a single encode (extra destinations cost bandwidth, not CPU/GPU).
- **All the OBS sources and filters** (display / window / game / webcam capture, audio, media, text, noise suppression, chroma key...).
- **Scenes, Studio Mode, per-scene transitions**, plus 11 RareStudio shader transitions (eased, motion-blurred, glitch, zoom, ripple...) and a procedural **brand stinger**; OBS stingers work too.
- **Embedded YouTube Studio and Twitch dashboards** (software-rendered, nothing runs until you open them).
- **Sign in once** with Twitch and Google to pull your stream keys automatically.
- **Instant replay clips**: full-size local clip, a **Twitch clip**, and a **9:16 YouTube Short** (rendered live on a vertical canvas, no conversion step) uploaded as soon as it is saved.
- Recording, global hotkeys, live CPU/RAM/dropped-frame readout, self-updating from GitHub Releases.

## Install

Download the latest `RareStudio-x.y.z.zip` from [Releases](../../releases), unzip it, and run
`installer\install.ps1` from a checkout, or build from source (below). The desktop shortcut points at a small
launcher that never changes; updates download into `%LOCALAPPDATA%\RareStudio\updates` and the launcher starts the newest version.

## Build from source

Requirements: Windows 10/11, Visual Studio 2022 Build Tools (C++ workload, Windows 11 SDK, ATL), Git, CMake, Ninja,
Python (for `aqtinstall`), and Qt 6.8 with WebEngine.

```powershell
scripts\build-obs.ps1      # builds the trimmed libobs (OBS Studio 32.2.2)
scripts\build-app.ps1      # builds RareStudio into build\stage
installer\install.ps1      # installs to Program Files + desktop shortcut
installer\package-update.ps1 -BaseUrl https://github.com/<you>/RareStudio/releases/download/v<version>   # release zip + latest.json
```

## Accounts and API credentials

RareStudio never ships anyone's API keys. To use Twitch/YouTube features you create your own (free) credentials once;
**File > Settings > Accounts > "How do I get these?"** lists the exact steps. Tokens are stored encrypted with Windows DPAPI.

Note: YouTube keeps videos uploaded by *unverified* API projects private until Google approves the project; you can
publish each Short from YouTube Studio. The default YouTube API quota allows roughly 6 uploads per day.

## Headless self-tests

These render through the real engine without opening a window (used to verify transitions and Shorts):

```
RareStudioApp.exe --selftest-transitions <dir>   # contact sheets of every transition
RareStudioApp.exe --selftest-vertical <dir>      # builds a 1080x1920 Short from a test scene
QT_QPA_PLATFORM=offscreen RareStudioApp.exe --selftest-ui <dir>
```

Set `RARESTUDIO_TEST_PROFILE=<dir>` to run against a throw-away settings/scenes profile.

## License

RareStudio is licensed under the **GNU GPL v2 or later** (see `LICENSE`) because it links libobs.
It includes and is built on [OBS Studio](https://obsproject.com) (c) Hugh Bailey and contributors, and uses Qt under the LGPL.
RareStudio is an independent project and is not affiliated with or endorsed by the OBS Project, Twitch or YouTube.
