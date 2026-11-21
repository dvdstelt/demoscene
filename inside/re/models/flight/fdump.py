"""Reader for the .dump files written by models/flight/run_inside.py (the flight copy of the runner).

Records: b'TICK' (tick routine 0x4021f8 entry), b'REND' (frame routine just before the scene is drawn, 0x402922),
b'AFTR' (after the scene was drawn, 0x402931), b'RAND' (a call of rand(), with its return address).
Each: kind, refresh, part ticks [0x4248c8], rand seed [0x423818], payload.
"""
import struct

import numpy as np

STATE0, STATE1 = 0x424a68, 0x425018      # part state block in the payload, then 0x4227b4..0x4227f8
GLOB0 = 0x4227b4


class Rec:
    __slots__ = ('kind', 'frame', 'ticks', 'seed', 'data')

    def st(self, addr, fmt='<d'):
        """value at a linear address of the state block (0x424a68..0x425018) or 0x4227b4..0x4227f8"""
        if STATE0 <= addr < STATE1:
            return struct.unpack_from(fmt, self.data, addr - STATE0)[0]
        return struct.unpack_from(fmt, self.data, (STATE1 - STATE0) + addr - GLOB0)[0]

    def vec(self, addr): return np.array([self.st(addr + 8 * k) for k in range(3)])
    def mat(self, addr): return np.array([self.st(addr + 8 * k) for k in range(9)]).reshape(3, 3)


def read(path):
    out = []
    with open(path, 'rb') as f:
        while True:
            h = f.read(20)
            if len(h) < 20:
                break
            r = Rec()
            r.kind, r.frame, r.ticks, r.seed, n = struct.unpack('<4sIIII', h)
            r.data = f.read(n)
            out.append(r)
    return out


# offsets inside a REND payload
REND_STATE = (STATE1 - STATE0) + 0x44
LAND = REND_STATE                     # landscape object: 0x10c header, 361 vertices of 0x40, 648 faces of 0x34
SCENE = LAND + 0xdeec                 # scene 0x50, camera object 0xc8
SHIPS = SCENE + 0x50 + 0xc8           # 3 x 0xd0 object headers
SUN = SHIPS + 3 * 0xd0                # 0x104
FLARES = SUN + 0x104                  # 8 x 0x104


def land_vertices(r):
    v = np.frombuffer(r.data, dtype=np.uint8, count=361 * 0x40, offset=LAND + 0x10c).reshape(361, 0x40)
    xyz = v[:, 8:0x14].copy().view('<i4')
    uv = v[:, 0x28:0x2c].copy().view('<i2')
    lf = v[:, 0x30:0x34].copy().view('<u2')
    return xyz, uv, lf
