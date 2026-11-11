import sys, pickle, unicorn
sys.path.insert(0, '/home/dennis/Downloads/acme_303/paper/re'); sys.path.insert(0, sys.argv[1])
import rep, m
import numpy as np
from PIL import Image
OUT = sys.argv[1]
A = m.load('r_after_init')
tb = m.d32(A, 0x1eddd)
T = np.array(A[tb:tb+65536]).reshape(256, 256)
for name in ('t7000', 't9500'):
    sn = pickle.load(open(OUT + '/../snaps/%s.snap' % name, 'rb'))
    M = np.frombuffer(sn['mem'], np.uint8)
    p = m.d32(M, 0x1eddd)
    C = np.array(M[0x2b0000 + 0x10000: 0x2b0000 + 0x20000]).reshape(256, 256)
    base = ((T >> 5) & 7) + 0x8b
    d = C.astype(int) - base
    print(name, '[0x1eddd]', hex(p), 'canyon tex range', C.min(), C.max(), 'residual values', sorted(set(d.ravel().tolist())), 'orig tex unchanged', np.array_equal(np.array(M[0x2b0000:0x2c0000]).reshape(256,256), T))
    pal = np.frombuffer(sn['pal'], np.uint8).reshape(256, 3).astype(int) * 255 // 63
    work = np.frombuffer(bytes(M[0x1eaad:0x1eaad+768]), np.uint8).reshape(256, 3).astype(int) * 255 // 63
    Image.fromarray(work[C].astype(np.uint8)).save(OUT + '/canyon_tex_%s.png' % name)
    if name == 't7000':
        print('teal colours 128..159', [tuple(x) for x in np.frombuffer(bytes(M[0x1ec2d:0x1ec2d+96]), np.uint8).reshape(32,3)])
        print('static 0x681a == pal128', bytes(M[0x1ec2d:0x1ec2d+96]) == rep.SEG[0x681a:0x681a+96])
