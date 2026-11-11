"""Model of the colour add-tables 0x1a336 (bias eax, gain [0x1a225], flash [0x1a478]) for 5:6:5, checked against
the tables at 0x1a479 / 0x1b079 / 0x1bc79 in a dump: python3 tables.py DUMP.pkl BIAS GAIN FLASH"""
import sys, pickle, struct
import numpy as np

def table(bias, gain, f, size, pos):
    out = []
    for i in range(768):
        v = (i * gain + (bias << 8)) >> 8
        v = 0 if v < 0 else (255 if v >= 256 else v)
        w = ((255 - f) * v + v + (255 - v) * f) >> 8
        out.append(((w << size) >> 8) << pos)
    return np.array(out, np.uint32)

if __name__ == '__main__':
    d = pickle.load(open(sys.argv[1], 'rb')); m = d['mem']
    bias, gain, f = int(sys.argv[2]), int(sys.argv[3], 0), int(sys.argv[4], 0)
    for name, a, size, pos in (('R', 0x1a479, 5, 11), ('G', 0x1b079, 6, 5), ('B', 0x1bc79, 5, 0)):
        mem = np.frombuffer(m[a:a + 3072], '<u4')
        print(name, 'equal' if np.array_equal(mem, table(bias, gain, f, size, pos)) else 'DIFFERENT')
