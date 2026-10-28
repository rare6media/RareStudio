# RareStudio changelog

Each release is a `## <version> - <date>` section. The newest goes first; the "What's new" window
shown after an update reads this file, so keep entries short and written for the person streaming.

## 0.11.0 - 2026-10-28

- **Clip editor**: every Short now opens in a review window before it goes anywhere. Watch it, trim it
  (drag the purple handles, or use Start here / End here), then save an edited copy or save and upload to
  YouTube with a title and visibility you choose. Space plays, I and O set the start and end. Turn the
  review off in Settings > Clips. Right-click any clip in the Clips panel and choose Edit / trim to open it
  later.
- **All audio tracks, managed**: clips are now recorded with separate audio tracks: Mix (everything),
  Microphone, Desktop and Apps & media. The editor shows a waveform for each one with Mute, Solo and a
  volume slider, and you hear exactly what the saved clip will sound like. Mute the Mix and bring in the
  tracks you want to rebalance, for example a louder mic over quieter game sound.
- Streams and recordings are unchanged: they still carry the Mix.

## 0.10.0 - 2026-10-28

- **Scene collections**: keep separate sets of scenes and sources (Just Chatting, Gaming, Podcast...).
  File > Scene collection lets you switch, create, duplicate, rename and delete them. Switching restarts
  RareStudio onto the chosen collection, which takes a few seconds and keeps everything safe. Your
  existing scenes become the "Default" collection.
- **Overlay scenes**: Edit > Add overlay scene makes a ready-to-use Starting Soon, Be Right Back, Stream
  Ending or Countdown scene with an animated aurora background. Choose the title and subtitle, a countdown
  of N minutes or to a clock time, and the message shown at zero. The countdown restarts each time the scene
  goes on air.
- RareStudio is now signed as Rare6Media.

## 0.9.0 - 2026-10-28

- **Startup sequence**: an aurora of light, streams of data flowing into the RareStudio logo, and your
  version number beneath the name. Click or press any key to skip it; turn animations off in Settings >
  Interface to skip it for good.
- **What's new window**: after an update RareStudio shows what changed, like OBS does. Reopen it any
  time from Help > What's new, or choose when it appears in Settings > Updates.
- **Version control** (Settings > Updates): see what you are running and the latest release, check now,
  keep older versions on disk, roll back to one if an update misbehaves, or skip a release you don't want.
- **Safer failures**: update downloads time out instead of hanging, retry once, and report what went
  wrong. If RareStudio ever crashes it writes a crash report (Help > Open logs folder) and tells you on
  the next start. Your scenes and stream keys are still restored from autosave.
- Vertical audio mixer strips, a source picker with previews, and a browser source.
- Tidier toolbar: related controls are grouped into capsules.

## 0.7.1 - 2026-10-27

- New RareStudio logo in the app, window, launcher and stinger.
- Recording pause and resume, with a PAUSED badge.
- Take a screenshot of what viewers see (File > Take screenshot); pause and screenshot have hotkeys.
- Copy, paste and duplicate sources from the Edit menu.
- Fixed a webcam crash when removing a source.

## 0.6.1 - 2026-10-27

- Live preview while editing source properties and filters.
- Game Capture hooks work with Store apps and sandboxed games.
- Destinations, keys and scenes are autosaved.

## 0.6.0 - 2026-10-27

- Professional audio mixer with organised filters and an Audio settings tab.
- Interface size and animation settings.
