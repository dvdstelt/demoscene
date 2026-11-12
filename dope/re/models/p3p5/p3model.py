"""p3 frame model: nebula picture + the Phong/bump object (engine_model) + the sliding label, from the camera
object's state. render_frame(o0_bytes, label_pos) -> 64000 bytes (screen rows 0..199)."""
import os, struct
import numpy as np
import engine_model as em

HERE = os.path.dirname(os.path.abspath(__file__))
RE = os.path.join(HERE, '..', '..')
IMG = open(os.path.join(RE, 'unpacked', 'p3.bin'), 'rb').read()
SIN = em.sine_table(IMG, 0x100 + 0x410)
MESH = em.Mesh(IMG, 0x1362)
BUMPTAB = np.array(struct.unpack_from('<256H', IMG, 0x100), np.int64)        # 0010:0000
PHONG = [0x0337, 0x033f, 0x0b37, 0x0b3f]                                     # 0050:0000 (= 0010:0400)
BUMP = [0x2341, 0x2349, 0x2b41, 0x2b41]                                      # 0050:0008 (= 0010:0408)
PIC = np.frombuffer(IMG[0x4bb10:0x4bb10 + 64010], np.uint8)
LABEL = np.frombuffer(IMG[0x33420 + 0x30a:0x33420 + 0x30a + 70 * 44], np.uint8).reshape(44, 70)
IMGA = np.frombuffer(IMG + bytes(0x20000), np.uint8)
OBJ4 = 0x100 + 0x18f8                                                        # the visible object (zoom 2500)


def obj_fields(b):
    w = lambda o: struct.unpack_from('<h', b, o)[0]
    d = lambda o: struct.unpack_from('<i', b, o)[0]
    return dict(seg=struct.unpack_from('<H', b, 0)[0], zoom=w(4), pos=(d(6), d(10), d(14)), ang=(w(0x1e), w(0x20), w(0x22)),
                mat=struct.unpack_from('<H', b, 0x2a)[0], cull=w(0x2c), pivot=(d(0x2e), d(0x32), d(0x36)))


def render_object(fb, cam, obj):
    ax, ay, az = ((obj['ang'][i] + cam['ang'][i]) & 0xffff for i in range(3))
    M = em.matrix(SIN, ax, ay, az)
    pos = tuple(em.s32(obj['pos'][i] + cam['pos'][i]) for i in range(3))
    verts = em.transform(MESH, M, pos, obj['pivot'], obj['zoom'], 'normal')
    faces = em.visible_faces(MESH, verts, obj['cull'])
    madd_lo, madd_hi = obj['mat'] & 0xff, obj['mat'] >> 8
    fillers = {}
    for f in faces:
        mat, ia, ib, ic = (int(v) for v in MESH.F[f])
        ph = PHONG[((mat & 0xff) + madd_lo) & 3] * 16
        bu = BUMP[((mat >> 8) + madd_hi) & 3] * 16
        if (ph, bu) not in fillers:
            def pixel(A, B, n, ph=ph, bu=bu):
                row = (A >> np.uint64(8)) & np.uint64(0x7f)
                col = (A >> np.uint64(24)) & np.uint64(0x7f)
                t = IMGA[bu + (row * np.uint64(256) + col).astype(np.int64)]
                si = BUMPTAB[t]
                bx = (((B >> np.uint64(8)) & np.uint64(0xff)) << np.uint64(8)) | (B >> np.uint64(24))
                return IMGA[ph + ((bx.astype(np.int64) + si) & 0xffff)]
            fillers[(ph, bu)] = em.Filler(IMG, fb, pixel)
        fillers[(ph, bu)].tri(verts[ia >> 2], verts[ib >> 2], verts[ic >> 2])


def render_frame(o0, label):
    fb = np.zeros(65536, np.uint8)
    fb[0x140:0x140 + 64010] = PIC
    cam = obj_fields(bytes(o0))
    obj = obj_fields(IMG[OBJ4:OBJ4 + 0x3a])
    render_object(fb, cam, obj)
    if label >= 0 and label & 0x8000 == 0:
        a = label; x0 = 0
        if a > 0x46:
            x0 = a - 0x46; a = 0x46
        for r in range(44):
            o = 0xb40 + x0 + r * 320
            fb[o:o + a] = LABEL[r, 70 - a:70]
    return fb[0x140:0x140 + 64000].copy()
