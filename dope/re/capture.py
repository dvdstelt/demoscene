"""Make the full capture from part snapshots in parallel, and stitch it together.

run_dope.py is deterministic: a run resumed from a snapshot produces exactly the frames, ticks and GUS writes the
uninterrupted run produced from that point on. So once a sequential run has saved a snapshot where each part starts
(`run_dope.py --snap-parts`, snaps/part_p1.snap .. part_p7.snap, part_term.snap), every part can be re-captured on
its own core: segment 'loader' = program start until p1 starts, segment pN = from snaps/part_pN.snap until the
next part starts, the last segment runs to the program exit. The segments are concatenated into OUT.frames,
OUT.ticks, OUT.gus (and OUT.dram from the last segment); segment logs go to OUT.<seg>.log.

usage: python3 capture.py --out full [--snapdir snaps] [--jobs 9] [--keep] [extra run_dope.py arguments ...]
       python3 capture.py --out full --segments loader,p1,p2     (run some segments now; stitch later with --stitch)
"""
import argparse
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ORDER = ['p1', 'p2', 'p3', 'p4', 'p5', 'p6', 'p7', 'term']


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', required=True)
    ap.add_argument('--snapdir', default=os.path.join(HERE, 'snaps'))
    ap.add_argument('--jobs', type=int, default=9)
    ap.add_argument('--keep', action='store_true', help='keep the segment files')
    ap.add_argument('--segments', default='', help='run only these segments (comma-separated: loader,p1,...,term) '
                    'and do not stitch')
    ap.add_argument('--stitch', action='store_true', help='only stitch segment files made earlier (with --keep or '
                    '--segments)')
    a, extra = ap.parse_known_args()
    segs = [('loader', None, 'p1')]
    for i, p in enumerate(ORDER):
        snap = os.path.join(a.snapdir, 'part_%s.snap' % p)
        if not os.path.exists(snap) and not a.segments and not a.stitch:
            sys.exit('missing %s (make it with run_dope.py --snap-parts)' % snap)
        segs.append((p, snap, ORDER[i + 1] if i + 1 < len(ORDER) else ''))
    procs = []
    running = []
    only = set(filter(None, a.segments.split(',')))
    for name, snap, stop in segs:
        out = '%s.%s' % (a.out, name)
        if a.stitch or (only and name not in only):
            procs.append((name, out, None))
            continue
        out = '%s.%s' % (a.out, name)
        cmd = [sys.executable, os.path.join(HERE, 'run_dope.py'), '--out', out] + extra
        if snap:
            cmd += ['--load', snap]
        if stop:
            cmd += ['--stop-part', stop]
        while len(running) >= a.jobs:
            running[0].wait()
            running = [p for p in running if p.poll() is None]
        log = open(out + '.log', 'w')
        p = subprocess.Popen(cmd, stdout=log, stderr=subprocess.STDOUT)
        procs.append((name, out, p))
        running.append(p)
        print('started', name, flush=True)
    for name, out, p in procs:
        if p is not None:
            p.wait()
            print('finished', name, 'exit', p.returncode, flush=True)
    if only:
        return
    # stitch
    with open(a.out + '.frames', 'wb') as ff, open(a.out + '.ticks', 'w') as ft, open(a.out + '.gus', 'w') as fg:
        for i, (name, out, p) in enumerate(procs):
            with open(out + '.frames', 'rb') as f:
                hdr = f.read(32)
                if i == 0:
                    ff.write(hdr)
                shutil.copyfileobj(f, ff, 1 << 24)
            for line in open(out + '.ticks'):
                if not line.startswith('#') or i == 0:
                    ft.write(line)
            for line in open(out + '.gus'):
                if not line.startswith('#') or i == 0:
                    fg.write(line)
    shutil.copyfile(procs[-1][1] + '.dram', a.out + '.dram')
    if not a.keep:
        for name, out, p in procs:
            for ext in ('.frames', '.ticks', '.gus', '.dram'):
                os.remove(out + ext)
    print('wrote', a.out + '.frames/.ticks/.gus/.dram')


if __name__ == '__main__':
    main()
