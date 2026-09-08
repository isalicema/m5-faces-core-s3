# M5 Faces CoreS3 Suite

An ESP32-S3 firmware suite for an M5 Faces/CoreS3 stack with a Keyboard3 and
Bottom3. It includes a keyboard test, a Mac music remote, an internet-radio
player, and a small on-device companion.

## What is included

- `suite` firmware: a launcher for Music Remote (`M`), Radio (`R`), and
  Companion (`A`). `SYM + 0` acts as Escape to return to the launcher.
- A pairing-token protected Mac music bridge with a local browser preview.
  It supports Apple Music and NetEase Cloud Music metadata and playback
  actions; audio remains on the Mac.
- CoreS3 touch, Keyboard3 indicator, battery, display-power, radio, OTA, and
  companion implementations, together with unit and regression tests.

## Privacy and publication boundary

This repository deliberately contains no device flash backups, firmware
images, serial captures, test receipts, pairing tokens, Wi-Fi credentials,
hardware identifiers, USB port names, or host-specific launch configuration.
The bridge creates a new token in the ignored `.local/music.json` on first
launch. It binds to localhost by default; use `--lan` only on a trusted LAN
after pairing the device.

## Build

Install PlatformIO and the toolchain declared in `platformio.ini`, then run:

```sh
pio run -e suite
```

Use an explicitly identified serial port for any upload. This project does not
ship a device-specific flashing script or prebuilt image; inspect the target,
its partition layout, and your own backup/recovery plan before writing flash.

The standalone keyboard test and individual apps remain available as
environments such as `cores3`, `controller`, and `radio`.

## Run the Mac music bridge

The Bridge is optional and does not open a USB serial port.

```sh
./tools/build_music_helper.sh
./tools/start_music.command
```

Open the displayed localhost URL for the preview. For a paired physical device
on a trusted LAN, restart with `./tools/start_music.command --lan` and enter
the Bridge address, port, and pairing token through the device setup screen.

To publish an OTA candidate for a device you control, provide that device's
MAC address at publish time. The address is retained only in ignored local OTA
metadata and remains bound to the firmware manifest:

```sh
python3 tools/publish_ota.py --device-mac aa:bb:cc:dd:ee:ff
```

## Test

Python bridge tests can be run with:

```sh
python3 -m unittest discover -s tests -p 'test_*.py'
```

Several C++ model tests are intentionally standalone so they can be compiled
with a C++17 compiler and sanitizers. The firmware build is also an important
integration check.

## Third-party material

`third_party/stackchan/` records attribution for the StackChan-derived
companion geometry under its MIT license. The embedded Noto Sans CJK SC assets
remain under the SIL Open Font License; see `assets/fonts/OFL.txt`.
