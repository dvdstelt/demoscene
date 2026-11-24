"""Statistics over the parts' frames: the largest summed index per frame (noise + blobs, before any 8-bit wrap),
how many pixels exceed 143 (white in both palettes) or 255 (wrap with carry), and the blob counts.

usage: python3 stats.py 4|6 [--every N]
"""
import argparse

import numpy as np

import common
import hooks


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('part', type=int)
    ap.add_argument('--every', type=int, default=10)
    a = ap.parse_args()
    if a.part == 4:
        import morph as M
        h = hooks.parse('p4_hooks.log')[::a.every]
        seeds, _ = M.noise_seeds(max(x['n'] for x in h) + 1)
    else:
        import star as M
        h = hooks.parse('p6_hooks.log')[::a.every]
        seeds = M.noise_seeds(max(x['n'] for x in h) + 1)
    rec = {}
    orig = common.add_blobs

    def spy(buf, pos, dwords):
        blob = common.blob_rows(dwords)
        acc = np.zeros(64000 + 3000, np.int64)
        if pos:
            p = np.array(pos, np.int64)
            base = p[:, 1] * 320 + p[:, 0]
            for r in range(8):
                for c in range(8):
                    if blob[r, c]:
                        np.add.at(acc, base + r * 320 + c, blob[r, c])
        tot = buf.astype(np.int64) + acc[:64000]
        rec.update(max=int(tot.max()), white=int((tot >= 143).sum()), wrap=int((tot > 255).sum()), blobs=len(pos))
        return orig(buf, pos, dwords)
    M.add_blobs = spy
    for x in h:
        M.frame(x['cbs'], seeds[x['n']])
        print('record %d c %d: max %d, >=143: %d px, >255: %d px, %d blobs' % (
            x['record'], x['cbs'], rec['max'], rec['white'], rec['wrap'], rec['blobs']), flush=True)


if __name__ == '__main__':
    main()
