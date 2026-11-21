"""When each row of EYE.AMF is read, in DSMI time, timer interrupts, refreshes and seconds (notes-music.md section 8).

Closed form (checked against the player model, which matches full.gus / full.ticks):
  player tick n (n = 1, 2, ...) is due at DSMI time T(n) = 200 n for n <= 6 (tempo 125 until the first row),
  T(n) = 1200 + 192 (n - 6) after it (tempo 130: period 10000 // (130 * 50 // 125) = 192);
  it runs in the k-th music callback, k = T(n) // 35 + 1 (the first k with 35 k > T(n));
  callback k is timer interrupt 90 + k in the capture (91 + k from k = 12027 on: interrupt 12117 lost its callback);
  interrupt i = 4 m + j (j = 1..4) is at refresh 15 + m + (4085 j - 37 * 449^-1 * FRAME) / FRAME for j <= 3 and at
  16 + m - 37 / 449 for j = 4 (refresh f = record f of full.frames, line 0).
  Song row R (= 64 order + row) is read at tick 6 + 4 R (R < 2304, orders 0..35 at speed 4) and
  6 + 4 * 2304 + 12 (R - 2304) in order 36 (speed 12).
usage: python3 timeline.py [--rows] [--check]   (--check runs the model and compares every tick)
"""
import argparse
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

PIT_HZ = 1193182
VGA_HZ = 25175000 / (800 * 449)
FRAME = PIT_HZ / VGA_HZ


def tick_time(n):
    return 200 * n if n <= 6 else 1200 + 192 * (n - 6)


def callback(n):
    return tick_time(n) // 35 + 1


def irq_of_callback(k, first=91, lost=(12117,)):
    i = first - 1 + k
    for x in sorted(lost):
        if i >= x:
            i += 1
    return i


def refresh_of_irq(i, first_record=15):
    m, j = divmod(i - 1, 4)
    j += 1
    if j == 4:
        return first_record + 1 + m - 37 / 449
    return first_record + m + (4085 * j - 37 * FRAME / 449) / FRAME


def row_tick(R):
    return 6 + 4 * R if R < 2304 else 6 + 4 * 2304 + 12 * (R - 2304)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--rows', action='store_true')
    ap.add_argument('--check', action='store_true')
    a = ap.parse_args()
    if a.check:
        import amf
        import dsmi
        m = amf.AMF(os.path.join(dsmi.ORIG, 'EYE.AMF'))
        ds = dsmi.Dsmi(m)
        ds.start()
        got = []
        orig = ds.amp.tick

        def tick():
            orig()
            got.append(i)
        ds.amp.tick = tick
        for i in range(91, 52263):
            if i != 12117:
                ds.interrupt()
        bad = [n for n, i in enumerate(got, 1) if irq_of_callback(callback(n)) != i]
        print('%d ticks, %d differ from the closed form' % (len(got), len(bad)))
    print('order  tick   DSMI time  interrupt  refresh    seconds (record 0 = 0 s)')
    for R in range(0, 37 * 64, 1 if a.rows else 64):
        n = row_tick(R)
        i = irq_of_callback(callback(n))
        f = refresh_of_irq(i)
        print('%2d:%02d %5d %10d %9d %10.4f %9.4f' % (R // 64, R % 64, n, tick_time(n), i, f, f / VGA_HZ))


if __name__ == '__main__':
    main()
