"""Parse the dumps written by run_hook.py (--hook / --hook-dump): one line per hit."""
import struct


def parse(path):
    out = []
    for line in open(path):
        p = line.split()
        rec = {'frame': int(p[0]), 'pit': int(p[1]), 'time': float(p[2])}
        for kv in p[3:]:
            k, v = kv.split('=')
            rec[k] = bytes.fromhex(v)
        out.append(rec)
    return out


def i32(b, o): return struct.unpack_from('<i', b, o)[0]
def i16(b, o): return struct.unpack_from('<h', b, o)[0]
def u8(b, o): return b[o]
