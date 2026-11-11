"""Convert run_jizz.py's .frames (+ .ticks for the time of each record) to a constant frame rate MP4.

usage: python3 frames2mp4.py full.frames out.mp4 [--fps 60] [--from SEC] [--to SEC] [--all] [--tick0 TICK]
                             [--size 960x720] [--vcodec "-c:v libopenh264 -b:v 12M"]

Each record is the page visible at the end of its timer tick; the tick times (column 3 of .ticks) are not evenly
spaced (18.2 Hz BIOS rate during the precalc, then MXMPlay's tick rate). Output frame k at time t = start + k/fps
shows the last record whose tick ended at or before t. By default the video starts at the music start (the first
tick with a music order >= 0, i.e. when xmpPlay installed its timer); --from / --to are seconds relative to that.
--all starts at program start instead (the precalc is black: text mode). The video has no sound.
"""
import argparse
import bisect
import shutil
import subprocess

import numpy as np

from sheet import open_frames, to_rgb

ap = argparse.ArgumentParser()
ap.add_argument('frames')
ap.add_argument('out')
ap.add_argument('--ticks', help='default: frames path with .ticks')
ap.add_argument('--fps', type=float, default=60.0)
ap.add_argument('--from', dest='t0', type=float, default=0.0)
ap.add_argument('--to', dest='t1', type=float, default=1e9)
ap.add_argument('--all', action='store_true')
ap.add_argument('--tick0', type=int, default=0, help='tick of the first record (run resumed from a snapshot)')
ap.add_argument('--size', default='960x720')
ap.add_argument('--vcodec', default='-c:v libopenh264 -b:v 12M')
a = ap.parse_args()

raw, w, h, bits, rec, hdr, n = open_frames(a.frames)
tf = a.ticks or a.frames.rsplit('.', 1)[0] + '.ticks'
ticks, times, orders = [], [], []
for line in open(tf):
    if line.startswith('#'): continue
    f = line.split()
    t = int(f[0])
    if 0 <= t - a.tick0 < n:
        ticks.append(t - a.tick0)
        times.append(float(f[2]))
        orders.append(int(f[3]))
base = 0.0 if a.all else next(tm for tm, o in zip(times, orders) if o >= 0)
start, end = base + a.t0, min(base + a.t1, times[-1])
cmd = (['flatpak-spawn', '--host'] if shutil.which('ffmpeg') is None else []) + [
    'ffmpeg', '-hide_banner', '-loglevel', 'error', '-y', '-f', 'rawvideo', '-pix_fmt', 'rgb24',
    '-s', '%dx%d' % (w, h), '-r', str(a.fps), '-i', '-',
    '-vf', 'scale=%s:flags=neighbor' % a.size.replace('x', ':')] + a.vcodec.split() + ['-pix_fmt', 'yuv420p', a.out]
p = subprocess.Popen(cmd, stdin=subprocess.PIPE)
k, last, cache = 0, None, None
while True:
    t = start + k / a.fps
    if t > end: break
    i = bisect.bisect_right(times, t) - 1
    if i < 0: i = 0
    if i != last:
        cache = to_rgb(raw, ticks[i], w, h, bits, rec, hdr).tobytes()
        last = i
    p.stdin.write(cache)
    k += 1
p.stdin.close()
p.wait()
print('wrote %s: %d frames at %g fps, %.2f s from %.3f s (program time; music start %.3f s)' % (
    a.out, k, a.fps, k / a.fps, start, base))
