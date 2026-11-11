# plugin for models/core/run_jizz.py: dumps buffers around the glow-line pipeline of parts 0x17003 / 0x169e7
# (0x183a9 lines, shr 3, 0x11eb2 blur, 0x12111 trail, 0x11396 fade-to-colour) into <out>.dump.pickle
from unicorn import UC_HOOK_CODE
from unicorn.x86_const import *
import pickle
def m32(x): return struct.unpack('<I', mu.mem_read(x, 4))[0]
def ms32(x): return struct.unpack('<i', mu.mem_read(x, 4))[0]
WANT = {130: 3, 300: 5, 510: 5, 520: 2, 225: 2, 230: 2}
got = {}
rec = []       # list of frames: dict of name -> data
cur = {'f': None}
def snapbuf(name, addr, n):
    if cur['f'] is not None: cur['f'][name] = bytes(mu.mem_read(addr, n))
def on(addr):
    ev = ms32(0x6413)
    f = cur['f']
    lb = m32(0x652a)
    if addr in (0x183a9,):
        if got.get(ev, 0) < WANT.get(ev, 0) and f is None:
            got[ev] = got.get(ev, 0) + 1
            cur['f'] = f = {'event': ev, 'tick': st['tick']}
            rec.append(f)
        if f is None: return
        f['rng'] = (m32(0x141ea), m32(0x141ee), m32(0x141f2))
        f['ebp'] = mu.reg_read(UC_X86_REG_EBP); f['q'] = (m32(0x1821c), m32(0x18220))
        f['quads'] = bytes(mu.mem_read(m32(0x181f0), 0x500))
        snapbuf('lines_before', lb, 0x20000)
    elif addr in (0x1735a, 0x16b81):
        snapbuf('lines_after', lb, 0x20000)
    elif addr in (0x1739c, 0x16c51):
        if f is None and got.get(ev, 0) < WANT.get(ev, 0):
            got[ev] = got.get(ev, 0) + 1
            cur['f'] = f = {'event': ev, 'tick': st['tick']}
            rec.append(f)
        snapbuf('pre_shr', lb, 0x20000)
        if f is not None: f['cl'] = mu.mem_read(0x17485, 2)
    elif addr == 0x11eb2:
        if f is not None: f['blur_args'] = (mu.reg_read(UC_X86_REG_EDI), mu.reg_read(UC_X86_REG_EBP))
        snapbuf('pre_blur', lb, 0x20000)
    elif addr == 0x12111:
        if f is not None: f['trail_args'] = (mu.reg_read(UC_X86_REG_ESI), mu.reg_read(UC_X86_REG_EDI), mu.reg_read(UC_X86_REG_ECX) & 0xff)
        snapbuf('pre_trail', lb, 0x20000)
    elif addr == 0x11396:
        snapbuf('pre_fade_buf', lb, 0x20000)
        snapbuf('pre_fade_page', mu.reg_read(UC_X86_REG_EDI), 128000)
        if f is not None:
            f['fade_args'] = (mu.reg_read(UC_X86_REG_ESI), mu.reg_read(UC_X86_REG_EDI))
            f['tables'] = bytes(mu.mem_read(m32(0x1130f), 0x6000))
    elif addr in (0x1740d, 0x16cb0):
        if f is not None:
            f['post_fade_page'] = bytes(mu.mem_read(m32(0x2060), 128000))
            cur['f'] = None
for h in (0x183a9, 0x1735a, 0x16b81, 0x1739c, 0x16c51, 0x11eb2, 0x12111, 0x11396, 0x1740d, 0x16cb0):
    mu.hook_add(UC_HOOK_CODE, lambda mu_, addr, size, _: on(addr), None, h, h)
def atexit_plugin():
    pickle.dump(rec, open(a.out + '.dump.pickle', 'wb'))
    print('dumped', [(r['event'], r['tick'], sorted(r)) for r in rec])
