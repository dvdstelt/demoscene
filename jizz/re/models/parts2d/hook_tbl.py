"""Hooks for run_hook.py: record the TBL part (0x12013) frame by frame.
Per frame: timer reads (with return address), the work buffer before >>3 (0x11dc6), the whole 0x20000-byte
buffer before the LUT copy (0x120b5), the LUT, light position, radius, fade state, colour."""
import pickle
import struct

from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ESP

OUT = None
frames = []
cur = {'timers': []}


def install(mu, st, a):
    global OUT
    OUT = a.out + '.tbl.pkl'
    from unicorn import UC_HOOK_CODE

    def rd(addr, n): return bytes(mu.mem_read(addr, n))
    def u32(addr): return struct.unpack('<I', rd(addr, 4))[0]

    def on_timer(mu, addr, size, _):
        ret = u32(mu.reg_read(UC_X86_REG_ESP) + 4)   # after 'pop ebp'? see below
        cur['timers'].append((ret, mu.reg_read(UC_X86_REG_EAX), st['tick']))

    def on_start(mu, addr, size, _):
        buf = u32(0x652a)
        cur['start_tick'] = st['tick']
        cur['angles0'] = rd(0x115c2, 0x140)
        cur['hist0'] = rd(buf + 0x10000, 0x10000)
        cur['ev0'] = u32(0x6413)

    def on_preshr(mu, addr, size, _):
        buf = u32(0x652a)
        cur['preshr'] = rd(buf, 64000)
        cur['light'] = struct.unpack('<ii', rd(0x1157a, 8))
        cur['R'] = u32(0x115ba)
        cur['corners'] = struct.unpack('<4f', rd(0x1159e, 16))
        cur['rays'] = rd(0x11702, 0x320)

    def on_bump(mu, addr, size, _):
        buf = u32(0x652a)
        cur['bump'] = rd(buf, 64000)

    def on_lut(mu, addr, size, _):
        buf = u32(0x652a)
        cur['buf'] = rd(buf, 0x20000)
        cur['lut'] = rd(0x11eef, 128)
        cur['fade'] = struct.unpack('<i', rd(0x1210d, 4))[0]
        cur['pulse'] = rd(0x12012, 1)[0]
        cur['col'] = rd(0x11f6f, 4)
        cur['ev'] = u32(0x6413)
        cur['tick'] = st['tick']
        cur['angles1'] = rd(0x115c2, 0x140)
        frames.append(dict(cur))
        cur.clear()
        cur['timers'] = []

    mu.hook_add(UC_HOOK_CODE, on_timer, None, 0x29f61, 0x29f61)
    mu.hook_add(UC_HOOK_CODE, on_start, None, 0x11986, 0x11986)
    mu.hook_add(UC_HOOK_CODE, on_bump, None, 0x11acf, 0x11acf)
    mu.hook_add(UC_HOOK_CODE, on_preshr, None, 0x11dc6, 0x11dc6)
    mu.hook_add(UC_HOOK_CODE, on_lut, None, 0x120b5, 0x120b5)


def finish():
    pickle.dump(frames, open(OUT, 'wb'))
    print('hook_tbl: %d frames -> %s' % (len(frames), OUT))
