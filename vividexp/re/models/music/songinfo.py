"""What the two songs use, after MIDAS's conversion (midas.convert_pattern): commands with their parameters, volume
column commands, speed / tempo changes by position, and the cases that reach a MIDAS quirk.

usage: python3 songinfo.py ../../../original/EXPRMNT1.XM
"""
import sys
from collections import Counter

import midas
import xm

NAMES = {1: 'arpeggio', 2: 'porta up', 3: 'porta down', 4: 'tone porta', 5: 'vibrato', 6: 'tone porta+vol slide',
         7: 'vibrato+vol slide', 8: 'tremolo', 9: 'set panning', 0xa: 'sample offset', 0xb: 'volume slide',
         0xc: 'position jump', 0xd: 'set volume', 0xe: 'pattern break', 0xf: 'speed', 0x10: 'tempo',
         0x11: 'fine porta', 0x12: 'pattern loop', 0x13: 'E8x panning', 0x14: 'retrig', 0x15: 'fine volume slide',
         0x16: 'note cut', 0x17: 'note delay', 0x18: 'pattern delay', 0x19: 'global volume',
         0x1a: 'global volume slide', 0x1b: 'multi retrig', 0x1c: 'sync', 0x1d: 'extra fine porta',
         0x1e: 'panning slide'}


def rows(m, pi):
    pd = m['patterns'][pi]['data']
    i = 0
    for r in range(m['patterns'][pi]['rows']):
        ents = []
        while True:
            b = pd[i]
            i += 1
            if b == 0:
                break
            e = {'ch': b & 0x1f}
            if b & 0x20:
                e['note'], e['inst'] = pd[i], pd[i + 1]
                i += 2
            if b & 0x40:
                e['vol'] = pd[i]
                i += 1
            if b & 0x80:
                e['cmd'], e['info'] = pd[i], pd[i + 1]
                i += 2
            ents.append(e)
        yield r, ents


class NullSD:
    def __init__(self):
        self.n = 0

    def add_sample(self, *a, **k):
        self.n += 1
        return self.n


def main():
    path = sys.argv[1]
    m = midas.load_xm(path, NullSD())
    f = xm.load(path)
    print('%s: %d orders, %d channels, speed %d, tempo %d, %s periods; patterns in the file %d, after loading %d; '
          'instruments in the file %d, after loading %d (used in the patterns: %s)' % (
              m['name'], m['length'], m['nchan'], m['speed'], m['tempo'], 'linear' if m['linear'] else 'Amiga',
              f['npatterns'], len(m['patterns']), f['ninstruments'], len(m['instruments']),
              ' '.join(str(i + 1) for i, ins in enumerate(m['instruments']) if ins.used)))
    cmds = Counter()
    infos = {}
    vols = Counter()
    quirks = Counter()
    seen = set()
    for o, pi in enumerate(m['orders']):
        for r, ents in rows(m, pi):
            for e in ents:
                c = e.get('cmd')
                if c is not None:
                    cmds[c] += 1
                    infos.setdefault(c, Counter())[e['info']] += 1
                    if c in (0xf, 0x10) and (o, r, c) not in seen:
                        seen.add((o, r, c))
                        print('  order %2d (pattern %2d) row %2d ch %2d: %s %d' % (o, pi, r, e['ch'], NAMES[c],
                                                                                 e['info']))
                    if c in (0xb, 0x15, 0x2, 0x3, 0x1, 0x5, 0x7, 0x6) and e['info'] == 0:
                        quirks['%s with info 0 (uses the shared memory +0x18)' % NAMES[c]] += 1
                    if c == 0xa and 'inst' in e and e['inst'] == 0xff:
                        quirks['sample offset on a note without instrument (offsets add up)'] += 1
                    if c == 0xa and 'note' not in e:
                        quirks['sample offset without a note'] += 1
                    if c == 4 and 'inst' in e and e['inst'] != 0xff:
                        quirks['tone porta with an instrument (sample of the current instrument)'] += 1
                if 'vol' in e and e['vol'] > 0x40:
                    vols[e['vol'] & 0xf0] += 1
                if e.get('note') == 0xfe:
                    quirks['key off (note 97 or Kxx)'] += 1
                if 'vol' in e and 0 < e['vol'] < 0x10 and False:
                    pass
    print('commands (played orders):')
    for c in sorted(cmds):
        top = ', '.join('%02x:%d' % kv for kv in sorted(infos[c].items())[:12])
        print('  %2d %-22s %5d   info: %s%s' % (c, NAMES[c], cmds[c], top, ' ...' if len(infos[c]) > 12 else ''))
    print('volume column commands:', ', '.join('%Xx:%d' % (k >> 4, v) for k, v in sorted(vols.items())))
    for q, n in quirks.items():
        print('  %5d  %s' % (n, q))


if __name__ == '__main__':
    main()
