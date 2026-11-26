"""Parse an XM file (FastTracker 2.00 format, as EXPRMNT1.XM / EXPRMNT2.XM are) into plain Python structures.

usage: python3 xm.py ../../../original/EXPRMNT1.XM      prints the song: header, orders, instruments, effects used
In Python: xm.load(path) -> dict (see load()).
"""
import struct
import sys
from collections import Counter

NOTE_OFF = 97


def load(path):
    d = open(path, 'rb').read()
    assert d[:17] == b'Extended Module: '
    m = {'name': d[17:37].rstrip(b'\0 ').decode('latin1'), 'tracker': d[38:58].decode('latin1')}
    m['version'] = struct.unpack_from('<H', d, 58)[0]
    hsize = struct.unpack_from('<I', d, 60)[0]
    (m['length'], m['restart'], m['channels'], m['npatterns'], m['ninstruments'], m['flags'], m['speed'],
     m['tempo']) = struct.unpack_from('<8H', d, 64)
    m['orders'] = list(d[80:80 + m['length']])
    m['order_table'] = d[80:80 + 256]
    p = 60 + hsize
    pats = []
    for i in range(m['npatterns']):
        plen, ptype, rows, psize = struct.unpack_from('<IBHH', d, p)
        raw = d[p + plen:p + plen + psize]
        cells = unpack_pattern(raw, rows, m['channels'])
        pats.append({'rows': rows, 'cells': cells, 'offset': p, 'hdr': plen, 'packed': psize})
        p += plen + psize
    m['patterns'] = pats
    insts = []
    for i in range(m['ninstruments']):
        isize = struct.unpack_from('<I', d, p)[0]
        ins = {'offset': p, 'size': isize, 'name': d[p + 4:p + 26].rstrip(b'\0 ').decode('latin1'),
               'type': d[p + 26], 'nsamples': struct.unpack_from('<H', d, p + 27)[0], 'samples': []}
        if ins['nsamples']:
            q = p + 29
            ins['shsize'] = struct.unpack_from('<I', d, q)[0]
            ins['keymap'] = list(d[q + 4:q + 100])
            ins['volenv'] = [struct.unpack_from('<2H', d, q + 100 + 4 * k) for k in range(12)]
            ins['panenv'] = [struct.unpack_from('<2H', d, q + 148 + 4 * k) for k in range(12)]
            (ins['nvol'], ins['npan'], ins['volsus'], ins['volls'], ins['volle'], ins['pansus'], ins['panls'],
             ins['panle'], ins['voltype'], ins['pantype'], ins['vibtype'], ins['vibsweep'], ins['vibdepth'],
             ins['vibrate']) = d[q + 196:q + 210]
            ins['fadeout'] = struct.unpack_from('<H', d, q + 210)[0]
            p += isize
            hdrs = []
            for s in range(ins['nsamples']):
                ln, ls, ll = struct.unpack_from('<3I', d, p)
                vol, fine, typ, pan, rel, res = struct.unpack_from('<BbBBbB', d, p + 12)
                nm = d[p + 18:p + 40].rstrip(b'\0 ').decode('latin1')
                hdrs.append({'length': ln, 'loopstart': ls, 'looplen': ll, 'volume': vol, 'finetune': fine,
                             'type': typ, 'panning': pan, 'relnote': rel, 'name': nm, 'hdr': p})
                p += ins['shsize']
            for h in hdrs:
                h['data_offset'] = p
                raw = d[p:p + h['length']]
                h['raw'] = raw
                p += h['length']
            ins['samples'] = hdrs
        else:
            p += isize
        insts.append(ins)
    m['instruments'] = insts
    m['end'] = p
    m['filesize'] = len(d)
    return m


def unpack_pattern(raw, rows, chans):
    cells = []
    i = 0
    for r in range(rows):
        row = []
        for c in range(chans):
            if not raw:
                row.append((0, 0, 0, 0, 0)); continue
            b = raw[i]
            if b & 0x80:
                i += 1
                v = []
                for bit in range(5):
                    if b & (1 << bit):
                        v.append(raw[i]); i += 1
                    else:
                        v.append(0)
                row.append(tuple(v))
            else:
                row.append(tuple(raw[i:i + 5])); i += 5
        cells.append(row)
    return cells


def delta_decode(raw, bits16):
    """XM sample data is stored as deltas; returns signed sample values."""
    out = []
    acc = 0
    if bits16:
        for k in range(0, len(raw) - 1, 2):
            acc = (acc + struct.unpack_from('<h', raw, k)[0]) & 0xffff
            out.append(acc - 0x10000 if acc & 0x8000 else acc)
    else:
        for b in raw:
            acc = (acc + b) & 0xff
            out.append(acc - 0x100 if acc & 0x80 else acc)
    return out


def summary(m):
    print(f"'{m['name']}' {m['tracker']} version {m['version']:#x}: {m['length']} orders, restart {m['restart']}, "
          f"{m['channels']} channels, {m['npatterns']} patterns, {m['ninstruments']} instruments, "
          f"flags {m['flags']} ({'linear' if m['flags'] & 1 else 'Amiga'}), speed {m['speed']}, tempo {m['tempo']}; "
          f"file {m['filesize']} bytes, parsed to {m['end']}")
    print('orders:', ' '.join(str(o) for o in m['orders']))
    eff = Counter()
    vcol = Counter()
    ex = Counter()
    used_pats = set(m['orders'])
    for pi in sorted(used_pats):
        if pi >= len(m['patterns']):
            print(f'order uses pattern {pi}, which the file does not have')
            continue
        pat = m['patterns'][pi]
        for row in pat['cells']:
            for (n, ins, vc, fx, fp) in row:
                if fx or fp:
                    eff[fx] += 1
                    if fx == 0xe:
                        ex[fp >> 4] += 1
                if vc:
                    vcol[vc >> 4] += 1
    print('pattern rows:', ' '.join(f"{i}:{p['rows']}" for i, p in enumerate(m['patterns'])))
    print('effects (used patterns):', ', '.join(f'{k:X}:{v}' for k, v in sorted(eff.items())))
    print('Exy:', ', '.join(f'E{k:X}:{v}' for k, v in sorted(ex.items())))
    print('volume column:', ', '.join(f'{k:X}x:{v}' for k, v in sorted(vcol.items())))
    for i, ins in enumerate(m['instruments']):
        s = f"ins {i + 1:2} '{ins['name']}' samples {ins['nsamples']}"
        if ins['nsamples']:
            s += (f" vol env {ins['nvol']}pts type {ins['voltype']} sus {ins['volsus']} loop {ins['volls']}-{ins['volle']}"
                  f" | pan env {ins['npan']} type {ins['pantype']} | vib {ins['vibtype']},{ins['vibsweep']},{ins['vibdepth']},{ins['vibrate']}"
                  f" fade {ins['fadeout']}")
        print(s)
        for j, sm in enumerate(ins['samples']):
            print(f"     smp {j} len {sm['length']} loop {sm['loopstart']}+{sm['looplen']} type {sm['type']:#x} vol {sm['volume']}"
                  f" fine {sm['finetune']} pan {sm['panning']} rel {sm['relnote']} '{sm['name']}'")


if __name__ == '__main__':
    summary(load(sys.argv[1]))
