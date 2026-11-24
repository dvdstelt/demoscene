"""Convert run_vivid.py's .frames to an MP4 (no sound).

usage: python3 frames2mp4.py full.frames out.mp4 [--from SEC] [--to SEC] [--all] [--size 960x720]
                             [--vcodec "-c:v libopenh264 -b:v 12M"]

Every record is one screen refresh at 70.086 Hz, so the video runs at 70.086 fps, one record per video frame. By
default it starts at the music start; --from / --to are seconds relative to that; --all starts at program start.
"""
import argparse
import shutil
import subprocess

from sheet import open_frames, rgb, read_ticks, music_start, W, H

ap = argparse.ArgumentParser()
ap.add_argument('frames')
ap.add_argument('out')
ap.add_argument('--from', dest='t0', type=float, default=0.0)
ap.add_argument('--to', dest='t1', type=float, default=1e9)
ap.add_argument('--all', action='store_true')
ap.add_argument('--size', default='960x720')
ap.add_argument('--vcodec', default='-c:v libopenh264 -b:v 12M')
a = ap.parse_args()

raw, n = open_frames(a.frames)
ticks = read_ticks(a.frames.rsplit('.', 1)[0] + '.ticks')
base = 0.0 if a.all else music_start(ticks)
sel = [k for k in sorted(ticks) if k < n and a.t0 <= ticks[k].time - base <= a.t1]
fps = 25175000 / (800 * 449)
cmd = (['flatpak-spawn', '--host'] if shutil.which('ffmpeg') is None else []) + [
    'ffmpeg', '-hide_banner', '-loglevel', 'error', '-y', '-f', 'rawvideo', '-pix_fmt', 'rgb24',
    '-s', '%dx%d' % (W, H), '-r', '%.6f' % fps, '-i', '-',
    '-vf', 'scale=%s:flags=neighbor' % a.size.replace('x', ':')] + a.vcodec.split() + ['-pix_fmt', 'yuv420p', a.out]
p = subprocess.Popen(cmd, stdin=subprocess.PIPE)
for k in sel:
    p.stdin.write(rgb(raw, k).tobytes())
p.stdin.close()
p.wait()
print('wrote %s: %d frames, %.2f s' % (a.out, len(sel), len(sel) / fps))
