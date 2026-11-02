# Changelog

All notable changes to MicVST are documented here.
The format is based on [Keep a Changelog](https://keepachangelog.com/), and this
project follows [Semantic Versioning](https://semver.org/).

## [1.2.0] - 2026-11-02

### Added
- Global hotkeys to toggle individual plugins. Click the hotkey field in a plugin row and
  press a key or combination - letters, digits, F1-F12 and the numeric keypad are accepted,
  with or without Ctrl/Alt/Shift/Win; Esc clears. The hotkey works while MicVST is in the
  tray and another application has focus, and it moves with the plugin when you reorder the
  chain.
  MicVST only watches for the key and never claims it, so binding something like Ctrl+C
  does not stop copying from working anywhere else. The flip side: a key without a modifier
  also fires while you use it in other programs, so MicVST asks before accepting one and
  outlines it in yellow afterwards.
  Nothing is bound by default, and as long as no hotkey is set MicVST does not watch the
  keyboard at all.

### Changed
- Plugin scanning no longer loads every single plugin in a file just to read its channel
  counts - the class metadata is read directly instead. Shell plugins get dramatically
  faster: WaveShell1-VST3 17.1 (347 Waves plugins) went from 178 s to 2.3 s. Files that
  this fast path cannot handle still fall back to the previous behaviour.
- Scan timeout per plugin raised from 120 s to 600 s. The old limit was right at the edge
  for Waves (~90 s warm, 178 s cold), so a cold first scan silently skipped the whole
  WaveShell - and a factory reset put users back in that state every time.

### Fixed
- Quitting MicVST while the plugin scan was still running threw away the whole scan, so
  the next start scanned everything again - for large collections (or with slow shell
  plugins like Waves WaveShell in the list) the scan never got the chance to finish and
  every start rescanned from scratch. Scan results are now saved after each file, so a
  quit at worst loses the plugin currently being scanned.
- The plugin chain no longer waits for the whole scan: it is restored as soon as its own
  plugins have been found. Previously a chain containing a not-yet-scanned plugin stayed
  empty (no microphone processing) for the entire scan, and was never restored at all if
  the scan got interrupted.
- "Retry skipped plugins" no longer forgets the plugins it has not reached yet when
  MicVST is quit mid-retry; each skip entry is now cleared only once its plugin has
  actually been retried.
- Plugins that ship as a bundle folder without a `moduleinfo.json` were rescanned on
  every single start, forever: they were stored under their inner binary's path while the
  scanner looked them up under the bundle path, so the cache never matched. This was a
  second, independent cause of the endless-rescan reports.
- Plugins housed in a shell file (all Waves plugins share one WaveShell) are now told
  apart in a saved chain. Previously only the file path was stored, so after a restart
  the chain got whichever plugin of that shell came first - with another plugin's saved
  settings applied to it.

### Internal
- The unit test runner prints its results to stdout; failures were previously only
  visible via the process exit code.

## [1.1.1] - 2026-07-27

### Added
- "Reset app (clear all data)..." in the Manage VST3 Folders menu: deletes all settings
  and the plugin cache, then closes MicVST for a fresh first-run setup.
- Scan diagnostics: skip reasons now include the scanner's exit code where relevant
  (e.g. "failed (exit 0xC0000005)"), and the scan log records why a file is rescanned.

### Fixed
- Plugins shipped as bundle folders (Acustica, Minimal Audio, UADx, ...) were rescanned
  on every start when vendor background services wrote into their bundles - the rescan
  check now tracks the plugin binary inside the bundle instead of the folder itself.
- A plugin that crashes the scanner mid-enumeration (e.g. Waves WaveShell) no longer
  loses everything: plugins found before the crash are kept and usable, and the file is
  marked "crashed (N plugin(s) rescued)" - use "Retry skipped plugins" to try again.

## [1.1.0] - 2026-07-22

### Added
- Searchable plugin picker: "+ Plugin" now opens a search box with live filtering
  (grouped by manufacturer when the search is empty). Built-ins are included.
- "Skip" button while scanning: skip the plugin that is currently being scanned.
- "Retry skipped plugins" (Manage VST3 Folders menu): rescans skipped plugins with a
  generous 10-minute timeout - lets huge shell plugins like Waves WaveShell finish.
- Buffer size dropdown (low-latency mode): when the selected devices support multiple
  buffer sizes, a "Buffer" row appears in the device panel ("Auto" = device default).

### Changed
- Scan timeout raised from 30 s to 120 s, so large shell plugins (e.g. Waves WaveShell)
  usually finish on the initial scan already.
- All standard VST3 locations are scanned (Program Files, %LOCALAPPDATA%, VST3_PATH).
- Audio devices now open in WASAPI low-latency mode when available (shared as fallback).

### Fixed
- Plugins that write into their own bundle during load (UAD, Acustica, ...) were
  rescanned on every start; their cache timestamp is now taken after the scan finishes.
- Plugin updates that change the internal plugin ID no longer cause endless rescans.
- Removing a custom folder like "C:\Plugins" no longer prunes plugins from sibling
  folders like "C:\Plugins2".
- Editing the plugin chain or the custom VST3 folders no longer resets the update-check
  consent and the saved window position in the config file.

## [1.0.3] - 2026-07-21

### Changed
- Window and tray icon now appear immediately; the VST3 scan runs in the background with
  visible progress (count + plugin name).
- "Manage custom VST3 Folders" is now "Manage VST3 Folders" and its menu also offers
  "Rescan all plugins" (previously only in the "+ Plugin" menu).

### Fixed
- A hanging or crashing VST3 no longer blocks startup invisibly: each plugin is scanned in an
  isolated helper process with a 30 s timeout and gets skipped (with a hint in the UI) instead.
- Scan results are cached (`plugin_cache.xml`) - startup no longer rescans every plugin.

## [1.0.2] - 2026-06-05

### Added
- Optional, **opt-in Auto-Update-Check** (off by default; one-time consent prompt on first
  start): checks the GitHub releases API once per startup, and on a newer version turns the
  version number into a download link and shows a one-time tray notification. No telemetry,
  no auto-installer.
- The window now **remembers its size and position** across restarts.
- Live **end-to-end latency** readout (now includes plugin/graph latency) shown right under
  the device list, together with the sample rate and buffer size.
- **Per-plugin latency** shown in each plugin row (left of "Bypass"), updated live.

### Changed
- Sample rate and buffer size are now **read-only** info (they're fixed by Windows shared
  audio anyway); the *Advanced* toggle and their dropdowns were removed.
- Smaller, more compact default window; extra height now extends the **plugin list**.

### Added (docs)
- README **Latency** section explaining the shared-audio path and why ASIO4All won't help.

## [1.0.1] - 2026-06-04

### Added
- **How To** screen - a quick setup guide with clickable links (download VB-Cable,
  "Check for updates on GitHub").
- Clickable **version number** in the main window (opens the GitHub repo).

### Changed
- Moved the status line (Active / sample rate / latency) out of the main window into the
  **Advanced** section of the device panel.

## [1.0.0] - 2026-06-04

### Added
- Initial public release.
- Route one microphone through a **VST3 plugin chain** into a **virtual audio cable**
  (VB-Cable, VoiceMeeter, Virtual Audio Cable), usable as a mic in any app.
- **Automatic cable detection** with an in-app download hint when none is installed.
- Built-in **Mono → Stereo** and **Stereo → Mono** nodes; channel-aware routing.
- **Drag-to-reorder** plugin list, per-row bypass, remove with confirmation, per-plugin
  editor windows.
- **Manage custom VST3 folders**; crash-resistant plugin scan (a plugin that crashes the
  scan is remembered and skipped next time).
- Horizontal **in/out level meters** with a dB scale.
- **Tray app** with silent autostart and an autostart checkbox.
- Persistent device + plugin settings (`%APPDATA%\MicVST\config.xml`).
- Portable, statically linked `.exe` (no installer, no Visual C++ Redistributable).
- GitHub Actions CI: build + unit tests on every push/PR, release build on tag.

[1.2.0]: https://github.com/philipz794/MicVST/releases/tag/v1.2.0
[1.1.1]: https://github.com/philipz794/MicVST/releases/tag/v1.1.1
[1.1.0]: https://github.com/philipz794/MicVST/releases/tag/v1.1.0
[1.0.3]: https://github.com/philipz794/MicVST/releases/tag/v1.0.3
[1.0.2]: https://github.com/philipz794/MicVST/releases/tag/v1.0.2
[1.0.1]: https://github.com/philipz794/MicVST/releases/tag/v1.0.1
[1.0.0]: https://github.com/philipz794/MicVST/releases/tag/v1.0.0
