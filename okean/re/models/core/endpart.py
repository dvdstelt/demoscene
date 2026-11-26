"""Model of the last part 0x4d370 (end texts over noise): renders every record of the part from the file and the
clock (callback count and music position per draw) and compares with full.frames bit for bit (indices and DAC).

The clock comes from run_core.py's event log (models/core/runs/p8.ev): for each draw (main loop iteration) the
callback count at its start and the music position at its page select ('psel', 0x4d3c8); for each callback the
position (for the fade).
usage: python3 models/core/endpart.py
"""
import os
import sys
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
RE = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, RE)
from sheet import open_frames  # noqa

IMG = open(os.path.join(RE, 'unpacked/korso2.bin'), 'rb').read()
B = 0xec0


def rd(a, n=1):
    return IMG[B + a:B + a + n]


def u32(a):
    return int.from_bytes(rd(a, 4), 'little')


FONT = np.frombuffer(rd(0x3cec2, 320 * 38), np.uint8).reshape(38, 320)
WIDTH = {chr(0x61 + i): rd(0x4c8c2 + i)[0] for i in range(26)}
START = {}
x = 0
for i in range(26):
    START[chr(0x61 + i)] = x
    x += WIDTH[chr(0x61 + i)]
EXTRA = u32(0x3ceb0)          # extra spacing after every glyph and space (0)
SPACE = 4


def adv(c):
    """advance of the newline measurement: [c + 0x4c861] (the width table seen from the character code)"""
    if c == ' ':
        return SPACE
    return IMG[B + 0x4c861 + ord(c)]


def draw_glyph(buf, off, c):
    w = WIDTH[c]
    g = FONT[:, START[c]:START[c] + w]
    y, x = divmod(off, 320)
    # the printer writes linearly (offset + row * 320 + column): clip like a flat 64,000-byte buffer would
    flat = buf.reshape(-1)
    for r in range(38):
        for cx in range(w):
            v = g[r, cx]
            if v:
                flat[off + r * 320 + cx] = v


def draw_page_end(buf, ptr):
    """the end part's inline printer (0x4d481..0x4d63f): lines 28 rows apart from y = 0, each centred"""
    pos = 0
    nextline = 0
    while True:
        c = IMG[B + ptr]
        if c == 0x40:            # '@': end of the page
            return
        if c == 0x0d:            # newline: measure up to the next CR (spaces 4, letters their width, '@' 0)
            ptr += 1
            tot = 0
            q = ptr
            while IMG[B + q] != 0x0d:
                tot += adv(chr(IMG[B + q]))
                q += 1
            pos = 0xa0 - (tot >> 1) + nextline
            nextline += 0x2300
            continue
        if c == 0x20:
            pos += SPACE + EXTRA
            ptr += 1
            continue
        if c == 0:
            raise RuntimeError('NUL: the printer would loop forever')
        ch = chr(c)
        draw_glyph(buf, pos, ch)
        pos += WIDTH[ch] + EXTRA
        ptr += 1


def noise(seed):
    out = np.empty(16000, np.uint32)
    s = seed
    for i in range(16000):
        out[i] = s & 0x1f1f1f1f
        s = (s + 0x9d) & 0xffffffff
        s = ((s << 11) | (s >> 21)) & 0xffffffff
        s ^= 0x9d
    return out.view(np.uint8).copy(), s


def load_events(path):
    ev = []
    for line in open(path):
        if line.startswith('#'):
            continue
        f = line.split()
        ev.append((f[0], int(f[1]), float(f[2]), f[4], int(f[5]), int(f[6]), int(f[7])))
    return ev


def main():
    ev = [e for e in load_events(os.path.join(HERE, 'runs', 'p8.ev'))]
    # draws: (position at top, cb count at top) and the record each copy lands in
    draws = []
    cur = None
    cbpos = []
    copy_of_rec = {}
    for kind, frame, line, part, cb, order, row in ev:
        if part != '4d370':
            continue
        if kind == 'cb':
            cbpos.append((order, row))
        elif kind == 'top':
            cur = dict(order=order, row=row, cb=cb, frame=frame)
            draws.append(cur)
        elif kind == 'psel':
            cur['order'], cur['row'] = order, row      # the position the page select reads
        elif kind == 'copy':
            cur['copied'] = frame
        elif kind == 'rec':
            pass
    # fade [0x3ceac] after n callbacks
    fade = [u32(0x3ceac)]
    for o, r in cbpos:
        f = fade[-1]
        if r >= 0x20 and o == 0x1e:
            f = max(f - 1, 0)
        elif f != 0x20:
            f += 1
        fade.append(f)
    pal_src = np.frombuffer(rd(0x4cd64, 768), np.uint8).astype(np.uint32)
    raw, W, H, rec, hdr, n = open_frames(os.path.join(RE, 'full.frames'))
    seed = u32(0x4d36c)
    page_next = u32(0x4d364)
    shown = {}       # record -> (image, palette)
    for d in draws:
        page = page_next
        img, seed = noise(seed)
        o, r = d['order'], d['row']
        if o == 0x1c:
            page_next = 0x4ccd9
        if o == 0x1d:
            page_next = 0x4cd0f
        if o == 0x1e:
            page_next = 0x4cd33
        if r > 0x20:
            page_next = 0x4cca0
        pal = ((pal_src * fade[d['cb']]) >> 5).astype(np.uint8)
        buf = img.reshape(200, 320)
        draw_page_end(buf, page)
        d['page'] = page
        if 'copied' in d:
            shown[d['copied']] = (buf.copy(), pal, d)
    # compare: record k shows the last copy at or before k
    last = None
    ok = bad = 0
    badlist = []
    k0 = min(shown)
    for k in range(k0, n):
        if k in shown:
            last = shown[k]
        o = hdr + k * rec
        cap = np.asarray(raw[o + 776:o + 776 + W * H]).reshape(H, W)[:200]
        cpal = np.asarray(raw[o + 8:o + 776])
        same = np.array_equal(cap, last[0]) and np.array_equal(cpal, last[1])
        if same:
            ok += 1
        else:
            bad += 1
            badlist.append((k, int(np.count_nonzero(cap != last[0])), bool(np.array_equal(cpal, last[1]))))
    print('records %d..%d: %d identical to the model (indices and DAC), %d differ' % (k0, n - 1, ok, bad))
    print(badlist[:20])
    pages = {}
    for d in draws:
        pages.setdefault(d['page'], []).append(d.get('copied'))
    for p, ks in pages.items():
        ks = [k for k in ks if k is not None]
        print('page %05x shown in records %s..%s (%d draws)' % (p, ks[0] if ks else '-', ks[-1] if ks else '-', len(ks)))
    print('fade: first callback with fade < 32:', next(i for i, f in enumerate(fade) if f < 32), 'of', len(fade) - 1)


main()
