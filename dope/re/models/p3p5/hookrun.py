"""Run p3 or p5 from a snapshot with hooks (uses the copy of run_dope.py in this folder).

Records, per drawn frame: the retrace count since the part's per-retrace routine first ran, the camera object
(object 0) and object 1 state when the transform starts, the label position, the script step (N of the current
"run N retraces" call), and for chosen frames the engine's frame buffer just before it is copied to VGA (label
included) plus, for p5, the bump offset table at 0x375b.

usage: hookrun.py p3|p5 SNAP OUT.npz --until FRAME [--dump F1,F2,...] [--dumpall]
"""
import argparse, os, sys, struct
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import run_dope as rd
from unicorn import UC_HOOK_CODE

LOADSEG = 0x0F64
CS = LOADSEG + 0x10
CFG = {
    # transform start, object list, fb segment var, before-copy, step entry, per-retrace routine, label var
    'p3': dict(xf=0x2a5a, obj=0x1810, fbvar=0x19d0, copy=0x2b78, step=0x2ba4, retr=0x1f60, label=0x19c4),
    'p5': dict(xf=0x2a17, obj=0x1412, fbvar=0x1564, copy=0x2b3a, step=0x2b66, retr=0x1a70, label=0x1558),
}

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('part'); ap.add_argument('snap'); ap.add_argument('out')
    ap.add_argument('--until', type=int, required=True)
    ap.add_argument('--dump', default='')
    ap.add_argument('--dumpall', action='store_true')
    ap.add_argument('--mips', type=float, default=100.0)
    x = ap.parse_args()
    c = CFG[x.part]
    scratch = os.path.join(os.path.dirname(x.out), '_hookrun_' + x.part)
    a = argparse.Namespace(out=scratch, mips=x.mips, quantum=20000, frames=x.until, start_frame=1 << 30, snap_at='',
                           snap_parts=False, snap='', load=x.snap, stop_part='', debug=0, trace='')
    m = rd.Machine(a)
    m.snap_frames = set()
    m.load_snapshot(x.snap)
    m.frames = open(scratch + '.frames', 'wb'); m.ticks = open(scratch + '.ticks', 'w')
    dumps = {int(v) for v in x.dump.split(',') if v}
    cs16 = CS * 16
    def r16(off): return struct.unpack('<H', m.rd(cs16 + off, 2))[0]
    def obj(i):
        b = m.rd(cs16 + c['obj'] + i * 0x3a, 0x3a)
        return np.frombuffer(b, np.uint8).copy()
    S = {'retr': 0, 'stepN': -1, 'steps': [], 'rows': [], 'fb': {}, 'tab': {}, 'cur': None}
    def on_retr(uc, addr, size, u):
        S['retr'] += 1
    def on_step(uc, addr, size, u):
        n = m.rm.reg_read(rd.UC_X86_REG_CX)
        S['steps'].append((m.st['frame'], S['retr'], n))
    def on_xf(uc, addr, size, u):
        S['cur'] = dict(frame=m.st['frame'], retr=S['retr'], o0=obj(0), o1=obj(1), label=r16(c['label']),
                        tab=np.frombuffer(m.rd(cs16 + 0x375b, 512), '<u2').copy() if x.part == 'p5' else None,
                        nsteps=len(S['steps']))
    def on_copy(uc, addr, size, u):
        cur = S['cur']
        if cur is None:
            return
        cur['frame_copy'] = m.st['frame']; cur['retr_copy'] = S['retr']
        S['rows'].append(cur)
        k = len(S['rows']) - 1
        if x.dumpall or cur['frame'] in dumps:
            seg = r16(c['fbvar'])
            fb = np.frombuffer(m.rd(seg * 16 + 0x140, 64000), np.uint8).copy()
            S['fb'][k] = fb
            if x.part == 'p5':
                S['tab'][k] = np.frombuffer(m.rd(cs16 + 0x375b, 512), '<u2').copy()
        S['cur'] = None
    for off, fn in ((c['retr'], on_retr), (c['step'], on_step), (c['xf'], on_xf), (c['copy'], on_copy)):
        lin = cs16 + off
        m.rm.hook_add(UC_HOOK_CODE, fn, None, lin, lin)
    try:
        m.run()
    finally:
        m.frames.close(); m.ticks.close(); m.glog.close()
    rows = S['rows']
    out = dict(frame=np.array([r['frame'] for r in rows]), retr=np.array([r['retr'] for r in rows]),
               frame_copy=np.array([r['frame_copy'] for r in rows]), retr_copy=np.array([r['retr_copy'] for r in rows]),
               o0=np.array([r['o0'] for r in rows]), o1=np.array([r['o1'] for r in rows]),
               label=np.array([r['label'] for r in rows]), nsteps=np.array([r['nsteps'] for r in rows]),
               steps=np.array(S['steps']))
    if x.part == 'p5':
        out['tab'] = np.array([r['tab'] for r in rows])
    for k, fb in S['fb'].items():
        out['fb_%d' % k] = fb
    for k, t in S['tab'].items():
        out['tabcopy_%d' % k] = t
    np.savez_compressed(x.out, **out)
    print('rows', len(rows), 'steps', len(S['steps']), 'fb dumps', len(S['fb']))

if __name__ == '__main__':
    main()
