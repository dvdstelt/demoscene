"""Hooks for run_inside.py (this folder's copy): record the intro's state.

Per tick routine call (0x403214 entry / 0x40337a ret): refresh, part ticks, X, Y, seed before and after.
Per frame routine call (0x403748): at entry, after the drops 0x40374d, after the blur 0x403752, before the flip
0x40376a: refresh, part ticks, count, flag, seed, X, Y, drops; the light buffer and the draw target for the frames
listed in --hook-arg (comma-separated drawn-frame numbers or ranges a-b; default the first 40 and 700..760).
Writes <out>.intro.pkl.
"""
import pickle
import struct

import numpy as np
from unicorn import UC_HOOK_CODE

SEED, X, Y, FLAG, COUNT, DROPS, LIGHT, T = 0x423818, 0x425548, 0x425550, 0x425570, 0x425574, 0x425568, 0x425564, 0x4248c8



def install(mu, st, a):
    want = set()
    for part in (a.hook_arg or '0-40,700-760').split(','):
        lo, _, hi = part.partition('-')
        want.update(range(int(lo), int(hi or lo) + 1))
    ev = []
    n = {'frame': -1}

    def u32(x): return struct.unpack('<I', mu.mem_read(x, 4))[0]
    def i32(x): return struct.unpack('<i', mu.mem_read(x, 4))[0]
    def f64(x): return struct.unpack('<d', mu.mem_read(x, 8))[0]

    def common(kind):
        return dict(kind=kind, refresh=st['frame'], now=st['now'], irqs=st['irqs'], T=i32(T), seed=u32(SEED),
                    X=f64(X), Y=f64(Y), flag=i32(FLAG), count=i32(COUNT), n=n['frame'], in_irq=st['in_irq'])

    def tick_in(mu, addr, size, _):
        ev.append(common('tick_in'))

    def tick_out(mu, addr, size, _):
        ev.append(common('tick_out'))

    def frame_pt(kind, light, target):
        def f(mu, addr, size, _):
            if kind == 'f_entry':
                n['frame'] += 1
            e = common(kind)
            e['drops'] = bytes(mu.mem_read(u32(DROPS), 0x820))
            if n['frame'] in want:
                if light:
                    e['light'] = bytes(mu.mem_read(u32(LIGHT), 128000))
                if target:
                    e['target'] = bytes(mu.mem_read(u32(0x4b6ec0), 64000))
            ev.append(e)
        return f

    mu.hook_add(UC_HOOK_CODE, tick_in, None, 0x403214, 0x403214)
    mu.hook_add(UC_HOOK_CODE, tick_out, None, 0x40337a, 0x40337a)
    mu.hook_add(UC_HOOK_CODE, frame_pt('f_entry', True, True), None, 0x403748, 0x403748)
    mu.hook_add(UC_HOOK_CODE, frame_pt('f_drops', True, False), None, 0x40374d, 0x40374d)
    mu.hook_add(UC_HOOK_CODE, frame_pt('f_blur', True, False), None, 0x403752, 0x403752)
    mu.hook_add(UC_HOOK_CODE, frame_pt('f_shaded', False, True), None, 0x40376a, 0x40376a)
    # the script routine 0x4031a0 (3:00) and the intro's end
    mu.hook_add(UC_HOOK_CODE, lambda mu, ad, sz, _: ev.append(common('ev_4031a0')), None, 0x4031a0, 0x4031a0)
    mu.hook_add(UC_HOOK_CODE, lambda mu, ad, sz, _: ev.append(common('ev_4031a0_end')), None, 0x40320c, 0x40320c)

    def finish():
        pickle.dump(ev, open(a.out + '.intro.pkl', 'wb'))
        print('hooks: %d events' % len(ev))
    return finish
