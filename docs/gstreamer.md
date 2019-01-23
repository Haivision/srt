# Using SRT with GStreamer

Starting from ver. 1.14 GStreamer supports SRT (see the [v.1.14 release notes](https://gstreamer.freedesktop.org/releases/1.14/)).
See the SRT plugin for GStreamer on [git](https://gitlab.freedesktop.org/gstreamer/gst-plugins-bad/tree/master/ext/srt).


## Using GStreamer and SRT to set up a screensharing

Based on the description in [#7](https://github.com/Haivision/srt/issues/7). Note that the commands are likely to change slightly for gstreamer 1.16 (see this [issue](https://gitlab.freedesktop.org/gstreamer/gst-plugins-bad/issues/874#note_106395)).

If you don't want to build GSteamer, SRT, and all the plugins from source or don't have a distribution that has 1.14 readily available, you can use [`nix`](https://nixos.org/nix/) to reproduce what is shown further.

Simply install `nix`; then use the command bellow to open a shell where the following commands work.

```
NIX_PATH=nixpkgs=https://github.com/nh2/nixpkgs/archive/a94ff5f6aaa.tar.gz nix-shell -p gst_all_1.gstreamer \
-p gst_all_1.gst-plugins-good -p gst_all_1.gst-plugins-base -p gst_all_1.gst-plugins-bad \
-p gst_all_1.gst-plugins-ugly -p gst_all_1.gst-libav
```

### Sender server

Set up a sender server that will grab a source raw video from a desktop or a webcam, encode it with x.264 (H.264/AVC) encoder, pack it in `MPEG-TS` ([more info about live streaming](live-streaming.md)). Then pipe it into the SRT sink that sends it over the network to the receiver client. The streaming URI should looks like `uri=srt://<ip>:<port>`. In the examples below the streaming is sent to port 888 on a localhost by specifying `uri=srt://0.0.0.0:8888`.


##### For screensharing (Linux with X Display)

The `ximagesrc` GStreamer plugin can be used to capture X Display and create raw RGB video.
Refer to `ximagesrc` [RM](https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-ximagesrc.html) for configuration options.

```
/usr/bin/time gst-launch-1.0 ximagesrc startx=0 show-pointer=true use-damage=0 ! videoconvert \
! x264enc bitrate=32000 tune=zerolatency speed-preset=veryfast byte-stream=true threads=1 key-int-max=15 \
intra-refresh=true ! video/x-h264, profile=baseline, framerate=30/1 ! mpegtsmux \
! srtserversink uri=srt://0.0.0.0:8888/ latency=100
```

##### For webcam images

The `v4l2src` GStreamer plugin can be used to capture video from v4l2 devices, like webcams and TV cards. 
Refer to `v4l2src` [RM](https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good-plugins/html/gst-plugins-good-plugins-v4l2src.html) for further information.

```
/usr/bin/time gst-launch-1.0 v4l2src ! videoconvert ! x264enc bitrate=8000 tune=zerolatency speed-preset=superfast \
byte-stream=true threads=1 key-int-max=15 intra-refresh=true ! video/x-h264, profile=baseline ! mpegtsmux \
! srtserversink uri=srt://0.0.0.0:8888/ latency=100
```

##### Notes

* The `decodebin` can also be used to configure settings automatically. Using explicit pipeline elements here make it possible to tune the settings when needed.
* A use of `time` helps to determine when the thread is capped at 100%, while the the `thread=1` parameter makes the encoding use only one thread. Remove `threads=1` to allow multiple cores, or cjange the  `speed-preset` to reduce CPU load.
* The `timeout` setting can be tuned. A recommended timeout is 2x-2.5x of the expected roundtrip time.
* The password functionality works as well, but only if a password is `>= 10` characters long; otherwise it's completely ignored. See [this bug](https://gitlab.freedesktop.org/gstreamer/gst-plugins-bad/issues/694#note_106616) of GStreamer.

### Receiver client

A client connection over SRT to the server with URI `srt://127.0.0.1:8888` (localhost) or a remote server is set up. URI syntax is `srt://<ip>:<port>`. Then MPEG-TS demuxer and video decoder is used to get a decompressed video, that goes to a playback plugin `autovideosink`.  Note that multiple clients can connect to the server started earlier.

`gst-launch-1.0 srtclientsrc uri=srt://127.0.0.1:8888 ! tsdemux ! h264parse ! video/x-h264 ! avdec_h264 ! autovideosink sync=false`

This works over both the internet and localhost.
