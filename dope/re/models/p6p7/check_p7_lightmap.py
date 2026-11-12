"""p7: rebuild the light map 13b4:0000 (frame loop 308f..3166: base 03b3 + 5 spots 37ea added with 32-bit adds)
from the retrace counter [19b8] and compare it with the snapshot."""
import struct
import sys
import numpy as np
from snap import Snap, img

b = img('p7')
BASE = np.frombuffer(b, np.uint8, 65536, 0x3b3 * 16)
SPOT = np.frombuffer(b, np.uint8, 128 * 128, 0x37ea * 16).reshape(128, 128)
import sine
SIN = sine.build(b, 0x100 + 0x410)


def tab(byteoff, cos=False):            # word at cs:[off + 0x410] (sin) or cs:[off + 0x810] (cos)
    return SIN[(byteoff >> 1) + (512 if cos else 0)]


def sar(v, n): return v >> n


def light_offsets(t):
    out = []
    d = t & 0xffe
    out.append((sar(tab(d), 9) + 0x4040) & 0xffff)
    d = (t << 3) & 0xffe
    out.append((((sar(tab(d, True), 9) + 0x40) << 8) + 0x40) & 0xffff)
    d = (t << 2) & 0xffe
    out.append((((sar(tab(d), 9) + 0x40) << 8) + sar(tab(d, True), 9) + 0x40) & 0xffff)
    d = (t << 1) & 0xffe
    out.append((((sar(tab(d, True), 9) + 0x40) << 8) + sar(tab(d), 9) + 0x40) & 0xffff)
    d = (3 * t) & 0xffe
    d = sar(tab(d), 3) & 0xffe
    out.append((((sar(tab(d), 9) + 0x40) << 8) + sar(tab(d, True), 9) + 0x40) & 0xffff)
    return out


def build(t):
    m = bytearray(BASE.tobytes())
    for di in light_offsets(t):
        si = 0
        for r in range(128):
            for c in range(32):
                a = (di + 4 * c) & 0xffff
                v = (struct.unpack_from('<I', m, a)[0] + struct.unpack_from('<I', SPOT.tobytes(), si)[0]) & 0xffffffff
                struct.pack_into('<I', m, a, v)
                si += 4
            di = (di + 0x100) & 0xffff
    return np.frombuffer(bytes(m), np.uint8)


if __name__ == '__main__':
    for path in sys.argv[1:]:
        s = Snap(path)
        t = s.cs16(0x19b8)
        lm = np.frombuffer(s.rd(0x13b4, 0, 65536), np.uint8)
        found = None
        for back in range(0, 6):
            if (build((t - back) & 0xffff) == lm).all():
                found = back
                break
        print(path, '[19b8]=%#x' % t, 'light map in memory = model for t - %s (the frame loop ran that many retraces '
              'before the snapshot)' % found)
