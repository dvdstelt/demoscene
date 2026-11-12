"""Dump p4's static variables and small tables from unpacked/p4.bin (segment 0010 = file offset 0x100)."""
import struct, sys
img = open('unpacked/p4.bin', 'rb').read()
CS = 0x100
def w(o): return struct.unpack_from('<H', img, CS + o)[0]
def sw(o): return struct.unpack_from('<h', img, CS + o)[0]
def b(o): return img[CS + o]
print('sine quarter cs:0000.. first 16:', [sw(2*i) for i in range(16)], '... entry 0x1ff:', sw(0x3fe))
print('max', max(sw(2*i) for i in range(512)))
print('vars 1400..1420:', img[CS+0x1400:CS+0x1420].hex(' '))
print('1576 step', hex(w(0x1576)), '1578 phase', hex(w(0x1578)), '157a', hex(struct.unpack_from('<I', img, CS+0x157a)[0]))
print('157e pal (16):', img[CS+0x157e:CS+0x15ae].hex(' '))
print('15ae fire pal (15):')
for i in range(15): print('  ', i, list(img[CS+0x15ae+3*i:CS+0x15ae+3*i+3]))
print('15db..15e9:', img[CS+0x15db:CS+0x15e9].hex(' '))
print('1960..1980:', img[CS+0x1960:CS+0x1980].hex(' '))
for o in (0x1969, 0x196b, 0x196d, 0x196f, 0x1971, 0x1973): print(hex(o), hex(w(o)))
print('2058 dist table:', [w(0x2058 + 2*i) for i in range(60)])
print('20d2 pal:', [list(img[CS+0x20d2+3*i:CS+0x20d2+3*i+3]) for i in range(16)])
print('2102..2121:', img[CS+0x2102:CS+0x2121].hex(' '))
for o in (0x210d, 0x210f, 0x2111, 0x2113, 0x2115, 0x2117, 0x2119, 0x211b): print(hex(o), hex(w(o)))
print('end of code image seg 0010 nonzero after 23a1+0x280?', max(img[CS+0x2121:CS+0x2121+0x500]))
