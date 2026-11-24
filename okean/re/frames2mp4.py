"""Convert run_okean.py's .frames (+ .ticks for the time of each frame) to a constant frame rate MP4.

usage: python3 frames2mp4.py full.frames out.mp4 [--fps 60] [--from SEC] [--to SEC] [--frame0 N]
                             [--size 960x720] [--vcodec "-c:v libopenh264 -b:v 12M"]

Each record is one displayed video frame (49.85 Hz in the intro's own 625-line "50 Hz" mode, 70.09 Hz with -M);
its start time is column 2 of .ticks. Output frame k at time t = start + k/fps shows the last record that started at or before t.
Frames are scaled to 4:3 (320x200 -> 960x720) with nearest-neighbour. The video has no sound. Times are seconds
from program start (the music starts about 0.11 s later; see PARTS.md).
"""
import argparse
import bisect
import shutil
import subprocess

from PIL import Image

from sheet import open_frames, frame

ap = argparse.ArgumentParser()
ap.add_argument('frames')
ap.add_argument('out')
ap.add_argument('--ticks', help='default: frames path with .ticks')
ap.add_argument('--fps', type=float, default=60.0)
ap.add_argument('--from', dest='t0', type=float, default=0.0)
ap.add_argument('--to', dest='t1', type=float, default=1e9)
ap.add_argument('--frame0', type=int, default=0, help='number of the first record (run resumed from a snapshot)')
ap.add_argument('--size', default='960x720')
ap.add_argument('--vcodec', default='-c:v libopenh264 -b:v 12M')
a = ap.parse_args()

raw, W, H, rec, hdr, n = open_frames(a.frames)
tf = a.ticks or a.frames.rsplit('.', 1)[0] + '.ticks'
nums, times = [], []
for line in open(tf):
    if line.startswith('#'):
        continue
    f = line.split()
    k = int(f[0]) - a.frame0
    if 0 <= k < n:
        nums.append(k)
        times.append(float(f[1]))
ow, oh = (int(x) for x in a.size.split('x'))
start, end = max(a.t0, times[0]), min(a.t1, times[-1])
# inside a toolbox without ffmpeg, run the host's
ffmpeg = ['ffmpeg'] if shutil.which('ffmpeg') else ['flatpak-spawn', '--host', 'ffmpeg']
cmd = ffmpeg + ['-hide_banner', '-loglevel', 'error', '-y', '-f', 'rawvideo', '-pix_fmt', 'rgb24',
       '-s', '%dx%d' % (ow, oh), '-r', str(a.fps), '-i', '-'] + a.vcodec.split() + ['-pix_fmt', 'yuv420p', a.out]
p = subprocess.Popen(cmd, stdin=subprocess.PIPE)
k, last, cache = 0, None, None
while True:
    t = start + k / a.fps
    if t > end:
        break
    i = max(bisect.bisect_right(times, t) - 1, 0)
    if i != last:
        rgb, w, h = frame(raw, nums[i], W, H, rec, hdr)
        cache = Image.fromarray(rgb).resize((ow, oh), Image.NEAREST).tobytes()
        last = i
    p.stdin.write(cache)
    k += 1
p.stdin.close()
p.wait()
print('wrote %s: %d frames at %g fps, %.2f s from %.3f s' % (a.out, k, a.fps, k / a.fps, start))
