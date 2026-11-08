#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Fake LensLink Camera for testing OneTakeLink without an iPhone.

Listens on TCP 9979 like the real app, sends HELLO / STATE / VIDEO_CONFIG
and an H.264 1080p60 test pattern (generated with the ffmpeg CLI), answers
TIMESYNC requests and prints the CONTROL commands it receives.

    python3 tools/fake_phone.py [--port 9979] [--fps 60] [--drop-after SEC]
    otl-cli monitor wifi:127.0.0.1
"""
import argparse, json, math, os, socket, struct, subprocess, sys, tempfile, threading, time

MAGIC = b"OBSC"
HELLO, VIDEO_CONFIG, VIDEO, PING, TS_REQ, TS_RESP, CONTROL, STATE = 1, 2, 3, 4, 5, 6, 7, 8
SCREEN_AUDIO = 10
AUDIO_RATE = 48000


def beep_pcm(first_sample, count, rate=AUDIO_RATE):
    """Stereo S16LE: a 50 ms 1 kHz beep at every whole second, silence
    otherwise -- lets a test check audio/video sync in the recording."""
    out = bytearray()
    for i in range(first_sample, first_sample + count):
        t = i / rate
        v = int(12000 * math.sin(2 * math.pi * 1000 * t)) if (t % 1.0) < 0.05 else 0
        out += struct.pack("<hh", v, v)
    return bytes(out)


def packet(ptype, payload=b"", pts=0, flags=0):
    return MAGIC + struct.pack(">BBHQI", 1, ptype, flags, pts, len(payload)) + payload


def make_stream(path, w, h, fps, seconds):
    subprocess.run([
        "ffmpeg", "-loglevel", "error", "-y", "-f", "lavfi",
        "-i", f"testsrc2=size={w}x{h}:rate={fps}", "-t", str(seconds),
        "-c:v", "libx264", "-preset", "ultrafast", "-tune", "zerolatency",
        "-bf", "0", "-g", str(fps * 2), "-pix_fmt", "yuv420p",
        "-x264-params", "repeat-headers=1", "-bsf:v", "h264_metadata=aud=insert",
        "-f", "h264", path], check=True)


def split_access_units(data):
    """Split an Annex B stream at access unit delimiters (NAL type 9)."""
    units, starts, i = [], [], 0
    while True:
        i = data.find(b"\x00\x00\x01", i)
        if i < 0:
            break
        if i + 3 < len(data) and (data[i + 3] & 0x1F) == 9:
            starts.append(i - 1 if i > 0 and data[i - 1] == 0 else i)
        i += 3
    starts.append(len(data))
    for a, b in zip(starts, starts[1:]):
        au = data[a:b]
        key = False
        j = 0
        while True:
            j = au.find(b"\x00\x00\x01", j)
            if j < 0:
                break
            if j + 3 < len(au) and (au[j + 3] & 0x1F) == 5:
                key = True
                break
            j += 3
        units.append((au, key))
    return units


def state(fps):
    return {"zoom": 1.0, "maxZoom": 10.0, "exposureBias": 0.0, "focusMode": "auto",
            "whiteBalanceMode": "auto", "exposureMode": "auto", "supportsWhiteBalanceLock": True,
            "camera": "back", "lens": "Main (Wide)", "lenses": ["Ultra Wide (0.5×)", "Main (Wide)"],
            "lensFactors": [0.5, 1.0], "resolution": "1080p", "fps": fps,
            "codec": "h264", "resolutions": ["720p", "1080p", "4K"], "frameRates": [30, 60],
            "codecs": ["h264", "hevc"], "quality": "balanced"}


def serve(conn, units, fps, drop_after, audio=False):
    conn.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    alive = [True]

    def reader():
        buf = b""
        while alive[0]:
            try:
                chunk = conn.recv(65536)
            except OSError:
                break
            if not chunk:
                break
            buf += chunk
            while len(buf) >= 20:
                _, _, ptype, _, pts, size = struct.unpack(">4sBBHQI", buf[:20])
                if len(buf) < 20 + size:
                    break
                payload, buf = buf[20:20 + size], buf[20 + size:]
                if ptype == TS_REQ:
                    try:
                        conn.sendall(packet(TS_RESP, struct.pack(">Q", pts), time.monotonic_ns()))
                    except OSError:
                        pass
                elif ptype == CONTROL:
                    print("  CONTROL", payload.decode("utf-8", "replace"), flush=True)
        alive[0] = False

    threading.Thread(target=reader, daemon=True).start()
    hello = {"name": "Fake iPhone", "app": "LensLink", "protocol": 1, "kind": "camera"}
    conn.sendall(packet(HELLO, json.dumps(hello).encode()))
    conn.sendall(packet(STATE, json.dumps(state(fps)).encode()))
    conn.sendall(packet(VIDEO_CONFIG, json.dumps(
        {"codec": "h264", "width": 1920, "height": 1080, "fps": fps, "kind": "camera"}).encode()))
    start = time.monotonic()
    start_ns = time.monotonic_ns()
    interval = 1.0 / fps
    n = 0
    audio_sent = 0  # samples
    # Start on a keyframe, then loop the clip.
    first_key = next(i for i, (_, k) in enumerate(units) if k)
    order = units[first_key:]
    while alive[0]:
        if drop_after and time.monotonic() - start > drop_after:
            print("  dropping the connection (simulated loss)", flush=True)
            break
        au, key = order[n % len(order)]
        target = start + n * interval
        delay = target - time.monotonic()
        if delay > 0:
            time.sleep(delay)
        try:
            # Video pts on the ideal frame grid of the same clock as audio.
            conn.sendall(packet(VIDEO, au, start_ns + int(n * 1e9 / fps), 1 if key else 0))
            if audio:
                due = int((time.monotonic() - start) * AUDIO_RATE)
                while audio_sent + 960 <= due:
                    pcm = beep_pcm(audio_sent, 960)
                    conn.sendall(packet(SCREEN_AUDIO, pcm, start_ns + int(audio_sent * 1e9 / AUDIO_RATE)))
                    audio_sent += 960
            if n % (fps * 2) == 0:
                conn.sendall(packet(PING))
        except OSError:
            break
        n += 1
    alive[0] = False
    conn.close()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=9979)
    ap.add_argument("--fps", type=int, default=60)
    ap.add_argument("--seconds", type=int, default=10, help="length of the looped test clip")
    ap.add_argument("--drop-after", type=float, default=0, help="close each connection after N s")
    ap.add_argument("--audio", action="store_true",
                    help='send phone-mic audio (as with "Send phone mic to OBS" on)')
    args = ap.parse_args()

    clip = os.path.join(tempfile.gettempdir(), f"otl_fake_{args.fps}.h264")
    if not os.path.exists(clip):
        print("generating test clip…", flush=True)
        make_stream(clip, 1920, 1080, args.fps, args.seconds)
    with open(clip, "rb") as f:
        units = split_access_units(f.read())
    print(f"{len(units)} access units; listening on :{args.port}", flush=True)

    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(("0.0.0.0", args.port))
    srv.listen(1)
    while True:
        conn, addr = srv.accept()
        print("client connected:", addr, flush=True)
        serve(conn, units, args.fps, args.drop_after, args.audio)
        print("client gone", flush=True)


if __name__ == "__main__":
    sys.exit(main())
