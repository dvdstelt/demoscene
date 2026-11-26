"""Play VIVID.EXE's music through the MIDAS port (midas.py) and MIDAS's screen-synchronised timer, and compare the GUS
register writes with a capture.

usage (inside the toolbox):
  python3 player.py full      ../../full.gus   both parts as the whole-demo run plays them
  python3 player.py part2     ../../part2.gus  part II on its own (vivid.exe 2)
  options: --out FILE (write the stream), --ticks FILE (write every player tick: interrupt, time, refresh, order,
           row), --positions ../../full.ticks (compare the music position per refresh)

The timer (0x410740..0x410f00) is simulated in PIT clocks like run_vivid.py runs it: the retrace of refresh k starts
at k * FRAME + 412 * LINE; interrupt handlers take no time except their retrace waits. The main-line events that the
music cannot know (when tmrSyncScr waits for its retrace, after which interrupt the module starts, when it stops)
are the run's parameters below (RUNS), read off the capture's .ticks / .log.
"""
import argparse
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import midas  # noqa: E402

PIT_HZ = 1193182
VGA_HZ = 25175000 / (800 * 449)
FRAME = PIT_HZ / VGA_HZ
LINE = FRAME / 449
ORIG = os.path.join(os.path.dirname(midas.RE), 'original')

# per part: the refresh whose retrace tmrSyncScr waits for, the interrupts before it, the interrupt after which
# midasPlayModule runs, the interrupt after which the part's music stops (None: the end of the run)
RUNS = {
    'full': [dict(xm='EXPRMNT1.XM', sync_frame=4, irqs0=0, start_after=31, stop_after=30267, init='first'),
             dict(xm='EXPRMNT2.XM', sync_frame=14448, irqs0=30267, start_after=30284, stop_after=52937, init='again')],
    'part2': [dict(xm='EXPRMNT2.XM', sync_frame=4, irqs0=0, start_after=17, stop_after=22670, init='first')],
}


def retrace(k):
    return k * FRAME + 412 * LINE + 1e-4


class Timer:
    """MIDAS's timer in screen-synchronised mode with a tempo-polled sound device (the GUS)."""

    def __init__(self, sync, on_player, frame0, irqs0):
        self.S = (0x3b6 * sync // 1000) >> 1      # scrTmrCnt [0x82323a]: 95 % of a refresh
        self.R = (sync >> 1) - self.S             # [0x82323e]
        self.P = 0x5d37                           # playTmrCnt [0x8231de]: 50 Hz until the player sets its tempo
        self.play_count = 0x5d37                  # [0x8231da] (tmrPlaySD)
        self.scr_count = self.S                   # [0x823236]
        self.irqs = irqs0
        self.on_player = on_player
        self.frame = frame0                       # refresh of the retrace the PIT was last programmed at
        self.mode = 2
        self.t = retrace(frame0)                  # tmrSyncScr: PIT = scrTmrCnt at the retrace
        self.count = self.S
        self.events = []

    def set_upd_rate(self, rate):                 # 0x410e21 tmrSetUpdRate
        self.P = 0x71ca5f0 // rate

    def set_next(self, t):                        # 0x41074d
        if self.play_count < 0:
            self.play_count = 10
            self.mode, c = 1, 10
        elif self.play_count < self.scr_count:
            self.mode, c = 1, self.play_count
        else:
            if self.scr_count < 0:
                self.scr_count = 10
            self.mode, c = 2, self.scr_count
        self.t, self.count = t, c

    def next_time(self):
        return self.t + self.count

    def interrupt(self):
        """Run the next interrupt; returns its kind."""
        t = self.next_time()
        self.irqs += 1
        if self.mode == 2:                        # 0x410843: the refresh
            k = int((t - 412 * LINE) // FRAME) + 1
            while retrace(k) <= t:
                k += 1
            e = self.scr_count + self.R
            self.play_count -= e
            self.scr_count = self.S
            self.frame = k
            self.set_next(retrace(k))
            return 'sync'
        e = self.play_count                       # 0x41092c: the player
        self.scr_count -= e
        self.play_count = self.P
        self.set_next(t)
        p0 = self.P
        self.on_player(self.irqs, t)
        if self.P != p0:
            self.play_count = self.P
            if self.mode == 1:
                self.set_next(t)
        return 'player'


class Run:
    def __init__(self, which, answers='capture'):
        self.port = midas.Gf1Port(answers)
        self.sd = midas.GusDevice(self.port)
        self.which = which
        self.ticks = []          # (irq, time, refresh, order, row, tick)
        self.gmp = None

    def go(self, sync=34049, on_irq=None):
        port, sd = self.port, self.sd
        midas.check_tables()
        for part in RUNS[self.which]:
            port.stamp = part['irqs0'] if part['init'] == 'first' else port.stamp
            if part['init'] == 'first':
                sd.detect()
                sd.detect()
            else:
                sd.close()
                sd.detect()
            sd.init(1)                             # main 0x4002e0: mode = sdMono (bit 0), stereo bit cleared
            self.gmp = None
            module = midas.load_xm(os.path.join(ORIG, part['xm']), sd)
            timer = Timer(sync, self.player_tick, part['sync_frame'], part['irqs0'])
            self.timer = timer
            started = False
            while True:
                if not started and timer.irqs >= part['start_after']:
                    port.stamp = timer.irqs
                    sd.open_channels(module['nchan'])
                    self.gmp = midas.Gmp(module, sd)
                    self.gmp.play_song()
                    self.gmp.setup_rate_callback(timer.set_upd_rate)
                    started = True
                if timer.irqs >= part['stop_after']:
                    break
                timer.interrupt()
                if on_irq:
                    on_irq(self)
            port.stamp = timer.irqs
            sd.close_channels()                    # midasStopModule
            self.gmp = None
        sd.close()

    def player_tick(self, irq, t):
        self.port.stamp = irq
        if self.sd.play() and self.gmp is not None:
            g = self.gmp
            g.play()
            self.ticks.append((irq, t, t / FRAME, g.info[0], g.info[2], g.tick))


def check_dram(sd, path):
    want = open(path, 'rb').read()
    got = bytes(sd.dram)
    diff = [i for i in range(len(want)) if want[i] != got[i]]
    print('%s: %d of %d bytes equal' % (os.path.basename(path), len(want) - len(diff), len(want)),
          ('first differences at ' + ', '.join('%#x' % i for i in diff[:8])) if diff else '')


def check_positions(ticks, path, parts):
    """The music position the main line copies (0x400104) against the capture's order / row columns: the GMP's
    position (the row read last) at the record's time, and at the retrace before it."""
    import bisect
    times = [t for _, t, _, _, _, _ in ticks]
    rec = []
    for line in open(path):
        if line.startswith('#'):
            continue
        a = line.split()
        rec.append((int(a[0]), int(a[2]), int(a[3]), int(a[5])))

    def at(t):
        i = bisect.bisect_right(times, t) - 1
        return (ticks[i][3], ticks[i][4]) if i >= 0 else None
    n = same_rec = same_ret = either = 0
    misses = []
    for k, o, r, part in rec:
        tr = (k + 1) * FRAME
        p1 = at(tr)
        if p1 is None or ticks[bisect.bisect_right(times, tr) - 1][0] < parts.get(part, 0):
            continue
        kr = k
        while retrace(kr) > tr:
            kr -= 1
        p2 = at(retrace(kr))
        n += 1
        same_rec += p1 == (o, r)
        same_ret += p2 == (o, r)
        if p1 == (o, r) or p2 == (o, r):
            either += 1
        elif len(misses) < 10:
            misses.append((k, (o, r), p1, p2))
    print('positions: %d records with the music playing; position at the record = capture: %d, at the retrace '
          'before it: %d, one of the two: %d' % (n, same_rec, same_ret, either))
    for m in misses:
        print('  record %d capture %s model (record) %s (retrace) %s' % m)
    # how stale is the main line's copy: the time since the GMP left the captured position (0: still current)
    hist = {}
    worst = []
    for k, o, r, part in rec:
        tr = (k + 1) * FRAME
        i = bisect.bisect_right(times, tr) - 1
        if i < 0 or ticks[i][0] < parts.get(part, 0):
            continue
        lag = None
        j = i
        while j >= 0 and tr - times[j] < 20 * FRAME:
            if (ticks[j][3], ticks[j][4]) == (o, r):
                lag = 0.0 if j == i else (times[j + 1] - 0) / FRAME
                lag = 0.0 if j == i else (tr - times[j + 1]) / FRAME
                break
            j -= 1
        key = 'never' if lag is None else '%d' % int(lag)
        hist[key] = hist.get(key, 0) + 1
        if lag is None or lag >= 1:
            worst.append((k, (o, r), lag))
    print('records by how many refreshes ago the GMP left the captured position:', dict(sorted(hist.items())))
    for w in sorted(worst, key=lambda w: -(w[2] or 99))[:12]:
        print('  record %d capture %s lag %s' % w)


def compare(got, path, limit=12):
    want = []
    for line in open(path):
        a = line.split(None, 1)
        want.append((int(a[0]), a[1].strip()))
    n = min(len(got), len(want))
    same = 0
    first = None
    for i in range(n):
        if got[i] == want[i]:
            same += 1
        elif first is None:
            first = i
    print('capture %d writes, model %d writes, %d equal in place' % (len(want), len(got), same))
    if first is not None:
        print('first difference at write %d:' % first)
        for j in range(max(0, first - 6), min(n, first + limit)):
            print('  %s %-22s | %s' % ('*' if got[j] != want[j] else ' ', '%d %s' % got[j], '%d %s' % want[j]))
    return same, len(want)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('which', choices=sorted(RUNS))
    ap.add_argument('capture', nargs='?')
    ap.add_argument('--out')
    ap.add_argument('--ticks')
    ap.add_argument('--positions', help='the capture\'s .ticks: compare the music position per refresh')
    ap.add_argument('--dram', help='the capture\'s .dram: compare the sample memory at the end')
    a = ap.parse_args()
    r = Run(a.which)
    r.go()
    if a.out:
        with open(a.out, 'w') as f:
            for s, t in r.port.log:
                f.write('%d %s\n' % (s, t))
    if a.ticks:
        with open(a.ticks, 'w') as f:
            f.write('# irq time_pit refresh order row\n')
            for irq, t, fr, o, row, tk in r.ticks:
                f.write('%d %.3f %.4f %d %d %d\n' % (irq, t, fr, o, row, tk))
    if a.capture:
        compare(r.port.log, a.capture)
    if a.dram:
        check_dram(r.sd, a.dram)
    if a.positions:
        check_positions(r.ticks, a.positions, {p + 1: x['start_after'] for p, x in enumerate(RUNS[a.which])})


if __name__ == '__main__':
    main()
