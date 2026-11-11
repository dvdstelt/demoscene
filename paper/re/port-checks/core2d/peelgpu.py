import sys
sys.path.insert(0, '.')
from PIL import Image
import numpy as np
exec(open('peelcheck.py').read().split('FR = 64768')[0])
def runs(rows):
    s = []; prev = None
    for r, c in enumerate(rows):
        if c != prev: s.append((r, c)); prev = c
    return s
for t in map(int, sys.argv[1].split(',')):
    im = np.asarray(Image.open('shots/shot_%05d.png' % t).convert('RGB').resize((320, 200), Image.BOX)).astype(int)
    rows = []
    for r in range(200):
        c = im[r].mean(axis=0)
        if c[1] > c[0] + 40: rows.append(0)
        elif c[0] > c[1] + 40: rows.append(1)
        elif abs(c[0] - c[1]) < 15 and abs(c[1] - c[2]) < 15: rows.append(2)
        else: rows.append(9)
    co = peel_cont_rows(prog(t))
    dif = [r for r in range(200) if rows[r] != co[r]]
    print(t, 'p', prog(t), 'gpu', runs(rows), 'cpu', runs(co), 'diff rows', dif)
