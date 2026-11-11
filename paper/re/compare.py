"""Put up to four .frames runs side by side (2x2 grid, 960x720) in one 70 fps MP4 with a label per panel.

usage: python3 compare.py out.mp4 --tick0 8700 --from 8800 --to 11000 [--audio full.wav] A.frames:label B.frames:label ...
All runs must start at the same tick (--tick0, e.g. runs resumed from one snapshot). Run inside the toolbox (numpy, PIL).
"""
import argparse
import shutil
import subprocess

import numpy as np
from PIL import Image, ImageDraw, ImageFont

FR = 64000 + 768

ap = argparse.ArgumentParser()
ap.add_argument('out')
ap.add_argument('runs', nargs='+')
ap.add_argument('--tick0', type=int, required=True)
ap.add_argument('--from', dest='t0', type=int, required=True)
ap.add_argument('--to', dest='t1', type=int, required=True)
ap.add_argument('--audio')
a = ap.parse_args()

runs = []
for r in a.runs:
    path, label = r.split(':', 1)
    runs.append((np.memmap(path, dtype=np.uint8, mode='r'), label))

try:
    font = ImageFont.truetype('DejaVuSans-Bold.ttf', 22)
except OSError:
    font = ImageFont.load_default()
labels = []
for _, label in runs:
    im = Image.new('L', (480, 34), 0)
    ImageDraw.Draw(im).text((10, 5), label, fill=255, font=font)
    labels.append(np.asarray(im) > 128)

cmd = (['flatpak-spawn', '--host'] if shutil.which('ffmpeg') is None else []) + [
    'ffmpeg', '-hide_banner', '-loglevel', 'error', '-y', '-f', 'rawvideo', '-pix_fmt', 'rgb24',
    '-s', '960x720', '-r', '70', '-i', '-']
if a.audio:
    cmd += ['-ss', '%.4f' % (a.t0 / 70.0), '-i', a.audio, '-c:a', 'aac', '-b:a', '192k', '-shortest']
cmd += ['-c:v', 'libopenh264', '-b:v', '12M', '-pix_fmt', 'yuv420p', a.out]
p = subprocess.Popen(cmd, stdin=subprocess.PIPE)
frame = np.zeros((720, 960, 3), np.uint8)
for t in range(a.t0, a.t1):
    k = t - a.tick0
    for i, (data, _) in enumerate(runs):
        if (k + 1) * FR > len(data):
            continue
        f = data[k * FR:(k + 1) * FR]
        pal = (f[64000:].reshape(256, 3).astype(np.uint16) * 255 // 63).astype(np.uint8)
        img = pal[f[:64000]].reshape(200, 320, 3)
        # 320x200 shown 4:3 -> 480x360: x1.5 horizontally, x1.8 vertically (nearest)
        img = img[(np.arange(360) * 200 // 360)][:, (np.arange(480) * 320 // 480)]
        y, x = (i // 2) * 360, (i % 2) * 480
        panel = frame[y:y + 360, x:x + 480]
        panel[:] = img
        m = labels[i]
        panel[:34][m] = 255
        panel[:34][~m] = (panel[:34][~m] * 0.45).astype(np.uint8)
    p.stdin.write(frame.tobytes())
p.stdin.close()
p.wait()
print('wrote', a.out, a.t1 - a.t0, 'frames')
