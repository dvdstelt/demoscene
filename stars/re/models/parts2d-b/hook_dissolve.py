"""Hooks for the dissolving picture 0x1535c: event log (PIT callbacks, frames, stamps, zoom offsets as read per
plane row, palette level) and the screen after each conversion (0x7a061), the zoomed planes for the first frames.
Output: <--out>.pkl"""
import struct, pickle
import numpy as np
from unicorn import UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_ECX

ev = []; vga = []; planes = []
OUT = None
st_ = {'plane': -1, 'last': None}


def install(mu, st, a):
    global OUT
    OUT = a.out + '.pkl'
    def u32(x): return struct.unpack('<I', mu.mem_read(x, 4))[0]
    def s32(x): return struct.unpack('<i', mu.mem_read(x, 4))[0]

    def h(mu, addr, size, _):
        if addr == 0x1544d:
            ev.append(('T', st['pit_ticks'], mu.mem_read(0x1c829, 1)[0], mu.mem_read(0x1c828, 1)[0], u32(0x15278),
                       s32(0x4a7a0)))
        elif addr == 0x15384:
            ev.append(('F', st['frame'], st['pit_ticks'], mu.mem_read(0x1c829, 1)[0], mu.mem_read(0x1c828, 1)[0]))
            st_['plane'] = -1
        elif addr == 0x15500:
            ev.append(('S', u32(0x15284), u32(0x15288)))
        elif addr == 0x154ce:
            st_['plane'] += 1; st_['last'] = None
        elif addr == 0x154e2:
            v = (s32(0x1527c), s32(0x15280))
            if v != st_['last']:
                ev.append(('off', st_['plane'], 199 - mu.reg_read(UC_X86_REG_ECX), v[0], v[1]))
                st_['last'] = v
        elif addr == 0x4a7b0:
            ev.append(('P', s32(0x4a7a0), s32(0x4a7a8), u32(0x4a7ac), st['frame']))
        elif addr == 0x7a061:
            ev.append(('C', mu.mem_read(0x79fac, 1)[0], u32(0x14258)))
            if len(planes) < 80:
                planes.append(bytes(mu.mem_read(u32(0x14258), 3 * 64000)))
        elif addr in (0x7a08c, 0x7a09c):
            vga.append(bytes(mu.mem_read(0xa0000, 64000)))
            ev.append(('E', st['frame']))
    for x in (0x1544d, 0x15384, 0x15500, 0x154ce, 0x154e2, 0x4a7b0, 0x7a061, 0x7a08c, 0x7a09c):
        mu.hook_add(UC_HOOK_CODE, h, None, x, x)


def finish():
    pickle.dump({'events': ev, 'vga': np.frombuffer(b''.join(vga), np.uint8).reshape(-1, 64000),
                 'planes': np.frombuffer(b''.join(planes), np.uint8).reshape(-1, 3, 64000)}, open(OUT, 'wb'))
    print('hook: %d events, %d frames -> %s' % (len(ev), len(vga), OUT))
