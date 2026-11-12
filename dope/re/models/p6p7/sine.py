"""The sine table built by p6 179b / p7 19f4 from the quarter wave stored in the image (cs:220 in p6, cs:410 in p7):
q[0..511] = round(32767 sin(2 pi i / 2048)) (data). Built: [0..511] = q, [512..1023] = q reversed (q[511-k]: one
entry late, the table has no 32767 at 512 exactly... see notes), [1024..2047] = -[0..1023], [2048..2559] = q.
Word index i covers the angle i * 2 pi / 2048; cos = entry + 512."""
import math
import struct


def build(img, off):
    q = [struct.unpack_from('<h', img, off + 2 * i)[0] for i in range(512)]
    t = q + q[::-1]
    t = t + [-x for x in t]
    t = t + q
    return t


if __name__ == '__main__':
    from snap import img
    for part, off in (('p6', 0x100 + 0x220), ('p7', 0x100 + 0x410)):
        t = build(img(part), off)
        err = max(abs(t[i] - round(32767 * math.sin(2 * math.pi * i / 2048))) for i in range(2048))
        err0 = max(abs(t[i] - round(32767 * math.sin(2 * math.pi * i / 2048))) for i in range(512))
        print(part, 'first quarter max err vs round(32767 sin)', err0, '; whole table', err)
