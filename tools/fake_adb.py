#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Fake `adb` + scrcpy server for testing the Android source without a phone.

Point OneTakeLink at it with environment variables:
    OTL_ADB=tools/fake_adb.py OTL_SCRCPY_SERVER=/any/file otl-cli record android out.mp4 5

Supports the commands OneTakeLink uses: `devices -l`, `push`, `forward`
(tcp:0 -> prints a port), `forward --remove`, and `shell ... app_process ...
com.genymobile.scrcpy.Server <version> key=value...`, which serves a camera
stream in the scrcpy wire format (dummy byte, 64-byte device name, codec id,
session packet, config packet, then media packets with 12-byte headers).

Env knobs: FAKE_ADB_STATE (device|unauthorized|none), FAKE_ADB_NO60=1
(refuse camera_fps=60 like many phones), FAKE_ADB_DROP_AFTER=<seconds>.
"""
import os, socket, struct, sys, time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from fake_phone import beep_pcm, make_stream, split_access_units  # noqa: E402
import threading  # noqa: E402

PORT = int(os.environ.get("FAKE_ADB_PORT", "27310"))
SERIAL = "FAKE0123456789"
VERSION = "5.0.1"


def nal_units(au):
    i, starts = 0, []
    while True:
        j = au.find(b"\x00\x00\x01", i)
        if j < 0:
            break
        starts.append(j + 3)
        i = j + 3
    for k, s in enumerate(starts):
        e = starts[k + 1] - 3 if k + 1 < len(starts) else len(au)
        while e > s and au[e - 1] == 0:
            e -= 1
        yield au[s:e]


def serve(args):
    opts = dict(a.split("=", 1) for a in args if "=" in a)
    version = next((a for a in args if a[:1].isdigit()), "")
    if version != VERSION:
        print(f"[server] ERROR: The server version ({VERSION}) does not match the client ({version})", flush=True)
        return 1
    fps = int(opts.get("camera_fps", "30"))
    if fps == 60 and os.environ.get("FAKE_ADB_NO60"):
        print("[server] ERROR: Could not open camera: unsupported fps 60", flush=True)
        return 1
    w, h = map(int, opts.get("camera_size", "1920x1080").split("x"))
    clip = os.path.join(os.environ.get("TMPDIR", "/tmp"), f"otl_fake_adb_{w}x{h}_{fps}.h264")
    if not os.path.exists(clip):
        make_stream(clip, w, h, fps, 6)
    units = split_access_units(open(clip, "rb").read())
    print(f"[server] INFO: Device: [Fake] Pixel 8 (Android 15), camera {w}x{h}@{fps}", flush=True)

    srv = socket.socket()
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(("127.0.0.1", PORT))
    srv.listen(1)
    conn, _ = srv.accept()
    conn.sendall(b"\x00")                                   # dummy byte
    audio_conn = None
    if opts.get("audio") == "true":
        audio_conn, _ = srv.accept()                        # 2nd socket: audio
        if os.environ.get("FAKE_ADB_NO_AUDIO"):
            audio_conn.sendall(struct.pack(">I", 0))        # "audio disabled"
            audio_conn.close()
            audio_conn = None
        else:
            audio_conn.sendall(struct.pack(">I", 0x00726177))  # "raw"
    conn.sendall(b"Pixel 8".ljust(64, b"\x00"))             # device meta
    conn.sendall(struct.pack(">I", 0x68323634))             # "h264"
    conn.sendall(struct.pack(">III", 0x80000000, w, h))     # session packet
    first_key = next(i for i, (_, k) in enumerate(units) if k)
    # Config packet: SPS+PPS of the first keyframe.
    cfg = b"".join(b"\x00\x00\x00\x01" + n for n in nal_units(units[first_key][0]) if n and (n[0] & 0x1F) in (7, 8))
    conn.sendall(struct.pack(">QI", 1 << 62, len(cfg)) + cfg)
    order = units[first_key:]
    drop = float(os.environ.get("FAKE_ADB_DROP_AFTER", "0"))
    start = time.monotonic()
    n = 0
    stop = [False]

    def audio_loop():
        sent = 0
        while not stop[0]:
            due = int((time.monotonic() - start) * 48000)
            if sent + 1024 > due:
                time.sleep(0.005)
                continue
            pcm = beep_pcm(sent, 1024)
            try:
                audio_conn.sendall(struct.pack(">QI", int(sent * 1e6 / 48000), len(pcm)) + pcm)
            except OSError:
                return
            sent += 1024

    if audio_conn:
        threading.Thread(target=audio_loop, daemon=True).start()
    try:
        while True:
            if drop and time.monotonic() - start > drop:
                break
            au, key = order[n % len(order)]
            # Like MediaCodec output: no AUD, no in-band SPS/PPS.
            payload = b"".join(b"\x00\x00\x00\x01" + u for u in nal_units(au) if u and (u[0] & 0x1F) not in (7, 8, 9))
            pts_us = int(n * 1e6 / fps)
            flags = pts_us | ((1 << 61) if key else 0)
            target = start + n / fps
            d = target - time.monotonic()
            if d > 0:
                time.sleep(d)
            conn.sendall(struct.pack(">QI", flags, len(payload)) + payload)
            n += 1
    except OSError:
        pass
    stop[0] = True
    conn.close()
    if audio_conn:
        audio_conn.close()
    return 0


def main():
    a = sys.argv[1:]
    state = os.environ.get("FAKE_ADB_STATE", "device")
    if a[:1] == ["-s"]:
        a = a[2:]
    if a[:1] == ["devices"]:
        print("List of devices attached")
        if state != "none":
            print(f"{SERIAL}\t{state} product:shiba model:Pixel_8 device:shiba transport_id:3")
        return 0
    if a[:1] == ["push"]:
        print("1 file pushed, 0 skipped.")
        return 0
    if a[:1] == ["forward"]:
        if "--remove" in a:
            return 0
        print(PORT)
        return 0
    if a[:1] == ["shell"] and "com.genymobile.scrcpy.Server" in a:
        i = a.index("com.genymobile.scrcpy.Server")
        return serve(a[i + 1:])
    print("fake adb: unsupported command " + " ".join(a), file=sys.stderr)
    return 1


if __name__ == "__main__":
    sys.exit(main())
