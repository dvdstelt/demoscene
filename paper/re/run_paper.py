"""Run Paper's 32-bit code headless in unicorn and record what it does.

No DOS and no PMODE kernel: the 32-bit segment is loaded at linear 0 with flat selectors (so PMODE's
_code32a = 0 and the intro reaches VGA memory at 0xa0000 directly). PMODE's variables are pre-set, the
IRQ-vector service is intercepted, int 0x33 (real-mode interrupt) is emulated for the few calls the intro
makes, and VGA (palette DAC, retrace), PIT, PIC, keyboard and a Gravis Ultrasound at port 0x240 are faked.
The 70 Hz timer interrupt is delivered every `--budget` instructions of main-line code.

Outputs (prefix given with --out):
  <out>.frames  raw: per tick, 64000 bytes of VGA memory + 768 bytes of 6-bit palette
  <out>.gus     text log of GUS register / port writes:  tick port value  (and DRAM uploads as ranges)
  <out>.dram    final GUS DRAM contents (1 MB)
  <out>.ticks   per tick: music counter [0x1ea8b] and current effect pointer [0x1e5ef]

usage: python3 run_paper.py --out run1 [--ticks 25000] [--budget 1500000]
"""
import argparse
import pickle
import struct
import sys

from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_INTR, UC_HOOK_CODE, UC_HOOK_INSN, UcError
from unicorn.x86_const import *

import rep

SEG = rep.SEG
PSP = 0x80000          # fake PSP + environment (below VGA)
ENV = 0x81000
HEAP = 0x200000
HEAPTOP = 0x4000000    # 64 MB
STACK = 0x1f0000
SENTINEL = 0x7ffff0    # return address for IRQ delivery
GDT = 0x7f0000
SETIRQ = struct.unpack_from('<I', SEG, 0x2a)[0]


class Gus:
    def __init__(self, log):
        self.dram = bytearray(1 << 20)
        self.voice = 0
        self.reg = 0
        self.addr = 0
        self.regs = {}
        self.log = log
        self.tick = 0
        self.lo = 0

    def out(self, port, size, val):
        p = port - 0x240
        if p == 0x102:                      # voice select
            self.voice = val & 31
        elif p == 0x103:                    # register select
            self.reg = val & 0xff
            if size == 2:                   # word out to 0x343: low byte reg, high byte data
                self.reg = val & 0xff
                self._write(self.reg, (val >> 8) & 0xff, 1)
        elif p == 0x104:                    # data low (word write = 16-bit register)
            if size == 2:
                self._write(self.reg, val & 0xffff, 2)
            else:
                self.lo = val & 0xff
        elif p == 0x105:                    # data high byte
            self._write(self.reg, val & 0xff if size == 1 else val, 1 if size == 1 else 2)
        elif p == 0x107:                    # DRAM data
            self.dram[self.addr & 0xfffff] = val & 0xff
        else:
            self.log.write('%d p%03x %x\n' % (self.tick, port, val))

    def _write(self, reg, val, size):
        if reg == 0x43:
            self.addr = (self.addr & 0xff0000) | (val & 0xffff)
        elif reg == 0x44:
            self.addr = (self.addr & 0xffff) | ((val & 0xff) << 16)
        else:
            self.regs[(self.voice, reg)] = val
            self.log.write('%d v%d r%02x %x\n' % (self.tick, self.voice, reg, val))

    def inp(self, port, size):
        p = port - 0x240
        if p == 0x107:
            return self.dram[self.addr & 0xfffff]
        if p in (0x105, 0x104):
            r = self.reg
            if r >= 0x80:
                v = self.regs.get((self.voice, r - 0x80), 0)
                if r == 0x80: v |= 1        # voice stopped
                if r == 0x8d: v |= 1        # volume ramp stopped
                return (v >> 8) & 0xff if (p == 0x105 and size == 1 and r in (0x81, 0x82, 0x83, 0x84, 0x85, 0x89, 0x8a, 0x8b)) else v & (0xff if size == 1 else 0xffff)
            return 0
        if p == 0x006:                      # IRQ status
            return 0
        if p == 0x103:
            return self.reg
        return 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', required=True)
    ap.add_argument('--ticks', type=int, default=25000)
    ap.add_argument('--budget', type=int, default=1500000)
    ap.add_argument('--start-tick', type=int, default=0, help='only record frames from this tick on')
    ap.add_argument('--snap-at', default='', help='save snapshots (memory, CPU, harness state) at these ticks '
                    '(comma-separated); --snap may contain {tick}')
    ap.add_argument('--snap', help='snapshot file to write (with --snap-at) or to resume from (with --load). '
                    'A snapshot is a pickle: mem (64 MB from linear 0, code32 = linear 0), ctx, pal (6-bit DAC), '
                    'irq, st, gus, eip, tick')
    ap.add_argument('--load', action='store_true', help='resume from --snap; frames/ticks then start at its tick')
    ap.add_argument('--plane-budget', type=int, help='instructions per tick during the paper-plane part (0x388b), '
                    'whose flock moves once per drawn frame: 750000 gives ~34 fps, the speed it is remembered at')
    ap.add_argument('--trace', default='', help='comma-separated code32 addresses: print the tick the first time each runs')
    ap.add_argument('--whole-frames', action='store_true', help='record the last completely drawn frame per tick '
                    '(no tearing) instead of whatever VGA memory holds when the tick ends')
    a = ap.parse_args()

    mu = Uc(UC_ARCH_X86, UC_MODE_32)
    mu.mem_map(0, HEAPTOP)
    seg = bytearray(SEG)
    # PMODE variables: lomembase = lomemtop (no low memory), himem = heap, pspa, code16a, code32a = 0
    struct.pack_into('<IIIIIII', seg, 0, 0x90000, 0x90000, HEAP, HEAPTOP - 0x100000, PSP, 0, 0)
    mu.mem_write(0, bytes(seg))
    # PSP with environment segment at 0x2c and ULTRASND
    psp = bytearray(256)
    struct.pack_into('<H', psp, 0x2c, ENV >> 4)
    mu.mem_write(PSP, bytes(psp))
    mu.mem_write(ENV, b'ULTRASND=240,3,3,7,7\x00\x00')
    # GDT: 0x08 code, 0x10 data, 0x18 data, all base 0 limit 4 GB
    def desc(access, flags=0xcf):
        return struct.pack('<HHBBBB', 0xffff, 0, 0, access, flags, 0)
    mu.mem_write(GDT, b'\x00' * 8 + desc(0x9a) + desc(0x92) + desc(0x92))
    mu.reg_write(UC_X86_REG_GDTR, (0, GDT, 4 * 8 - 1, 0))
    mu.reg_write(UC_X86_REG_CS, 0x08)
    for r in (UC_X86_REG_DS, UC_X86_REG_ES, UC_X86_REG_SS, UC_X86_REG_FS, UC_X86_REG_GS):
        mu.reg_write(r, 0x10)
    mu.reg_write(UC_X86_REG_ESP, STACK)

    glog = open(a.out + '.gus', 'w')
    gus = Gus(glog)
    irq = {}
    pal = bytearray(768)
    st = {'dac_w': 0, 'retrace': 0, 'stop': False, 'tick': 0, 'frames': 0}

    def on_setirq(mu, addr, size, _):
        bl = mu.reg_read(UC_X86_REG_EBX) & 0xff
        irq[bl] = mu.reg_read(UC_X86_REG_EDX)
        print('setirqvect irq %d -> %x' % (bl, irq[bl]))
        esp = mu.reg_read(UC_X86_REG_ESP)
        ret = struct.unpack('<I', mu.mem_read(esp, 4))[0]
        mu.reg_write(UC_X86_REG_ESP, esp + 4)
        mu.reg_write(UC_X86_REG_EIP, ret)

    def on_in(mu, port, size, _):
        if port == 0x3da:
            st['retrace'] ^= 0x09
            return st['retrace']
        if port == 0x3c9:
            return 0
        if port == 0x60:
            return 0
        if 0x240 <= port < 0x350:
            return gus.inp(port, size)
        return 0

    outs = {}
    def on_out(mu, port, size, val, _):
        outs[port] = outs.get(port, 0) + 1
        if port == 0x3c8:
            st['dac_w'] = (val & 0xff) * 3
        elif port == 0x3c9:
            pal[st['dac_w'] % 768] = val & 0x3f
            st['dac_w'] += 1
        elif 0x240 <= port < 0x350:
            gus.out(port, size, val)

    def on_intr(mu, intno, _):
        if intno == 0x33:
            al = mu.reg_read(UC_X86_REG_EAX) & 0xff
            if al == 0x16:                     # keyboard read: space (no sound selection, unused)
                mu.mem_write(0x9a, struct.pack('<H', 0x3920))
            elif al == 0x21:
                ah = mu.mem_read(0x9b, 1)[0]
                if ah == 0x4c:
                    st['stop'] = True
                    mu.emu_stop()
            # al == 0x10: video mode set, nothing to do
        else:
            print('int %x at %x' % (intno, mu.reg_read(UC_X86_REG_EIP)))
            st['stop'] = True
            mu.emu_stop()

    def on_planes_enter(mu, addr, size, _):
        st['plane_first'] = True
        st['in_planes'] = True

    def on_planes_leave(mu, addr, size, _):
        st['in_planes'] = False

    def on_planes(mu, addr, size, _):
        # Bug in the original: the paper-plane part (0x388b) loops while the palette level [0x1edb5] is non-zero,
        # but that level only rises in the 70 Hz IRQ. If the setup and first frame finish before the next tick, the
        # level is still 0 at the first test (0x397e) and the part is skipped. Give it the one fade step a slower PC
        # would have had.
        if not st.get('plane_first'):
            return
        st['plane_first'] = False
        lvl = struct.unpack('<i', mu.mem_read(0x1edb5, 4))[0]
        if lvl == 0:
            mu.mem_write(0x1edb5, struct.pack('<i', max(1, struct.unpack('<i', mu.mem_read(0x1edb9, 4))[0])))
            print('plane part: palette level still 0 at the first loop test (the original would skip the part); '
                  'set to one fade step')
        else:
            print('plane part: palette level %d at the first loop test, plays without help' % lvl)

    def on_frame(mu, addr, size, _):
        # 0x7a64 (key check) is called once per frame by every part loop, after the previous frame was copied to
        # VGA and before the next one is drawn: grab the finished frame here so the capture never shows a half copy.
        st['frames'] += 1
        st['vga'] = bytes(mu.mem_read(0xa0000, 64000))

    traced = set()
    def on_trace(mu, addr, size, _):
        if addr not in traced:
            traced.add(addr)
            print('trace %x tick %d counter %d' % (addr, st['tick'], struct.unpack('<i', mu.mem_read(0x1ea8b, 4))[0]))
    for x in filter(None, a.trace.split(',')):
        mu.hook_add(UC_HOOK_CODE, on_trace, None, int(x, 16), int(x, 16))

    mu.hook_add(UC_HOOK_CODE, on_setirq, None, SETIRQ, SETIRQ)
    mu.hook_add(UC_HOOK_CODE, on_frame, None, 0x7a64, 0x7a64)
    mu.hook_add(UC_HOOK_CODE, on_planes_enter, None, 0x388b, 0x388b)
    mu.hook_add(UC_HOOK_CODE, on_planes, None, 0x397e, 0x397e)
    mu.hook_add(UC_HOOK_CODE, on_planes_leave, None, 0x3987, 0x3987)
    mu.hook_add(UC_HOOK_INSN, on_in, None, 1, 0, UC_X86_INS_IN)
    mu.hook_add(UC_HOOK_INSN, on_out, None, 1, 0, UC_X86_INS_OUT)
    mu.hook_add(UC_HOOK_INTR, on_intr)

    frames = open(a.out + '.frames', 'wb')
    ticks = open(a.out + '.ticks', 'w')
    eip = 0x7626
    t0 = 0
    snap_ticks = {int(x) for x in a.snap_at.split(',') if x}
    if a.load:
        snap = pickle.load(open(a.snap, 'rb'))
        mu.mem_write(0, snap['mem'])
        mu.context_restore(snap['ctx'])
        pal[:] = snap['pal']
        irq.update(snap['irq'])
        st.update(snap['st'])
        gus.__dict__.update({k: v for k, v in snap['gus'].items()})
        gus.log = glog
        eip, t0 = snap['eip'], snap['tick']
        print('resumed at tick', t0)
    for t in range(t0, a.ticks):
        st['tick'] = t
        gus.tick = t
        if t in snap_ticks:
            g = {k: v for k, v in gus.__dict__.items() if k != 'log'}
            pickle.dump({'mem': bytes(mu.mem_read(0, HEAPTOP)), 'ctx': mu.context_save(), 'pal': bytes(pal),
                         'irq': dict(irq), 'st': dict(st), 'gus': g, 'eip': eip, 'tick': t},
                        open(a.snap.format(tick=t), 'wb'))
            print('snapshot at tick', t)
        try:
            budget = a.plane_budget if a.plane_budget and st.get('in_planes') else a.budget
            mu.emu_start(eip, 0xffffffff, count=budget)
        except UcError as e:
            print('error in main at %x: %s' % (mu.reg_read(UC_X86_REG_EIP), e))
            break
        if st['stop']:
            print('program exit at tick', t)
            break
        eip = mu.reg_read(UC_X86_REG_EIP)
        if 0 in irq:                           # deliver IRQ0
            ctx = mu.context_save()
            esp = mu.reg_read(UC_X86_REG_ESP) - 12
            mu.mem_write(esp, struct.pack('<III', SENTINEL, 0x08, 0x202))
            mu.reg_write(UC_X86_REG_ESP, esp)
            try:
                mu.emu_start(irq[0], SENTINEL, count=200_000_000)
            except UcError as e:
                print('error in irq at %x: %s' % (mu.reg_read(UC_X86_REG_EIP), e))
                break
            mu.context_restore(ctx)
        cnt = struct.unpack('<i', mu.mem_read(0x1ea8b, 4))[0]
        eff = struct.unpack('<I', mu.mem_read(0x1e5ef, 4))[0]
        ticks.write('%d %d %x %x %d\n' % (t, cnt, eff, eip, st['frames']))
        st['frames'] = 0
        if t >= a.start_tick:
            frames.write(st['vga'] if a.whole_frames and st.get('vga') else bytes(mu.mem_read(0xa0000, 64000)))
            frames.write(bytes(pal))
        if t % 700 == 0:
            print('tick %d counter %d effect %x eip %x' % (t, cnt, eff, eip), flush=True)
    open(a.out + '.dram', 'wb').write(bytes(gus.dram))
    glog.close()
    print('out ports', {hex(k): v for k, v in sorted(outs.items())})
    print('done')


if __name__ == '__main__':
    main()
