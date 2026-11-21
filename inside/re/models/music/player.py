"""Play EYE.AMF through the DSMI port (dsmi.py) and check it against the capture:
  - the GUS register writes of full.gus (start-up: same writes in the same order; music: same writes with the same
    timer-interrupt stamps),
  - the music order / row columns of full.ticks (the AMP state after the interrupts counted in each record).

usage: python3 player.py [--gus ../../full.gus] [--ticks ../../full.ticks] [--first-irq 91]
                         [--out x.gus] [--ticktimes x.txt] [--until IRQ]
--first-irq: the first timer interrupt that runs the music callback (91 in the capture; see notes-music.md).
--ticktimes: writes one line per player tick: tick number, DSMI time it was due, interrupt, order, row, tick in row.
"""
import argparse
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import amf   # noqa: E402
import dsmi  # noqa: E402

RE = os.path.normpath(os.path.join(HERE, '..', '..'))


def read_gus(path):
    out = []
    with open(path) as f:
        for line in f:
            p = line.split(None, 1)
            if len(p) == 2:
                out.append((int(p[0]), p[1].strip()))
    return out


def read_ticks(path):
    out = []
    with open(path) as f:
        for line in f:
            if line.startswith('#'):
                continue
            p = line.split()
            if len(p) < 12:
                continue
            out.append((int(p[0]), int(p[2]), int(p[3]), int(p[10])))   # frame, order, row, irqs
    return out


class TickLog:
    def __init__(self, ds):
        self.ds = ds
        self.rows = []
        self.n = 0
        orig = ds.amp.tick

        def tick():
            self.n += 1
            a = ds.amp
            due = a.last + a.period()
            orig()
            self.rows.append((self.n, due, ds.card.stamp, a.order, a.row, a.tick_))
        ds.amp.tick = tick


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--amf', default=os.path.join(dsmi.ORIG, 'EYE.AMF'))
    ap.add_argument('--gus', default=os.path.join(RE, 'full.gus'))
    ap.add_argument('--ticks', default=os.path.join(RE, 'full.ticks'))
    ap.add_argument('--first-irq', type=int, default=91)
    ap.add_argument('--until', type=int, default=0, help='last interrupt to run (default: the capture\'s)')
    ap.add_argument('--skip', default='12117', help='interrupts whose callback did not run (the credits\' frame '
                    'routine 0x406794 sets [0x42278c] around 0x4061fc; comma-separated; "" for none)')
    ap.add_argument('--out', help='write the model\'s register log here')
    ap.add_argument('--ticktimes', help='write the time of every player tick here')
    a = ap.parse_args()

    m = amf.AMF(a.amf)
    ds = dsmi.Dsmi(m)
    tl = TickLog(ds)
    ds.card.stamp = -1
    ds.start()
    init = list(ds.card.log)
    ref = read_gus(a.gus) if os.path.exists(a.gus) else []
    ticks = read_ticks(a.ticks) if os.path.exists(a.ticks) else []
    last_irq = a.until or max([s for s, _ in ref] + [t[3] for t in ticks] + [0])

    # the music: the callback of interrupt first_irq + k - 1 is DSMI's k-th
    by_irq = {}
    for t in ticks:
        by_irq.setdefault(t[3], []).append(t)
    pos_ok = pos_bad = 0
    first_bad_pos = None
    state = {}
    skip = {int(x) for x in a.skip.split(',') if x}
    for irq in range(a.first_irq, last_irq + 1):
        ds.card.stamp = irq
        if irq not in skip:
            ds.interrupt()
        state[irq] = (ds.amp.order, ds.amp.row)
    if ref and len(ds.card.log) < len(ref):
        ds.card.stamp = ref[-1][0]          # the exit runs in the main line after the last interrupt
        ds.exit()
    for f, order, row, irqs in ticks:
        if order < 0:
            continue
        mine = state.get(irqs, (0, 0)) if irqs >= a.first_irq else (0, 0)
        if mine == (order, row):
            pos_ok += 1
        else:
            pos_bad += 1
            if first_bad_pos is None:
                first_bad_pos = (f, irqs, (order, row), mine)
    log = ds.card.log

    # compare the start-up writes (text only: their stamps depend on the main line's speed) and the music
    ninit = len(init)
    ok_init = [x[1] for x in ref[:ninit]] == [x[1] for x in init]
    if not ok_init:
        for k, (r, mi) in enumerate(zip(ref, init)):
            if r[1] != mi[1]:
                print('start-up differs at line %d: capture %r, model %r' % (k + 1, r, mi))
                break
    music_ref = [x for x in ref[ninit:] if x[0] <= last_irq]
    music = [x for x in log[ninit:] if x[0] <= last_irq]
    same = 0
    for r, mi in zip(music_ref, music):
        if r != mi:
            break
        same += 1
    print('start-up: %d writes, %s (capture stamps %d..%d)' % (
        ninit, 'identical' if ok_init else 'DIFFERENT', ref[0][0] if ref else -1,
        ref[ninit - 1][0] if len(ref) >= ninit else -1))
    print('music: capture %d writes, model %d, identical for the first %d' % (len(music_ref), len(music), same))
    if same < min(len(music_ref), len(music)):
        k = same
        print('first difference at music write %d (line %d):' % (k, ninit + k + 1))
        for j in range(max(0, k - 5), min(k + 6, len(music_ref), len(music))):
            print('  %s capture %-22s model %s' % ('>' if j == k else ' ', '%d %s' % music_ref[j],
                                                    '%d %s' % music[j]))
    print('positions: %d records match, %d differ%s' % (pos_ok, pos_bad, '' if not first_bad_pos else
                                                          ' (first: frame %d irqs %d capture %s model %s)' %
                                                          first_bad_pos))
    ok = ok_init and same == len(music_ref) == len(music) and pos_bad == 0
    print('ALL MATCH' if ok else 'MISMATCH')
    print('effects (AMP index: count):', dict(sorted((k, v) for k, v in ds.amp.stats.items() if k != 'note')),
          'notes', ds.amp.stats.get('note', 0), 'ticks', tl.n)
    if a.out:
        with open(a.out, 'w') as f:
            for s, t in log:
                f.write('%d %s\n' % (s, t))
    if a.ticktimes:
        with open(a.ticktimes, 'w') as f:
            f.write('# tick due_dsmi_time irq order row tick_in_row\n')
            for r in tl.rows:
                f.write('%d %d %d %d %d %d\n' % r)


if __name__ == '__main__':
    main()
