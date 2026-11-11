import sys; sys.path.insert(0, sys.argv[1])
import m
M = m.load(sys.argv[2] if len(sys.argv) > 2 else 't120')
names = {0x2449d:'objlist?',0x24499:'objcount?',0x1ee05:'rnd shades',0x1ee09:'rnd shades2',0x1ee0d:'rnd jitter',0x1edfd:'scratch128k',0x1edd1:'lightmap512x256',0x1edc5:'flags64000',0x1edd5:'tab1440',0x1edc9:'buf320 A',0x1edcd:'buf320 B',0x1eddd:'tex256',0x1eded:'shade tabs',0x1edd9:'grid41x26',0x1edf5:'paper',0x1edf9:'backbuf',0x1ede1:'logo glow',0x1ede5:'psychic link text',0x1ede9:'font',0x483b:'strokes',0x4837:'strokes cur',0x4833:'stroke n',0x1edf1:'vga',8:'heap ptr',0xc:'heap top', 0x1edb5:'pal level',0x1edbd:'pal target',0x1edb9:'pal step',0x1edad:'blur budget', 0x5de4:'plane obj'}
for a in sorted(names): print(hex(a), names[a], hex(m.d32(M, a)))
print('5db8 table', [hex(m.d32(M, 0x5db8 + 4*i)) for i in range(11)])
