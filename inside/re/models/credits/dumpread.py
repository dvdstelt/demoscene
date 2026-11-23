"""Read the credits dumps written by models/credits/run_inside.py --dump PREFIX (a pickle stream)."""
import pickle, struct
import numpy as np


def records(path):
    f = open(path, 'rb')
    while True:
        try:
            yield pickle.load(f)
        except EOFError:
            return


def I(b, o, n=1): return struct.unpack_from('<%di' % n, b, o) if n > 1 else struct.unpack_from('<i', b, o)[0]
def D(b, o, n=1): return struct.unpack_from('<%dd' % n, b, o) if n > 1 else struct.unpack_from('<d', b, o)[0]
def F(b, o, n=1): return struct.unpack_from('<%df' % n, b, o) if n > 1 else struct.unpack_from('<f', b, o)[0]
def H(b, o): return struct.unpack_from('<h', b, o)[0]
def U(b, o): return struct.unpack_from('<H', b, o)[0]


def mat(b, o=0x28): return np.array(D(b, o, 9)).reshape(3, 3)


def tvars(v):
    """the doubles / floats at 0x426320..: S (spike), C (camera), pos, name index, ..."""
    return {'S': D(v, 0, 3), 'C': D(v, 0x70, 3), 'pos': F(v, 0x114), 'name': I(v, 0x118), 'event': I(v, 0x110)}
