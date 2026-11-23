# Rendering the videos

The remakes are meant to be watched on YouTube too, so nobody has to build or run anything. The videos are not
kept in the repository.

## What to render

| video | contents |
|---|---|
| `paper-remake-2160p60.mp4` | Paper remake, 2880×2160, 60 fps |
| `paper-original-vs-remake.mp4` | the original (emulator capture, pixels kept sharp) next to the remake, 2880×1080 |
| `303-remake-2160p60.mp4` | 303 remake, 2880×2160, 60 fps |
| `jizz-remake-2160p60.mp4` | JIZZ remake, 2880×2160, 60 fps |
| `stars-remake-2160p60.mp4` | STARS remake, 2880×2160, 60 fps |
| `dope-remake-2160p60.mp4` | DOPE remake, 2880×2160, 60 fps |
| `clone-remake-2160p60.mp4` | clone meets clone remake, 2880×2160, 60 fps |
| `inside-remake-2160p60.mp4` | Inside remake, 2880×2160, 60 fps |

2880×2160 is 4:3 at 2160 lines, so YouTube offers it in 4K. The remakes render offline, frame by frame, so the result does not depend on how fast the
machine is.

```
scripts/render-videos.sh                  # all
scripts/render-videos.sh paper            # one
SIZE=1920x1440 FPS=60 scripts/render-videos.sh paper
```

The script needs the remakes built, the originals in `<demo>/original/`, and an `ffmpeg` with `libsvtav1` (AV1).
YouTube accepts AV1 in MP4. At CRF 18 the files are large but clean, which is what an upload needs. The
comparison video also needs the emulator capture (`paper/re/README.md`).

By hand, the Paper remake does it itself:

```
paper/remake/build/paper-remake --video out.mp4 --from 0 --to 12235 --fps 60 --shotsize 2880x2160 \
    --vcodec "-c:v libsvtav1 -crf 18 -preset 5"
```

`--from` and `--to` are in the intro's 70 Hz ticks.

The 303 remake takes the same options, with `--from` and `--to` in seconds:

```
303/remake/build/acme303 --video out.mp4 --fps 60 --shotsize 2880x2160 --vcodec "-c:v libsvtav1 -crf 18 -preset 5"
```

So does the JIZZ remake, in seconds of music time (the intro is 147.75 s long):

```
jizz/remake/build/jizz-remake --video out.mp4 --fps 60 --shotsize 2880x2160 --vcodec "-c:v libsvtav1 -crf 18 -preset 5"
```

And the STARS, DOPE and clone meets clone remakes, in seconds of music time (STARS is 376.49 s long, its music
372.1 s; DOPE is 461.98 s long; clone meets clone 216.91 s). They show the redrawn pictures; add `--original-art`
for the original pixels (`scripts/render-videos.sh stars-original dope-original clone-original`).

```
stars/remake/build/stars-remake --video out.mp4 --fps 60 --shotsize 2880x2160 --vcodec "-c:v libsvtav1 -crf 18 -preset 5"
dope/remake/build/dope-remake --video out.mp4 --fps 60 --shotsize 2880x2160 --vcodec "-c:v libsvtav1 -crf 18 -preset 5"
clone/remake/build/clone-remake --video out.mp4 --fps 60 --shotsize 2880x2160 --vcodec "-c:v libsvtav1 -crf 18 -preset 5"
```

The Inside remake too, in seconds of music time (the demo is 186.1 s long); `scripts/render-videos.sh
inside-original` renders it with the original pixels:

```
inside/remake/build/inside-remake --video out.mp4 --fps 60 --shotsize 2880x2160 --vcodec "-c:v libsvtav1 -crf 18 -preset 5"
```

## Uploading

Suggested title: *Paper by Psychic Link (Wired '96 64k intro), 4K remake*. In the description, put the credits of
the original, a link to this repository and the original's pouet.net page, and a note that it is a non-commercial
fan remake. Turn off monetisation for the video. The music belongs to the original's musicians (Vic for Paper and
303), and nobody should earn from it.

After uploading, put the links in the root README and the demo's README.
