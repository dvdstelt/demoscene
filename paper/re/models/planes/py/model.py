import numpy as np
def s32(x):
    x &= 0xffffffff
    return x - (1 << 32) if x >> 31 else x
def trunc_div(a, b):
    q = abs(a) // abs(b)
    return q if (a >= 0) == (b >= 0) else -q
class Fix:
    def __init__(self, sin): self.sin = sin
    def S(self, a): return self.sin[a & 0x3ff]
    def C(self, a): return self.sin[(a + 0x100) & 0x3ff]
    def rotx(self, a):  # 0xbaa
        s, c = self.S(a), self.C(a); return [0x10000, 0, 0, 0, c, -s, 0, s, c]
    def roty(self, a):  # 0xbed
        s, c = self.S(a), self.C(a); return [c, 0, s, 0, 0x10000, 0, -s, 0, c]
    def rotz(self, a):  # 0xc30
        s, c = self.S(a), self.C(a); return [c, -s, 0, s, c, 0, 0, 0, 0x10000]
    def rotxy(self, a, b):  # 0xb1f(eax=a, ebx=b)
        cb, sb, ca, sa = self.C(b), self.S(b), self.C(a), self.S(a)
        return [cb, 0, -sb, s32(-sb * sa) >> 16 if False else ((-sb * sa) >> 16), ca, -((cb * sa) >> 16), (sb * ca) >> 16, sa, (cb * ca) >> 16]
    @staticmethod
    def mul(A, B):  # 0xae2: out = A*B, each product (a*b)>>16 truncated to 32 bit, summed
        out = []
        for r in range(3):
            for c in range(3):
                acc = 0
                for k in range(3):
                    acc += s32((A[3 * r + k] * B[3 * k + c]) >> 16)
                out.append(s32(acc))
        return out
