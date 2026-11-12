"""Run p4 from snaps/part_p4.snap with hooks that record its state (uses the local copy of run_dope.py).

Outputs in models/p4/out/ (ignored by git, made from the original):
  frames.log   one line per call of the frame routine 1b91 (after 1aab returned): video frame, time, IRQ count
               [141e], retrace counter [1969], fade [1971]/[1973], label [196d], fire phase [1578]/[157a]/[1576],
               landscape position [2113], the fire offset in use [1b0b], CRC32 of the fire buffer 66cd
  retrace.log  one line per per-retrace call (1a20 entry): video frame, [141e], [1969], [1971], [196d], [2113]
  init.npz     at the first frame: DAC, picture copy 4f1e, scaled cloud 1ec1, blend table 5ecd, map 07d9
  fire_<n>.npy the fire buffer after recompute number n (every 100th, and the first 20)
  land_<f>.npz the landscape buffer 4f1e after 1efc (some frames) with [2113] and the column buffer
  lbm.npz      4eed after the chequerboard was decoded (palette and picture)
usage (from dope/re): python3 models/p4/hook_p4.py
"""
import os
import struct
import sys
import zlib

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import run_dope as R  # noqa: E402  (the local copy)

LOADSEG = 0x0F64                 # p4's load segment in the run (its image file offset X is at linear 0xF640 + X)
BASE = LOADSEG * 16
CSL = BASE + 0x100               # linear address of 0010:0000
OUT = os.path.join(HERE, 'out')


def lin(seg, off):
    return BASE + seg * 16 + off


class M(R.Machine):
    def hooks(self):
        super().hooks()
        os.makedirs(OUT, exist_ok=True)
        self.flog = open(os.path.join(OUT, 'frames.log'), 'w')
        self.rlog = open(os.path.join(OUT, 'retrace.log'), 'w')
        self.nrec = 0
        self.last_off = None
        self.did_init = False
        self.nland = 0
        rm = self.rm
        rm.hook_add(R.UC_HOOK_CODE, self.h_frame, None, CSL + 0x1b94, CSL + 0x1b94)
        rm.hook_add(R.UC_HOOK_CODE, self.h_retrace, None, CSL + 0x1a20, CSL + 0x1a20)
        for a in (0x1daf, 0x1de0, 0x1e26):
            rm.hook_add(R.UC_HOOK_CODE, self.h_land, None, CSL + a, CSL + a)
        rm.hook_add(R.UC_HOOK_CODE, self.h_lbm, None, CSL + 0x1e5c, CSL + 0x1e5c)
        rm.hook_add(R.UC_HOOK_CODE, self.h_init, None, CSL + 0x1d16, CSL + 0x1d16)

    def w(self, off):
        return struct.unpack('<H', self.rd(CSL + off, 2))[0]

    def sw(self, off):
        return struct.unpack('<h', self.rd(CSL + off, 2))[0]

    def run(self):
        try:
            super().run()
        finally:
            self.flog.close()
            self.rlog.close()

    def h_init(self, uc, addr, size, _):
        np.savez(os.path.join(OUT, 'init.npz'),
                 dac=np.frombuffer(bytes(self.vga.dac), np.uint8),
                 pal157e=np.frombuffer(self.rd(CSL + 0x157e, 0x30), np.uint8),
                 pic=np.frombuffer(self.rd(lin(0x4f1e, 0), 64000), np.uint8),
                 cloud=np.frombuffer(self.rd(lin(0x1ec1, 0), 65536), np.uint8),
                 blend=np.frombuffer(self.rd(lin(0x5ecd, 0), 32768), np.uint8),
                 tmap=np.frombuffer(self.rd(lin(0x07d9, 0), 64000), np.uint8),
                 fire=np.frombuffer(self.rd(lin(0x66cd, 0), 32000), np.uint8),
                 sine=np.frombuffer(self.rd(CSL, 0x1400), np.uint8),
                 vram=np.frombuffer(bytes(self.rm.mem_read(0xa0000, 64000)), np.uint8))
        print('p4 init state saved at frame', self.st['frame'], flush=True)

    def h_frame(self, uc, addr, size, _):
        off = self.w(0x1b0b)
        fire = self.rd(lin(0x66cd, 0), 32000)
        rec = off != self.last_off
        if rec:
            self.nrec += 1
            if self.nrec <= 20 or self.nrec % 100 == 0:
                np.save(os.path.join(OUT, 'fire_%d.npy' % self.nrec), np.frombuffer(fire, np.uint8))
        self.last_off = off
        self.flog.write('%d %.6f %d %d %d %d %d %d %d %d %d %d %d %08x %d\n' % (
            self.st['frame'], self.t_io(), self.w(0x141e), self.w(0x1969), self.sw(0x1971), self.sw(0x1973),
            self.sw(0x196d), self.w(0x1578), struct.unpack('<i', self.rd(CSL + 0x157a, 4))[0], self.w(0x1576),
            self.w(0x2113), off, self.nrec if rec else -1, zlib.crc32(fire), self.w(0x196b)))

    def h_retrace(self, uc, addr, size, _):
        self.rlog.write('%d %.6f %d %d %d %d %d\n' % (self.st['frame'], self.t_io(), self.w(0x141e), self.w(0x1969),
                                                      self.sw(0x1971), self.sw(0x196d), self.w(0x2113)))

    def h_land(self, uc, addr, size, _):
        self.nland += 1
        if self.nland <= 5 or self.nland % 150 == 0:
            np.savez(os.path.join(OUT, 'land_%d.npz' % self.nland),
                     buf=np.frombuffer(self.rd(lin(0x4f1e, 0), 64000), np.uint8),
                     pos=self.w(0x2113), frame=self.st['frame'],
                     ybuf=np.frombuffer(self.rd(CSL + 0x2121, 0x282), np.uint8))

    def h_lbm(self, uc, addr, size, _):
        np.savez(os.path.join(OUT, 'lbm.npz'), area=np.frombuffer(self.rd(lin(0x4eed, 0), 0x310 + 64000), np.uint8))
        print('p4 chequerboard decoded at frame', self.st['frame'], flush=True)


def main():
    sys.argv = [sys.argv[0], '--load', os.path.join(R.RE, 'snaps', 'part_p4.snap'), '--out', os.path.join(OUT, 'run'),
                '--stop-part', 'p5', '--start-frame', str(1 << 29)] + sys.argv[1:]
    os.makedirs(OUT, exist_ok=True)
    R.Machine = M
    R.main()


if __name__ == '__main__':
    main()
