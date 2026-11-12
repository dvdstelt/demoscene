#!/usr/bin/env python3
# The restored video frames of the art folder (art/README.md, docs/redrawing-pictures.md "Videos").
#
#   video_art.py inputs DUMP OUT
#       DUMP = re/dump/video (dumpvideo's PNG frames, re/notes-pictures.md section 3). Writes the clips to upload to
#       SeedVR2 as lossless VP9 MP4s (yuv444p, the clip's own frame rate): OUT/303_<CLIP>_<first>-<last>.mp4 with
#       the frames in UPLOAD.
#   video_art.py frames CLIP RESULT ART [--quality Q]
#       RESULT = the restored clip (animated WebP, PNG folder or any video ffmpeg reads), frame k = original frame k.
#       Writes the frames in INCLUDE as ART/video/<CLIP>/NNNN.jpg (4:4:4; PARANOID as grey, its luminance).
#
# Needs Pillow and ffmpeg (with libvpx-vp9 for "inputs").
import os
import subprocess
import sys
import tempfile

from PIL import Image, ImageSequence

# frame rates: one frame per 6 ticks (HARL), 8 ticks (VS, ALEX) of 70.086 Hz; PARANOID one per 4 half-rows of the
# module (0.20975 s)
RATE = {"HARL": "70086/6000", "VS": "70086/8000", "ALEX": "70086/8000", "PARANOID": "4000/839"}
# frames restored: what the parts show (HARL 0..116, VS 0..56, ALEX 0..22, PARANOID 0..24), with a margin, rounded
# to whole SeedVR2 batches of 61 frames; ALEX and PARANOID whole
UPLOAD = {"HARL": range(0, 122), "VS": range(0, 61), "ALEX": range(0, 43), "PARANOID": range(0, 45)}
# frames in the art folder: what the parts show plus a margin of four to six frames
INCLUDE = {"HARL": range(0, 122), "VS": range(0, 61), "ALEX": range(0, 27), "PARANOID": range(0, 31)}


def inputs(dump, out):
    os.makedirs(out, exist_ok=True)
    for clip, fr in UPLOAD.items():
        dst = os.path.join(out, f"303_{clip}_{fr.start}-{fr.stop - 1}.mp4")
        subprocess.run(["ffmpeg", "-v", "error", "-y", "-framerate", RATE[clip], "-start_number", str(fr.start),
                        "-i", os.path.join(dump, clip, "%04d.png"), "-frames:v", str(len(fr)), "-c:v", "libvpx-vp9",
                        "-lossless", "1", "-pix_fmt", "yuv444p", dst], check=True)
        print(dst)


def read_frames(path):
    if os.path.isdir(path):
        return [Image.open(os.path.join(path, f)).convert("RGB") for f in sorted(os.listdir(path))
                if f.lower().endswith(".png")]
    if path.lower().endswith(".webp"):
        with Image.open(path) as im:
            return [f.convert("RGB") for f in ImageSequence.Iterator(im)]
    with tempfile.TemporaryDirectory() as tmp:
        subprocess.run(["ffmpeg", "-v", "error", "-i", path, "-vsync", "passthrough",
                        os.path.join(tmp, "%04d.png")], check=True)
        return read_frames(tmp)


def frames(clip, result, art, quality):
    fr = read_frames(result)
    first = UPLOAD[clip].start
    od = os.path.join(art, "video", clip)
    os.makedirs(od, exist_ok=True)
    size = 0
    for n in INCLUDE[clip]:
        im = fr[n - first]
        if abs(im.width / im.height - 5 / 3) > 0.01:
            sys.exit(f"{clip} frame {n}: {im.width}x{im.height} is not 5:3")
        if clip == "PARANOID":
            im = im.convert("L")  # ITU-R 601 luma, the weights the paranoid shaders use
        dst = os.path.join(od, f"{n:04d}.jpg")
        im.save(dst, quality=quality, subsampling=0, optimize=True)
        size += os.path.getsize(dst)
    print(f"{clip}: {len(fr)} frames read, {len(INCLUDE[clip])} written, {size / 1e6:.1f} MB")


if __name__ == "__main__":
    a = sys.argv[1:]
    q = 90
    if "--quality" in a:
        i = a.index("--quality")
        q = int(a[i + 1])
        del a[i:i + 2]
    if len(a) == 3 and a[0] == "inputs":
        inputs(a[1], a[2])
    elif len(a) == 4 and a[0] == "frames":
        frames(a[1], a[2], a[3], q)
    else:
        sys.exit(__doc__ or "usage: video_art.py inputs DUMP OUT | frames CLIP RESULT ART [--quality Q]")
