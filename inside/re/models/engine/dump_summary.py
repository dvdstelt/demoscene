"""Summarise what a hooks_scene.py dump exercises: objects, flags, parents, rasterizers, billboards."""
import pickle, struct, sys
D = pickle.load(open(sys.argv[1], 'rb'))
seen = {}
for d in D:
    if d['when'] != 'before':
        continue
    for o in d['objs']:
        h = o['hdr']
        u = lambda off: struct.unpack_from('<I', h, off)[0]
        key = o['addr']
        draws = sorted(set(hex(struct.unpack_from('<I', fb, 8)[0]) for _, fb in o['faces']))
        if u(0) == 0x42381c:
            draws = [hex(u(0xd0))]
        e = seen.setdefault(key, {'pipe': hex(u(0)), 'parent': hex(u(0xc)), 'flags': set(), 'draws': set(), 'n': (len(o['verts']), len(o['faces'])), 'bias': struct.unpack_from('<i', h, 0xcc)[0]})
        e['flags'].add(hex(u(0xc4) & 0xffff))
        e['draws'].update(draws)
for k, e in seen.items():
    print(hex(k), e['pipe'], 'parent', e['parent'], 'flags', sorted(e['flags']), 'draw', sorted(e['draws']), 'v/f', e['n'], 'bias', e['bias'])
