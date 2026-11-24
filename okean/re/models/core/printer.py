"""The shared text printer 0x4cacc (parts 1..7): which page each draw prints (from run_core.py's 'print' events)
and a check of the captions against full.frames: the caption's glyph pixels (font 0x3cec2, row 162) must have
the glyph's index in the captured record (other pixels are the part's own drawing).
usage: python3 models/core/printer.py
"""
import glob
import os
import sys
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
RE = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, RE)
sys.path.insert(0, HERE)
from sheet import open_frames  # noqa
IMG = open(os.path.join(RE, 'unpacked/korso2.bin'), 'rb').read()
B = 0xec0
FONT = np.frombuffer(IMG[B + 0x3cec2:B + 0x3cec2 + 320 * 38], np.uint8).reshape(38, 320)
W = [IMG[B + 0x4c8c2 + i] for i in range(26)]
S = [sum(W[:i]) for i in range(26)]


def page_text(ptr):
    e = IMG.index(b'@', B + ptr)
    return IMG[B + ptr:e].decode('latin-1')


def render_shared(ptr):
    """0x4cacc: returns a 200x320 layer (0 = untouched); the page must be '\\r<line>' up to '@'"""
    layer = np.zeros(64000, np.uint8)
    pos = 0
    while True:
        c = IMG[B + ptr]
        if c == 0x40:
            break
        if c == 0x0d:
            ptr += 1
            tot = 0
            q = ptr
            while IMG[B + q] != 0x0d:
                ch = IMG[B + q]
                tot += 4 if ch == 0x20 else IMG[B + 0x4c861 + ch]
                q += 1
            pos = 0xa0 - (tot >> 1) + 0xca80
            continue
        if c == 0x20:
            pos += 4
            ptr += 1
            continue
        k = c - 0x61
        g = FONT[:, S[k]:S[k] + W[k]]
        for r in range(38):
            for x in range(W[k]):
                if g[r, x]:
                    layer[pos + r * 320 + x] = g[r, x]
        pos += W[k]
        ptr += 1
    return layer.reshape(200, 320)


def main():
    draws = []
    for path in sorted(glob.glob(os.path.join(HERE, 'runs', 'p[1-7].ev'))):
        cur = None
        for line in open(path):
            f = line.split()
            if f[0] == 'top':
                cur = {'part': f[4]}
            elif f[0] == 'print' and cur is not None:
                cur['ptr'] = int(f[11], 16)
                cur['pos'] = (int(f[6]), int(f[7]))
            elif f[0] == 'printx' and cur is not None:
                cur['how'] = f[11]
            elif f[0] == 'copy' and cur is not None and 'ptr' in cur:
                cur['rec'] = int(f[1])
                draws.append(cur)
                cur = None
    byrec = {}
    for d in draws:
        byrec[d['rec']] = d
    recs = sorted(byrec)
    # the page shown in each record: the last copy at or before it
    shown = {}
    last = None
    for k in range(recs[0], recs[-1] + 1):
        if k in byrec:
            last = byrec[k]
        shown[k] = last
    # intervals per page
    iv = []
    for k in sorted(shown):
        p = shown[k]['ptr']
        if iv and iv[-1][0] == p and iv[-1][2] == k - 1:
            iv[-1][2] = k
        else:
            iv.append([p, k, k, shown[k]['pos'], shown[k]['part']])
    raw, Wd, H, rec, hdr, n = open_frames(os.path.join(RE, 'full.frames'))
    print('page    records       first draw at  part   text / check (glyph pixels equal to the capture)')
    for p, k0, k1, pos, part in iv:
        t = page_text(p)
        line = '%05x  %5d..%5d  %2d:%02d  %s  %r' % (p, k0, k1, pos[0], pos[1], part, t)
        if t.strip('\r'):
            lay = render_shared(p)
            m = lay > 0
            tot = bad = 0
            worst = []
            for k in range(k0, k1 + 1):
                o = hdr + k * rec
                cap = np.asarray(raw[o + 776:o + 776 + Wd * H]).reshape(H, Wd)[:200]
                b = int(np.count_nonzero(cap[m] != lay[m]))
                tot += int(m.sum())
                bad += b
                if b:
                    worst.append((k, b))
            line += '  %d of %d glyph pixels differ; records with differences: %s' % (bad, tot, worst[:6])
        print(line)


main()
