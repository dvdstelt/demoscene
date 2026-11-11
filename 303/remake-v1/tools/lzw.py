"""Variable-width LZW (Nelson's LZW15V: 256=EOS, 257=BUMP, 258=FLUSH, first code 259, MSB-first bits)."""
import struct

def unlzw(data, pos=0, maxout=None):
    out = bytearray()
    n = len(data); p = pos; acc = 0; nacc = 0
    parent = [0] * 32768; char = [0] * 32768
    def rd(w):
        nonlocal p, acc, nacc
        while nacc < w:
            if p >= n: raise EOFError
            acc = ((acc << 8) | data[p]) & 0xFFFFFFFFFF; p += 1; nacc += 8
        nacc -= w
        return (acc >> nacc) & ((1 << w) - 1)
    try:
        while True:
            nxt, width = 259, 9
            old = rd(width)
            if old == 256: break
            ch = old; out.append(old)
            while True:
                if maxout and len(out) >= maxout: return bytes(out[:maxout]), p
                new = rd(width)
                if new == 256: return bytes(out), p
                if new == 258: break
                if new == 257: width += 1; continue
                stack = bytearray()
                if new >= nxt:
                    stack.append(ch); c = old
                else:
                    c = new
                while c > 255:
                    stack.append(char[c]); c = parent[c]
                stack.append(c)
                ch = c
                stack.reverse(); out += stack
                parent[nxt] = old; char[nxt] = ch; nxt += 1
                old = new
    except EOFError:
        pass
    return bytes(out), p

def read_pak(path):
    d = open(path, 'rb').read()
    assert d[:4] == b'ALEX'
    n, diroff = struct.unpack_from('<II', d, 4)
    ents = []
    for i in range(n):
        name, off, csz, usz = struct.unpack_from('<12sIII', d, diroff + 24 * i)
        ents.append((name.split(b'\0')[0].decode(), off, csz, usz))
    return d, ents
