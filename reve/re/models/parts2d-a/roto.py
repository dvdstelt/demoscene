"""The two rotozoom layers 0x25a69c / 0x25a73c and their state, as the original computes them.

Layer 1 (0x25a69c): 16 x 16 texture [0x205e90], angle = sine index [0x2174c8] & 0x1ffe, steps sin/cos << 12 (4.28
fixed point, 1/2 texel per written pixel). Layer 2 (0x25a73c): 64 x 64 texture [0x205e94], angle index
(-[0x2174c8]) & 0x1ffe, steps sin/cos << 11 (6.26 fixed point, 1 texel per written pixel). Each call writes 160 x 100
texels to every other pixel of every other row of the buffer: the unrolled code variant (even or odd columns) and the
row offset (0 or 320) advance per call, a cycle of four calls (state at 0x2174d4..0x2174e8).
"""
import numpy as np

from common import rep

SIN = 0x2176c8          # 5120 words: round(32767 sin(2 pi i / 4096)) (+-1); the cos table 0x217ec8 = SIN + 0x800
M32 = 0xffffffff

L1_A, L1_B = 0x25a7dd, 0x25b8a0
L2_A, L2_B = 0x25c963, 0x25da26


def sin_word(byte_index):
    """The signed word at SIN + (byte_index & 0x1ffe) (the routines mask a byte offset)."""
    v = rep.rd16(SIN + (byte_index & 0x1ffe))
    return v - 0x10000 if v & 0x8000 else v


def cos_word(byte_index):
    v = rep.rd16(SIN + 0x800 + (byte_index & 0x1ffe))
    return v - 0x10000 if v & 0x8000 else v


class RotoState:
    """0x2174d4 (layer 1 toggle), 0x2174d8 (layer 1 row offset), 0x2174dc (layer 1 code), 0x2174e0, 0x2174e4,
    0x2174e8 (the same for layer 2). Defaults: the image's values."""
    def __init__(self, mem=None):
        if mem is None:
            self.t1, self.off1, self.code1 = rep.rd32(0x2174d4), rep.rd32(0x2174d8), rep.rd32(0x2174dc)
            self.t2, self.off2, self.code2 = rep.rd32(0x2174e0), rep.rd32(0x2174e4), rep.rd32(0x2174e8)
        else:
            self.t1, self.off1, self.code1 = mem.u32(0x2174d4), mem.u32(0x2174d8), mem.u32(0x2174dc)
            self.t2, self.off2, self.code2 = mem.u32(0x2174e0), mem.u32(0x2174e4), mem.u32(0x2174e8)


def _texels(angle_index, shift, bits):
    """u, v (uint32) of the 100 x 160 texels: start (-80, -50) steps from the centre, per texel (+S, +C), per line
    (-C, +S); S, C = the sine and cosine words << 16 >> shift."""
    s = sin_word(angle_index) * (1 << (16 - shift))
    c = cos_word(angle_index) * (1 << (16 - shift))
    edx0 = (c * -0x50 + s * -0x32) & M32
    ecx0 = (s * -0x50 - c * -0x32) & M32
    j = np.arange(100, dtype=np.int64)[:, None]
    k = np.arange(160, dtype=np.int64)[None, :]
    u = (ecx0 - j * c + k * s) & M32
    v = (edx0 + j * s + k * c) & M32
    return (u >> (32 - bits)) << bits | (v >> (32 - bits))


def layer1(buf, st, a, tex16):
    """0x25a69c: buf is the 200 x 320 uint8 buffer [0x205e8c]; a = [0x2174c8]; tex16 = 256 bytes."""
    idx = _texels(a & 0x1ffe, 4, 4)
    row = st.off1 // 320
    col = 0 if st.code1 == L1_A else 1
    buf[row::2, col::2][:100, :160] = tex16[idx]
    st.code1 ^= L1_A ^ L1_B
    st.t1 ^= 1
    if st.t1:
        st.off1 ^= 0x140


def layer2(buf, st, a, tex64):
    """0x25a73c: the angle index is (-[0x2174c8]) & 0x1ffe; tex64 = 4096 bytes (64 rows of 64)."""
    idx = _texels((-a) & 0x1ffe, 5, 6)
    row = st.off2 // 320
    col = 0 if st.code2 == L2_A else 1
    buf[row::2, col::2][:100, :160] = tex64[idx]
    st.code2 ^= L2_A ^ L2_B
    st.t2 ^= 1
    if st.t2:
        st.off2 ^= 0x140
