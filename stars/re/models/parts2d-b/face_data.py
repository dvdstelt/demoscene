"""Face part data: file 34 (face spans with per-pixel texture coordinates), file 35 (palette, 64K texture, 32 remap
tables). decode34() mirrors 0x11a0a: per span: u16 ?, u16 skip, u16 count, u16 ?, then count dwords d; per pixel
  a = sar8((d >> 1) & 0xff, 1), b = sar8((d >> 12) & 0xff, 1)       (bits 1..8 and 12..19 of d, signed, halved)
  word = (a + 0x80) << 8 | (b + 0x80 + (a < 0))   (16-bit add of 0x8080: al's carry goes into ah; then xchg)
  -> texture offset; the list ends when the running pixel position reaches 0xf8c0."""
import numpy as np
import snap


def s8(v): return v - 256 if v >= 128 else v


def decode34():
    f = snap.ovl(34)
    spans = []           # (skip, count, [words])
    k = 0; bp = 0
    while True:
        skip = int.from_bytes(f[k + 2:k + 4], 'little'); cnt = int.from_bytes(f[k + 4:k + 6], 'little')
        hdr = (int.from_bytes(f[k:k + 2], 'little'), int.from_bytes(f[k + 6:k + 8], 'little'))
        k += 8; bp = (bp + skip) & 0xffff
        words = []
        for i in range(cnt):
            d = int.from_bytes(f[k:k + 4], 'little'); k += 4
            lo = s8((d >> 1) & 0xff) >> 1               # sar al, 1
            hi = s8((d >> 12) & 0xff) >> 1              # sar ah, 1
            w = ((hi & 0xff) << 8 | (lo & 0xff)) + 0x8080 & 0xffff     # add ax, 0x8080: al's carry goes into ah
            words.append((w & 0xff) << 8 | w >> 8)      # xchg ah, al
            bp = (bp + 1) & 0xffff
        spans.append((skip, cnt, words, hdr))
        if bp >= 0xf8c0: break
    return spans, k, len(f)


def file35():
    f = snap.ovl(35)
    pal = np.frombuffer(f, np.uint8, 0x300, 0).reshape(256, 3)
    tables = np.frombuffer(f, np.uint8, 0x2000, 0x102ff).reshape(32, 256)
    return f, pal, tables


if __name__ == '__main__':
    from PIL import Image
    spans, used, n = decode34()
    print('spans', len(spans), 'bytes used', used, 'of', n, 'headers', set(s[3] for s in spans))
    U = np.zeros(64000, np.int32) - 1; V = np.zeros(64000, np.int32) - 1; pos = 0
    for skip, cnt, words, hdr in spans:
        pos += skip
        for w in words:
            U[pos] = w >> 8; V[pos] = w & 0xff; pos += 1
    print('end pos', pos)
    mk = U >= 0
    ys, xs = np.where(mk.reshape(200, 320)); print('face bbox x', xs.min(), xs.max(), 'y', ys.min(), ys.max(), 'pixels', mk.sum())
    print('U range', U[mk].min(), U[mk].max(), 'V range', V[mk].min(), V[mk].max())
    im = np.zeros((200, 320, 3), np.uint8)
    im[..., 0] = np.where(mk, U, 0).reshape(200, 320); im[..., 1] = np.where(mk, V, 0).reshape(200, 320)
    Image.fromarray(im).resize((640, 400), Image.NEAREST).save('face_uv.png')
    f, pal, tables = file35()
    tex = np.frombuffer(f, np.uint8, 0x10000 - 0x300 + 0x300, 0).copy()
    # texture as the program reads it: byte at file35 + off, off = 0x1800..0x2800 + 0x4000..0xbfff
    t = np.frombuffer(f[:0x10000], np.uint8).reshape(256, 256)
    p8 = (pal.astype(np.uint16) * 255 // 63).astype(np.uint8)
    Image.fromarray(p8[t]).save('face_tex.png')
    print('pal', pal[::16].tolist())
    for i in range(32):
        tb = tables[i]
        print(i, 'fixed', int((tb == np.arange(256)).sum()), tb[:16].tolist(), tb[240:].tolist())
