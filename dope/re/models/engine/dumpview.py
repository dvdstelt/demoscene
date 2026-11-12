"""Helpers to read the dumps written by models/engine/run_dope.py (--dump): memory (first 1.1 MB), VGA window, DAC."""
import struct
import numpy as np

LOADSEG = {'p2': 0x0F64, 'p3': 0x0F64, 'p5': 0x0F64, 'p6': 0x0F64, 'p7': 0x0F64}


class Dump:
    def __init__(self, path, part='p2'):
        d = np.load(path)
        self.mem = d['mem'].tobytes()
        self.vram = d['vram']
        self.dac = d['dac']
        self.regs = dict(zip([str(x) for x in d['regnames']], [int(x) for x in d['regs']]))
        self.frame = int(d['frame'])
        self.base = LOADSEG[part] * 16           # image segment S is at linear (S + 0F64) * 16

    def lin(self, seg, off=0):
        return self.base + seg * 16 + off

    def b(self, seg, off, n):
        a = self.lin(seg, off)
        return self.mem[a:a + n]

    def u8(self, seg, off): return self.mem[self.lin(seg, off)]
    def u16(self, seg, off): return struct.unpack_from('<H', self.mem, self.lin(seg, off))[0]
    def s16(self, seg, off): return struct.unpack_from('<h', self.mem, self.lin(seg, off))[0]
    def u32(self, seg, off): return struct.unpack_from('<I', self.mem, self.lin(seg, off))[0]
    def s32(self, seg, off): return struct.unpack_from('<i', self.mem, self.lin(seg, off))[0]
    def cs16(self, off): return self.u16(0x10, off)
    def cs32(self, off): return self.u32(0x10, off)

    def seg_of(self, rseg):
        """a segment value read from memory (relocated for the load segment) -> image segment"""
        return (rseg - self.base // 16) & 0xffff

    def arr(self, seg, off, n, dt):
        a = self.lin(seg, off)
        return np.frombuffer(self.mem[a:a + n * np.dtype(dt).itemsize], dt).copy()

    def rgb(self, idx):
        pal = (self.dac.reshape(256, 3).astype(np.float32) * 255 / 63).round().astype(np.uint8)
        return pal[idx]
