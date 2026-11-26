"""The time of every player tick: runs player.py's model and prints, per tick, the interrupt, the refresh whose
retrace it follows, the PIT clocks after that retrace, the tick period in force and whether MIDAS's timer had to
start it late (playCount negative at the refresh: the tick runs 10 clocks after the retrace).

usage: python3 timeline.py full [--list N]
"""
import argparse

import player


class TLTimer(player.Timer):
    def interrupt(self):
        if self.mode == 2:
            pc = self.play_count - (self.scr_count + self.R)
            self.late_next = pc < 0
            self.lateness = -pc if pc < 0 else 0
        return super().interrupt()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('which', choices=sorted(player.RUNS))
    ap.add_argument('--list', type=int, default=20)
    a = ap.parse_args()
    late = []
    rows = []
    orig = player.Timer
    player.Timer = TLTimer
    r = player.Run(a.which)
    tick_info = []

    def tick(irq, t, run=r):
        tm = run.timer
        k = tm.frame
        dt = t - player.retrace(k)
        lt = getattr(tm, 'late_next', False) and abs(dt - 10) < 1e-6
        tick_info.append((irq, k, dt, tm.P, lt, getattr(tm, 'lateness', 0) if lt else 0))
        player.Run.player_tick(run, irq, t)
    r.player_tick = tick
    r.go()
    player.Timer = orig
    started = [x for x in tick_info]
    n_late = sum(1 for x in started if x[4])
    print('%d player interrupts (with the GMP: %d); started late at the retrace + 10: %d (largest delay %d PIT clocks)'
          % (len(started), len(r.ticks), n_late, max([x[5] for x in started] or [0])))
    print('irq refresh clocks_after_retrace period late')
    for x in started[:a.list]:
        print('%d %d %.3f %d %s' % (x[0], x[1], x[2], x[3], 'late by %d' % x[5] if x[4] else ''))


if __name__ == '__main__':
    main()
