"""p6/p7: widest triangle (x extent) in dumped frames: the plain filler (25f0) counts span pixels in AL (8 bits)."""
import struct, sys
L = 0xF64
for path in sys.argv[1:]:
    a = open(path, 'rb').read()
    p7 = '2C90' in path
    gs = ((0x3de1 if p7 else 0x667b) + L) * 16
    scr_off = 0x6d60 if p7 else 0x1900
    lst = 0 if p7 else 0x3200
    tabs = ((-0x63c0) & 0xffff, (-0x5420) & 0xffff, (-0x4480) & 0xffff) if p7 else (0x5140, 0x5910, 0x60e0)
    i = 0; widest = (0, None)
    while True:
        e = struct.unpack_from('<I', a, gs + lst + 4 * i)[0]
        if e & 0xffff == 0xffff: break
        f = e & 0xffff; i += 1
        xs = []
        for t in tabs:
            v = struct.unpack_from('<H', a, gs + ((t + 2 * f) & 0xffff))[0]
            bsel = v & 3
            s = struct.unpack_from('<I', a, gs + scr_off + (v & 0xfffc))[0]
            xs.append(struct.unpack('<h', struct.pack('<H', s >> 16))[0])
        w = max(xs) - min(xs)
        if w > widest[0]: widest = (w, f)
    print(path, 'faces', i, 'widest x extent', widest)
