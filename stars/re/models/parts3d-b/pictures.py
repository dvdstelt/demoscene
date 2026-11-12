"""Write the pictures and maps of parts 0x164dc and 0x16f7c as PNGs (with the palettes they are shown with)."""
import numpy as np
from PIL import Image

import snap


def F(n): return np.frombuffer(open(f'{snap.RE}/unpacked/files/{n:02d}.bin', 'rb').read(), np.uint8)


def pal8(p6): return (np.asarray(p6, np.int32).reshape(256, 3) * 255 // 63).astype(np.uint8)


if __name__ == '__main__':
    I = snap.IMG
    p_star = np.frombuffer(I, np.uint8, 768, 0x15e60)
    f49 = np.concatenate([F(49), [0]]).reshape(256, 256)
    Image.fromarray(pal8(p_star)[f49]).resize((512, 512), Image.NEAREST).save('out/env49_star.png')
    p42 = F(42)
    f29 = F(29); f44 = F(44); f47 = F(47); f43 = F(43)
    print('29', len(f29), f29[:65536].min(), f29[:65536].max(), 'tail', f29[65536:65536 + 16].tolist())
    print('44', len(f44), f44[:65536].min(), f44[:65536].max(), 'tail', f44[65536:65536 + 16].tolist())
    print('47', len(f47), f47.min(), f47.max(), np.unique(f47)[:20])
    print('43==21', (f43 == F(21)).all())
    env29 = ((f29[:65536] >> 1) + 0x60).reshape(256, 256)
    Image.fromarray(pal8(p42)[env29]).resize((512, 512), Image.NEAREST).save('out/env29_ring.png')
    Image.fromarray(pal8(p42)[f44[:65536].reshape(256, 256)]).resize((512, 512), Image.NEAREST).save('out/env44_mirror.png')
    bg = (f47.astype(np.int32) + 0xe6) & 255
    bg = bg.reshape(200, 320); bg[0] = 0
    Image.fromarray(pal8(p42)[bg]).resize((640, 400), Image.NEAREST).save('out/bg47_torso.png')
    ramp = np.kron(pal8(p42).reshape(16, 16, 3), np.ones((16, 16, 1), np.uint8))
    Image.fromarray(ramp).save('out/pal42.png')
    ramp = np.kron(pal8(p_star).reshape(16, 16, 3), np.ones((16, 16, 1), np.uint8))
    Image.fromarray(ramp).save('out/pal_star_15e60.png')
