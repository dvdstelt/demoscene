"""p5's hand-over to p6's picture (0010:369c..3746): check the cross-fade palettes (3038 / 2f99 / 2fbd) against
the capture, and the final screen (p6's picture 4ed1:0000 with the palette 4ea1:0000)."""
import sys, os, pickle
import numpy as np
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
import sheet
RE = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..')
img = open(os.path.join(RE, 'unpacked', 'p5.bin'), 'rb').read()
snap = pickle.load(open(os.path.join(RE, 'snaps', 'part_p5.snap'), 'rb'))
pal1 = np.frombuffer(bytes(snap['vga']['dac']), np.uint8).reshape(256, 3)[:16].astype(int)   # chequerboard (DAC 0..15 at p5 start)
pal2 = np.frombuffer(img[0x4ea10:0x4ea10 + 48], np.uint8).reshape(16, 3).astype(int)            # p6 picture palette
def table(w):
    dx = max(w, 0) >> 4
    dx = min(dx, 0x100)
    t = []; ax = 0
    for i in range(64):
        t.append(ax >> 8)
        if ax <= 0x3f00:
            ax += dx
    return np.array(t)
def mixed(w):
    A = table(0x1000 - w); B = table(w)
    p = np.zeros((256, 3), int)
    for hi in range(16):
        for lo in range(16):
            p[hi * 16 + lo] = np.minimum(63, A[pal1[lo]] + B[pal2[hi]])
    return p
cands = {w: mixed(w) for w in range(0, 0x1000 + 0x40, 0x40)}
raw, W, H, rec, hdr, n = sheet.open_frames(sys.argv[1])
for f in range(25565, 25592):
    o = hdr + f * rec
    dac = np.frombuffer(bytes(raw[o + 8:o + 776]), np.uint8).reshape(256, 3).astype(int)
    m = [w for w, p in cands.items() if (p == dac).all()]
    idx = np.asarray(raw[o + 776:o + 776 + W * H]).reshape(H, W)[:200, :320]
    print(f, 'palette = mix(w) for w in', [hex(x) for x in m], 'pixels>=16:', int((idx >= 16).sum()))
