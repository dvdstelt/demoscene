import sys, pickle, struct, time
sys.path.insert(0, '.')
import model
cap = pickle.load(open(sys.argv[1], 'rb'))
memA, memB = cap['memA'], cap['memB']
R = model.Renderer(memA)
t = time.time()
buf = R.scene(int(sys.argv[2]) if len(sys.argv) > 2 else 0)
tex = R.M.u32(0x1edf5)
out = R.combine(buf, tex)
back = R.back
real = memB[back:back + 64000]
diff = [i for i in range(64000) if out[i] != real[i]]
print('tick', cap['tick'], 'time %.1f' % (time.time() - t), 'diff pixels', len(diff), 'oob', R.oob)
if diff:
    print([(i % 320, i // 320, out[i], real[i]) for i in diff[:20]])
open('model_out.raw', 'wb').write(bytes(out))
open('real_out.raw', 'wb').write(bytes(real))
