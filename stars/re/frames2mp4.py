"""Convert run_stars.py's .frames (+ .ticks for the time of each record) to a constant frame rate MP4.

usage: python3 frames2mp4.py full.frames out.mp4 [--fps 60] [--from SEC] [--to SEC] [--frame0 F]
                             [--size 960x720] [--vcodec "-c:v libopenh264 -b:v 12M"] [--audio full.wav]

Each record is the screen (VGA memory + palette) at the start of a vertical retrace, 70.086 Hz; its time is column 2
of .ticks (program time in seconds). Output frame k at time t = from + k/fps shows the last record that started at
or before t. --from / --to are program time; the default starts at the music start (the first record whose GUS
player tick count, column 4, is non-zero). Pixels are scaled with nearest-neighbour to --size (4:3).
"""
import argparse
import bisect
import shutil
import subprocess

from sheet import open_frames, to_rgb

ap = argparse.ArgumentParser()
ap.add_argument('frames')
ap.add_argument('out')
ap.add_argument('--ticks', help='default: frames path with .ticks')
ap.add_argument('--fps', type=float, default=60.0)
ap.add_argument('--from', dest='t0', type=float)
ap.add_argument('--to', dest='t1', type=float, default=1e9)
ap.add_argument('--frame0', type=int, default=0, help='frame number of the first record (run resumed from a snapshot)')
ap.add_argument('--size', default='960x720')
ap.add_argument('--vcodec', default='-c:v libopenh264 -b:v 12M')
ap.add_argument('--audio', help='WAV to mux, starting at program time --audio-t0')
ap.add_argument('--audio-t0', type=float, default=0.0)
a = ap.parse_args()

raw, w, h, rec, hdr, n = open_frames(a.frames)
tf = a.ticks or a.frames.rsplit('.', 1)[0] + '.ticks'
recs, times, gus = [], [], []
for line in open(tf):
    if line.startswith('#'): continue
    f = line.split()
    fr = int(f[0])
    if 0 <= fr - a.frame0 < n:
        recs.append(fr - a.frame0)
        times.append(float(f[1]))
        gus.append(int(f[3]))
start = a.t0 if a.t0 is not None else next(tm for tm, g in zip(times, gus) if g > 0)
end = min(a.t1, times[-1])
# inside the toolbox there is no ffmpeg: run the host's
cmd = (['flatpak-spawn', '--host'] if shutil.which('ffmpeg') is None else []) + [
    'ffmpeg', '-hide_banner', '-loglevel', 'error', '-y', '-f', 'rawvideo', '-pix_fmt', 'rgb24',
    '-s', '%dx%d' % (w, h), '-r', str(a.fps), '-i', '-']
if a.audio:
    cmd += ['-ss', '%.4f' % (start - a.audio_t0), '-i', a.audio, '-c:a', 'aac', '-b:a', '192k', '-shortest']
cmd += ['-vf', 'scale=%s:flags=neighbor' % a.size.replace('x', ':')] + a.vcodec.split() + ['-pix_fmt', 'yuv420p', a.out]
p = subprocess.Popen(cmd, stdin=subprocess.PIPE)
k, last, cache = 0, None, None
while True:
    t = start + k / a.fps
    if t > end: break
    i = max(0, bisect.bisect_right(times, t) - 1)
    if i != last:
        cache = to_rgb(raw, recs[i], w, h, rec, hdr).tobytes()
        last = i
    p.stdin.write(cache)
    k += 1
p.stdin.close()
p.wait()
print('wrote %s: %d frames at %g fps, %.2f s from program time %.3f s' % (a.out, k, a.fps, k / a.fps, start))
