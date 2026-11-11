import sys, pickle
sys.path.insert(0, '.')
from model import *
for f in sys.argv[1:]:
    cap = pickle.load(open(f, 'rb'))
    R = Renderer(cap['memA']); M = R.M
    vga = bytearray(cap['memA'][0xa0000:0xa0000 + 64000])
    out = peel2b6c(R, vga)
    real = cap['memB'][0xa0000:0xa0000 + 64000]
    d = [i for i in range(64000) if out[i] != real[i]]
    print(f, 'tick', cap['tick'], 'p', M.i32(0x1eaa9), 'diff', len(d), [(i % 320, i // 320, out[i], real[i]) for i in d[:6]])
    open('p_model.raw', 'wb').write(bytes(out)); open('p_real.raw', 'wb').write(bytes(real))
