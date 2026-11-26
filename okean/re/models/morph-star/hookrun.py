"""Run the intro from a snapshot (the copy of run_okean.py next to this file) and log, for the morph and star
parts, every retrace callback, every main-loop start and every copy to video memory, with the variables the
models need; optionally dump the draw buffer at the copy for chosen frames.

usage: python3 hookrun.py --load ../../snaps/part_367e5.snap --out x --frames 3600 --part 4 [--dump 3480,3481]  (writes x_hooks.log, x_buf<F>.npz)
"""
import argparse
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import run_okean as R

PARTS = {
    4: dict(cb=0x36af4, top=0x36943, copy=0x36aca, buf=0x35d94,
            vars={'seed': (0x35d44, 4), 'nedge': (0x35d40, 4), 'fade': (0x35d3c, 4), 'p4c': (0x35d4c, 4),
                  'p50': (0x35d50, 4), 'p48': (0x35d48, 4), 'a60': (0x35d60, 4), 'a64': (0x35d64, 4),
                  'a68': (0x35d68, 4), 'zoff': (0x35dac, 4), 'p6c': (0x35d6c, 4)}),
    6: dict(cb=0x3cad3, top=0x3c94b, copy=0x3caa9, buf=0x38e00,
            vars={'seed': (0x38d1c, 4), 'scroll': (0x38db0, 4), 'fade': (0x3be24, 4), 'dc0': (0x38dc0, 4),
                  'dd0': (0x38dd0, 4), 'da4': (0x38da4, 4)}),
}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--load', required=True)
    ap.add_argument('--out', required=True)
    ap.add_argument('--frames', type=int, required=True)
    ap.add_argument('--part', type=int, required=True)
    ap.add_argument('--dump', default='', help='frames whose draw buffer is saved at the copy')
    a0 = ap.parse_args()
    # run_okean's arguments
    a = argparse.Namespace(out=a0.out, mips=100.0, quantum=20000, frames=a0.frames, start_frame=0, snap_at='',
                           snap_parts=False, snap=os.path.join(a0.out + '_{tag}.snap'), load=a0.load, args='',
                           trace='')
    m = R.Machine(a)
    m.load_snapshot(a.load)
    P = PARTS[a0.part]
    log = open(a0.out + '_hooks.log', 'w', buffering=1)
    dumps = {int(x) for x in a0.dump.split(',') if x}
    cnt = {'cb': 0, 'top': 0}

    def rdv():
        return ' '.join('%s=%d' % (k, int.from_bytes(m.rd(R.C32 + ad, n), 'little', signed=True))
                        for k, (ad, n) in P['vars'].items())

    def music():
        return '%d:%d' % (m.rd(R.C32 + 0x12b75, 1)[0], int.from_bytes(m.rd(R.C32 + 0x12b73, 2), 'little'))

    def on_cb(uc, addr, size, _):
        cnt['cb'] += 1
        log.write('cb %d frame %d t %.6f pos %s %s\n' % (cnt['cb'], m.st['frame'], m.t_io(), music(), rdv()))

    def on_top(uc, addr, size, _):
        cnt['top'] += 1
        log.write('top %d frame %d t %.6f cbs %d pos %s %s\n' % (cnt['top'], m.st['frame'], m.t_io(), cnt['cb'],
                                                                 music(), rdv()))

    def on_copy(uc, addr, size, _):
        f = m.st['frame']                                  # at 0x36aca / 0x3caa9, just before the rep movsd
        log.write('copy frame %d t %.6f cbs %d tops %d pos %s %s\n' % (f, m.t_io(), cnt['cb'], cnt['top'], music(),
                                                                      rdv()))
        if f in dumps:
            b = int.from_bytes(m.rd(R.C32 + P['buf'], 4), 'little')
            np.savez(a0.out + '_buf%d.npz' % f, buf=np.frombuffer(m.rd(R.C32 + b, 64000), np.uint8))

    m.pm.hook_add(R.UC_HOOK_CODE, on_cb, None, R.C32 + P['cb'], R.C32 + P['cb'])
    m.pm.hook_add(R.UC_HOOK_CODE, on_top, None, R.C32 + P['top'], R.C32 + P['top'])
    m.pm.hook_add(R.UC_HOOK_CODE, on_copy, None, R.C32 + P['copy'], R.C32 + P['copy'])
    m.frames = open(a.out + '.frames', 'wb')
    m.frames.write(b'OKEANFR1' + R.struct.pack('<IIIIII', R.FW, R.FH, R.REC, 32, 0, 0))
    m.ticks = open(a.out + '.ticks', 'w', buffering=1)
    m.ticks.write('# frame time_s order row speedcount mtick part start flips timer_irqs mode eip\n')
    try:
        m.run()
    finally:
        m.glog.close()
        m.frames.close()
        m.ticks.close()
        log.close()


if __name__ == '__main__':
    main()
