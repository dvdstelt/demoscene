# Print the bitmap effects' offsets (0x40c21c..0x40c22a, 8 int16 words, 8.8 fixed point), speeds (0x825c6c..),
# ini ints (0x825c7c..), frame counter and the palette fade state at each snapshot.
import sys, os, pickle, glob, struct
import unicorn  # needed to unpickle
import numpy as np
HERE=os.path.dirname(os.path.abspath(__file__)); RE=os.path.abspath(os.path.join(HERE,'..','..'))
def words(m,a,n): return struct.unpack('<%dh'%n, bytes(m[a:a+2*n]))
for d in sys.argv[1:]:
    for p in sorted(glob.glob(os.path.join(RE,d,'f*.snap')), key=lambda s:int(os.path.basename(s)[1:-5])):
        s=pickle.load(open(p,'rb')); m=s['mem']
        fc=struct.unpack('<I',bytes(m[0x825c5c:0x825c60]))[0]
        print(d, os.path.basename(p), 'fc',fc, 'offs',words(m,0x40c21c,8),'speeds',words(m,0x825c6c,8),'ini',struct.unpack('<4i',bytes(m[0x825c7c:0x825c8c])), 'fade', struct.unpack('<5i',bytes(m[0x8282bc:0x8282d0])), 'eip %x'%s['eip'])
