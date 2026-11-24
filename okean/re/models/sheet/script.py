"""Print the text printer's state at the start of the sheet part and the script it will print from."""
import os, pickle, struct
import unicorn  # noqa
HERE = os.path.dirname(os.path.abspath(__file__))
RE = os.path.dirname(os.path.dirname(HERE))
snap = pickle.load(open(os.path.join(RE, 'snaps', 'part_5334f.snap'), 'rb'))
mem = snap['mem']
binf = open(os.path.join(RE, 'unpacked', 'korso2.bin'), 'rb').read()
C32 = 0x8fc0
def m32(a): return struct.unpack('<i', mem[C32 + a:C32 + a + 4])[0]
for n, a in [('ptr 4c8e4', 0x4c8e4), ('pos 4c8dc', 0x4c8dc), ('lines 4c8e0', 0x4c8e0), ('spacing 3ceb0', 0x3ceb0),
             ('order 3ceb8(b)', 0x3ceb8), ('rowbit 3cebe', 0x3cebe), ('3ceac', 0x3ceac), ('4cc98', 0x4cc98)]:
    print(n, m32(a), hex(m32(a) & 0xffffffff))
print('order/row', mem[C32 + 0x12b75], struct.unpack('<H', mem[C32 + 0x12b73:C32 + 0x12b75])[0])
p = m32(0x4c8e4)
s = bytes(mem[C32 + p - 40:C32 + p + 300])
print(repr(s))
# find the whole script: look for strings with '@' around
print(hex(p))
print(repr(bytes(mem[C32 + p:C32 + p + 200])))
cm = bytes(mem[C32 + 0x4c861:C32 + 0x4c861 + 0x90])
print('charmap', list(cm))
gw = bytes(mem[C32 + 0x4c8c2:C32 + 0x4c8c2 + 40])
print('glyph widths', list(gw))
print('file script same', bytes(mem[C32 + 0x4c8e8:C32 + 0x4ca80]) == binf[0xec0 + 0x4c8e8:0xec0 + 0x4ca80])
# whole script start
st = bytes(mem[C32 + 0x4c8e8:C32 + 0x4ca80])
print(repr(st))
