"""Hooks for the particle parts (0x133ad / 0x133f1): an event log of PIT callbacks and drawn frames, with the
variables at the instructions that read them, and the new frame (dst buffer) after each zoom step.
Output: <--out>.pkl: events (list of tuples), frames (N x 64000 uint8)."""
import os, struct, pickle
import numpy as np
from unicorn import UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EAX

ev = []
frames = []
srcs = []
OUT = None


def install(mu, st, a):
    global OUT
    OUT = a.out + '.pkl'
    def u32(x): return struct.unpack('<I', mu.mem_read(x, 4))[0]
    def s32(x): return struct.unpack('<i', mu.mem_read(x, 4))[0]

    def h(mu, addr, size, _):
        if addr == 0x1353d:      # PIT callback body (both variants): tick
            ev.append(('T', st['pit_ticks'], mu.mem_read(0x1c829, 1)[0], mu.mem_read(0x1c828, 1)[0],
                       u32(0x13044), s32(0x13170), u32(0x13539)))
        elif addr == 0x135c5:    # frame: start of the draw step
            ev.append(('F', st['frame'], st['pit_ticks'], u32(0x4af35), s32(0x4a7a0), s32(0x4a7a8), u32(0x13038)))
        elif addr == 0x13676:
            ev.append(('t170', s32(0x13170)))
        elif addr == 0x13683:
            ev.append(('t539', u32(0x13539), s32(0x13170)))
        elif addr == 0x136e5:
            ev.append(('idx', u32(0x13174)))
        elif addr in (0x13783, 0x137c8, 0x1380d, 0x13854):     # the four reads of the tick counter (variant 2)
            ev.append(('n', u32(0x13044), (0x13783, 0x137c8, 0x1380d, 0x13854).index(addr)))
        elif addr == 0x135b4:    # zoom done: dst = [0x1303c]
            frames.append(bytes(mu.mem_read(u32(0x1303c), 64000)))
            ev.append(('E', st['frame'], u32(0x4af35)))
        elif addr == 0x13579 and len(srcs) < 40:   # before the zoom: the source with this frame's drawing
            srcs.append(bytes(mu.mem_read(u32(0x13038), 64000)))
        elif addr == 0x4a7b0:
            ev.append(('P', s32(0x4a7a0), s32(0x4a7a8), u32(0x4a7ac), st['frame']))
    for x in (0x13579, 0x1353d, 0x135c5, 0x13676, 0x13683, 0x136e5, 0x13783, 0x137c8, 0x1380d, 0x13854, 0x135b4, 0x4a7b0):
        mu.hook_add(UC_HOOK_CODE, h, None, x, x)


def finish():
    pickle.dump({'events': ev, 'frames': np.frombuffer(b''.join(frames), np.uint8).reshape(-1, 64000),
                 'srcs': np.frombuffer(b''.join(srcs), np.uint8).reshape(-1, 64000)},
                open(OUT, 'wb'))
    print('hook: %d events, %d frames -> %s' % (len(ev), len(frames), OUT))
