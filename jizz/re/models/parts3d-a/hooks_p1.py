"""Hooks for run_hook.py in part 0x17003: per-frame time/event, ring table, camera, 8-bit overlay buffers."""
import pickle, struct
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ESI, UC_X86_REG_EDI
OUT = '/home/dennis/src/dvdstelt/demoscene/jizz/re/models/parts3d-a/p1_hooks.pkl'
WIN = [(1560, 1572), (1940, 1950), (2140, 2152), (2010, 2016)]
rec = {'frames': [], 'rings': [], 'cam': [], 'buf': [], 'rects': []}

def inwin(t): return any(a <= t <= b for a, b in WIN)
def u32(mu, a): return struct.unpack('<I', mu.mem_read(a, 4))[0]

def h_time(mu, st):
    rec['frames'].append((st['tick'], mu.reg_read(UC_X86_REG_EAX), u32(mu, 0x6413), st['flips']))

def h_rings(mu, st):
    rec['rings'].append((st['tick'], st['flips'], bytes(mu.mem_read(u32(mu, 0x18005), 150 * 32))))

def h_cam(mu, st):
    rec['cam'].append((st['tick'], st['flips'], u32(mu, 0x2000), bytes(mu.mem_read(0x159f7, 4)),
                       bytes(mu.mem_read(0x1549c, 32))))

def h_decay(mu, st):
    if inwin(st['tick']):
        b = u32(mu, 0x11207)
        rec['buf'].append(('pre', st['tick'], st['flips'], bytes(mu.mem_read(b, 0x20000)),
                           mu.mem_read(0x17485, 2)))

def h_blend(mu, st):
    if inwin(st['tick']):
        esi = mu.reg_read(UC_X86_REG_ESI); edi = mu.reg_read(UC_X86_REG_EDI)
        rec['buf'].append(('blend', st['tick'], st['flips'], esi, bytes(mu.mem_read(esi, 64000)),
                           bytes(mu.mem_read(edi, 128000))))

def h_after(mu, st):
    if inwin(st['tick']):
        rec['buf'].append(('after', st['tick'], st['flips'], bytes(mu.mem_read(u32(mu, 0x2060), 128000))))

def h_rect(mu, st):
    rec['rects'].append((st['tick'], st['flips'], u32(mu, 0x1821c), u32(mu, 0x18220),
                         bytes(mu.mem_read(u32(mu, 0x181f0), 8 * 32)), bytes(mu.mem_read(0x141ea, 12))))

HOOKS = {0x17162: h_time, 0x18009: h_rings, 0x15c24: h_cam, 0x1739c: h_decay, 0x11396: h_blend,
         0x1740d: h_after, 0x183a9: h_rect}

def finish():
    pickle.dump(rec, open(OUT, 'wb'))
