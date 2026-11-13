"""Check the particle-part model against the capture full.frames, frame by frame.

From a snapshot (taken in the middle of a frame) the emulator first finishes that frame (runs on to 0x400f29, the
call of the frame routine in main). Then, for each following frame of the capture:
  - dt from full.flips' timer column the way main computes it (fpu.t70_dt),
  - the sync events of the part before the first flip whose predecessor was shown at or after their position
    (main checks the table right after showing a frame),
  - the draw target advanced like 0x400000 does (four pages),
  - the part routine run once in unicorn (emu.Emu.call) and the model (parts_model.State) on the same state,
and both pictures are compared with the capture record of a tick at which that flip was on screen.
usage: python3 capture_check.py part frames      (1: t11148, 1a: t11 (first run), 3: t7884, 6: t3925)
"""
import os
import sys

import numpy as np

import emu
import parts_model
from fpu import t70_dt

RE = emu.RE
EVENTS = {   # part: [(order, row, {addr: value})]  values: int or float (event handlers, NOTES.md)
    '1a': [(0x00, 0x20, {0x422500: 0}), (0x04, 0x00, {0x4224f8: 2000, 0x4224fc: 2000, 0x422500: 1}),
           (0x08, 0x00, {0x4224f8: 0, 0x4224fc: 0, 0x422510: 1.0})],
    '1': [(0x3c, 0x00, {0x4224f8: 0, 0x4224fc: 0, 0x422504: 255.0}), (0x3c, 0x10, {0x422508: 1.0})],
    '3': [(0x30, 0x00, {0x422518: 1})],
    '6': [(0x17, 0x20, {0x42252c: 1})],
}
SNAP = {'1': 't11148', '1a': 't11', '3': 't7884', '6': 't3925'}
ROUTINE = {'1': 0x405527, '1a': 0x405527, '3': 0x405c45, '6': 0x406677}
LFB = 0xe0000000


def load_flips():
    rows = []
    for line in open(os.path.join(RE, 'full.flips')):
        if line.startswith('#'): continue
        f = line.split()
        rows.append((int(f[0]), int(f[1]), int(f[3]), int(f[5]), int(f[6]), int(f[7])))
    return rows


def load_visible():
    """flip -> a tick at whose end it was on screen (full.ticks column 9 = flips shown so far)."""
    vis = {}
    for line in open(os.path.join(RE, 'full.ticks')):
        f = line.split()
        if not f[0].isdigit(): continue
        vis.setdefault(int(f[8]), int(f[0]))
    return vis


def capture(tick):
    raw = np.memmap(os.path.join(RE, 'full.frames'), dtype=np.uint8, mode='r')
    rec = 192000
    return np.asarray(raw[32 + tick * rec:32 + (tick + 1) * rec]).reshape(200, 320, 3)


def rgb(page):
    return np.stack([(page >> 16) & 0xff, (page >> 8) & 0xff, page & 0xff], axis=-1).astype(np.uint8)


def main(part, frames):
    e = emu.Emu(SNAP[part])
    e.mu.emu_start(e.mu.reg_read(emu.UC_X86_REG_EIP), 0x400f29)    # finish the frame in progress
    flips = load_flips()
    vis = load_visible()
    import pickle
    st = pickle.load(open(os.path.join(RE, 'snaps', SNAP[part] + '.snap'), 'rb'))['st']
    F = st['flips'] + 1                                            # the flip just drawn
    idx = {r[0]: r for r in flips}
    page = (e.rd32(0xe350ec) - LFB) // 0x3e800
    pending = [ev for ev in EVENTS[part] if (ev[0], ev[1]) > (idx[F][4], idx[F][5])]
    worst = []
    for n in range(frames):
        # show flip F, advance the page; then draw flip F + 1
        page = (page + 1) & 3
        e.wr32(0xe350ec, LFB + page * 0x3e800)
        F += 1
        if F not in idx or idx[F][3] not in (1, 3, 6): break
        (t70, dt), = t70_dt([idx[F - 1][2], idx[F][2]])
        e.wrf(0x420489, dt)
        order, row = idx[F][4], idx[F][5]
        # main checks the sync table right after showing the previous frame: use the position recorded with it
        while pending and (idx[F - 1][4], idx[F - 1][5]) >= (pending[0][0], pending[0][1]):
            for a, v in pending.pop(0)[2].items():
                if isinstance(v, float): e.wrf(a, v)
                else: e.wr32(a, v)
        s = parts_model.State(e)
        tgt_addr = e.rd32(0xe350ec)
        tgt = np.frombuffer(e.rd(tgt_addr, 256000), dtype=np.uint32).reshape(200, 320).copy()
        e.call(ROUTINE[part])
        getattr(s, 'part%s' % part[0])(tgt)
        et = np.frombuffer(e.rd(tgt_addr, 256000), dtype=np.uint32).reshape(200, 320)
        if F not in vis:
            continue                                               # drawn but replaced within the same tick
        cap = capture(vis[F])
        de = np.abs(rgb(et).astype(int) - cap).max(axis=2)
        dm = np.abs(rgb(tgt).astype(int) - cap).max(axis=2)
        worst.append((np.count_nonzero(de), np.count_nonzero(dm)))
        if n % 25 == 0 or np.count_nonzero(de) or np.count_nonzero(dm):
            print('flip %d (tick %d, %02x:%02x) dt %.4f: pixels differing from the capture: emulated routine %d, '
                  'model %d (max %d)' % (F, vis[F], order, row, dt, np.count_nonzero(de), np.count_nonzero(dm),
                                         dm.max()), flush=True)
    print('%d frames compared with the capture; frames with any differing pixel: emulated %d, model %d' % (
        len(worst), sum(1 for a, b in worst if a), sum(1 for a, b in worst if b)))


if __name__ == '__main__':
    main(sys.argv[1], int(sys.argv[2]))
