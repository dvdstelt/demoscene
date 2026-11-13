"""Part 1: how the emitter phase [0xf5939c] and the particle count advance with the frame step (model only).

Runs the part 1 model (parts_model.State.part1) from snapshot t11 with event 103 at the first frame, at a fixed dt,
for the music time from 00:20 to 04:00 (14.24 - 1.80 s = 871 units of 1/70 s), and prints per time unit: spawn
calls, emitter phase, the target count; and the phase reached. The capture's dt goes from 0.73 to 1.33 there.
usage: python3 rate_study.py dt [dt ...]
"""
import sys

import numpy as np

import emu
from parts_model import State

for dt in map(float, sys.argv[1:] or ['0.75', '1.33', '2.7']):
    e = emu.Emu('t11')
    e.wr32(0x422500, 0)                       # event 103 (growth on)
    s = State(e)
    s.setf(0x420489, dt)
    tgt = np.zeros((200, 320), dtype=np.uint32)
    s.mem[0xeaf410:0xeaf410 + 2 * 64000] = 0
    t, ph0, rows = 0.0, s.f(0xf5939c), []
    while t < 871:
        s.part1(tgt)
        t += dt
        rows.append((t, s.target, s.count, s.f(0xf5939c)))
    ph = s.f(0xf5939c)
    t5 = next((r[0] for r in rows if r[1] >= 5000), None)
    late = [r for r in rows if r[0] > 600]
    rate = (late[-1][3] - late[0][3]) / (late[-1][0] - late[0][0])
    print('dt %.2f (%.1f fps): %d frames; target reaches 5000 after %s units; emitter phase %.0f at 04:00; '
          'phase rate with 5000 particles %.1f per unit (x turns %.3f rad/s)' % (
              dt, 70 / dt, len(rows), '%.0f' % t5 if t5 else 'never', ph - ph0, rate, rate * 0.002 * 70), flush=True)
