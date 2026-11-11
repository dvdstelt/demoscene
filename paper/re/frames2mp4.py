"""Convert run_paper.py's .frames (per tick: 64000 bytes VGA + 768 bytes 6-bit palette) to an MP4.

usage: python3 frames2mp4.py full.frames out.mp4 [--audio full.wav] [--from TICK] [--to TICK] [--tick0 TICK]
                             [--size 960x720] [--vcodec "-c:v libopenh264 -b:v 10M"]
--tick0: the tick of the file's first record (a run resumed from a snapshot), so the audio lines up.
Video runs at 70 fps (one frame per timer tick), scaled to --size (4:3) with nearest-neighbour pixels.
"""
import argparse
import subprocess

import numpy as np

FR = 64000 + 768

ap = argparse.ArgumentParser()
ap.add_argument('frames')
ap.add_argument('out')
ap.add_argument('--audio')
ap.add_argument('--from', dest='t0', type=int, default=0)
ap.add_argument('--to', dest='t1', type=int, default=1 << 30)
ap.add_argument('--tick0', type=int, default=0)
ap.add_argument('--size', default='960x720')
ap.add_argument('--vcodec', default='-c:v libopenh264 -b:v 10M')
a = ap.parse_args()

data = np.memmap(a.frames, dtype=np.uint8, mode='r')
n = min(len(data) // FR, a.t1)
cmd = (['flatpak-spawn', '--host'] if __import__('shutil').which('ffmpeg') is None else []) + ['ffmpeg', '-hide_banner', '-loglevel', 'error', '-y', '-f', 'rawvideo', '-pix_fmt', 'rgb24',
       '-s', '320x200', '-r', '70', '-i', '-']
if a.audio:
    cmd += ['-ss', '%.4f' % ((a.t0 + a.tick0) / 70.0), '-i', a.audio, '-c:a', 'aac', '-b:a', '192k', '-shortest']
cmd += ['-vf', 'scale=%s:flags=neighbor' % a.size.replace('x', ':')] + a.vcodec.split() + ['-pix_fmt', 'yuv420p', a.out]
p = subprocess.Popen(cmd, stdin=subprocess.PIPE)
for t in range(a.t0, n):
    f = data[t * FR:(t + 1) * FR]
    pal = (f[64000:].reshape(256, 3).astype(np.uint16) * 255 // 63).astype(np.uint8)
    p.stdin.write(pal[f[:64000]].tobytes())
p.stdin.close()
p.wait()
print('wrote', a.out, n - a.t0, 'frames')
