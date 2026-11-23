"""Convert run_reve.py's .frames to an MP4 (no sound) that starts at the remake's time 0.

usage: python3 frames2mp4.py full.frames out.mp4 [--from SEC] [--to SEC] [--all] [--size 960x720]
                             [--start-refresh R] [--vcodec "-c:v libopenh264 -b:v 12M"]

Every record is one screen refresh at 70.086 Hz, so the video runs at 70.086 fps, one record per video frame. By
default it starts at the music start, the remake's time 0: the refresh at which the player starts its GUS timers,
--start-refresh R (default: the remake's Soundtrack::startRefresh as measured in the capture, START_REFRESH below);
--from / --to are seconds relative to that; --all starts at program start.
"""
import argparse
import shutil
import subprocess

from sheet import open_frames, rgb, W, H, FPS

# The refresh (record) at which the music starts in the capture: reve/re/notes-music.md, the remake's
# Soundtrack::startRefresh. Records before it are dropped, so video time 0 is the remake's music time 0.
START_REFRESH = 31

ap = argparse.ArgumentParser()
ap.add_argument('frames')
ap.add_argument('out')
ap.add_argument('--from', dest='t0', type=float, default=0.0)
ap.add_argument('--to', dest='t1', type=float, default=1e9)
ap.add_argument('--all', action='store_true')
ap.add_argument('--start-refresh', type=float, default=START_REFRESH)
ap.add_argument('--size', default='960x720')
ap.add_argument('--vcodec', default='-c:v libopenh264 -b:v 12M')
a = ap.parse_args()

raw, n = open_frames(a.frames)
base = 0.0 if a.all else a.start_refresh
sel = [k for k in range(n) if a.t0 <= (k - base) / FPS <= a.t1 and k >= base]
cmd = (['flatpak-spawn', '--host'] if shutil.which('ffmpeg') is None else []) + [
    'ffmpeg', '-hide_banner', '-loglevel', 'error', '-y', '-f', 'rawvideo', '-pix_fmt', 'rgb24',
    '-s', '%dx%d' % (W, H), '-r', '%.6f' % FPS, '-i', '-',
    '-vf', 'scale=%s:flags=neighbor' % a.size.replace('x', ':')] + a.vcodec.split() + ['-pix_fmt', 'yuv420p', a.out]
p = subprocess.Popen(cmd, stdin=subprocess.PIPE)
for k in sel:
    p.stdin.write(rgb(raw, k).tobytes())
p.stdin.close()
p.wait()
print('wrote %s: %d frames, %.2f s' % (a.out, len(sel), len(sel) / FPS))
