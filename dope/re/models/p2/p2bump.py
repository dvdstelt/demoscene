"""Check the bump-map addressing of the discs' filler: hook the `mov al, fs:[bx]` reads (0010:26D2 and 0010:27F2)
for a few dozen frames from the start of p2 and histogram the row (bh) and column (bl) read.
usage: python3 p2bump.py --load ../../snaps/part_p2.snap --out out/runs/bump --frames 2760 --start-frame 99999999"""
import os, sys, collections
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import run_dope as R
from unicorn import UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_BX, UC_X86_REG_ECX

L = lambda off: 0x0F74 * 16 + off


class P2(R.Machine):
    def hooks(self):
        super().hooks()
        self.rows = collections.Counter(); self.cols = collections.Counter(); self.n = 0; self.mism = 0
        for off in (0x26d2, 0x27f2):
            self.rm.hook_add(UC_HOOK_CODE, self.h, None, L(off), L(off))

    def h(self, uc, addr, size, _):
        bx = uc.reg_read(UC_X86_REG_BX); ecx = uc.reg_read(UC_X86_REG_ECX)
        self.rows[bx >> 8] += 1; self.cols[bx & 0xff] += 1; self.n += 1
        u, w = ecx & 0xffff, ecx >> 16
        exp = ((((u >> 8) & 0x3f) << 1) | (w >> 15)) << 8 | ((w >> 7) & 0x7f)
        if exp != bx: self.mism += 1


R.Machine = P2
if __name__ == '__main__':
    m = None
    orig = R.Machine.run
    def run(self):
        try:
            orig(self)
        finally:
            print('bump reads', self.n, 'formula mismatches', self.mism)
            print('rows read: even', sum(v for k, v in self.rows.items() if k % 2 == 0), 'odd',
                  sum(v for k, v in self.rows.items() if k % 2), 'range', min(self.rows), max(self.rows))
            print('cols range', min(self.cols), max(self.cols), 'distinct rows', len(self.rows), 'cols', len(self.cols))
    R.Machine.run = run
    R.main()
