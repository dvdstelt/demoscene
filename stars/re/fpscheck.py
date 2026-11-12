"""What depends on the frame rate? Runs run_stars.py from snapshots for --frames frames:
  - with --freeze-pit at the default speed: the PIT callbacks stop, so tick-driven animation stops; anything that
    still changes on screen is stepped per drawn frame, by the retrace waits, or by the music position;
  - normally at each speed of --speeds (instructions per second): the frames drawn per second.
Reports per snapshot: screen / palette changes between consecutive records with the PIT frozen, and fps per speed.

usage: python3 fpscheck.py snaps/f5947_11e7f.snap [more snaps] [--frames 400] [--speeds 20e6,40e6,80e6]
                           [--outdir dump/fps] [--jobs 8]
"""
import argparse
import os
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

import numpy as np

from sheet import open_frames

HERE = os.path.dirname(os.path.abspath(__file__))


def changes(path):
    raw, w, h, rec, hdr, n = open_frames(path)
    scr = pal = 0
    prev = None
    for i in range(n):
        r = np.asarray(raw[hdr + i * rec:hdr + (i + 1) * rec])
        if prev is not None:
            scr += int(not np.array_equal(r[:w * h], prev[:w * h]))
            pal += int(not np.array_equal(r[w * h:], prev[w * h:]))
        prev = r
    return n, scr, pal


def fps(ticks):
    rows = [l.split() for l in open(ticks) if not l.startswith('#')]
    if len(rows) < 2:
        return 0.0
    return (int(rows[-1][8]) - int(rows[0][8])) / (float(rows[-1][1]) - float(rows[0][1]))


def run(snap, frames, outdir, freeze, ips):
    base = os.path.basename(snap).rsplit('.', 1)[0] + ('_frozen' if freeze else '_%dM' % (ips / 1e6))
    out = os.path.join(outdir, base)
    f0 = int(os.path.basename(snap).split('_')[0][1:])
    cmd = [sys.executable, os.path.join(HERE, 'run_stars.py'), '--load', '--snap', snap, '--out', out,
           '--frames', str(f0 + frames), '--ips', str(ips)] + (['--freeze-pit'] if freeze else [])
    subprocess.run(cmd, stdout=open(out + '.log', 'w'), stderr=subprocess.STDOUT)
    return snap, freeze, ips, (changes(out + '.frames') if freeze else fps(out + '.ticks'))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('snaps', nargs='+')
    ap.add_argument('--frames', type=int, default=400)
    ap.add_argument('--speeds', default='20e6,40e6,80e6')
    ap.add_argument('--ips', type=float, default=40e6, help='speed of the frozen run')
    ap.add_argument('--outdir', default=os.path.join(HERE, 'dump', 'fps'))
    ap.add_argument('--jobs', type=int, default=8)
    a = ap.parse_args()
    os.makedirs(a.outdir, exist_ok=True)
    speeds = [float(x) for x in a.speeds.split(',') if x]
    jobs = [(s, True, a.ips) for s in a.snaps] + [(s, False, v) for s in a.snaps for v in speeds]
    with ThreadPoolExecutor(a.jobs) as ex:
        res = list(ex.map(lambda j: run(j[0], a.frames, a.outdir, j[1], j[2]), jobs))
    print('%-24s %8s %14s %15s  %s' % ('snapshot', 'frames', 'screen changes', 'palette changes',
                                        '  '.join('fps@%dM' % (v / 1e6) for v in speeds)))
    for s in a.snaps:
        fr = next(r[3] for r in res if r[0] == s and r[1])
        f = [next(r[3] for r in res if r[0] == s and not r[1] and r[2] == v) for v in speeds]
        print('%-24s %8d %14d %15d  %s' % (os.path.basename(s), fr[0], fr[1], fr[2],
                                            '  '.join('%7.1f' % x for x in f)))


if __name__ == '__main__':
    main()
