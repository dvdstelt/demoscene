"""Compare the model's AMP, DSMI-timer and driver state with the snapshots (snaps/f*.snap): run dsmi.py to the
snapshot's interrupt count and compare AMP's globals, the 4 channel records (0x422a99 + 0x2d ch), their sample
records (0x423039 + 0x1a ch), DSMI time (0x4b9148) and the driver's voice records (0x1155fe8 + 14 v).
usage: python3 statecheck.py [snapshots...]   (default: all of ../../snaps)"""
import glob
import os
import pickle
import struct
import sys

import unicorn  # noqa: F401  (the snapshots need it)

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import amf   # noqa: E402
import dsmi  # noqa: E402


def b(mem, a): return mem[a]
def w(mem, a): return struct.unpack_from('<H', mem, a)[0]
def d(mem, a): return struct.unpack_from('<I', mem, a)[0]


def model_state(ds):
    a, dr = ds.amp, ds.drv
    s = {'order': a.order, 'row': a.row, 'rowc': a.rowc, 'tick': a.tick_, 'speed': a.speed, 'tempo': a.tempo,
         'div': a.div, 'last': a.last & 0xffffffff, 'time': ds.time, 'status': a.playing}
    for i in range(4):
        c = a.ch[i]
        pos = 0xffff if c.pos == -1 else 3 + 3 * c.pos
        s.update({('ch%d' % i, k): v for k, v in (
            ('pos', pos), ('note', c.note), ('inst', c.inst), ('vol', c.vol), ('count', c.count),
            ('period', c.period), ('cmd', c.cmd), ('val', c.val & 0xff), ('target', c.target),
            ('tpspeed', c.tpspeed & 0xff), ('tplast', c.tplast & 0xff), ('portalast', c.portalast & 0xff),
            ('vibpos', c.vibpos), ('offlast', c.offlast), ('pan', c.pan & 0xff),
            ('s.length', c.s['length']), ('s.rate', c.s['rate']), ('s.freq', c.s['freq']), ('s.vol', c.s['vol']),
            ('s.loopstart', c.s['loopstart']), ('s.loopend', c.s['loopend']))})
    for v in range(24):
        x = dr.v[v]
        s.update({('voice%d' % v, k): val for k, val in (('addr', x.addr), ('freq', x.freq), ('vol', x.vol),
                                                          ('mode', x.mode), ('muted', x.muted))})
    return s


def snap_state(mem):
    s = {'order': b(mem, 0x422a5d), 'row': w(mem, 0x422a61), 'rowc': w(mem, 0x422a5f), 'tick': b(mem, 0x422a63),
         'speed': b(mem, 0x422a6c), 'tempo': b(mem, 0x422a6d), 'div': w(mem, 0x4233a8), 'last': d(mem, 0x4233aa),
         'time': d(mem, 0x4b9148), 'status': b(mem, 0x422a58)}
    for i in range(4):
        r = 0x422a99 + 0x2d * i
        q = 0x423039 + 0x1a * i
        s.update({('ch%d' % i, k): v for k, v in (
            ('pos', w(mem, r + 4)), ('note', b(mem, r + 8)), ('inst', b(mem, r + 9)), ('vol', b(mem, r + 0xa)),
            ('count', w(mem, r + 0xb)), ('period', w(mem, r + 0xd)), ('cmd', b(mem, r + 0xf)),
            ('val', b(mem, r + 0x10)), ('target', w(mem, r + 0x11)), ('tpspeed', b(mem, r + 0x13)),
            ('tplast', b(mem, r + 0x14)), ('portalast', b(mem, r + 0x15)), ('vibpos', b(mem, r + 0x23)),
            ('offlast', b(mem, r + 0x29)), ('pan', b(mem, r + 0x2c)),
            ('s.length', d(mem, q + 4)), ('s.rate', w(mem, q + 0x13)), ('s.freq', d(mem, q + 0x15)),
            ('s.vol', b(mem, q + 0x19)), ('s.loopstart', d(mem, q + 8)), ('s.loopend', d(mem, q + 0xc)))})
    for v in range(24):
        r = 0x1155fe8 + 14 * v
        s.update({('voice%d' % v, k): val for k, val in (('addr', d(mem, r)), ('freq', d(mem, r + 4)),
                                                          ('vol', d(mem, r + 8)), ('mode', b(mem, r + 0xc)),
                                                          ('muted', b(mem, r + 0xd)))})
    return s


def main():
    snaps = sys.argv[1:] or sorted(glob.glob(os.path.join(HERE, '..', '..', 'snaps', 'f*.snap')),
                                   key=lambda p: int(os.path.basename(p)[1:-5]))
    m = amf.AMF(os.path.join(dsmi.ORIG, 'EYE.AMF'))
    allok = True
    for path in snaps:
        snap = pickle.load(open(path, 'rb'))
        irqs = snap['st']['irqs']
        ds = dsmi.Dsmi(m)
        ds.start()
        for i in range(91, irqs + 1):
            if i != 12117:
                ds.interrupt()
        mine, ref = model_state(ds), snap_state(snap['mem'])
        bad = [(k, ref[k], mine[k]) for k in ref if ref[k] != mine[k]]
        allok &= not bad
        print('%-10s irqs %5d  %d fields, %s' % (os.path.basename(path), irqs, len(ref),
                                                'identical' if not bad else '%d differ: %s' % (len(bad), bad[:6])))
    print('ALL MATCH' if allok else 'MISMATCH')


if __name__ == '__main__':
    main()
