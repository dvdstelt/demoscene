# plugin for models/core/run_jizz.py: logs calls of the core helpers (call site, registers, key variables),
# reads of the colour tables by EIP, and RNG calls by call site. Output: <out>.calls.txt
from unicorn import UC_HOOK_CODE, UC_HOOK_MEM_READ
from unicorn.x86_const import *
import collections
R = {'eax': UC_X86_REG_EAX, 'ebx': UC_X86_REG_EBX, 'ecx': UC_X86_REG_ECX, 'edx': UC_X86_REG_EDX,
     'esi': UC_X86_REG_ESI, 'edi': UC_X86_REG_EDI, 'ebp': UC_X86_REG_EBP, 'esp': UC_X86_REG_ESP}
def g(n): return mu.reg_read(R[n])
def m32(x): return struct.unpack('<I', mu.mem_read(x, 4))[0]
def ms32(x): return struct.unpack('<i', mu.mem_read(x, 4))[0]
def m8(x): return mu.mem_read(x, 1)[0]
HELP = {0x17737: '', 0x17727: '', 0x13d73: 'edi', 0x13eec: 'edi', 0x1131b: 'c', 0x11396: 'esi edi', 0x1145c: 'line',
        0x1151c: 'frame', 0x11566: 'eax edi', 0x11eb2: 'edi ebp', 0x12111: 'esi edi ecx', 0x11f73: 'f73',
        0x183a9: 'ebp q', 0x17979: 'eax t', 0x17999: 'fade', 0x179f6: 'eax', 0x1a229: 'eax flash', 0x1a336: 'eax flash',
        0x1a236: 'flash', 0x1f5c5: 'eax flash'}
log = open(a.out + '.calls.txt', 'w')
seen = collections.Counter()
def on_help(mu_, addr, size, _):
    site = m32(g('esp')) - 5
    key = (addr, site)
    seen[key] += 1
    n = seen[key]
    if n > 3 and n % 50: return
    f = HELP[addr]
    parts = []
    for k in f.split():
        if k in R: parts.append(f'{k}={g(k):#x}')
        elif k == 'c': parts.append('C=%d,%d,%d' % (m32(0x11303), m32(0x11307), m32(0x1130b)))
        elif k == 'line': parts.append('(%d,%d)-(%d,%d) buf=%#x' % (ms32(0x1121b), ms32(0x1121f), ms32(0x11223), ms32(0x11227), m32(0x11207)))
        elif k == 'frame': parts.append('off=%#x w=%d h=%d' % (m32(0x15c3b), m32(0x18d64), m32(0x18d68)))
        elif k == 'f73': parts.append('base=%s lvl=%d' % (bytes(mu.mem_read(0x11f6f, 3)).hex(), struct.unpack('b', mu.mem_read(0x11f72, 1))[0]))
        elif k == 'q': parts.append('quads %d..%d' % (m32(0x1821c), m32(0x18220)))
        elif k == 't': parts.append('timer+6542=%d' % ((m32(0x6542) + 0) & 0xffffffff))
        elif k == 'fade': parts.append('lvl=%d start=%d rate=%#x' % (ms32(0x179da), ms32(0x179d6), m32(0x179de)))
        elif k == 'flash': parts.append('F=%d dec=%d S=%#x' % (m8(0x1a478), m8(0x6386), m32(0x1a225)))
    log.write(f'tick {st["tick"]} {addr:#x} from {site:#x} #{n} ' + ' '.join(parts) + '\n')
for h in HELP:
    mu.hook_add(UC_HOOK_CODE, on_help, None, h, h)
rng = collections.Counter()
def on_rng(mu_, addr, size, _):
    rng[(m32(g('esp')) - 5, st['tick'] // 100)] += 1
mu.hook_add(UC_HOOK_CODE, on_rng, None, 0x141f6, 0x141f6)
reads = collections.Counter()
def on_read(mu_, access, addr, size, value, _):
    reads[(mu.reg_read(UC_X86_REG_EIP), (addr - 0x1a479) // 0xc00 if addr < 0x1c879 else 3 + (addr - 0x1c879) // 0x800)] += 1
mu.hook_add(UC_HOOK_MEM_READ, on_read, None, 0x1a479, 0x1e078)
def atexit_plugin():
    log.write('RNG calls (site, tick//100): %s\n' % sorted(rng.items()))
    log.write('colour table reads (eip, table 0..2 = 768-entry R,G,B, 3..5 = 512-entry R,G,B):\n')
    for (e, t), n in sorted(reads.items()): log.write(f'  {e:#x} table {t}: {n}\n')
    log.write('rng state %s\n' % [hex(m32(x)) for x in (0x141ea, 0x141ee, 0x141f2)])
    log.close()
