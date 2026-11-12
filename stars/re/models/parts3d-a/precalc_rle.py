"""Decode the 64 sprite frames that the precalc 0x16945 stores (RLE written by 0x16b1e, played by 0x16acc).

Reads them from a snapshot taken after the precalc, draws frames as images with the chrome star part's palette
(from full.frames), and checks the playback against full.frames (the row of small shapes along the bottom).
"""
import struct
import numpy as np
from PIL import Image
from common import out, snap, ru32, frame, save_png


def frames_from(mem):
    out = []
    for f in range(64):
        p = ru32(mem, 0x163ba + 4 * f)
        img = np.zeros(64000, np.int16) - 1            # -1 = transparent
        pos = 0x6180
        while True:
            skip = struct.unpack_from('<H', mem, p)[0]; p += 2
            if skip == 0xffff: break
            pos += skip
            n = struct.unpack_from('<H', mem, p)[0]; p += 2
            if n == 0xffff: break
            img[pos:pos + n] = np.frombuffer(mem[p:p + n], np.uint8); p += n; pos += n
        out.append(img.reshape(200, 320))
    return out, ru32(mem, 0x163ba + 63 * 4), p


if __name__ == '__main__':
    s = snap('f235_5b84.snap')
    m = s['mem']
    fr, last, end = frames_from(m)
    base = ru32(m, 0x163b2)
    print('block', hex(base), 'used bytes', end - base, 'frame offsets', [ru32(m, 0x163ba + 4 * f) - base for f in range(0, 64, 8)])
    pix, pal = frame(17000)
    sheet = np.zeros((4 * 122, 8 * 320), np.uint8)
    for f in range(32):
        im = fr[f][78:200].copy(); im[im < 0] = 0
        r, c = divmod(f, 8)
        sheet[r * 122:(r + 1) * 122, c * 320:(c + 1) * 320] = im
    save_png(out('precalc_frames_0_31.png'), sheet, pal)
    # are frames f and f+32 equal?
    for f in (0, 1, 5, 31):
        print('frame', f, 'vs', f + 32, 'differing pixels', int((fr[f] != fr[f + 32]).sum()))
    np.save(out('precalc_frames.npy'), np.array(fr))
