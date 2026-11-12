"""Decode the dissolving part's pictures (files 36..41) the way 0x15500 / 0x15522 does: run list (36, 37, 38: words
skip, count, ..., 0xffff) applied three times to consecutive bytes of the data file (39, 40, 41), giving three
320x200 planes (0..63). Writes an RGB view (planes taken as R, G, B, 6 bit) for inspection."""
import struct, sys
import numpy as np
from PIL import Image
import cap


def decode(runs_file, data_file):
    runs = cap.overlay(runs_file)
    data = cap.overlay(data_file)
    planes = np.zeros((3, 64000), np.uint8)
    p = 0
    for k in range(3):
        o = 0
        i = 0
        while True:
            skip = struct.unpack_from('<H', runs, i)[0]; i += 2
            if skip == 0xffff: break
            o += skip
            cnt = struct.unpack_from('<H', runs, i)[0]; i += 2
            if cnt == 0xffff: break
            planes[k, o:o + cnt] = np.frombuffer(data[p:p + cnt], np.uint8)
            o += cnt; p += cnt
    return planes.reshape(3, 200, 320), p, len(data)


if __name__ == '__main__':
    out = sys.argv[1]
    ims = []
    for r, d in ((36, 39), (37, 40), (38, 41)):
        pl, used, size = decode(r, d)
        print(r, d, 'data used', used, 'of', size, 'plane max', [int(x.max()) for x in pl])
        rgb = np.stack([pl[0], pl[1], pl[2]], -1).astype(np.uint32) * 255 // 63
        ims.append(rgb.astype(np.uint8))
    Image.fromarray(np.vstack(ims)).save(out)
