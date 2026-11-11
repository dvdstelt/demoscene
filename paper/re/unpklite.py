"""Unpack a PKLITE-compressed DOS executable by running its own decompression stub in unicorn.

Loads the MZ image as DOS would (PSP at load_seg - 0x10), emulates real mode with a minimal int 21h,
and stops when control first returns to the low image area after the stub has moved itself up in memory.
Writes the memory image from the load segment, plus the entry CS:IP / SS:SP relative to the load segment.

usage: python3 unpklite.py PAPER.EXE out_prefix
"""
import struct
import sys

from unicorn import Uc, UC_ARCH_X86, UC_MODE_16, UC_HOOK_INTR, UC_HOOK_CODE, UcError
from unicorn.x86_const import (UC_X86_REG_AX, UC_X86_REG_BX, UC_X86_REG_CX, UC_X86_REG_DX, UC_X86_REG_SI,
                               UC_X86_REG_DI, UC_X86_REG_BP, UC_X86_REG_SP, UC_X86_REG_IP, UC_X86_REG_CS,
                               UC_X86_REG_DS, UC_X86_REG_ES, UC_X86_REG_SS, UC_X86_REG_FLAGS)

LOAD = 0x1000          # load segment (PSP at 0x0ff0)
MEMTOP = 0xA000        # top of conventional memory (segment)

def main(path, out):
    d = open(path, 'rb').read()
    (sig, cblp, cp, crlc, cparhdr, minalloc, maxalloc, ss, sp, csum, ip, cs, lfarlc, ovno) = struct.unpack_from('<14H', d, 0)
    size = (cp - 1) * 512 + cblp if cblp else cp * 512
    image = d[cparhdr * 16:size]
    mu = Uc(UC_ARCH_X86, UC_MODE_16)
    mu.mem_map(0, 0x110000)
    psp = LOAD - 0x10
    # PSP: int 20h at 0, top of memory at 2, empty command tail
    p = bytearray(256)
    p[0:2] = b'\xcd\x20'
    struct.pack_into('<H', p, 2, MEMTOP)
    p[0x80] = 0
    p[0x81] = 0x0d
    mu.mem_write(psp * 16, bytes(p))
    mu.mem_write(LOAD * 16, image)
    for i in range(crlc):
        off, seg = struct.unpack_from('<HH', d, lfarlc + 4 * i)
        a = (LOAD + seg) * 16 + off
        v = struct.unpack('<H', mu.mem_read(a, 2))[0]
        mu.mem_write(a, struct.pack('<H', (v + LOAD) & 0xffff))
    for r, v in ((UC_X86_REG_DS, psp), (UC_X86_REG_ES, psp), (UC_X86_REG_SS, (LOAD + ss) & 0xffff),
                 (UC_X86_REG_SP, sp), (UC_X86_REG_CS, (LOAD + cs) & 0xffff), (UC_X86_REG_IP, ip),
                 (UC_X86_REG_AX, 0), (UC_X86_REG_BX, 0), (UC_X86_REG_CX, 0xff), (UC_X86_REG_DX, psp),
                 (UC_X86_REG_SI, ip), (UC_X86_REG_DI, sp), (UC_X86_REG_BP, 0x91c)):
        mu.reg_write(r, v)
    start_cs = (LOAD + cs) & 0xffff
    state = {'went_high': False, 'oep': None, 'n': 0, 'high_cs': None}

    def on_intr(mu, intno, _):
        ah = mu.reg_read(UC_X86_REG_AX) >> 8
        if intno == 0x21:
            if ah == 0x4a:      # resize memory block: succeed
                f = mu.reg_read(UC_X86_REG_FLAGS) & ~1
                mu.reg_write(UC_X86_REG_FLAGS, f)
            elif ah == 0x30:    # DOS version 5.0
                mu.reg_write(UC_X86_REG_AX, 0x0005)
            elif ah == 0x09:
                a = mu.reg_read(UC_X86_REG_DS) * 16 + mu.reg_read(UC_X86_REG_DX)
                s = bytes(mu.mem_read(a, 200)).split(b'$')[0]
                print('int21/09:', s)
            elif ah == 0x4c:
                print('int21/4c exit', mu.reg_read(UC_X86_REG_AX) & 0xff); mu.emu_stop()
            else:
                print('unhandled int21 ah=%02x' % ah)
        else:
            print('int %02x ah=%02x' % (intno, ah))

    def on_code(mu, addr, size, _):
        state['n'] += 1
        c = mu.reg_read(UC_X86_REG_CS)
        if c != start_cs and not state['went_high'] and c > start_cs:
            state['went_high'] = True
            state['high_cs'] = c
            print('stub moved to CS=%04x after %d instructions' % (c, state['n']))
        if state['went_high'] and c != state['high_cs']:
            state['oep'] = (c, mu.reg_read(UC_X86_REG_IP))
            mu.emu_stop()

    mu.hook_add(UC_HOOK_INTR, on_intr)
    mu.hook_add(UC_HOOK_CODE, on_code)
    try:
        mu.emu_start(start_cs * 16 + ip, 0x110000, count=200_000_000)
    except UcError as e:
        print('emulation error', e, 'at %04x:%04x' % (mu.reg_read(UC_X86_REG_CS), mu.reg_read(UC_X86_REG_IP)))
    print('instructions', state['n'], 'oep', state['oep'] and '%04x:%04x' % state['oep'])
    regs = {n: mu.reg_read(r) for n, r in (('ss', UC_X86_REG_SS), ('sp', UC_X86_REG_SP), ('ds', UC_X86_REG_DS),
                                           ('es', UC_X86_REG_ES), ('ax', UC_X86_REG_AX), ('bx', UC_X86_REG_BX))}
    print('regs', {k: hex(v) for k, v in regs.items()})
    mem = bytes(mu.mem_read(psp * 16, (MEMTOP - psp) * 16))
    open(out + '.mem', 'wb').write(mem)
    with open(out + '.txt', 'w') as f:
        f.write('psp=%04x load=%04x oep=%04x:%04x ss=%04x sp=%04x ds=%04x es=%04x\n' % (
            psp, LOAD, state['oep'][0], state['oep'][1], regs['ss'], regs['sp'], regs['ds'], regs['es']))
    print('wrote', out + '.mem', len(mem))

if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])
