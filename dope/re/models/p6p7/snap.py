"""Helpers: load a run_dope snapshot and read a part's memory by image address.

The part is loaded at segment 0F64 (image file offset X at linear 0xF640 + X); segment 0010 of the image is CS=0F74.
"""
import os
import pickle
import struct
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
RE = os.path.dirname(os.path.dirname(HERE))
LOADSEG = 0x0F64


class Snap:
    def __init__(self, path):
        if not os.path.isabs(path) and not os.path.exists(path):
            for d in (HERE, RE):
                if os.path.exists(os.path.join(d, path)):
                    path = os.path.join(d, path)
                    break
        d = pickle.load(open(path, 'rb'))
        self.d = d
        self.mem = d['mem'] if isinstance(d['mem'], (bytes, bytearray)) else bytes(d['mem'])
        self.vga = d['vga']
        self.st = d['st']

    def lin(self, seg, off):
        """image SEG:OFF -> linear"""
        return (seg + LOADSEG) * 16 + off

    def rd(self, seg, off, n):
        a = self.lin(seg, off)
        return self.mem[a:a + n]

    def u8(self, seg, off): return self.mem[self.lin(seg, off)]
    def u16(self, seg, off): return struct.unpack_from('<H', self.mem, self.lin(seg, off))[0]
    def s16(self, seg, off): return struct.unpack_from('<h', self.mem, self.lin(seg, off))[0]
    def u32(self, seg, off): return struct.unpack_from('<I', self.mem, self.lin(seg, off))[0]
    def s32(self, seg, off): return struct.unpack_from('<i', self.mem, self.lin(seg, off))[0]
    def cs16(self, off): return self.u16(0x10, off)
    def cs32(self, off): return self.s32(0x10, off)

    def linear(self, a, n):
        return self.mem[a:a + n]

    def dac(self):
        return np.frombuffer(bytes(self.vga['dac']), np.uint8).reshape(256, 3) if 'dac' in self.vga else None


def img(name):
    return open(os.path.join(RE, 'unpacked', name + '.bin'), 'rb').read()
