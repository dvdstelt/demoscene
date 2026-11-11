import sys, pickle, struct
import numpy as np
sys.path.insert(0, '/home/dennis/Downloads/acme_303/paper/re')
import unicorn, rep
from PIL import Image
S = '/tmp/claude-1000/-home-dennis--config-Claude-scratch-workspaces-e97b6b66-a77a-46ac-a12a-f43ab86d2564-b079650b-97d8-49d5-bd62-940ddce6ee3d-scratch-2026-11-10-9f02fa/0a51be05-fc78-48c4-9845-e78eac5ea96a/scratchpad'
O = S + '/re-logo/'
SEG = rep.SEG
snap = pickle.load(open(f'{S}/snaps/t5500.snap', 'rb'))
m = snap['mem']
d = lambda a: struct.unpack_from('<I', m, a)[0]
tex = np.frombuffer(m, np.uint8, 64000, d(0x1edf5)).copy()
lut = np.frombuffer(m, np.uint8, 65536, d(0x1eded)).reshape(256, 256).copy()
font = np.frombuffer(m, np.uint8, 0xe800, d(0x1ede9)).copy()
page_snap = np.frombuffer(m, np.uint8, 64000, d(0x1edc5)).copy()
print('tex values', np.unique(tex, return_counts=True))

# --- LUT check
lut2 = np.zeros((256, 256), np.int32)
for ch in range(0x41):
    c2 = ch if ch <= 0x20 else ((ch - 0x20) >> 1) + ((ch - 0x20) >> 3)
    for cl in range(256):
        if cl < 0x40:
            v = cl + 2 * c2 - 0x40
            v = max(v, 0)
        else:
            v = max((cl & 0x1f) + c2 - 0x20, 0) + (cl & 0xe0)
        lut2[ch, cl] = v & 0xff
print('LUT rows 0..64 match:', (lut2[:0x41] == lut[:0x41]).all())

# --- font
def glyph(c):
    o = (c - 0x61) << 11
    w = int(font[o])
    g = font[o + 1:o + 1 + 32 * 60].reshape(32, 60)[:, :w]
    return w, g

def draw_text(buf, pos, s, stride=320):
    for ch in s:
        c = ord(ch)
        a = (c - 0x61) & 0xff
        if c < 0x61:
            pos += 10
            if a == 0xa0: pos += 0x303e
            continue
        w, g = glyph(c)
        for r in range(32):
            p = pos + r * stride
            seg = buf[p:p + w].astype(np.int32) + g[r]
            buf[p:p + w] = np.minimum(seg, 255)
        pos += w + 1
    return pos

def blur(buf, start, rows, stride=320):
    n = rows * stride
    src = buf  # pass 1 reads buf around [start-stride, start+n+stride)
    tmp = np.zeros(n + 2 * 640, np.int32)
    i = np.arange(n)
    s = buf.astype(np.int32)
    tmp[640 + i] = (s[start + i - stride] + s[start + i + stride] + s[start + i - 1] + s[start + i + 1]) >> 2
    t = tmp
    buf[start + i] = ((t[640 + i - stride] + t[640 + i + stride] + t[640 + i - 1] + t[640 + i + 1]) >> 2).astype(np.uint8)

bb = np.zeros(64000 + 2000, np.uint8)
TXT = 'present the\x01intro'
end1 = draw_text(bb, 0x514, TXT)
print('first draw: cursor end', end1, divmod(end1, 320))
# cursor of '\x01'
pos = 0x514
for ch in 'present the':
    c = ord(ch)
    if c < 0x61: pos += 10
    else: pos += glyph(c)[0] + 1
print('after "present the":', pos, divmod(pos - 0x514, 320), ' intro start:', pos + 0x3048, divmod(pos + 0x3048, 320))
for k in range(5, 0, -1):
    blur(bb, 0x140, 0x4e)
    if k == 2:
        bb[:0x6400] = bb[:0x6400] >> 1
        draw_text(bb, 0x150, TXT)
page = np.full(64000, 0x3f, np.uint8)
src = bb[:0x6400]
v = (0x3f - (src >> 2)).astype(np.uint8)
page[0xc84:0xc84 + 0x3200] = v[:0x3200]
o2 = 0xc84 + 0x3200 + 0x8700
page[o2:o2 + 0x3200] = v[0x3200:]
print('rows of second half start', divmod(o2, 320))

def nib(addr, rows, inv):
    out = np.zeros((rows, 224), np.uint8)
    a = addr
    for r in range(rows):
        b = np.frombuffer(SEG, np.uint8, 112, a); a += 112
        row = np.empty(224, np.uint8); row[0::2] = b >> 4; row[1::2] = b & 15
        if inv: row = ((row ^ 15) << 2) + 3
        out[r] = row
    return out, a
outline, aend = nib(0x1bf03, 0x53, True)
fillmap, aend2 = nib(0x196c3, 0x5c, False)
print('image ends', hex(aend), hex(aend2))
P = page.reshape(200, 320)
P[50:50 + 83, 48:48 + 224] = outline
final = lut[tex, page]
print('page match:', (final == page_snap).sum(), 'of 64000')
diff = np.nonzero(final != page_snap)[0]
if len(diff): print('first diffs', [(i // 320, i % 320, final[i], page_snap[i]) for i in diff[:10]])
np.save(O + 'page_v.npy', page); np.save(O + 'fillmap.npy', fillmap); np.save(O + 'outline.npy', outline)
np.save(O + 'tex.npy', tex)
# images
pal = np.frombuffer(SEG, np.uint8, 768, 0x169a3).reshape(256, 3).astype(np.uint16) * 255 // 63
Image.fromarray(pal[page_snap].reshape(200, 320, 3).astype(np.uint8)).resize((640, 400), Image.NEAREST).save(O + 'page_snap.png')
Image.fromarray((outline * 4).astype(np.uint8)).resize((448, 166), Image.NEAREST).save(O + 'outline.png')
fm = np.zeros((92, 224, 3), np.uint8)
fm[:] = pal[0x40 + fillmap]; fm[fillmap == 0] = (0, 0, 128)
Image.fromarray(fm).resize((672, 276), Image.NEAREST).save(O + 'fillmap.png')
print('fillmap values', np.unique(fillmap, return_counts=True))
print('static pal 0x40..0x4f', np.frombuffer(SEG, np.uint8, 48, 0x169a3 + 0xc0).reshape(16, 3).tolist())
print('static pal 0..3, 50..63', np.frombuffer(SEG, np.uint8, 768, 0x169a3).reshape(256, 3)[[0,1,2,3,50,51,52,53,54,62,63]].tolist())
# glyph sheet
ws = []
for c in range(0x61, 0x61 + 29):
    w, g = glyph(c); ws.append(w)
print('glyph widths a..}', ws)
sheet = np.zeros((32, sum(ws) + 2 * 29), np.uint8); x = 0
for c in range(0x61, 0x61 + 29):
    w, g = glyph(c); sheet[:, x:x + w] = g; x += w + 2
Image.fromarray(sheet).resize((sheet.shape[1] * 2, 64), Image.NEAREST).save(O + 'font.png')
T = bb[:25600].reshape(80, 320)
for r in range(30, 50):
    row = T[r]
    nz = np.nonzero(row)[0]
    print('textbuf row', r, 'max', row.max(), 'x range', (nz.min(), nz.max()) if len(nz) else None)
# separate contributions: present-the only vs intro only
def build(txt_a, txt_b):
    b = np.zeros(64000 + 2000, np.uint8)
    draw_text(b, 0x514, txt_a)
    for k in range(5, 0, -1):
        blur(b, 0x140, 0x4e)
        if k == 2:
            b[:0x6400] = b[:0x6400] >> 1
            draw_text(b, 0x150, txt_b)
    return b[:25600].reshape(80, 320)
A = build('present the', 'present the')
B = build('present the\x01intro', 'present the\x01intro')
print('present-the contribution in rows 40..79: max', A[40:].max(), 'nonzero', (A[40:] > 0).sum(), 'rows', np.nonzero(A[40:].any(1))[0] + 40)
I = B.astype(int) - A.astype(int)
print('intro contribution in rows 0..39: max', I[:40].max(), 'nonzero', (I[:40] > 0).sum(), 'rows', np.nonzero((I[:40] > 0).any(1))[0])
print('max T', T.max(), 'shadow-only interior value sample', T[20, 30:40])
