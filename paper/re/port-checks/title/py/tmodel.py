"""Exact title / the end model for timing checks against full.frames (uses re-title/model.py)."""
import struct, sys
import numpy as np
sys.path.insert(0, '/tmp/claude-1000/-home-dennis--config-Claude-scratch-workspaces-e97b6b66-a77a-46ac-a12a-f43ab86d2564-b079650b-97d8-49d5-bd62-940ddce6ee3d-scratch-2026-11-10-9f02fa/0a51be05-fc78-48c4-9845-e78eac5ea96a/scratchpad/re-title')
import model
SEG = model.SEG
FR = 64000 + 768
frames = np.memmap('/home/dennis/Downloads/acme_303/paper/re/full.frames', dtype=np.uint8, mode='r')
ticks = [l.split() for l in open('/home/dennis/Downloads/acme_303/paper/re/full.ticks').read().split('\n') if l.strip()]
def counter(t): return int(ticks[t][1])

def cap(t):
    f = np.asarray(frames[t*FR:(t+1)*FR])
    return f[:64000].reshape(200, 320), f[64000:].reshape(256, 3)

class Rng:
    def __init__(self):
        self.t = list(struct.unpack_from('<64I', SEG, 0x9a0)); self.i = SEG[0xaa0] & 63; self.j = SEG[0xaa1] & 63
    def next(self):
        v = (self.t[self.i] + self.t[self.j]) & 0xffffffff
        self.t[self.j] = v; self.i = (self.i+1) & 63; self.j = (self.j+1) & 63
        return v
_cache = {}
def rng_at(n):
    # cached sequential
    r = Rng()
    for _ in range(n): r.next()
    return r
_seq = None
def rng_values(start, count):
    global _seq
    if _seq is None:
        r = Rng(); vals = []
        for _ in range(300000): vals.append(r.next())
        _seq = vals
    return _seq[start:start+count]

J = [struct.unpack('b', bytes([b]))[0] for b in SEG[0x2a42:0x2a52]]
def jitter(pos):
    v = rng_values(pos, 192); k = 0; offs = []
    for rec in range(24):
        o = []
        for m in range(4):
            dy = J[v[k] & 15]; dx = J[v[k+1] & 15]; k += 2
            o.append((dx, dy))
        offs.append(o)
    return offs

def osc(pos, scale=300):
    v = rng_values(pos, 216); out = []
    for k in range(72):
        ph = v[3*k]; amp = (v[3*k+1] & 0x3ff) + 0x100; sp = (((v[3*k+2] & 0xff) - 0x80) * scale) >> 8
        out.append([ph, amp, sp])
    return out

def adv(o, n):
    return [[(ph + sp*n) & 0xffffffff, amp, sp] for ph, amp, sp in o]

G, _ = model.font()
SP = model.sprites(G)
_, T2 = model.shade_tables()
TEX = model.tex()

def pens(s):
    out = []; x, y = 5, 50
    for i, ch in enumerate(s):
        if i == 12: x, y = 5, 110
        out.append((ch, x, y))
        x += (0 if ch in ' \0' else G[ord(ch)-97][0] >> 1) + 15
    return out

def background(o, shx):
    P, X, Y, A, B = model.warp_grid(o, shx, 5)
    return model.render_blocks(A, B, TEX, T2[10])

def letters(bg, s, offs, visible):
    pad = 20000
    buf = np.zeros(64000 + 2*pad, np.int64); buf[pad:pad+64000] = bg.reshape(-1)
    for i, (ch, x, y) in enumerate(pens(s)):
        if i >= visible: break
        if ch == '\0': break
        if ch == ' ': continue
        sp = SP[ord(ch)-97][:, 0::2].astype(np.int64)
        for k, (dx, dy) in enumerate(offs[i]):
            o = dy*320 + dx + (3*320 - 3 if k == 0 else 0)
            p = pad + y*320 + x + o
            for r in range(40):
                seg = buf[p + r*320: p + r*320 + 40]
                if k == 0: seg[:] = np.maximum(0, seg - sp[r])
                else: seg[:] = np.minimum(255, seg + (sp[r] >> 1))
    return buf[pad:pad+64000].reshape(200, 320).astype(np.uint8)
