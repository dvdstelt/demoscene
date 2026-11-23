"""Writes src/scenes/intro_frames.h: the capture's frame schedule in the intro (inside/re/notes-intro-logo.md 2.6, 2.7).

    python3 tools/intro_frames.py > src/scenes/intro_frames.h        (inside the toolbox: numpy)
    options: --flips FILE --frames FILE (default ../../re/full.flips, full.frames)

The intro steps its light field once per drawn frame, and its tick routine (once per refresh) and its frame routine
share Watcom's rand(): the drops the capture shows depend on how ticks and frames interleaved. This tool measures
that from the capture and checks it by replaying the whole intro with the model (re/models/intro-logo/intro_model.py)
against the frames of full.frames:

- the record at which the capture shows each intro frame (full.flips: the copies while the frame routine is 0x403748,
  plus the next copy, the intro's last frame shown at the logo's first record, if the replay matches it). Frame j's
  drop loop runs after the tick routine calls of the records before the one that shows frame j - 1 (the frame routine
  does not wait for the copy: it starts frame j right after flipping frame j - 1);
- the frame before whose drop loop the 3:00 event 0x4031a0 ran (after how many ticks), and the ticks that fell inside
  a drop loop (before which drop), found by matching the replay to the capture where the plain schedule does not;
- the number of rand() calls between srand(0x45eafc) (the flight's init) and the intro's first frame (the other
  parts' inits), checked by the first frame.

Every replayed frame (the light through intro.shd over intro.raw) must equal the capture's VGA memory, except the
pixels where intro.raw is 0 (those keep an older black pixel). These are values measured from the capture, not data
of the original program.
"""
import argparse
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
RE = os.path.join(HERE, '..', '..', 're')
sys.path.insert(0, os.path.join(RE, 'models', 'intro-logo'))
import intro_model as M  # noqa: E402

ap = argparse.ArgumentParser()
ap.add_argument('--flips', default=os.path.join(RE, 'full.flips'))
ap.add_argument('--frames', default=os.path.join(RE, 'full.frames'))
a = ap.parse_args()

FRAME_FN, START, SRAND = 0x403748, 38, 0x45eafc     # the intro's frame routine; its first record (script 0:00)
SEED_CALLS = 63                                    # notes-core.md 9 (water, temple, flight inits after srand)
REC, HDR = 64768, 24

# ---------------------------------------------------------------------------------------------------- the capture
flips = []
for line in open(a.flips):
    if line.startswith('#'):
        continue
    f = line.split()
    flips.append((int(f[1]), int(f[7], 16)))         # record, frame routine at the copy
first = next(i for i, f in enumerate(flips) if f[1] == FRAME_FN)
last = max(i for i, f in enumerate(flips) if f[1] == FRAME_FN)
shown = [flips[i][0] for i in range(first, last + 2)]  # + the copy after the routine changed (checked below)
cap = np.memmap(a.frames, dtype=np.uint8, mode='r')
MASK = M.PIC != 0


def captured(rec):
    return np.asarray(cap[HDR + rec * REC:HDR + rec * REC + 64000]).reshape(200, 320)


def matches(L, rec):
    return bool((M.shade(L, np.zeros((200, 320), np.uint8))[MASK] == captured(rec)[MASK]).all())


# ---------------------------------------------------------------------------------------------------- the replay
class State:
    def __init__(self):
        r = M.Rng(1)                                 # the init 0x403770: 130 drops from seed 1, intensity 0
        self.D = []
        for _ in range(130):
            x = float(r() % 320)
            self.D.append([x, float(r() % 200), 0])
        r = M.Rng(SRAND)
        for _ in range(SEED_CALLS):
            r()
        self.rng = r
        self.src = {'X': 200.0, 'Y': 150.0}
        self.count, self.flag, self.ticks = 22, 0, 0
        self.L = np.zeros((200, 320), np.int64)

    def copy(self):
        s = State.__new__(State)
        s.D = [d[:] for d in self.D]
        s.rng = M.Rng(self.rng.seed)
        s.src = dict(self.src)
        s.count, s.flag, s.ticks = self.count, self.flag, self.ticks
        s.L = self.L.copy()
        return s

    def tick(self):
        self.src['X'], self.src['Y'] = M.tick(4 * self.ticks + 2, self.src['X'], self.src['Y'], self.rng)
        self.ticks += 1


def ticks_before(j):
    return 0 if j == 0 else shown[j - 1] - START


def frame(s, j, plan, blur=True):
    """Frame j: the ticks before its drop loop, the 3:00 event, the drop loop (a tick inside it), the blur."""
    ev = plan['event']
    while s.ticks < ticks_before(j):
        if ev and ev[0] == j and s.ticks == ev[1]:
            break
        s.tick()
    if ev and ev[0] == j:
        s.flag = M.event_3_00(s.D, s.rng)
        while s.ticks < ticks_before(j):
            s.tick()
    split = (plan['mid'][j], s.tick) if j in plan['mid'] else None
    s.count = M.drops(s.D, s.L, s.count, s.flag, s.src, s.rng, split)
    if blur:
        M.blur(s.L)


def run(plan, upto, start):
    """Replays frames from a saved state (k, state after k frames) and returns (the first frame that does not match
    the capture, or upto; the states after every 16th frame and after the last 8 frames)."""
    j0, s = start
    s = s.copy()
    states, recent = {}, []
    for j in range(j0, upto):
        frame(s, j, plan)
        if (j + 1) % 16 == 0:
            states[j + 1] = s.copy()
        recent = (recent + [(j + 1, s.copy())])[-8:]
        if not matches(s.L, shown[j]):
            break
    else:
        j = upto
    states.update(recent)
    return j, states


plan = {'event': None, 'mid': {}}
n = len(shown)
saved = {0: State()}
while True:
    base = max(saved)
    bad, states = run(plan, n, (base, saved[base]))
    saved.update(states)
    if bad >= n:
        break
    if bad == n - 1 and len(shown) > len(range(first, last + 1)):
        shown.pop()                                  # the copy after the intro is not an intro frame
        n -= 1
        break
    # explain the mismatch at frame `bad`: the 3:00 event before it (once), or a tick inside a drop loop (its effect
    # can stay hidden for a few frames under light that is already saturated)
    tries = []
    if plan['event'] is None:
        for e in range(bad, max(0, bad - 3) - 1, -1):
            for t in range(ticks_before(e), ticks_before(e - 1) - 1 if e > 0 else -1, -1):
                tries.append(('event', (e, t)))
    for f in range(bad, max(0, bad - 12) - 1, -1):
        if f in plan['mid']:
            continue
        base = max(k for k in saved if k <= f)
        s0 = saved[base].copy()
        for k in range(base, f):
            frame(s0, k, plan)
        seen = set()
        for d in range(s0.count + 1, -1, -1):         # one try per distinct outcome of the drop loop, latest first
            t = s0.copy()
            frame(t, f, {'event': plan['event'], 'mid': {**plan['mid'], f: d}}, blur=False)
            key = (t.rng.seed, t.ticks, t.src['X'], t.src['Y'], str(t.D), t.L.tobytes())
            if key not in seen:
                seen.add(key)
                tries.append(('mid', (f, d)))
    found, changed = None, None
    for kind, v in tries:
        p = {'event': plan['event'], 'mid': dict(plan['mid'])}
        if kind == 'event':
            p['event'] = v
        else:
            p['mid'][v[0]] = v[1]
        f0 = v[0]
        base = max(k for k in saved if k <= f0)
        upto = min(n, bad + 60)
        if run(p, upto, (base, saved[base]))[0] >= upto:
            found, changed = p, f0
            break
    if not found:
        sys.exit('no schedule explains intro frame %d (record %d)' % (bad, shown[bad]))
    plan = found
    saved = {k: v for k, v in saved.items() if k <= changed}
    print('frame %d: %s' % (bad, plan), file=sys.stderr)

print('all %d intro frames equal the capture' % n, file=sys.stderr)

# ---------------------------------------------------------------------------------------------------- the header
steps = [shown[0] - START] + [b - a_ for a_, b in zip(shown, shown[1:])]
assert all(0 < x < 256 for x in steps)
out = []
out.append('// Generated by tools/intro_frames.py from inside/re/full.flips and checked against full.frames: the intro\'s frame')
out.append('// schedule in the capture (inside/re/notes-intro-logo.md 2.6, 2.7). Values measured from the capture: replaying the')
out.append('// light simulation with them gives every one of the capture\'s %d intro frames exactly.' % n)
out.append('#pragma once')
out.append('#include <cstdint>')
out.append('')
out.append('namespace intro_frames {')
out.append('// Frame j is shown from record start + shown_j: kShowSteps[j] = shown_j - shown_(j-1), shown_(-1) = 0 (start =')
out.append('// the intro\'s first record). Its drop loop runs after the tick routine calls of records start .. start +')
out.append('// shown_(j-1) - 1 (none for frame 0).')
out.append('inline constexpr int kFrames = %d;' % n)
out.append('inline constexpr uint8_t kShowSteps[kFrames] = {')
for k in range(0, n, 32):
    out.append('    ' + ', '.join(str(x) for x in steps[k:k + 32]) + ',')
out.append('};')
e = plan['event']
out.append('// The 3:00 event 0x4031a0 runs before frame kEventFrame\'s drop loop, after kEventTicks tick routine calls.')
out.append('inline constexpr int kEventFrame = %d, kEventTicks = %d;' % (e[0], e[1]))
out.append('// Tick routine calls that fell inside a drop loop: the tick runs before drop `drop` of frame `frame` (the latest')
out.append('// drop index that reproduces the capture; frame -1: none).')
out.append('struct MidLoopTick { int frame, drop; };')
mids = sorted(plan['mid'].items()) or [(-1, 0)]
out.append('inline constexpr MidLoopTick kMidLoop[] = {%s};' % ', '.join('{%d, %d}' % m for m in mids))
out.append('// rand() calls between srand(0x45eafc) in the flight\'s init and the intro\'s first frame (the inits of the later')
out.append('// parts run in the precalc; inside/re/notes-core.md 9).')
out.append('inline constexpr int kSeedCalls = %d;' % SEED_CALLS)
out.append('}  // namespace intro_frames')
print('\n'.join(out))
