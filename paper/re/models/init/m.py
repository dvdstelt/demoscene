import numpy as np, struct, sys, os
D = os.path.dirname(os.path.abspath(__file__))
def load(name='t120'):
    return np.memmap(os.path.join(D, name + '.mem'), dtype=np.uint8, mode='r')
def d32(m, a): return struct.unpack('<I', bytes(m[a:a+4]))[0]
def i32(m, a): return struct.unpack('<i', bytes(m[a:a+4]))[0]
