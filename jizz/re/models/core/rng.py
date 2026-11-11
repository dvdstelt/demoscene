"""rng.py: the random generator 0x141f6 (three 32-bit state words at 0x141ea / 0x141ee / 0x141f2, all 0 in the
image; 0x12e72 reseeds them before each texture script)."""
M = 0xffffffff
def rol(x, n): return ((x << n) | (x >> (32 - n))) & M
def s32(x): return x - (1 << 32) if x & 0x80000000 else x
class Rng:
    def __init__(self, a=0, b=0, c=0): self.a, self.b, self.c = a, b, c
    def state(self): return (self.a, self.b, self.c)
    def next(self):
        eax = (self.a + 0x0e372471) & M
        ebx = (self.b + 0x00bc5ea9) & M
        ecx = (self.c + 0x00c8ade7) & M
        eax ^= ebx
        p = (s32(eax) * s32(ecx)) & 0xffffffffffffffff     # imul ecx (signed 32x32 -> 64)
        eax, edx = p & M, p >> 32
        edx = (rol(edx, 13) + eax) & M
        ecx = rol(ecx, 14)
        edx = (edx - ebx) & M
        eax ^= edx
        ebx = (ebx + ecx) & M
        ecx ^= edx
        eax = (eax - ebx) & M
        ebx = rol(ebx, 9)
        p = eax * ecx                                        # mul ecx (unsigned)
        eax, edx = p & M, p >> 32
        ebx ^= edx
        eax = (eax + 0x01e42ac3) & M
        ebx = (ebx + 0x005edf04) & M
        ecx = (ecx + 0x011a2ec1) & M
        self.a, self.b, self.c = ebx, ecx, eax
        return eax
