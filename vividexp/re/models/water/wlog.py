"""Read the hooked run's logs (run_vivid.py copy in this folder): runs/w.wlog, runs/w.wdump, runs/w.wenv.

frames(): list of drawn frames in order: {'counts': [count per object k], 'draws': [(obj ptr, record)], 'flip': record,
'behind': letters drawn into the env map, 'buf': buffer index drawn into, 'shown': [records showing it]}
"""
import os
import struct

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
OBJ = {0x100f21c: 0, 0x1014e7c: 1, 0x102d5e8: 2, 0x10cfe7c: 3}


def frames(prefix=os.path.join(HERE, 'runs', 'w'), drawn0=1, shown0=1):
    out = []
    cur = {'counts': [None] * 4, 'draws': []}
    drawn = drawn0                    # [0x8324f8] at the snapshot
    last = {}                         # buffer -> frame index
    for line in open(prefix + '.wlog'):
        f = line.split()
        if f[0] == 'T':
            cur['counts'][int(f[4])] = int(f[2]) - int(f[3])
        elif f[0] == 'D':
            cur['draws'].append((OBJ[int(f[2], 16)], int(f[1])))
        elif f[0] == 'F':
            cur['flip'] = int(f[1])
            cur['behind'] = int(f[3])
            cur['buf'] = drawn
            cur['shown'] = []
            last[drawn] = len(out)
            out.append(cur)
            drawn = (drawn + 1) % 2
            cur = {'counts': [None] * 4, 'draws': []}
        elif f[0] == 'C':
            b = int(f[2])
            if b in last:
                out[last[b]]['shown'].append(int(f[1]))
    return out


def dumps(prefix=os.path.join(HERE, 'runs', 'w')):
    """yields (record, obj k, verts (n, 10) int16 words, normals (n, 6) int16, flags uint8)"""
    with open(prefix + '.wdump', 'rb') as fh:
        while True:
            h = fh.read(24)
            if len(h) < 24:
                return
            fr, obj, tex, lv, ln, lf = struct.unpack('<6I', h)
            v = np.frombuffer(fh.read(lv), '<i2').reshape(-1, 10)
            n = np.frombuffer(fh.read(ln), '<i2').reshape(-1, 6)
            fl = np.frombuffer(fh.read(lf), np.uint8)
            yield fr, OBJ[obj], v, n, fl


def envs(prefix=os.path.join(HERE, 'runs', 'w')):
    with open(prefix + '.wenv', 'rb') as fh:
        while True:
            h = fh.read(4)
            if len(h) < 4:
                return
            yield struct.unpack('<I', h)[0], np.frombuffer(fh.read(65536), np.uint8).reshape(256, 256)
