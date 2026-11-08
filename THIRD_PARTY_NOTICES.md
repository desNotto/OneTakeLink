# Third-party components

## LensLink — GPL-2.0-or-later
https://github.com/MyNamesEMurray/LensLink

The Windows connection module is derived from the LensLink OBS plugin
(`obs-plugin/src`, commit `4988b11e5d46c12d7d20b1d1394282033e52f7af`):

- `src/camera/lenslink/proto/*` — copied; each file states whether it was modified.
- `src/camera/lenslink/LensLinkSource.*`, `VideoDecoder.*`, `LensLinkDiscovery.*` —
  rewritten from `ios-camera-source.c`, `h264-decoder.c` and the property UI,
  without libobs.
- `docs/LENSLINK_PROTOCOL.md` — copy of `docs/PROTOCOL.md`.

The LensLink Camera iOS app itself is not part of this project; users install
the official app from the App Store.

## scrcpy server — Apache-2.0
https://github.com/Genymobile/scrcpy — `android/scrcpy-server` (v5.0.1) is
pushed to Android phones to capture the camera. `src/camera/android/` is an
independent client implementation of its documented protocol.

## Android Debug Bridge (adb) — Apache-2.0 (AOSP)
`android/adb.exe`, `AdbWinApi.dll`, `AdbWinUsbApi.dll`, redistributed as
packaged in the scrcpy Windows release.

## Qt 6 — LGPL-3.0
https://www.qt.io — dynamically linked (Qt*.dll next to the executable).
Source: https://download.qt.io/official_releases/qt/

## FFmpeg — GPL-3.0 (gyan.dev "full_build-shared" build)
https://ffmpeg.org — dynamically linked (av*.dll, sw*.dll).
Windows builds: https://www.gyan.dev/ffmpeg/builds/ ,
source: https://github.com/GyanD/codexffmpeg and https://ffmpeg.org/download.html

## Microsoft Visual C++ runtime
msvcp140.dll, vcruntime140.dll, vcruntime140_1.dll — redistributed under the
Visual Studio redistributable license terms.
