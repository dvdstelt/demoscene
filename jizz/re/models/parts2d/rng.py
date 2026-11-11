"""The random generator 0x141f6 (state 3 dwords at 0x141ea, read from the image)."""
import struct
import common as c

M = 0xffffffff


def rol(x, n): return ((x << n) | (x >> (32 - n))) & M


class Rng:
    def __init__(self, state=None):
        if state is None:
            state = struct.unpack_from('<III', c.rep.SEG, 0x141ea)
        self.a, self.b, self.c = state

    def __call__(self):
        eax = (self.a + 0xe372471) & M
        ebx = (self.b + 0xbc5ea9) & M
        ecx = (self.c + 0xc8ade7) & M
        eax ^= ebx
        p = (struct.unpack('<i', struct.pack('<I', eax))[0] * struct.unpack('<i', struct.pack('<I', ecx))[0]) & 0xffffffffffffffff
        eax, edx = p & M, p >> 32
        edx = (rol(edx, 13) + eax) & M
        ecx = rol(ecx, 14)
        edx = (edx - ebx) & M
        eax ^= edx
        ebx = (ebx + ecx) & M
        ecx ^= edx
        eax = (eax - ebx) & M
        ebx = rol(ebx, 9)
        p = eax * ecx
        eax, edx = p & M, p >> 32
        ebx ^= edx
        eax = (eax + 0x1e42ac3) & M
        ebx = (ebx + 0x5edf04) & M
        ecx = (ecx + 0x11a2ec1) & M
        self.a, self.b, self.c = ebx, ecx, eax
        return eax
