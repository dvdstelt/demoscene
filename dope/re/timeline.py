"""Per-part summary of a run_dope.py capture (.ticks): frames, times, music positions, drawn frames, page flips,
modes.

usage: python3 timeline.py full.ticks [--steps PART]     (--steps: also list the frames where the mode changes)

Times are seconds from program start; 'music' = order:row in hex at the part's first and last frame; 'drawn fps' =
key checks per second (the engine parts p2, p3, p5, p6, p7 check the keyboard once per drawn frame; p1 and p4 do not
use int 16h); 'flips' = CRTC start-address writes per second.
"""
import sys


def main():
    path = sys.argv[1]
    rows = []
    for line in open(path):
        if line.startswith('#'):
            continue
        f = line.split()
        rows.append(dict(frame=int(f[0]), t=float(f[1]), order=int(f[2]), row=int(f[3]), part=f[6],
                         flips=int(f[8]), keys=int(f[9]) if len(f) >= 13 else 0, mode=f[-3]))
    parts = []
    for r in rows:
        if not parts or parts[-1][0] != r['part']:
            parts.append((r['part'], []))
        parts[-1][1].append(r)
    print('| part | frames | time (s) | length (s) | music | drawn fps | flips/s | modes |')
    print('|---|---|---|---|---|---|---|---|')
    for name, rs in parts:
        a, b = rs[0], rs[-1]
        dur = b['t'] - a['t'] + (rs[1]['t'] - rs[0]['t'] if len(rs) > 1 else 0)
        modes = []
        for r in rs:
            if not modes or modes[-1] != r['mode']:
                modes.append(r['mode'])
        print('| %s | %d–%d | %.2f–%.2f | %.2f | %02x:%02x–%02x:%02x | %.1f | %.1f | %s |' % (
            name, a['frame'], b['frame'], a['t'], b['t'], dur, a['order'], a['row'], b['order'], b['row'],
            (b['keys'] - a['keys']) / dur if dur else 0, (b['flips'] - a['flips']) / dur if dur else 0,
            ' '.join(modes[:6]) + (' ...' if len(modes) > 6 else '')))


if __name__ == '__main__':
    main()
