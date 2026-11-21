# Hooks for run_engine.py: dump the logo part's engine state before and after each render (frame routine 0x402f1c).
# out/logo_dump.pkl: list of dicts {'frame', 'when', regions...}
import os, pickle, struct
OUT = os.environ.get('LOGO_DUMP', 'out/logo_dump.pkl')
FROM = int(os.environ.get('LOGO_FROM', '0'))
DUMPS = []

def _r32(mu, a): return struct.unpack('<I', mu.mem_read(a, 4))[0]

def _state(mu, st, when):
    if st['frame'] < FROM:
        return
    scene = _r32(mu, 0x425510); obj = _r32(mu, 0x425488); cam = _r32(mu, 0x42550c); light = _r32(mu, 0x425518)
    nv, nf = _r32(mu, obj + 0xd4), _r32(mu, obj + 0xdc)
    d = {'frame': st['frame'], 'irqs': st['irqs'], 'when': when, 'scene_addr': scene, 'obj_addr': obj,
         'cam_addr': cam, 'light_addr': light,
         'scene': bytes(mu.mem_read(scene, 0x50)), 'cam': bytes(mu.mem_read(cam, 0xc8)),
         'light': bytes(mu.mem_read(light, 0xd4)),
         'obj': bytes(mu.mem_read(obj, 0x10c + nv * 0x40 + nf * 0x34)),
         'glob': bytes(mu.mem_read(0x4b8f78, 0x4b9030 - 0x4b8f78)),
         'm425190': bytes(mu.mem_read(0x425190, 72)), 'flag42553c': _r32(mu, 0x42553c),
         'partticks': _r32(mu, 0x4248c8)}
    if when != 'before':
        d['buf'] = bytes(mu.mem_read(_r32(mu, scene + 8), 64000))
    DUMPS.append(d)
    if len(DUMPS) % 30 == 0 or when == 'final' and len(DUMPS) < 4:
        pickle.dump(DUMPS, open(OUT, 'wb'))

def before(mu, st): _state(mu, st, 'before')
def after(mu, st): _state(mu, st, 'after')
def final(mu, st):
    _state(mu, st, 'final')
    if DUMPS:
        pickle.dump(DUMPS, open(OUT, 'wb'))

HOOKS = {0x402f57: before, 0x402f5c: after, 0x402f92: final}
