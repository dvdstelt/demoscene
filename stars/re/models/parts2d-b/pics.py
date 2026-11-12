"""Decode the dissolving picture's three masked 6:6:6 pictures: span lists (files 36, 37, 38) + plane data (39, 40, 41).
Span list: u16 skip, u16 count, ... (skip is added to the write pointer, then count bytes are copied), 0xffff ends.
The data file holds the R plane's bytes, then G, then B, in span order (same span list for the three planes)."""
import numpy as np
import snap


def decode(span_file, data_file):
    sp = snap.ovl(span_file); dat = snap.ovl(data_file)
    planes = np.zeros((3, 64000), np.uint8); mask = np.zeros(64000, bool)
    pos = 0
    for p in range(3):
        k = 0; o = 0
        while True:
            skip = int.from_bytes(sp[k:k + 2], 'little'); k += 2
            if skip == 0xffff: break
            o += skip
            cnt = int.from_bytes(sp[k:k + 2], 'little'); k += 2
            if cnt == 0xffff: break
            planes[p, o:o + cnt] = np.frombuffer(dat, np.uint8, cnt, pos); pos += cnt
            mask[o:o + cnt] = True; o += cnt
    return planes.reshape(3, 200, 320), mask.reshape(200, 320), pos, len(dat), k, len(sp)


if __name__ == '__main__':
    from PIL import Image
    for a, b in ((36, 39), (37, 40), (38, 41)):
        pl, mk, used, n, k, ns = decode(a, b)
        ys, xs = np.where(mk)
        print(a, b, 'data used', used, 'of', n, 'span bytes', k, 'of', ns, 'pixels', mk.sum(), 'bbox x', xs.min(), xs.max(),
              'y', ys.min(), ys.max(), 'max RGB', pl[0].max(), pl[1].max(), pl[2].max())
        rgb = (np.moveaxis(pl, 0, 2).astype(np.uint16) * 255 // 63).astype(np.uint8)
        Image.fromarray(rgb).resize((640, 480), Image.NEAREST).save('pic%d.png' % a)
