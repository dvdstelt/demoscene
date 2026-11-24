"""Clock analysis of the event logs written by run_core.py (models/core/runs/p<n>.ev).

For every part: the refresh of its first callback, the callbacks per refresh, which callback count each record
shows (the count the draw started with), dropped frames (a draw that spans more than one retrace), and the
music position at the part's start and exit.
usage: python3 models/core/clock.py models/core/runs/p2.ev [...]
       python3 models/core/clock.py --table      writes models/core/record_clock.txt from all runs, lists the drops
"""
import sys
from collections import defaultdict

T = 808 * 625 / 25.175e6


def load(path):
    ev = []
    for line in open(path):
        if line.startswith('#'):
            continue
        f = line.split()
        ev.append(dict(kind=f[0], frame=int(f[1]), line=float(f[2]), t=float(f[3]), part=f[4], cb=int(f[5]),
                       order=int(f[6]), row=int(f[7]), cd=int(f[9]), mtick=int(f[10]), extra=' '.join(f[11:])))
    return ev


def analyse(path):
    ev = load(path)
    parts = []
    for e in ev:
        if not parts or parts[-1] != e['part']:
            parts.append(e['part'])
    print('==', path, 'parts', parts)
    for p in parts:
        pe = [e for e in ev if e['part'] == p]
        cbs = [e for e in pe if e['kind'] == 'cb']
        tops = [e for e in pe if e['kind'] == 'top']
        copies = [e for e in pe if e['kind'] == 'copy']
        recs = [e for e in ev if e['kind'] == 'rec']
        meas = [e for e in pe if e['kind'] == 'measure']
        load_ = [e for e in pe if e['kind'] == 'load']
        if meas:
            print('  part %s: install/measure at frame %d line %.1f (music %d:%d), count loaded at frame %d line %.1f'
                  % (p, meas[0]['frame'], meas[0]['line'], meas[0]['order'], meas[0]['row'], load_[0]['frame'],
                     load_[0]['line']))
        if not cbs:
            continue
        print('  first callback: frame %d line %.2f, music %d:%d countdown %d mtick %d' % (
            cbs[0]['frame'], cbs[0]['line'], cbs[0]['order'], cbs[0]['row'], cbs[0]['cd'], cbs[0]['mtick']))
        # callbacks per refresh
        per = defaultdict(int)
        for e in cbs:
            per[e['frame']] += 1
        fr = sorted(per)
        bad = [f for f in range(fr[0], fr[-1] + 1) if per.get(f, 0) != 1]
        print('  callbacks in frames %d..%d, refreshes without exactly one callback: %s' % (fr[0], fr[-1], bad[:20]))
        lines = [e['line'] for e in cbs]
        print('  callback lines: min %.2f max %.2f' % (min(lines), max(lines)))
        # draws: top -> next copy
        draws = []
        ti = 0
        allk = [e for e in pe if e['kind'] in ('top', 'copy')]
        cur = None
        for e in allk:
            if e['kind'] == 'top':
                cur = e
            elif cur is not None:
                draws.append((cur, e))
                cur = None
        drops = [(a['frame'], b['frame'], b['cb'] - a['cb']) for a, b in draws if b['cb'] - a['cb'] != 1]
        print('  draws %d; draws whose copy is not exactly one callback after the draw start: %s' % (
            len(draws), drops[:20]))
        # record k shows the last copy before it
        m = {}
        di = 0
        for r in recs:
            while di < len(draws) and draws[di][1]['t'] <= r['t']:
                di += 1
            if di and draws[di - 1][1]['part'] == p:
                m[r['frame']] = draws[di - 1][0]['cb']
        ks = sorted(m)
        if ks:
            off = set(k - m[k] for k in ks)
            print('  records %d..%d show callback counts %d..%d; record - count = %s' % (
                ks[0], ks[-1], m[ks[0]], m[ks[-1]], sorted(off)))
            # in terms of callbacks done at the end of the record's refresh
            first_cb = cbs[0]['frame']
            print('  => record k shows the draw made with n = k - %d callbacks (first callback in refresh %d)' % (
                ks[0] - m[ks[0]], first_cb))
        ex = [e for e in pe if e['kind'] == 'restore']
        if ex:
            e = ex[0]
            print('  exit (restore) frame %d line %.1f music %d:%d countdown %d mtick %d, callbacks %d' % (
                e['frame'], e['line'], e['order'], e['row'], e['cd'], e['mtick'], e['cb']))


if len(sys.argv) > 1 and sys.argv[1] == '--table':
    pass
else:
    for p in sys.argv[1:]:
        analyse(p)


CLEARED = (621, 4693, 6893)    # the set-ups of 0x2e779, 0x38520 and 0x5334f clear video memory (rep stosd at A0000)


def record_table(paths, out):
    """models/core/record_clock.txt: for every record of full.frames the part whose draw it shows and that draw's
    callback count n (callbacks of that part before the draw started), plus the drops (a draw that missed a retrace;
    the record before its copy repeats the previous draw)"""
    shown = {}
    drops = []
    ticks = {}
    first_cb = {}
    exits = {}
    for path in paths:
        ev = load(path)
        cur = None
        for e in ev:
            if e['kind'] == 'restore':
                exits[e['part']] = e['frame']
            if e['kind'] == 'cb':
                first_cb.setdefault(e['part'], e['frame'])
            if e['kind'] == 'tick':
                ticks.setdefault(e['frame'], []).append(e)
            if e['kind'] == 'top':
                cur = e
            elif e['kind'] == 'copy' and cur is not None:
                shown[e['frame']] = (cur['part'], cur['cb'], cur['frame'])
                if e['cb'] - cur['cb'] != 1:
                    drops.append((cur['frame'], e['frame'], cur['part']))
                cur = None
    last = None
    with open(out, 'w') as f:
        f.write('# record part n (the part\'s callbacks before the draw) k-F (F = refresh of the part\'s first callback)'
                ' note\n')
        for k in range(0, 9109):
            if k in shown:
                last = shown[k]
                note = ''
            else:
                note = 'repeat' if last else ''
                if last and exits.get(last[0], 1 << 30) < k:
                    note = 'gap (the next part\'s set-up and timer calibration)'
            if last and exits.get(last[0]) == k and k in CLEARED:
                note = 'cleared to index 0 by the next part\'s set-up (shows nothing of this draw)'
            if last is None:
                f.write('%d - - - before the first copy\n' % k)
                continue
            p, n, start = last
            F = first_cb[p]
            if not note and n != k - F:
                note = 'late'
            f.write('%d %s %d %d %s\n' % (k, p, n, k - F, note))
    print('first callback refresh per part:', first_cb)
    print('drops (draw started in refresh a, copied in a + 2): %d' % len(drops))
    for a, b, p in drops:
        t = [e for e in ticks.get(a + 1, []) if e['line'] < 502]
        row = ['%d:%d cd%d @%.0f' % (e['order'], e['row'], e['cd'], e['line']) for e in t]
        print('  %s draw %d copied %d; ticks in refresh %d before the retrace: %s' % (p, a, b, a + 1, row))


if len(sys.argv) > 1 and sys.argv[1] == '--table':
    import glob, os
    here = os.path.dirname(os.path.abspath(__file__))
    record_table(sorted(glob.glob(os.path.join(here, 'runs', 'p*.ev'))), os.path.join(here, 'record_clock.txt'))
