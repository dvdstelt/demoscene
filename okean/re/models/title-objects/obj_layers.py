"""render frames of part 2 with three layers: lines between vertices in front of the camera (white), lines with
an end at z <= 0 (red), lines whose count was cut to its low byte (green); with the capture beside them"""
import sys, copy, pickle
from vlog import *
import objects as O
from objects import *
from PIL import Image
ev = parse('vlog2.txt')
s = snap('2e779')
st = init_from_snapshot(s)
cbs = [(d['b12b75'], d['w12b73']) for a, f, irq, t, d in ev if a == '2eb81']
ds = draws(ev)
want = set(map(int, sys.argv[1:]))
done = 0
rows = []
def zs(stt):
    out = []
    for (x, y, z) in VERTS:
        x, y = rot(x, y, stt['angA']); x, z = rot(x, z, stt['angB'])
        x = s32(x + stt['xoff'] + stt['xoff2']); x, z = rot(x, z, stt['angC']); y, z = rot(y, z, stt['angD'])
        out.append(s32(z + stt['zdist']))
    return out
for r in ds:
    if r['frame'] not in want:
        continue
    c0 = r['irq0'] - 617
    while done < c0:
        callback(st, *cbs[done]); done += 1
    order = r['bg']['b12b75']
    sd = copy.deepcopy(st)
    objs = []
    if order < 3:
        sd['angC'] = sd['angC0']
        for k in range(8):
            objs.append(dict(sd)); sd['angC'] = (sd['angC'] + 0x1ff) & 0xfff
    else:
        sd['xoff2'] = s32(sd['tubeX'] - 0x5000)
        for k in range(5):
            objs.append(dict(sd)); sd['xoff2'] = s32(sd['xoff2'] + 0x2000)
    L = [[], [], []]
    for o in objs:
        P = project(o); Z = zs(o)
        for a, b in EDGES:
            x1, y1 = P[a]; x2, y2 = P[b]
            d = s32(x2 - x1); n = (d ^ (d >> 31)) + 1; d2 = s32(y2 - y1); m = (d2 ^ (d2 >> 31)) + 1
            n = max(n & M32, m & M32) >> o['shift']
            k = 2 if n > 255 else (1 if (Z[a] <= 0 or Z[b] <= 0) else 0)
            O.line(L[k], x1, y1, x2, y2, o['shift'])
    im = np.zeros((200, 320, 3))
    for k, col in enumerate(((1, 1, 1), (1, 0.2, 0.2), (0.2, 1, 0.2))):
        a = stamp_image(L[k]).astype(float)
        im += a[:, :, None] * np.array(col) * 6
    cidx, cpal = cap_frame(r['frame'])
    p = cpal.astype(int) * 255 // 63
    rows.append(np.concatenate([p[cidx], np.clip(im, 0, 255)], 1))
Image.fromarray(np.concatenate(rows, 0).astype(np.uint8)).save('obj_layers.png')
