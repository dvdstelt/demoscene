"""Find every copy of the retrace timer (0x14bec..0x14d1b in the first part) in code32 and compare them.

The copies differ only in their own addresses (variables, handler, stub). We search for the handler's opcode
pattern and then compare each copy byte by byte with the first after relocating by the copy's offset.
usage: python3 models/core/find_timers.py   (from okean/re, in the toolbox)
"""
import os, sys, struct
RE = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, RE)
import rep

v = rep.View('code32')
img = v.img
B = 0xec0
A0, A1 = 0x14be0, 0x14d1c       # callback pointer .. end of the real-mode stub
ref = img[B + A0:B + A1]
# signature: the handler's "in al,dx; and eax,8; dec eax; neg eax"
sig = bytes([0xec, 0x83, 0xe0, 0x08, 0x48, 0xf7, 0xd8])
hits = []
i = 0
while True:
    i = img.find(sig, i + 1)
    if i < 0:
        break
    hits.append(i - B)
h0 = 0x14cc6 + 0             # in al,dx of the reference handler
h0 = next(h for h in hits if 0x14cb8 < h < 0x14cf4)
print('signature hits:', ' '.join('%05x' % h for h in hits))
for h in hits:
    if not (h0 - 0x14cc6 + 0x14cc6):
        pass
for h in [x for x in hits if not (0x14cf4 <= x < 0x14d1c) and not any(0 < x - y < 0x40 for y in hits)]:
    d = h - (0x14cc6 + 0)   # displacement of the copy relative to the reference
    # the reference handler's "in al,dx" is at 0x14cc6
    d = h - 0x14cc6
    base = A0 + d
    cp = img[B + base:B + base + (A1 - A0)]
    # relocate: every dword in ref that lies in [A0, A1) or equals another known address shifts by d
    diffs = []
    k = 0
    while k < len(ref):
        if ref[k] != cp[k]:
            # try a dword relocation covering this byte
            ok = False
            for s in range(max(0, k - 3), k + 1):
                if s + 4 <= len(ref):
                    r = struct.unpack_from('<I', ref, s)[0]
                    c = struct.unpack_from('<I', cp, s)[0]
                    if (c - r) & 0xffffffff == d & 0xffffffff:
                        ok = True
                        break
            if not ok:
                diffs.append(A0 + k)
        k += 1
    cb = struct.unpack_from('<I', img, B + base)[0]
    print('copy at %05x..%05x (shift %+06x): callback [%05x] = %05x, handler %05x, install %05x, measure %05x, '
          'restore %05x, real-mode stub %05x, count word %05x; bytes not explained by the shift: %s' % (
              base, base + (A1 - A0) - 1, d, base, cb, 0x14cb8 + d, 0x14bec + d, 0x14c67 + d, 0x14c40 + d,
              0x14cf4 + d, 0x14d0d + d, ' '.join('%05x' % x for x in diffs) or 'none'))
