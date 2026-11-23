# Rendering the videos

The remakes are meant to be watched on YouTube too, so nobody has to build or run anything. The videos are not
kept in the repository.

## What to render

`scripts/render-videos.sh` renders, for every demo folder with a remake (`<demo>` is the folder's name):

| video | contents |
|---|---|
| `<demo>-remake-2160p60.mp4` | the remake, 2880×2160, 60 fps, with its redrawn pictures |
| `<demo>-remake-original-art-2160p60.mp4` | the same with the original pixels (`--original-art`) |
| `<demo>-original-vs-remake.mp4` | the original (its emulator capture, pixels kept sharp) left, the remake right, 2880×1080 |

2880×2160 is 4:3 at 2160 lines, so YouTube offers it in 4K. The remakes render offline, frame by frame, so the result
does not depend on how fast the machine is.

```
scripts/render-videos.sh                    # every remake, and every comparison that can be made
scripts/render-videos.sh inside             # one remake
scripts/render-videos.sh inside-original    # with the original pixels
scripts/render-videos.sh inside-compare     # the original next to the remake
SIZE=1920x1440 FPS=60 scripts/render-videos.sh paper
```

The script needs the remakes built, the originals in `<demo>/original/`, and an `ffmpeg` with `libsvtav1` (AV1).
YouTube accepts AV1 in MP4. At CRF 18 the files are large but clean, which is what an upload needs. Run it in the
toolbox (the comparisons need python3 with numpy; ffmpeg is the host's).

A comparison needs the demo's emulator capture, `<demo>/re/full.frames` (its `re/README.md` says how to make it),
and `<demo>/re/frames2mp4.py`, which turns it into a video that starts at the remake's time 0: the music start, or
for Paper the first tick. The capture runs at the original's refresh rate and is converted to 60 fps; the sound is
the remake's. 303 has no emulator capture (its remake was compared with a video of the original), so it has no
comparison. The capture shows each frame a refresh or two after the remake does, as the original did on its
machine; each remake's README lists such differences.

A new demo gets all three videos without a change to the script if its program is
`<demo>/remake/build/<demo>-remake`, its `--video` renders the whole production by default, and its
`re/frames2mp4.py` takes `full.frames OUT --size WxH --vcodec "..."` and starts at the remake's time 0. The script's
three small tables (`remake_bin`, `remake_range`, `capture_args`) hold the exceptions: 303's program is `acme303`,
Paper's `--video` counts 70 Hz ticks, and DOPE's converter counts program time.

By hand, every remake renders a video itself:

```
inside/remake/build/inside-remake --video out.mp4 --fps 60 --shotsize 2880x2160 --vcodec "-c:v libsvtav1 -crf 18 -preset 5"
```

`--from` and `--to` select a range, in seconds of music time (303: seconds; Paper: 70 Hz ticks, the whole intro is
`--from 0 --to 12235`), and `--original-art` shows the original pixels.

## Uploading

Suggested title: *Paper by Psychic Link (Wired '96 64k intro), 4K remake*. In the description, put the credits of
the original, a link to this repository and the original's pouet.net page, and a note that it is a non-commercial
fan remake. Turn off monetisation for the video. The music belongs to the original's musicians (Vic for Paper and
303), and nobody should earn from it.

After uploading, put the links in the root README and the demo's README.
