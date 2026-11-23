"""Check the ANI model (Anim.step) against dumps: for each dump, find the number of per-refresh steps since the
part's init after which the model's camera (integer and fraction) and every object's position and angles equal the
dump's, and print it next to the dump's refresh.

usage: python3 test_anim.py DUMPDIR NAME [--max N]
"""
import argparse
import glob
import os

import dumps
import engine


def snapshot(an, world):
    return (tuple(an.cam), tuple(an.camf), tuple(tuple(o.pos16) + tuple(o.ang) for o in world.objects))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('folder')
    ap.add_argument('name')
    ap.add_argument('--max', type=int, default=20000)
    ap.add_argument('--addr', default='25f5b5')
    ap.add_argument('--snaps', default='', help='snapshot files to check too (comma-separated)')
    a = ap.parse_args()
    world = engine.World(engine.arch_file('3D/%s.WLD' % a.name))
    an = engine.Anim(engine.arch_file('3D/%s.ANI' % a.name))
    an.reset(world)
    an.step()
    states = {snapshot(an, world): 0}
    seq = [snapshot(an, world)]
    for n in range(1, a.max):
        an.step()
        s = snapshot(an, world)
        seq.append(s)
        states.setdefault(s, n)
    for p in sorted(glob.glob(os.path.join(a.folder, 'd*_%s_*.dump' % a.addr))) + [x for x in a.snaps.split(',') if x]:
        d = dumps.Dump(p)
        w2 = engine.World.__new__(engine.World)
        w2.objects = world.objects
        base = d.u32(0x217460)
        objs = tuple(tuple(d.s32(base + 0x12c * i + 0x1c + 4 * k) for k in range(3)) +
                     tuple(d.s32(base + 0x12c * i + 0x10 + 4 * k) for k in range(3)) for i in range(len(world.objects)))
        cam, camf = dumps.state(d, world)
        s = (tuple(cam), tuple(camf), objs)
        n = states.get(s)
        print('%s: refresh %d, key %d count %d -> steps %s' % (os.path.basename(p), d.frame, d.u32(0x21746c),
                                                             d.u8(0x217470), n))


if __name__ == '__main__':
    main()
