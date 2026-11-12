"""Run the engine model on a dump from run_engine.py --engine-dump and compare the screen buffer with the original.

usage: python3 test_dump.py DUMP.npz [...]   (prints differing pixels; writes DUMP_cmp.png next to it with -p)
"""
import sys
import numpy as np
import engine


def run(path, png=False):
    d = np.load(path)
    pre = bytearray(d['pre'].tobytes())
    post = d['post'].tobytes()
    regs = d['regs']
    entry = int(d['entry'])
    m = engine.Mem(pre)
    scr = m.selbase(m.u16(0x558d0))
    engine.USED.clear()
    if entry == 0x79483:
        engine.draw_list(m, int(regs[4]))
    else:
        engine.draw_object(m, int(regs[6]))       # ebp = object
    n = 65536 if any(not k[2] for k in engine.USED) else 64000
    a = np.frombuffer(bytes(m.b[scr:scr + n]), np.uint8).reshape(-1, 320 if n == 64000 else 256)
    b = np.frombuffer(post[scr:scr + n], np.uint8).reshape(a.shape)
    p0 = np.frombuffer(d['pre'].tobytes()[scr:scr + n], np.uint8).reshape(a.shape)
    diff = a != b
    print('%s: frame %d entry %x, %d pixels drawn by the original, %d differ; faces drawn %s' % (
        path.split('/')[-1], int(d['frame']), entry, int((b != p0).sum()), int(diff.sum()),
        ', '.join('mode %d %s%s: %d' % (k[0], k[1], '' if k[2] else ' (256 wide)', v)
                  for k, v in sorted(engine.USED.items()))))
    if diff.any():
        ys, xs = np.nonzero(diff)
        print('   first diffs:', [(int(x), int(y), int(a[y, x]), int(b[y, x])) for y, x in zip(ys[:10], xs[:10])])
    if png:
        from PIL import Image
        pal = np.frombuffer(d['post'].tobytes()[0:0], np.uint8)
        img = np.concatenate([b, a, np.where(diff, 255, 0).astype(np.uint8)], axis=1)
        Image.fromarray(img).save(path.replace('.npz', '_cmp.png'))
    return int(diff.sum())


if __name__ == '__main__':
    png = '-p' in sys.argv
    for p in sys.argv[1:]:
        if p != '-p':
            run(p, png)
