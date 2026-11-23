"""Hooks for run_inside.py (this folder's copy): record the logo part's state.

Per tick routine call (0x402e30): refresh, part ticks, the object matrix 0x425190 (9 doubles) after the step.
Per frame routine call (0x402f1c): at entry: refresh, part ticks, the matrix, [0x42553c]; at the draw stage
0x40c13c: the object (0x10c-byte header, 130 vertex records of 0x40 bytes, 256 face records of 0x34 bytes), the
camera (0xc8 bytes), the light (0xd4 bytes), the engine globals 0x4b8fc0..0x4b9028 and the sorted face list (face
addresses in drawing order); after 0x40c6ac (0x402f5c) the draw target, and before the flip (0x402f92) the draw
target again, for the drawn frames listed in --hook-arg (default 0-20,170-200,400-420). Writes <out>.logo.pkl.
"""
import pickle
import struct

from unicorn import UC_HOOK_CODE

MAT, OBJ, CAM, LIGHT, T, TEXT = 0x425190, 0x425488, 0x42550c, 0x425518, 0x4248c8, 0x42553c


def install(mu, st, a):
    want = set()
    for part in (a.hook_arg or '0-20,170-200,400-420').split(','):
        lo, _, hi = part.partition('-')
        want.update(range(int(lo), int(hi or lo) + 1))
    ev = []
    n = {'frame': -1}

    def u32(x): return struct.unpack('<I', mu.mem_read(x, 4))[0]
    def i32(x): return struct.unpack('<i', mu.mem_read(x, 4))[0]
    def mat(): return struct.unpack('<9d', mu.mem_read(MAT, 72))

    def common(kind):
        return dict(kind=kind, refresh=st['frame'], now=st['now'], irqs=st['irqs'], T=i32(T), n=n['frame'],
                    mat=mat(), text=i32(TEXT))

    def tick_out(mu, addr, size, _):
        ev.append(common('tick'))

    def f_entry(mu, addr, size, _):
        n['frame'] += 1
        ev.append(common('f_entry'))

    def f_draw(mu, addr, size, _):
        e = common('f_draw')
        o = u32(OBJ)
        e['obj_addr'] = o
        e['obj'] = bytes(mu.mem_read(o, 0x10c + 130 * 0x40 + 256 * 0x34))
        e['cam'] = bytes(mu.mem_read(u32(CAM), 0xc8))
        e['light'] = bytes(mu.mem_read(u32(LIGHT), 0xd4))
        e['glob'] = bytes(mu.mem_read(0x4b8fc0, 0x68))
        order = []
        f = u32(0x4b8fc4)
        while f and len(order) < 2000:
            order.append(f)
            f = u32(f + 0xc)
        e['order'] = order
        ev.append(e)

    def target(kind):
        def f(mu, addr, size, _):
            if n['frame'] in want:
                e = common(kind)
                e['target'] = bytes(mu.mem_read(u32(0x4b6ec0), 64000))
                ev.append(e)
        return f

    mu.hook_add(UC_HOOK_CODE, tick_out, None, 0x402e40, 0x402e40)
    mu.hook_add(UC_HOOK_CODE, f_entry, None, 0x402f1c, 0x402f1c)
    mu.hook_add(UC_HOOK_CODE, f_draw, None, 0x40c13c, 0x40c13c)
    mu.hook_add(UC_HOOK_CODE, target('t_3d'), None, 0x402f5c, 0x402f5c)
    mu.hook_add(UC_HOOK_CODE, target('t_final'), None, 0x402f92, 0x402f92)

    def finish():
        pickle.dump(ev, open(a.out + '.logo.pkl', 'wb'))
        print('hooks: %d events' % len(ev))
    return finish
