"""Part 0xa0ca: show maps 21 and 23 (the two textures), the 8 morph frames, palettes 20 / 22 and background 25."""
import numpy as np
from PIL import Image

import snap


def F(n): return np.frombuffer(open(f'{snap.RE}/unpacked/files/{n:02d}.bin', 'rb').read(), np.uint8)


def pal8(p6): return (p6.astype(np.int32) * 255 // 63).astype(np.uint8)


def morph_frames(a, b):
    """0xa5c5: frame 0 = A, 7 = B, k = 1..6: A + ((sbyte(B - A) * k) >> 3) (16-bit, arithmetic), low byte."""
    A = a[:128, :64].astype(np.int32)
    B = b[:128, :64].astype(np.int32)
    d = ((B - A + 128) & 255) - 128
    fr = [A]
    for k in range(1, 7):
        fr.append(((d * k) >> 3) + A & 255)
    fr.append(B)
    return [f.astype(np.uint8) for f in fr]


if __name__ == '__main__':
    m23 = F(23).reshape(256, 256); m21 = F(21).reshape(256, 256)
    p20 = F(20).reshape(256, 3); p22 = F(22).reshape(256, 3)
    lvl = 20
    # textures at light level 20 with their palettes, scaled up
    def show(m, p): return pal8(p)[m.astype(np.int32) + 8 * lvl]
    Image.fromarray(np.hstack([show(m21, p20), show(m23, p22), show(m21, p22), show(m23, p20)])).save('out/tex_maps_21_23.png')
    fr = morph_frames(m23, m21)
    Image.fromarray(np.hstack([np.kron(f * 32, np.ones((2, 2), np.uint8)) for f in fr])).save('out/tex_morph_frames.png')
    bg = F(25).reshape(200, 320)
    Image.fromarray(pal8(p20)[bg]).resize((640, 400), Image.NEAREST).save('out/bg_25.png')
    # palette ramps: 32 rows (light level) x 8 columns (texel), for both palettes
    ramp = lambda p: np.kron(pal8(p).reshape(32, 8, 3), np.ones((8, 16, 1), np.uint8))
    Image.fromarray(np.hstack([ramp(p20), np.zeros((256, 8, 3), np.uint8), ramp(p22)])).save('out/pal_20_22.png')
    print('unique checks: frames identical to maps?', (fr[0] == m23[:128, :64]).all(), (fr[7] == m21[:128, :64]).all())
    for name, m in (('21', m21), ('23', m23)):
        t = m[:128, :64]
        print(name, 'tile periodic in 256x256?', (np.tile(t, (2, 4)) == m).mean())
