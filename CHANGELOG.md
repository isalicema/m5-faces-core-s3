# Changelog

## 2026-11-08 — O-key system AirPlay routing

- Added an `O` shortcut in Music and Collection to toggle the Mac's system
  audio between an exact-name AirPlay target and the previous output.
- Added an authenticated, single-flight Bridge endpoint with persistent request
  replay protection so uncertain or repeated requests do not toggle twice.
- Added a shared macOS helper that selects the exact Sound output row, hides
  System Settings after dispatch, and restores the prior foreground app without
  stealing focus from a third app.
- Added public setup guidance and regression coverage; runtime target names,
  output identifiers and route state remain in ignored local files.

This entry describes source and local validation only. It does not publish a
Release, deploy a production Bridge, perform OTA, or flash a device.

## 2026-09-18 — Paper UI and reliability sync

- Reworked Home, Music Remote and Internet Radio into a shared 320×240 paper UI.
- Added persistent battery/USB telemetry to Home, Music and Radio.
- Added native-stage asset generation from the self-contained public preview;
  all embedded fonts are generated from the repository's OFL-licensed Noto source.
- Improved music input responsiveness, action timeouts, staged artwork/theme
  replacement, lyric rendering and partial animation updates.
- Added Radio tuning, local audio controls, volume touch targets, and a browser
  preview that never starts Mac playback.
- Added input, paper geometry, radio layout and power-telemetry regression tests.

This entry describes source and local validation only. No prebuilt firmware,
device backup or device-specific deployment artifact is published.
