"""End to end: reproduce captured frames of a 3D entry from the archive's files and the refresh number alone.

The model reads 3D\\<NAME>.WLD / .ANI, GFX\\TEXTURE.RAW (the texture buffer [0x21b140]), GFX\\GOURAUD.PAL,
GFX\\ANTIALIA.PAL and GFX\\CREDITS.RAW, steps the animation once per refresh from the entry's start (the refresh at
which its retrace routine was set, full.log) and, for sampled capture records R, renders the state after n steps
for n = R - start - k (k = 0..5: the frame shown at R was drawn from a state 0 to 3 refreshes old), runs the
entry's 2D passes and reports which k reproduces the record exactly. The outcode bytes 0x21eea8 persist across the
sampled renders, as in the original across frames; decisions that depend on their stale part are counted.

usage: python3 model_part.py ENTRY [--every N] [--limit N]
"""
import argparse
import copy
import os
import sys

import numpy as np

import engine
from tables import raw_picture
from test_frame import CREDITS, sprite

sys.path.insert(0, engine.RE)
from sheet import open_frames, indexed  # noqa: E402

# entry: (name, refresh the retrace routine was set, refresh the next entry starts, loop period of the ANI)
PARTS = {1: ('INTRO', 652, 1327), 2: ('INTRO', 652, 1953), 3: ('INTRO', 652, 2371), 4: ('INTRO', 652, 2736),
         8: ('P3D1', 4097, 6328), 11: ('PART2', 7230, 8119), 14: ('P3D3', 9915, 10806),
         22: ('PART4', 14492, 15377), 26: ('FIN', 16276, 16720)}
FIRST = {1: 652, 2: 1327, 3: 1953, 4: 2371}


class StaleCounter(engine.Renderer):
    """Counts the objects whose culling decision depends on outcode bytes left from earlier objects or frames."""

    def object_setup(self, o):
        self.stale_decides = False
        r = engine.Renderer.object_setup(self, o)
        if self.stale_decides:
            self.stale += 1
            # would the object put pixels on the screen if it were drawn (or not drawn)?
            saved = self.buf
            self.buf = bytearray(64000)
            st = dict(self.stats)
            self.draw_object(o)
            self.stats = st
            if any(self.buf):
                self.stale_visible += 1
            self.buf = saved
        return r


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('entry', type=int)
    ap.add_argument('--every', type=int, default=97)
    ap.add_argument('--limit', type=int, default=1 << 30)
    a = ap.parse_args()
    name, start, end = PARTS[a.entry]
    first = FIRST.get(a.entry, start)
    raw, nrec = open_frames(os.path.join(engine.RE, 'full.frames'))
    world = engine.World(engine.arch_file('3D/%s.WLD' % name))
    an = engine.Anim(engine.arch_file('3D/%s.ANI' % name))
    an.reset(world)
    an.step()                                       # the init's call (key 0)
    tex = raw_picture('GFX/TEXTURE.RAW')[3]
    gour = engine.arch_file('GFX/GOURAUD.PAL')
    T = engine.arch_file('GFX/ANTIALIA.PAL')
    credits = raw_picture('GFX/CREDITS.RAW')[3]
    r = StaleCounter(world, tex, gour, part4=(a.entry == 22))
    r.stale = 0
    r.stale_visible = 0
    states = {0: (list(an.cam), [(list(o.pos16), list(o.ang)) for o in world.objects])}
    for n in range(1, end - start + 1):
        an.step()
        states[n] = (list(an.cam), [(list(o.pos16), list(o.ang)) for o in world.objects])
    res = {}
    count = 0
    for R in range(first + 4, end - 1, a.every):
        if count >= a.limit:
            break
        count += 1
        ref = indexed(raw, R)[0].tobytes()
        prev = indexed(raw, R - 1)[0].tobytes()
        found = None
        imgs = {}
        for k in (0, 1, 2, 3, 4, 5):
            n = R - start - k
            if n not in states:
                continue
            cam, objs = states[n]
            for o, (p, g) in zip(world.objects, objs):
                o.pos16, o.ang = list(p), list(g)

            class A:
                pass
            st = A()
            st.cam = cam
            keep = bytes(r.outcodes)
            buf = r.render(st)
            if a.entry in CREDITS:
                dst = bytearray(64000)
                engine.aa_pass(buf, dst, T)
                off, w, h = CREDITS[a.entry]
                sprite(dst, credits, off, w, h, 231)
                img = bytes(dst)
            elif a.entry == 11:
                img = bytes(buf)
            elif a.entry == 26:
                scr = bytearray(prev)
                engine.fin_pass(buf, scr, T)
                img = bytes(scr)
            else:
                dst = bytearray(64000)
                engine.aa_pass(buf, dst, T)
                img = bytes(dst)
            imgs[k] = img
            if img == ref:
                found = k
                break
            r.outcodes[:] = keep
        if found is None:
            # a torn record: pixels before p from a newer frame, from p on an older one (direct writes to VGA)
            for kn in sorted(imgs):
                first_new = next((i for i in range(64000) if imgs[kn][i] != ref[i]), 64000)
                for ko in sorted(imgs):
                    if ko <= kn:
                        continue
                    last_old = next((i for i in range(63999, -1, -1) if imgs[ko][i] != ref[i]), -1)
                    if last_old < first_new:
                        found = 'torn'
                        print('record %d: torn at row %d between the frames of states %d and %d refreshes old'
                              % (R, first_new // 320, kn, ko))
                        break
                if found:
                    break
            if found:
                res[found] = res.get(found, 0) + 1
                continue
            # the timer interrupt during the render's object loop: camera and objects 0..m-1 from step n,
            # objects m.. from step n + 1
            for k in sorted(imgs):
                if k == 0 or found:
                    continue
                for m in range(1, len(world.objects)):
                    cam = states[R - start - k][0]
                    for i, o in enumerate(world.objects):
                        p, g = states[R - start - (k if i < m else k - 1)][1][i]
                        o.pos16, o.ang = list(p), list(g)
                    st = A()
                    st.cam = cam
                    keep = bytes(r.outcodes)
                    buf = r.render(st)
                    r.outcodes[:] = keep
                    if a.entry in CREDITS or a.entry in (11, 26):
                        break                       # (only the AA entries are searched)
                    dst = bytearray(64000)
                    engine.aa_pass(buf, dst, T)
                    if bytes(dst) == ref:
                        found = 'split'
                        print('record %d: split state: camera and objects 0..%d from %d refreshes old, the rest '
                              'one step newer' % (R, m - 1, k))
                        break
            if found:
                res[found] = res.get(found, 0) + 1
                continue
        res[found] = res.get(found, 0) + 1
        print('record %d: %s' % (R, 'state %d refreshes old' % found if found is not None else 'no exact match'), flush=True)
    print('%s entry %d: %d records, exact matches by age %s; culling decisions depending on stale outcodes: %d, '
          'of which the object would put pixels on the screen: %d' % (name, a.entry, count, res, r.stale,
                                                                       r.stale_visible))


if __name__ == '__main__':
    main()
