"""Render drawn frames with wmodel and compare them with the records of part2.frames that show them; also compare the
transparent object's environment map (letters drawn in with 0x41448a) with the hooked run's dumps (runs/w.wenv).

usage: check_frames.py env [N]           first N env-map dumps
       check_frames.py frames i1 i2 ...  drawn-frame indices (wlog.frames() order); 'every:K' = every K-th frame
"""
import os
import sys

import numpy as np
from PIL import Image

import wdata
import wlog
import wmodel

sys.path.insert(0, wdata.RE)
from sheet import open_frames, indexed  # noqa: E402

D = wdata.load()
F = wlog.frames()
OUT = os.path.join(wlog.HERE, 'runs')


def env_after(fi):
    """the transparent object's env map after drawn frame fi (letters drawn in the last frame that had one behind)"""
    j = fi
    while j >= 0 and F[j]['behind'] == 0:
        j -= 1
    if j < 0 or F[j]['counts'][0] is None:
        return None
    _, env, _ = wmodel.render(D, F[j]['counts'], back=False)
    return env


def check_env(n):
    rec2fi = {}
    for i, f in enumerate(F):
        rec2fi.setdefault(f['flip'], []).append(i)
    done = bad = 0
    for rec, env in wlog.envs():
        if done >= n:
            break
        cand = [i for i in rec2fi.get(rec, []) if F[i]['behind'] > 0]
        if not cand:
            continue
        i = cand[0]
        m = env_after(i)
        d = int((m.reshape(256, 256) != env).sum())
        done += 1
        if d:
            bad += 1
            if bad <= 5:
                print('env map frame', i, 'record', rec, 'differs in', d, 'pixels')
                Image.fromarray(np.hstack([env, m.reshape(256, 256)]) * 1).save(os.path.join(OUT, 'env_%d.png' % i))
    print('env maps checked', done, 'differing', bad)


def check_frames(idx):
    raw, nrec = open_frames(os.path.join(wdata.RE, 'part2.frames'))
    tot = bad = 0
    for i in idx:
        f = F[i]
        if f['counts'][0] is None or not f['shown']:
            continue
        rec = f['shown'][0]
        if rec >= nrec:
            continue
        envs = env_after(i - 1) if i > 0 else None
        if f['behind'] == 0:
            fb, _, _ = wmodel.render(D, f['counts'], env_state=envs)
        else:
            fb, _, _ = wmodel.render(D, f['counts'])
        px, pal = indexed(raw, rec)
        d = int((fb != px).sum())
        tot += 1
        print('frame %d counts %s record %d: %d pixels differ' % (i, f['counts'], rec, d), flush=True)
        if d:
            bad += 1
            lut = (pal.astype(np.uint32) * 255 + 31) // 63
            diff = (fb != px)
            img = np.hstack([lut[px], lut[fb], np.repeat(diff[:, :, None] * 255, 3, 2)]).astype(np.uint8)
            Image.fromarray(img).save(os.path.join(OUT, 'frame_%d.png' % i))
    print('frames checked', tot, 'differing', bad)


if __name__ == '__main__':
    if sys.argv[1] == 'env':
        check_env(int(sys.argv[2]) if len(sys.argv) > 2 else 20)
    else:
        idx = []
        for a in sys.argv[2:]:
            if a.startswith('every:'):
                idx += list(range(1, len(F), int(a[6:])))
            else:
                idx.append(int(a))
        check_frames(idx)
