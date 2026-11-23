"""Hooks for models/water/run_inside.py --water-dump FILE: per call of 0x4078d4 (one drawn water frame) the state the
water part reads and writes, pickled into FILE. Full dumps (height fields, back buffer before and after the 3D
render, rasterizer faces) for the calls listed in --water-full."""
import pickle
import struct

from unicorn import UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_ESP

D = {'frames': [], 'ticks': [], 'pre': {}}
_a = None


def install(mu, st, a):
    global _a
    _a = a
    full = {int(x) for x in a.water_full.split(',') if x}

    def rd(x, n): return bytes(mu.mem_read(x, n))
    def r32(x): return struct.unpack('<I', rd(x, 4))[0]
    def i32s(x, n): return struct.unpack('<%di' % n, rd(x, 4 * n))
    def f64(x): return struct.unpack('<d', rd(x, 8))[0]
    def f64s(x, n): return struct.unpack('<%dd' % n, rd(x, 8 * n))
    def reg(r): return mu.reg_read(r)

    def cur():
        return D['frames'][-1] if D['frames'] else D['pre']

    def is_full():
        return D['frames'] and (len(D['frames']) - 1) in full

    def at(addr):
        def deco(fn):
            mu.hook_add(UC_HOOK_CODE, lambda u, ad, sz, ud: fn(), None, addr, addr)
            return fn
        return deco

    @at(0x4078d4)
    def frame_start():
        fish = []
        for i in range(8):
            f = r32(0x426570 + 4 * i)
            vb = r32(f + 0xd0)
            fish.append({'pos': i32s(f + 0x10, 3), 'mat': f64s(f + 0x28, 9),
                         'verts': [i32s(vb + 64 * v, 16) for v in range(8)]})
        D['frames'].append({'refresh': st['frame'], 'now': st['now'], 'c': r32(0x4248c4), 'seed': r32(0x423818),
                            'fish': fish, 'obj_mat': f64s(0x4264d8, 9), 'proj': [], 'a': [], 's': [], 'r': [],
                            'faces': []})

    @at(0x40748c)
    def got_a():
        cur()['a'].append(f64(reg(UC_X86_REG_ESP) + 0x5c))

    @at(0x407619)
    def got_s():
        cur()['s'].append(f64(reg(UC_X86_REG_ESP) + 0x5c))

    @at(0x4077ac)
    def got_r():
        cur()['r'].append(reg(UC_X86_REG_EAX))

    @at(0x4076e8)
    def got_proj():
        sp = reg(UC_X86_REG_ESP)
        cur()['proj'].append((reg(UC_X86_REG_ECX), f64(sp + 0x30), f64(sp + 0x38)))

    @at(0x4079cf)
    def before_ripple():
        c = cur()
        c['seed_drops'] = r32(0x423818)
        c['buf_addr'] = (r32(0x4265e4), r32(0x4265d8))
        if is_full():
            c['cur_in'] = rd(r32(0x4265e4), 128000)
            c['prev_in'] = rd(r32(0x4265d8), 128000)
            c['dst_in'] = rd(r32(0x4248d0), 64000)

    @at(0x4079d4)
    def after_ripple():
        c = cur()
        if is_full():
            c['cur_out'] = rd(r32(0x4265e4), 128000)
            c['dst_bg'] = rd(r32(0x4248d0), 64000)

    @at(0x407dde)
    def after_render():
        c = cur()
        sc = r32(0x4265ec)
        c['scene'] = i32s(sc, 20)
        if is_full():
            c['dst_final'] = rd(r32(0x4248d0), 64000)

    @at(0x407de3)
    def flip():
        c = cur()
        c['flip_refresh'] = st['frame']
        c['flip_now'] = st['now']

    def raster(name):
        def fn():
            if not is_full():
                return
            f = reg(UC_X86_REG_EAX)
            face = i32s(f, 13)
            vs = [i32s(face[k], 16) for k in (5, 6, 7)]
            uv = [struct.unpack('<hh', rd(face[k] + 0x28, 4)) for k in (5, 6, 7)]
            cur()['faces'].append((name, f, face, vs, uv))
        return fn
    mu.hook_add(UC_HOOK_CODE, lambda u, ad, sz, ud: raster('fish')(), None, 0x41669c, 0x41669c)
    mu.hook_add(UC_HOOK_CODE, lambda u, ad, sz, ud: raster('obuke')(), None, 0x412db0, 0x412db0)

    @at(0x407c78)
    def tick():
        D['ticks'].append((st['frame'], st['now'], r32(0x4248c4), f64s(0x4264d8, 9)))


def finish():
    pickle.dump(D, open(_a.water_dump, 'wb'))
    print('water dump: %d frames, %d ticks' % (len(D['frames']), len(D['ticks'])))
