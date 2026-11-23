# Generic hooks for run_engine.py: dump a scene's engine state before a render call and the buffer after it.
# Environment (pass with `toolbox run ... env NAME=value python3 ...`):
#   SCENE_PTR  address of the global holding the scene pointer (hex), e.g. 4b6820 for the flower
#   CALL_AT    address of the `call 0x40c6ac` (hex); AFTER_AT the next instruction (hex)
#   DUMP       output pickle; FROM / TO: refreshes to dump; EVERY: dump every n-th drawn frame
# Each entry: {'frame', 'when', 'scene', 'objs': [{addr, hdr (0x10c bytes), verts [(addr, 0x40 bytes)], faces
# [(addr, 0x34 bytes)]}], 'mats': {addr: 0x44 bytes}, 'glob': engine globals, 'extra': {addr: 4 bytes}, 'buf'}
import os, pickle, struct
E = os.environ
SCENE_PTR = int(E.get('SCENE_PTR', '4b6820'), 16)
CALL_AT = int(E.get('CALL_AT', '40970c'), 16)
AFTER_AT = int(E.get('AFTER_AT', '409711'), 16)
OUT = E.get('DUMP', 'out/scene_dump.pkl')
FROM, TO, EVERY = int(E.get('FROM', '0')), int(E.get('TO', '1000000')), int(E.get('EVERY', '1'))
EXTRA = [int(x, 16) for x in E.get('EXTRA', '').split(',') if x]
DUMPS = []
STATE = {'n': 0, 'on': False}

def r32(mu, a): return struct.unpack('<I', mu.mem_read(a, 4))[0]

def walk(mu, first, size):
    out, p, k = [], first, 0
    while p and k < 100000:
        out.append((p, bytes(mu.mem_read(p, size))))
        p = r32(mu, p)
        k += 1
    return out

def before(mu, st):
    STATE['on'] = False
    if not (FROM <= st['frame'] <= TO):
        return
    STATE['n'] += 1
    if (STATE['n'] - 1) % EVERY:
        return
    sc = r32(mu, SCENE_PTR)
    objs, mats = [], {}
    o, k = r32(mu, sc + 4), 0
    while o and k < 1000:
        hdr = bytes(mu.mem_read(o, 0x10c))
        pipe = struct.unpack_from('<I', hdr, 0)[0]
        ent = {'addr': o, 'hdr': hdr, 'verts': [], 'faces': []}
        if pipe == 0x423a84:
            ent['verts'] = walk(mu, struct.unpack_from('<I', hdr, 0xd0)[0], 0x40)
            ent['faces'] = walk(mu, struct.unpack_from('<I', hdr, 0xd8)[0], 0x34)
            for _, fb in ent['faces']:
                m = struct.unpack_from('<I', fb, 0x10)[0]
                if m and m not in mats:
                    mats[m] = bytes(mu.mem_read(m, 0x44))
        objs.append(ent)
        o = r32(mu, o + 4)
        k += 1
    mem = {}                              # textures and tables the materials and billboards point at
    for m, mb in mats.items():
        for off, size in ((0, 0x40000), (4, 0x10000), (8, 0x10000), (0xc, 0x10000)):
            a = struct.unpack_from('<I', mb, off)[0]
            if a and a not in mem:
                mem[a] = bytes(mu.mem_read(a, size))
    for ent in objs:
        h = ent['hdr']
        if struct.unpack_from('<I', h, 0)[0] == 0x42381c:
            for off, size in ((0xd8, struct.unpack_from('<i', h, 0xe0)[0] * struct.unpack_from('<i', h, 0xe8)[0]),
                              (0xdc, 0x10000)):
                a = struct.unpack_from('<I', h, off)[0]
                if a and a not in mem and size > 0:
                    mem[a] = bytes(mu.mem_read(a, size))
    cam = r32(mu, sc + 0x38)
    DUMPS.append({'frame': st['frame'], 'when': 'before', 'scene_addr': sc, 'scene': bytes(mu.mem_read(sc, 0x50)),
                  'cam': cam, 'objs': objs, 'mats': mats,
                  'extra': {a: bytes(mu.mem_read(a, 4)) for a in EXTRA},
                  'buf0': bytes(mu.mem_read(r32(mu, sc + 8), 64000)), 'mem': mem})
    STATE['on'] = True

def after(mu, st):
    if not STATE['on']:
        return
    sc = r32(mu, SCENE_PTR)
    DUMPS.append({'frame': st['frame'], 'when': 'after', 'buf': bytes(mu.mem_read(r32(mu, sc + 8), 64000))})
    STATE['on'] = False
    if len(DUMPS) % 20 == 0:
        pickle.dump(DUMPS, open(OUT, 'wb'))

HOOKS = {CALL_AT: before, AFTER_AT: after}

import atexit
atexit.register(lambda: DUMPS and pickle.dump(DUMPS, open(OUT, 'wb')))
