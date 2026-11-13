"""Where each part's clock starts, from the capture (full.flips).

A part advances its state by dt once per frame. Its first frame n (the frame whose event switched to it) is drawn
with dt = t70(n) - t70(n-1), so after frame m its integrated time is t70(m) - t70(n-1): the part's clock starts at
the timer value of the previous part's last frame, which lies before the music position of the event. The offset
between that start and the event's music position (T_event - t70(n-1)) is what a remake that runs the part on
(music time - event time) must add to reproduce the capture.

t70 = timer / 17043 (the program's unit). xmpGetPos reaches position p = order * 64 + row when xmpGetTimer =
p * 66300 - 22100 (speed 3, 22100 PIT clocks per tick; the position is one tick ahead of the timer, see
notes-core.md), so an event at p fires at the first frame whose timer read is at or after that, give or take the
few instructions between the timer read and xmpGetPos.
usage (toolbox or host, from clone/re): python3 models/core/partclock.py
"""
import os

RE = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
flips = []
for line in open(os.path.join(RE, 'full.flips')):
    if line.startswith('#'): continue
    f = line.split()
    flips.append((int(f[0]), int(f[1]), int(f[3]), int(f[5]), int(f[6]), int(f[7])))

events = {1: (0x00, 0x00), 11: (0x08, 0x20), 2: (0x0c, 0), 8: (0x10, 0), 6: (0x14, 0x18), 9: (0x18, 0), 5: (0x20, 0),
          7: (0x24, 0), 3: (0x29, 0), 4: (0x30, 0x20), 10: (0x36, 0), '1b': (0x3a, 0)}
print('part  pos    first flip  dt(first)   t70(n-1)    T_event   offset T_event - t70(n-1)   (t70 units = 1/70.01 s)')
prev = None
seen = set()
check = []
for i, (n, tick, timer, part, order, row) in enumerate(flips):
    if i and timer > -(1 << 39) and flips[i - 1][2] > -(1 << 39):
        # the frame drawn with this timer read the position after it: is (order, row) the position at the timer?
        p = (timer + 22100) // 66300
        check.append((n, p, order * 64 + row))
    key = part if part not in seen else (str(part) + 'b')
    if part != (prev[3] if prev else None) and part != 0:
        seen.add(part)
        o, r = events[key]
        tev = ((o * 64 + r) * 66300 - 22100) / 17043
        t1 = timer / 17043
        t0 = prev[2] / 17043 if prev and prev[2] > -(1 << 39) else 0.0
        dt = t1 - t0 if prev and prev[2] > -(1 << 39) else 6.0
        print(f'{str(key):4s}  {o:02x}:{r:02x}  {n:6d}  {dt:10.4f}  {t0:10.4f}  {tev:10.4f}  {tev - t0:10.4f} '
              f'({(tev - t0) / 70.0101 * 1000:.1f} ms)')
    prev = (n, tick, timer, part, order, row)

# full.flips' order/row is the position when the frame was shown (one frame later than the read), so only count
# frames where the position did not change during the frame
bad = [(n, p, q) for n, p, q in check if p != q]
print(f'position = (timer + 22100) // 66300: {len(check) - len(bad)} of {len(check)} frames agree with the shown '
      f'position; the others: {len(bad)} (frames that cross a row while drawing)')
