"""Hooks for the face part 0x1089c (PIT callback 0x10ae2): event log (callbacks, frames with the parameter block,
remap / face flags, the warp's column and row tables as built) and the new frame after each warp.
Output: <--out>.pkl"""
import struct, pickle
import numpy as np
from unicorn import UC_HOOK_CODE

ev = []; frames = []; srcs = []
OUT = None


def install(mu, st, a):
    global OUT
    OUT = a.out + '.pkl'
    def u32(x): return struct.unpack('<I', mu.mem_read(x, 4))[0]
    def s32(x): return struct.unpack('<i', mu.mem_read(x, 4))[0]
    def u16(x): return struct.unpack('<H', mu.mem_read(x, 2))[0]
    patch = []      # filled at the first warp (install() runs before the snapshot is loaded)

    def h(mu, addr, size, _):
        if addr == 0x10ae2:
            ev.append(('T', st['pit_ticks'], mu.mem_read(0x1c829, 1)[0], mu.mem_read(0x1c828, 1)[0], u32(0x107ca),
                       u32(0x107ce), [u32(0xf770 + 4 * i) for i in range(4)], s32(0xf764), u32(0x11a8d),
                       u32(0x11aa1), s32(0x4a7a0)))
        elif addr == 0x10a86:
            ev.append(('F', st['frame'], st['pit_ticks'], mu.mem_read(0x1c829, 1)[0], mu.mem_read(0x1c828, 1)[0],
                       [s32(0xf740 + 4 * i) for i in range(16)], u32(0x107ca), u16(0x10a7e), u16(0x10a80),
                       u32(0x10a82)))
        elif addr == 0x1178c:
            ev.append(('R', u32(0xf768), s32(0xf76c), u32(0x107c6)))
        elif addr == 0x10bf3:
            ev.append(('W0', [u32(0xf770 + 4 * i) for i in range(4)], s32(0xf764)))
            if len(srcs) < 60:
                srcs.append(bytes(mu.mem_read(u32(0xffa4), 64000)))
        elif addr == 0x10c6a:      # the row loop reads its phases here
            ev.append(('W1', [u32(0xf778), u32(0xf77c)], s32(0xf764)))
        elif addr == 0x10cdf:
            if not patch: patch.extend(u32(0xfaa0 + 4 * x) for x in range(320))
            cols = [struct.unpack('<i', mu.mem_read(p, 4))[0] for p in patch]
            rows = [s32(0xf780 + 4 * y) for y in range(200)]
            ev.append(('W', cols, rows, u32(0xffa4), [u32(0xf770 + 4 * i) for i in range(4)], s32(0xf764)))
        elif addr == 0x10ad1:
            frames.append(bytes(mu.mem_read(u32(0xffa8), 64000)))
            ev.append(('E', st['frame'], mu.mem_read(0x1c829, 1)[0], mu.mem_read(0x1c828, 1)[0], u32(0x107ca)))
        elif addr == 0x4a7b0:
            ev.append(('P', s32(0x4a7a0), s32(0x4a7a8), u32(0x4a7ac), st['frame']))
    for x in (0x10ae2, 0x10a86, 0x1178c, 0x10bf3, 0x10c6a, 0x10cdf, 0x10ad1, 0x4a7b0):
        mu.hook_add(UC_HOOK_CODE, h, None, x, x)


def finish():
    pickle.dump({'events': ev, 'frames': np.frombuffer(b''.join(frames), np.uint8).reshape(-1, 64000),
                 'srcs': np.frombuffer(b''.join(srcs), np.uint8).reshape(-1, 64000)}, open(OUT, 'wb'))
    print('hook: %d events, %d frames -> %s' % (len(ev), len(frames), OUT))
