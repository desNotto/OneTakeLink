# LensLink protocol code (ported)

Files in this folder come from the open-source
[LensLink](https://github.com/MyNamesEMurray/LensLink) project
(`obs-plugin/src`, commit `4988b11e5d46c12d7d20b1d1394282033e52f7af`),
licensed **GPL-2.0-or-later**. Each file states whether it was changed.

They contain the pieces that never depended on OBS:

| File | What it does |
|------|--------------|
| `protocol.h` | Packet framing of the LensLink wire protocol (20-byte header) |
| `json-reader.*` | Strict bounded JSON pull reader |
| `handshake.*` | HELLO / VIDEO_CONFIG parsers |
| `usbmux.*` | usbmuxd client (Apple Mobile Device Service on Windows) |
| `mdns.*` | One-shot Bonjour browse for `_lenslink._tcp` |
| `net-compat.h` | Winsock / POSIX socket shim |
| `ll-compat.h` | New: replacements for libobs helpers |

The OBS-specific parts of the plugin (source, properties, GPU textures,
decoder output) were rewritten in C++ in `src/camera/lenslink/`.
See `docs/LENSLINK_PROTOCOL.md` for the protocol specification.
