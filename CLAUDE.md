# OneTakeLink — notes for development sessions

Windows desktop app (C++20, Qt 6 Widgets, FFmpeg) that records video takes
from an iPhone running the official **LensLink Camera** app, or from an
Android 12+ phone over adb using the bundled scrcpy server. Spec (Russian):
`docs/spec/Technical_Specification_OneTakeLink_v1.0.txt`. User-facing text
and the README are in **Russian**; code, comments and commits in English.

## Layout (maps to spec §14)

| Layer | Path | Notes |
|-------|------|-------|
| Camera/Input | `src/camera/` | `CameraSource` = interface; everything else depends only on it and `core/VideoTypes.h` |
| LensLink module | `src/camera/lenslink/` | `LensLinkSource` (worker thread: dial USB/Wi-Fi, protocol, decode), `VideoDecoder` (D3D11VA→DXVA2→software), `LensLinkDiscovery` |
| Shared stream | `src/camera/StreamHandler.*`, `VideoDecoder.*` | decode for preview, passthrough packets to the recorder, stream stats — used by every source |
| Android | `src/camera/android/` | `Adb` (bundled `android/adb.exe`, env `OTL_ADB`), `AndroidSource` (scrcpy 5.0.1 client: push server, `adb forward`, video socket; see `docs/SCRCPY_PROTOCOL.md`). `OTL_SCRCPY_VERSION` in CMake must equal `SCRCPY_VERSION` in build.yml |
| Discovery | `src/camera/DeviceDiscovery.*` | iPhones (usbmuxd + Bonjour) + `adb devices` |
| Ported C | `src/camera/lenslink/proto/` | from LensLink `obs-plugin/src`, GPL-2.0-or-later; keep the per-file provenance header up to date when changing them |
| Preview | `src/preview/` | `PreviewWidget` (QOpenGLWidget, NV12/I420 → RGB shader, QPainter overlays), `CropMath.h` shared with export |
| Recording | `src/recording/` | `Recorder` (passthrough remux, worker thread, GOP pre-roll), `Mp4Writer`, `TakeNamer` |
| Library | `src/library/`, `src/ui/LibraryPage.*`, `src/ui/PlayerPanel.*` | `MediaProbe`, `Thumbnailer` (disk cache in CacheLocation/thumbs), `VideoPlayer` (worker thread, pts-paced, exact seek / frame step, feeds a `PreviewWidget`) |
| UI | `src/ui/` | `MainWindow` (single screen: splitters [player │ monitor] / media pool │ inspector), `ShootPage` and `LibraryPage` are hidden logic owners that hand their widgets to MainWindow, `Style` |
| CLI | `tools/otl_cli.cpp` | `otl-cli devices|monitor|record|recover` |

Targets: `otl_core` (static, no Widgets), `OneTakeLink` (GUI), `otl-cli`, `otl_tests`.

## Key design decisions

- **Recording is passthrough**: the phone's H.264/HEVC access units are muxed
  into MP4 as-is (no re-encode). Written as fragmented MP4 `*.recording.mp4`
  (0.5 s fragments), remuxed to a regular faststart MP4 on stop; leftovers are
  repaired at startup (`Recorder::recoverInterrupted`). Re-encoding (NVENC/QSV/
  AMF/x264) would belong to an export layer — decided NOT to build (stage 4
  dropped): takes always go to a video editor in full frame; the format frame
  and safe zones are monitor guides only.
- Takes start at the last keyframe before REC (LensLink sends a keyframe every
  2–4 s and ignores keyframe requests).
- LensLink protocol: the phone listens on TCP 9979; we dial (USB via usbmuxd
  `localhost:27015` on Windows). See `docs/LENSLINK_PROTOCOL.md`. On connect we
  send `identify`, `reference off`, and once per connection `set_format`
  (default 1080p/60/h264) + `set_quality maximum` if STATE says it differs.
- Signals from worker threads (std::thread) reach the UI queued — always
  connect with a receiver/context object.
- Show only real camera controls (spec §5.2): gate on STATE fields.
- App data lives in `%LOCALAPPDATA%\OneTakeLink\OneTakeLink\` (settings.ini,
  `cache\thumbs`, `logs`). Everything written there must clean itself up:
  thumbnails are removed with their take and pruned at startup (60 days /
  256 MB), logs keep 2 sessions capped at 10 MB. README "Data on disk"
  documents it — keep that table in sync.
- README is a dry functional description in RU + EN; keep both in sync.
- UI strings are written in Russian inside `tr()` / `QCoreApplication::translate()`
  (never raw literals); English lives in `translations/onetakelink_en.ts`.
  After adding strings run `tools/update_translations.sh`, translate the new
  entries in the .ts, run it again and commit .ts + .qm. Language: settings
  `ui/language` (auto/ru/en), applied at startup; switching restarts the app.
- Social safe zones (Reels/Shorts) are approximations in `PreviewWidget.cpp`
  (`layoutFor`), drawn only inside a 9:16 crop frame. Top bar: format combo,
  Instagram/YouTube logo toggles (mutually exclusive, click again = off;
  turning one on forces 9:16), thirds-grid toggle (G). Icons are drawn in
  `src/ui/Icons.cpp` in the brand line style.
- Hotkeys: Space is always REC/STOP (spec §6.5); the take player uses K, `,` `.`;
  R / Shift+R rotate / reset the monitor.
- Monitor rotation (setting `camera/rotation`, clockwise) is written into takes
  as the MP4 display matrix (`av_display_rotation_set` takes a CLOCKWISE angle,
  `av_display_rotation_get` returns counter-clockwise; ffprobe shows -90 for a
  90° cw take, like portrait iPhone video). Player, thumbnails and MediaProbe
  apply it (`displayRotationCw`); crop/guides are in display space.
- Media pool = one QStandardItemModel + TakeFilterModel proxy shown by a
  thumbnail QListView and a detail QTreeView sharing one selection model.
- No telemetry, no network except the phone link.
- Brand palette (`src/ui/Style.cpp`): Cetacean Blue `#150A35` surfaces,
  Lavender `#A577FF` accent, Ghost White `#F5F7FC` text; REC stays red.
  Icon source is `assets/icon/onetakelink.svg`; regenerate the .ico/.png with
  `python3 assets/icon/make_icons.py` (needs rsvg-convert + Pillow).

## Build & test (Linux container)

```bash
apt-get install -y qt6-base-dev libavcodec-dev libavformat-dev libavutil-dev libswscale-dev libgl-dev ninja-build
cmake -S . -B build -G Ninja && cmake --build build && ./build/otl_tests
python3 tools/fake_phone.py &            # fake LensLink Camera on :9979
./build/otl-cli record wifi:127.0.0.1 /tmp/t.mp4 5
OTL_ADB=$PWD/tools/fake_adb.py OTL_SCRCPY_SERVER=/etc/hostname ./build/otl-cli record android /tmp/a.mp4 5
```

GUI on a virtual display: `Xvfb :99 &` then
`DISPLAY=:99 ./build/OneTakeLink --connect=wifi:127.0.0.1`.
Windows builds come from `.github/workflows/build.yml` (Qt 6.8.3 MSVC,
FFmpeg 7.1.1 gyan.dev shared). Don't include `windows.h`/`winsock2.h` inside
`extern "C"` blocks.
