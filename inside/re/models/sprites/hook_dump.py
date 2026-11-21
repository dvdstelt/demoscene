"""Hooks for models/sprites/run_inside.py (--hook): per frame the sprite part draws, record its state.

At 0x404fbe (the frame routine's call of the sprite blitter 0x415df0, after the scene is rendered) it saves the
draw buffer (background only), the warp phases, the picture index, the camera, light, scene and the objects'
matrices and vertex arrays; at 0x404fc8 (after the sprite) the finished frame; on entry of the rasterizers
0x4172e0 (warp grid), 0x414e5c (textured Gouraud, the box), 0x41669c (shadow) the face drawn (the order is the
depth order) with copies of the face, its three vertices and its material ('draws'). Written to <out>.dump.pkl (a list of dicts) when the run ends; environment DUMP_FROM=<refresh> skips earlier frames.
"""
import atexit, os, pickle, struct
from unicorn import UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EAX


def install(mu, st, a):
    frames = []
    cur = {}

    def d32(x): return struct.unpack('<I', mu.mem_read(x, 4))[0]

    def obj(p, nv=None):
        o = {'raw': bytes(mu.mem_read(p, 0x100))}
        if nv is None:
            nv = d32(p + 0xd4)
        vp = d32(p + 0xd0)
        o['verts'] = bytes(mu.mem_read(vp, 0x40 * nv)) if vp else b''
        o['vbase'] = vp
        fp, nf = d32(p + 0xd8), d32(p + 0xdc)
        o['faces'] = bytes(mu.mem_read(fp, 0x34 * nf)) if fp and nf < 1000 else b''
        o['fbase'] = fp
        return o

    def on_before_sprite(mu, addr, size, _):
        cur['frame'] = st['frame']
        cur['irqs'] = d32(0x4248c4)
        cur['part_ticks'] = d32(0x4248c8)
        cur['pic'] = d32(0x4227fc)
        cur['step'] = d32(0x422800)
        cur['params'] = struct.unpack('<15f', bytes(mu.mem_read(0x422810, 60)))
        cur['bg'] = bytes(mu.mem_read(d32(0x4b6ec0), 64000))
        cur['scene'] = bytes(mu.mem_read(d32(0x425fc4), 0x50))
        cur['camera'] = obj(d32(0x425fc0), 0)
        cur['light'] = obj(d32(0x425fd0), 0)
        cur['warpgrid'] = obj(d32(0x425fd8))
        cur['kuubio'] = obj(d32(0x425fcc))
        cur['shadepoly'] = obj(d32(0x425fc8))
        cur['materials'] = {n: bytes(mu.mem_read(d32(p), 0x44)) for n, p in
                            (('material', 0x425fd4), ('material2', 0x425fdc), ('shadowmap', 0x425fbc))}
        cur['rotstep'] = bytes(mu.mem_read(0x425ed8, 0x48))

    first = int(os.environ.get('DUMP_FROM', '0'))      # dump only frames from this refresh on

    def on_after_sprite(mu, addr, size, _):
        if st['frame'] < first:
            cur.clear()
            return
        cur['final'] = bytes(mu.mem_read(d32(0x4b6ec0), 64000))
        frames.append(dict(cur))
        cur.clear()

    def on_raster(mu, addr, size, _):
        f = mu.reg_read(UC_X86_REG_EAX)
        cur.setdefault('drawn', []).append((addr, f))
        face = bytes(mu.mem_read(f, 0x34))
        vs = [bytes(mu.mem_read(struct.unpack_from('<I', face, o)[0], 0x40)) for o in (0x14, 0x18, 0x1c)]
        mat = bytes(mu.mem_read(struct.unpack_from('<I', face, 0x10)[0], 0x44))
        cur.setdefault('draws', []).append((addr, f, face, vs, mat))

    mu.hook_add(UC_HOOK_CODE, on_before_sprite, None, 0x404fbe, 0x404fbe)
    mu.hook_add(UC_HOOK_CODE, on_after_sprite, None, 0x404fc8, 0x404fc8)
    for r in (0x4172e0, 0x414e5c, 0x41669c):
        mu.hook_add(UC_HOOK_CODE, on_raster, None, r, r)

    def save():
        pickle.dump(frames, open(a.out + '.dump.pkl', 'wb'))
        print('dumped %d frames to %s.dump.pkl' % (len(frames), a.out))
    atexit.register(save)
