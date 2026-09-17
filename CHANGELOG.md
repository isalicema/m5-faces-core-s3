# Changelog

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
