"""Convert run_okean.py's .frames (+ .ticks for the time of each frame) to a constant frame rate MP4.

usage: python3 frames2mp4.py full.frames out.mp4 [--fps 60] [--from SEC] [--to SEC] [--frame0 N]
                             [--size 960x720] [--vcodec "-c:v libopenh264 -b:v 12M"]

Each record is one displayed video frame (49.85 Hz in the intro's own 625-line "50 Hz" mode, 70.09 Hz with -M):
record k is video memory at the end of refresh k, which the monitor shows during refresh k + 1, so it is shown from
the start of the next record (column 2 of .ticks of record k + 1). Output frame j at time t = start + j/fps shows the
last record shown at or before t (black before the first). Frames are scaled to 4:3 (320x200 -> 960x720) with
nearest-neighbour. The video has no sound. Times are seconds from program start, as the remake's music time.
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
rows = [line.split() for line in open(tf) if not line.startswith('#')]
for i, f in enumerate(rows):
    k = int(f[0]) - a.frame0
    if 0 <= k < n:
        nums.append(k)
        # shown from the start of the next refresh
        t = float(rows[i + 1][1]) if i + 1 < len(rows) else 2 * float(f[1]) - float(rows[i - 1][1])
        times.append(t)
ow, oh = (int(x) for x in a.size.split('x'))
start, end = a.t0, min(a.t1, times[-1] + (times[-1] - times[-2]))
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
    i = bisect.bisect_right(times, t) - 1
    if i != last:
        if i < 0:
            cache = bytes(ow * oh * 3)
        else:
            rgb, w, h = frame(raw, nums[i], W, H, rec, hdr)
            cache = Image.fromarray(rgb).resize((ow, oh), Image.NEAREST).tobytes()
        last = i
    p.stdin.write(cache)
    k += 1
p.stdin.close()
p.wait()
print('wrote %s: %d frames at %g fps, %.2f s from %.3f s' % (a.out, k, a.fps, k / a.fps, start))
