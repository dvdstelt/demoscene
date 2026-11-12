"""Disassembly helper for DOPE's unpacked executables (unpacked/<name>.bin from unrnc.py).

Images: loader, p1 .. p7 (and term). Every image is a real-mode load module relocated for load segment 0, so an
address is SEG:OFF with SEG relative to the load segment, and the file offset is SEG*16 + OFF. Code is 16-bit
(with 386 instructions) except the loader's 32-bit segment.

Views (what an address without a segment means, and the code size):
  p1 .. p7    16-bit, default segment 0010 (all part code is in segment 0010 = file offset 0x100)
  loader      16-bit, default segment 0000: Tran's PMODE 16-bit kernel
  cdpi        16-bit, segment 00EB (file offset 0xeb0): CDPI, the chain loader that runs the parts
  loader32    32-bit, base file offset 0x17e0 (PMODE's code32): addresses are code32 offsets (the 32-bit code's
              near pointers). The music player and the loader's main program live here.

usage (inside the toolbox, capstone needed):
  python3 rep.py p1 func 0bd5            recursive-descent listing (follows jumps, stops at ret/retf/iret/jmp)
  python3 rep.py p1 func 0010:0bd5       the same with an explicit segment
  python3 rep.py p1 dis 0bd5 40          linear listing
  python3 rep.py p1 calls 0bd5           near call sites of a routine (same segment), and far calls to it
  python3 rep.py p1 xref 1234            instructions with this 16-bit immediate/displacement (32-bit for loader32)
  python3 rep.py p1 funcs                near call targets with caller counts
  python3 rep.py p1 far                  far calls / jumps and their targets
  python3 rep.py p1 str [minlen]         printable strings with file offsets
  python3 rep.py p1 ints                 int instructions with the AH/AX value loaded just before
  python3 rep.py p1 ports                in/out instructions with the port (immediate or the DX value loaded before)
In Python: `import rep; v = rep.View('p1')`, then v.rd8/rd16/rd32(linear), v.dis(addr, n), v.func(addr), ...
"""
import os
import re
import struct
import sys

import capstone

HERE = os.path.dirname(os.path.abspath(__file__))
UNP = os.path.join(HERE, 'unpacked')

VIEWS = {
    # name: (file, bits, default segment (16-bit) or base offset (32-bit), code range in the file)
    'loader': ('loader', 16, 0x000, (0x0000, 0x0ea0)),
    'cdpi': ('loader', 16, 0x0eb, (0x0eb0, 0x17e0)),
    'loader32': ('loader', 32, 0x17e0, (0x17e0, 0x63e2)),
}
for _i in range(1, 8):
    VIEWS['p%d' % _i] = ('p%d' % _i, 16, 0x010, None)


class View:
    def __init__(self, name):
        self.name = name
        f, self.bits, self.seg, rng = VIEWS[name]
        self.img = open(os.path.join(UNP, f + '.bin'), 'rb').read()
        self.md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32 if self.bits == 32 else capstone.CS_MODE_16)
        if self.bits == 32:
            self.base = self.seg
            self.code = rng
        else:
            self.base = self.seg * 16
            self.code = rng or (self.base, min(len(self.img), self.base + 0x10000))

    # addresses: (segment, offset) for 16-bit views, offset for the 32-bit view
    def parse(self, s):
        if ':' in s:
            a, b = s.split(':')
            return int(a, 16), int(b, 16)
        v = int(s, 16)
        return (self.seg, v) if self.bits == 16 else v

    def lin(self, a):
        return a[0] * 16 + a[1] if self.bits == 16 else self.base + a

    def fmt(self, seg, off):
        return '%04x:%04x' % (seg, off) if self.bits == 16 else '%05x' % off

    def rd8(self, x): return self.img[x]
    def rd16(self, x): return struct.unpack_from('<H', self.img, x)[0]
    def rd32(self, x): return struct.unpack_from('<I', self.img, x)[0]

    def cstr(self, x, n=60):
        e = x
        while e < len(self.img) and e - x < n and 32 <= self.img[e] < 127:
            e += 1
        return self.img[x:e].decode('latin1')

    def _one(self, seg, off):
        """decode one instruction at seg:off (16-bit) or offset off (32-bit)"""
        x = seg * 16 + off if self.bits == 16 else self.base + off
        return next(self.md.disasm(self.img[x:x + 15], off), None)

    def annotate(self, seg, ins):
        txt = '%s: %-6s %s' % (self.fmt(seg, ins.address), ins.mnemonic, ins.op_str)
        if self.bits == 32:
            for m in re.finditer(r'0x([0-9a-f]{3,5})\b', ins.op_str):
                v = int(m.group(1), 16)
                if self.base + v < len(self.img):
                    s = self.cstr(self.base + v)
                    if len(s) >= 4:
                        txt += '   ; "%s"' % s
        return txt

    def dis(self, a, n=60):
        seg, off = a if self.bits == 16 else (0, a)
        out = []
        while len(out) < n:
            ins = self._one(seg, off)
            if ins is None:
                break
            out.append(self.annotate(seg, ins))
            off += ins.size
        return '\n'.join(out)

    def func(self, a, limit=20000):
        seg, start = a if self.bits == 16 else (0, a)
        seen, todo, out = set(), [start], {}
        while todo and len(out) < limit:
            off = todo.pop()
            while off not in seen and 0 <= off < 0x10000 * (1 if self.bits == 16 else 0x10000):
                seen.add(off)
                ins = self._one(seg, off)
                if ins is None:
                    break
                out[off] = ins
                m = ins.mnemonic
                if m.startswith('j') or m.startswith('loop') or m == 'jcxz':
                    try:
                        todo.append(int(ins.op_str, 16))
                    except ValueError:
                        pass
                    if m == 'jmp':
                        break
                if m in ('ret', 'retf', 'iret', 'iretd', 'ljmp'):
                    break
                off += ins.size
        return '\n'.join(self.annotate(seg, out[o]) for o in sorted(out))

    def near_calls(self):
        """near call targets in the default code segment: target offset -> list of call sites (byte scan for E8)"""
        t = {}
        lo, hi = self.code
        for x in range(lo, hi - 3):
            if self.img[x] != 0xe8:
                continue
            if self.bits == 16:
                dst = (x - self.base + 3 + struct.unpack_from('<h', self.img, x + 1)[0]) & 0xffff
                t.setdefault(dst, []).append(x - self.base)
            else:
                dst = x - self.base + 5 + struct.unpack_from('<i', self.img, x + 1)[0]
                if 0 <= dst < hi - self.base:
                    t.setdefault(dst, []).append(x - self.base)
        return t

    def far_refs(self):
        """far calls (9A) and jumps (EA) with immediate seg:off targets inside the image (byte scan)"""
        out = []
        lo, hi = self.code
        for x in range(lo, hi - 5):
            if self.img[x] in (0x9a, 0xea) and self.bits == 16:
                off, seg = struct.unpack_from('<HH', self.img, x + 1)
                if seg * 16 + off < len(self.img) and seg < 0x8000:
                    out.append((x, 'call' if self.img[x] == 0x9a else 'jmp', seg, off))
        return out

    def xref(self, v):
        pat = struct.pack('<H' if self.bits == 16 else '<I', v)
        lo, hi = self.code
        return [m.start() for m in re.finditer(re.escape(pat), self.img[lo:hi])]

    def strings(self, minlen=5):
        for m in re.finditer(rb'[\x20-\x7e]{%d,}' % minlen, self.img):
            yield m.start(), m.group().decode('latin1')

    def _before(self, x, k=8):
        """instructions that decode to end exactly at file offset x (looking back), most recent last"""
        for back in range(2, 48):
            if x - back < 0:
                break
            off = (x - back - self.base) & (0xffff if self.bits == 16 else 0xffffffff)
            seq = list(self.md.disasm(self.img[x - back:x], off))
            if seq and sum(s.size for s in seq) == back and len(seq) >= k:
                return seq[-k:]
        return []

    def ints(self):
        out = []
        lo, hi = self.code
        for x in range(lo, hi - 1):
            if self.img[x] != 0xcd or self.img[x + 1] in (0x20,) and False:
                continue
            ins = next(self.md.disasm(self.img[x:x + 2], 0), None)
            if ins is None:
                continue
            ctx = ''
            for s in self._before(x, 6):
                if s.mnemonic == 'mov' and re.match(r'(e?ax|ah|al)\b', s.op_str):
                    ctx = s.op_str
            out.append('%s  int %-5s %s' % (self.where(x), ins.op_str, ctx))
        return out

    def ports(self):
        out = []
        lo, hi = self.code
        for x in range(lo, hi - 1):
            b = self.img[x]
            if b in (0xe4, 0xe5, 0xe6, 0xe7):
                out.append('%s  %-3s port %#x' % (self.where(x), 'in' if b < 0xe6 else 'out', self.img[x + 1]))
            elif b in (0xec, 0xed, 0xee, 0xef):
                dx = '?'
                for s in self._before(x, 6):
                    if s.mnemonic == 'mov' and s.op_str.startswith('dx, 0x'):
                        dx = s.op_str[4:]
                    elif s.mnemonic in ('inc', 'dec') and s.op_str == 'dx' and dx != '?':
                        dx = hex(int(dx, 16) + (1 if s.mnemonic == 'inc' else -1))
                out.append('%s  %-3s dx=%s' % (self.where(x), 'in' if b < 0xee else 'out', dx))
        return out

    def where(self, x):
        if self.bits == 16:
            return '%04x:%04x' % (self.seg, x - self.base) if 0 <= x - self.base < 0x10000 else 'file %06x' % x
        return '%05x' % (x - self.base)


def main():
    v = View(sys.argv[1])
    cmd = sys.argv[2]
    args = sys.argv[3:]
    if cmd == 'func':
        print(v.func(v.parse(args[0])))
    elif cmd == 'dis':
        print(v.dis(v.parse(args[0]), int(args[1]) if len(args) > 1 else 60))
    elif cmd == 'calls':
        a = v.parse(args[0])
        off = a[1] if v.bits == 16 else a
        print('near:', ' '.join(v.where(v.base + x) for x in v.near_calls().get(off, [])))
        if v.bits == 16:
            print('far: ', ' '.join(v.where(x) for x, k, s, o in v.far_refs() if (s, o) == a))
    elif cmd == 'xref':
        print(' '.join(v.where(v.code[0] + x) for x in v.xref(int(args[0], 16))))
    elif cmd == 'funcs':
        t = v.near_calls()
        for dst in sorted(t):
            if len(t[dst]) >= 2 or (v.base + dst < len(v.img) and v.img[v.base + dst] in (0x55, 0x60, 0x66, 0x53, 0x56, 0x57, 0x1e, 0x06)):
                print('%s callers=%d' % (v.fmt(v.seg, dst), len(t[dst])))
    elif cmd == 'far':
        for x, k, s, o in v.far_refs():
            print('%s  %s far %04x:%04x' % (v.where(x), k, s, o))
    elif cmd == 'str':
        for x, s in v.strings(int(args[0]) if args else 5):
            print('%06x: %s' % (x, s))
    elif cmd == 'ints':
        print('\n'.join(v.ints()))
    elif cmd == 'ports':
        print('\n'.join(v.ports()))
    else:
        print(__doc__)


if __name__ == '__main__':
    main()
