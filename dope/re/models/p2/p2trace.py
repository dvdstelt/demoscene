"""Run p2 from snaps/part_p2.snap with hooks that log p2's state (uses the copy run_dope.py in this folder).

usage: python3 models/p2/p2trace.py --load snaps/part_p2.snap --out models/p2/out/runs/p2 --stop-part p3 \
           --start-frame 99999999
Writes <out>.p2log:
  S <step> <cx> <retrace> <frame>            script step start (0010:3732: mov [371c], cx)
  R <retrace> <frame> <state...>             per-retrace routine 2580 entry (state before this retrace's update)
  F <retrace> <frame> <drawn> <vars...>      frame start (354a)
  C <retrace> <frame> <drawn>                after the blend and the label (36e3), just before the copy to A000
and <out>_dump_<drawn>.npz at the first drawn frame at or after each retrace in DUMP_AT: new (4527, before the
blend), old (5517, before the blend), table (3527), out (5517 after blend + label), vars.
retrace = number of 2580 calls since p2 started (the first call is retrace 1); frame = capture frame number.
"""
import os, struct, sys
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import run_dope as R
from unicorn import UC_HOOK_CODE

CS = 0x0F74                       # segment 0010 of p2's image at run time
def L(off): return CS * 16 + off
def SEG(s): return (s + 0x0F64) * 16

DUMP_AT = [300, 500, 1000, 2700, 2840, 3200, 3600, 5300, 6200, 7280]


class P2(R.Machine):
    def hooks(self):
        super().hooks()
        self.p2 = {'ret': 0, 'drawn': 0, 'dumps': list(DUMP_AT), 'cur': None, 'step': 0}
        self.p2log = open(self.a.out + '.p2log', 'w', buffering=1)
        for off, fn in ((0x2580, self.h_retrace), (0x3732, self.h_step), (0x354a, self.h_frame),
                        (0x3668, self.h_blend), (0x36e3, self.h_copy)):
            self.rm.hook_add(UC_HOOK_CODE, fn, None, L(off), L(off))

    def m(self, off, fmt):
        return struct.unpack(fmt, bytes(self.rm.mem_read(L(off), struct.calcsize(fmt))))

    def cam(self):
        x, y, z, vx, vy, vz = self.m(0x1816, '<6i')
        a, b, c, va, vb, vc = self.m(0x182e, '<3H3h')
        return '%d %d %d %d %d %d %d %d %d %d %d %d' % (x, y, z, vx, vy, vz, a, b, c, va, vb, vc)

    def h_retrace(self, uc, addr, size, _):
        self.p2['ret'] += 1
        piv, = self.m(0x1878, '<i')
        lp, ld = self.m(0x1955, '<hh')
        fl, = self.m(0x193f, '<H'); fv, = self.m(0x1941, '<H')
        self.p2log.write('R %d %d %s %d %d %d %d %d\n' % (self.p2['ret'], self.st['frame'], self.cam(), piv, lp, ld,
                                                         fl, fv))

    def h_step(self, uc, addr, size, _):
        cx = self.rm.reg_read(R.UC_X86_REG_CX)
        self.p2log.write('S %d %d %d %d\n' % (self.p2['step'], cx, self.p2['ret'], self.st['frame']))
        self.p2['step'] += 1

    def vars(self):
        s, = self.m(0x1965, '<H'); strip, = self.m(0x1904, '<h'); row, = self.m(0x3626, '<H')
        bd, = self.m(0x1947, '<H'); ts, = self.m(0x194f, '<H'); hid, = self.m(0x1886, '<h'); lp, = self.m(0x1955, '<h')
        return dict(s=s, strip=strip, row=row, backdrop=bd, texsel=ts, Chidden=hid, label=lp)

    def h_frame(self, uc, addr, size, _):
        self.p2['drawn'] += 1
        v = self.vars()
        self.p2log.write('F %d %d %d %s %s\n' % (self.p2['ret'], self.st['frame'], self.p2['drawn'],
                                                ' '.join('%s=%d' % kv for kv in v.items()), self.cam()))
        d = self.p2['dumps']
        if d and self.p2['ret'] >= d[0]:
            d.pop(0)
            self.p2['cur'] = {'drawn': self.p2['drawn'], 'ret': self.p2['ret'], 'frame': self.st['frame'], **v}

    def h_blend(self, uc, addr, size, _):
        c = self.p2['cur']
        if c is not None and c['drawn'] == self.p2['drawn']:
            rd = lambda s: np.frombuffer(bytes(self.rm.mem_read(SEG(s), 0x10000)), np.uint8).copy()
            c['new'] = rd(0x4527); c['old'] = rd(0x5517); c['table'] = rd(0x3527)

    def h_copy(self, uc, addr, size, _):
        self.p2log.write('C %d %d %d\n' % (self.p2['ret'], self.st['frame'], self.p2['drawn']))
        c = self.p2['cur']
        if c is not None and c['drawn'] == self.p2['drawn'] and 'new' in c:
            c['out'] = np.frombuffer(bytes(self.rm.mem_read(SEG(0x5517), 0x10000)), np.uint8).copy()
            c['copy_frame'] = self.st['frame']
            np.savez_compressed('%s_dump_%d.npz' % (self.a.out, c['drawn']), **{k: np.asarray(v) for k, v in c.items()})
            self.p2['cur'] = None


R.Machine = P2
if __name__ == '__main__':
    R.main()
