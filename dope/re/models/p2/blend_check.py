"""Check the p2 frame pipeline (0010:360c) against the dumps written by p2trace.py.

For each out/runs/p2_dump_<n>.npz: rebuild the 256x256 table from the formulas of 1998 / 1967 (and the clear at
3a5d once the step 22 has run), check it equals the table in memory (3527:0000); apply strip + blend + label to
the dumped buffers and check the result equals 5517 after 360c; check the capture frame shows rows 1..200 of it.
"""
import glob, os, sys
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', '..'))
import sheet

img = open(os.path.join(HERE, '../../unpacked/p2.bin'), 'rb').read()


def table(s, cleared):
    T = np.zeros((256, 256), np.int64)                 # T[old][new]; 3527 starts as zeros
    y = np.arange(64)[:, None]; x = np.arange(128)[None, :]
    v = 0x80 | ((((x * 0x60 + y * 0x1a0) >> 1) + ((x * 0x60 + y * 0x1a0) & 1)) >> 8)    # 1998
    T[0:64, 128:256] = v; T[128:192, 0:128] = v; T[128:192, 128:256] = v
    y = np.arange(128)[:, None]; x = np.arange(128)[None, :]
    T[0:128, 0:128] = ((x * s + y * (256 - s)) >> 8) & 0xff                             # 1967
    if cleared:                                                                         # 3a5d
        T[0, 0x80] = 0; T[0x80:, :] = 0
    return T.astype(np.uint8)


def pipeline(old, new, T, strip, row, label):
    new = new.copy()
    if strip != -2:
        r = slice(row, row + 0x2800)
        if strip == -1: new[r] = 0
        elif strip == 0: new[r] = 0x80
        else:
            o = 0x4510 + ((strip * 0x2800 + 0xdb0a) & 0xffff)
            new[r] = np.frombuffer(img[o:o + 0x2800], np.uint8)
    out = old.copy()
    out[:64000] = T[old[:64000], new[:64000]]
    if label >= 0:
        lab = np.frombuffer(img[0x2bd20 + 0x30a:0x2bd20 + 0x30a + 70 * 44], np.uint8).reshape(44, 70)
        ax, di = label, 0xb40
        if ax > 70: di += ax - 70; ax = 70
        for k in range(44):
            out[di + k * 320: di + k * 320 + ax] = lab[k, 70 - ax:]
    return out


def label_at_copy(drawn):
    """[1955] when 360C reads it: after the last retrace before the copy (C line) of this drawn frame"""
    log = os.path.join(HERE, 'out', 'runs', 'p2.p2log')
    cret = None
    for line in open(log):
        f = line.split()
        if f[0] == 'C' and int(f[3]) == drawn:
            cret = int(f[1]); break
    for line in open(log):
        f = line.split()
        if f[0] == 'R' and int(f[1]) == cret + 1:
            return int(f[16])
    return None


def main():
    raw, W, H, rec, hdr, n = sheet.open_frames(os.path.join(HERE, '../../full.frames'))
    for p in sorted(glob.glob(os.path.join(HERE, 'out', 'runs', 'p2_dump_*.npz')), key=lambda s: int(s.split('_')[-1][:-4])):
        d = np.load(p)
        s, ret = int(d['s']), int(d['ret'])
        T = table(s, ret > 2863)
        tm = d['table'].reshape(256, 256)
        nd = int((T != tm).sum())
        lab = label_at_copy(int(d['drawn']))
        out = pipeline(d['old'], d['new'], tm, int(d['strip']), int(d['row']), lab)
        nb = int((out != d['out']).sum())
        # capture: the copy happens at copy_frame; find the capture frame equal to out rows 1..200
        want = d['out'][0x140:0x140 + 64000].reshape(200, 320)
        hit = None
        for f in range(int(d['copy_frame']) - 1, int(d['copy_frame']) + 3):
            o = hdr + f * rec + 776
            idx = np.asarray(raw[o:o + W * H]).reshape(H, W)[:200, :320]
            if (idx == want).all():
                hit = f; break
        print('%s ret %d s %d strip %d label %d: table diffs %d, pipeline diffs %d, capture frame %s (copy at %d)' % (
            os.path.basename(p), ret, s, int(d['strip']), lab, nd, nb, hit, int(d['copy_frame'])))


if __name__ == '__main__':
    main()
