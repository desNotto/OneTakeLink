# scrcpy wire protocol as used by OneTakeLink (scrcpy 5.0.1)

OneTakeLink's Android source (`src/camera/android/AndroidSource.cpp`) is a
minimal client of the scrcpy server, video socket only. Reference:
[scrcpy doc/develop.md](https://github.com/Genymobile/scrcpy/blob/master/doc/develop.md).

1. `adb -s SERIAL push scrcpy-server /data/local/tmp/scrcpy-server.jar`
2. `adb -s SERIAL forward tcp:0 localabstract:scrcpy_<scid as %08x>` → prints the local port
3. `adb -s SERIAL shell CLASSPATH=/data/local/tmp/scrcpy-server.jar app_process / com.genymobile.scrcpy.Server 5.0.1 scid=<hex> video_source=camera audio=false control=false tunnel_forward=true send_dummy_byte=true send_device_meta=true send_stream_meta=true send_frame_meta=true video_codec=h264 video_bit_rate=… camera_facing=back camera_size=WxH camera_fps=N video_codec_options=i-frame-interval:int=1 cleanup=true`
   (the version must equal the server's exactly)
4. Connect to `127.0.0.1:<port>`; retry until a dummy byte `0x00` is read.
5. Stream (all integers big-endian):
   - 64 bytes: device name, NUL-padded
   - u32 codec id: `0x68323634` "h264", `0x68323635` "h265" (0 / 1 = stream disabled / error)
   - packets with a 12-byte header:
     - session packet: bit 63 set; bytes 4–7 width, 8–11 height (new capture session)
     - media packet: u64 `[0][config][key][pts µs:61]`, u32 size, then `size` bytes
       (Annex B). Config packets (SPS/PPS) are prepended to the next frame.
6. The server stops when the socket closes; `adb forward --remove tcp:<port>`.

Server log lines (`[server] INFO|WARN|ERROR: …`) arrive on the adb shell
output; errors such as "Camera mirroring is not supported before Android 12"
are shown to the user. A start that fails at 60 fps is retried once at 30 fps.
