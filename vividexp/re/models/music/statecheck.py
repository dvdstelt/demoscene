"""Compare the port's state with the snapshots: the GMP channels (0x40 bytes each at [[0x844cc4] + 0x48]), the
play handle, the module's global volume and the GUS voice records (0x4c bytes at 0x8239a4), after the same number of
timer interrupts.

usage: python3 statecheck.py full ../../snaps        (or part2 ../../snaps2)
"""
import glob
import os
import pickle
import struct
import sys

import unicorn  # noqa: F401  (the snapshots' pickled context)

import player


def snap_state(path):
    s = pickle.load(open(path, 'rb'))
    mem = s['mem']
    d32 = lambda a: struct.unpack_from('<i', mem, a)[0]
    u8 = lambda a: mem[a]
    h = d32(0x844cc4)
    st = {'irqs': s['st']['irqs']}
    st['pos'], st['pattern'], st['row'] = d32(h + 0xc), d32(h + 0x10), d32(h + 0x14)
    st['speed'], st['tick'], st['tempo'] = d32(h + 0x24), d32(h + 0x28), d32(0x844cc0)
    mod = d32(h + 4)
    st['gv'] = u8(mod + 0x35)
    n = d32(h + 0x30)
    chans = []
    base = d32(h + 0x48)
    for i in range(n):
        c = base + 0x40 * i
        chans.append({'period': d32(c), 'inst': d32(c + 4), 'newinst': d32(c + 8), 'target': d32(c + 0xc),
                      'offset': d32(c + 0x10), 'sample': u8(c + 0x14), 'note': u8(c + 0x17), 'info': d32(c + 0x18),
                      'vol': u8(c + 0x1c), 'vol2': u8(c + 0x20), 'pan': u8(c + 0x21),
                      'vpos': d32(c + 0x22), 'ppos': d32(c + 0x26), 'fade': d32(c + 0x2a), 'vdone': u8(c + 0x2e),
                      'pdone': u8(c + 0x2f), 'release': u8(c + 0x30), 'final': u8(c + 0x32),
                      'vibspeed': u8(c + 0x34), 'vibdepth': u8(c + 0x35), 'vibpos': u8(c + 0x36),
                      'offmem': u8(c + 0x37), 'portaspeed': u8(c + 0x1f), 'flags': u8(c + 0x3c) & 0x1f})
    st['chans'] = chans
    voices = []
    for v in range(n):
        a = 0x8239a4 + 0x4c * v
        voices.append({'flags': d32(a), 'sample': d32(a + 4), 'playing': d32(a + 8), 'fc': d32(a + 0xc),
                       'rate': d32(a + 0x10), 'volume': d32(a + 0x14), 'start': d32(a + 0x2c),
                       'lstart': d32(a + 0x30), 'lend': d32(a + 0x34), 'pan': d32(a + 0x40), 'active': d32(a + 0x48)})
    st['voices'] = voices
    return st


def model_state(r):
    g = r.gmp
    st = {'irqs': r.timer.irqs, 'pos': g.pos, 'pattern': g.pattern, 'row': g.row, 'speed': g.speed, 'tick': g.tick,
          'tempo': g.tempo, 'gv': g.m['gv']}
    chans = []
    for c in g.chans:
        chans.append({'period': c.period, 'inst': c.inst, 'newinst': c.newinst, 'target': c.target,
                      'offset': c.offset, 'sample': c.sample, 'note': c.note, 'info': c.info, 'vol': c.vol,
                      'vol2': c.vol2, 'pan': c.pan, 'vpos': c.vpos, 'ppos': c.ppos, 'fade': c.fade,
                      'vdone': c.vdone, 'pdone': c.pdone, 'release': c.release, 'final': c.final,
                      'vibspeed': c.vibspeed, 'vibdepth': c.vibdepth, 'vibpos': c.vibpos, 'offmem': c.offmem,
                      'portaspeed': c.portaspeed, 'flags': c.flags & 0x1f})
    st['chans'] = chans
    voices = []
    for v in range(len(g.chans)):
        x = r.sd.voices[v]
        voices.append({'flags': x.flags, 'sample': x.sample, 'playing': x.playing, 'fc': x.fc, 'rate': x.rate,
                       'volume': x.volume, 'start': x.start, 'lstart': x.lstart, 'lend': x.lend, 'pan': x.pan,
                       'active': x.active})
    st['voices'] = voices
    return st


def clean(s, m):
    """Fields the original never initialises (heap garbage until first written) are not compared until written."""
    for cs, cm in zip(s['chans'], m['chans']):
        if cm['info'] == 0 and cs['info'] != 0:
            cs['info'] = 0
        cs['flags'] &= 0x0f
        cm['flags'] &= 0x0f
        if cm['inst'] == -1:
            for k in ('vpos', 'ppos', 'fade', 'vdone', 'pdone'):
                cs[k] = cm[k]


def diff(a, b, path=''):
    out = []
    if isinstance(a, dict):
        for k in a:
            out += diff(a[k], b[k], path + '.' + str(k))
    elif isinstance(a, list):
        for i, (x, y) in enumerate(zip(a, b)):
            out += diff(x, y, '%s[%d]' % (path, i))
    elif a != b:
        out.append('%s: snapshot %s model %s' % (path, a, b))
    return out


def main():
    which, d = sys.argv[1], sys.argv[2]
    snaps = {}
    for p in sorted(glob.glob(os.path.join(d, '*.snap'))):
        s = snap_state(p)
        snaps.setdefault(s['irqs'], []).append((p, s))
    print('%d snapshots' % sum(len(v) for v in snaps.values()))
    results = []
    fields = [0]

    def on_irq(r):
        if r.timer.irqs in snaps and r.gmp is not None:
            m = model_state(r)
            for p, s in snaps.pop(r.timer.irqs):
                if s['chans'] and len(s['chans']) == len(m['chans']):
                    clean(s, m)
                    dd = diff(s, m)
                    fields[0] += 9 + 24 * len(m['chans']) + 11 * len(m['voices'])
                    results.append((os.path.basename(p), s['irqs'], dd))
    r = player.Run(which)
    r.go(on_irq=on_irq)
    bad = 0
    for name, irq, dd in results:
        print('%-14s irq %6d: %s' % (name, irq, 'MATCH' if not dd else '%d differences' % len(dd)))
        for x in dd[:int(os.environ.get("SHOW", "8"))]:
            print('     ', x)
        bad += bool(dd)
    print('%d snapshots compared, %d fields each run, %d with differences' % (len(results), fields[0], bad))


if __name__ == '__main__':
    main()
