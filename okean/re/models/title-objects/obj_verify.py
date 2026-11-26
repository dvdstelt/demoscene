"""verify the objects model against the emulator log (vlog2.txt from run_okean.py --vlog) and the capture.
For every copy: step the callback model with the positions the callbacks saw, compare every object's variables
with the log, render the frame with the draw's seed and compare with capture record = interval of the copy.
usage: obj_verify.py [every] [first_interval] [last_interval]"""
import sys, copy, pickle
from vlog import *
from objects import *
m_dump = lambda f: True
every = int(sys.argv[1]) if len(sys.argv) > 1 else 1
f0 = int(sys.argv[2]) if len(sys.argv) > 2 else 0
f1 = int(sys.argv[3]) if len(sys.argv) > 3 else 99999
ev = parse('vlog2.txt')
s = snap('2e779')
st = init_from_snapshot(s)
seeds = seed_list(1700, mi32(s, 0x2c9b4) & M32)
cbs = [(d['b12b75'], d['w12b73']) for a, f, irq, t, d in ev if a == '2eb81']
ds = draws(ev)
names = {'2cabc': 'angA', '2c9bc': 'angB', '2cab4': 'angC', '2ca98': 'angD', '2ca94': 'xoff', '2c9c0': 'xoff2', '2ca90': 'zdist',
         'b2e376': 'shift'}
done = 0
var_bad = 0
nbad = 0
results = []
ncarry = 0
carried_frames = []
for r in ds:
    c0 = r['irq0'] - 617
    while done < c0:
        callback(st, *cbs[done]); done += 1
    order = r['bg']['b12b75']
    if order < 3 and len(r['objs']) == 5:     # the order changed while the background was drawn (read at 0x2ea68)
        order = 3
    # check state
    sd = copy.deepcopy(st)
    assert sd['bg2'] == r['bg']['2cac4'], (r['frame'], sd['bg2'], r['bg'])
    # replicate the main loop's per-object variables
    objs_model = []
    if order < 3:
        sd['angC'] = sd['angC0']
        for k in range(8):
            objs_model.append(dict(sd)); sd['angC'] = (sd['angC'] + 0x1ff) & 0xfff
    else:
        sd['xoff2'] = s32(sd['tubeX'] - 0x5000)
        for k in range(5):
            objs_model.append(dict(sd)); sd['xoff2'] = s32(sd['xoff2'] + 0x2000)
    for om, ol in zip(objs_model, r['objs']):
        for a, n in names.items():
            if om[n] != ol[a]:
                var_bad += 1
                if var_bad < 10:
                    print('var mismatch', r['frame'], n, om[n], ol[a])
    if len(objs_model) != len(r['objs']):
        print('object count', r['frame'], len(objs_model), len(r['objs']))
    f = r['frame']
    if order < 3:
        st['angC'] = (st['angC0'] + 8 * 0x1ff) & 0xfff
    else:
        st['xoff2'] = s32(st['tubeX'] - 0x5000 + 5 * 0x2000)
    if f0 <= f <= f1 and (f - f0) % every == 0:
        img, _, carried = draw_exact(st, order, seeds[r['draw']])
        cidx, cpal = cap_frame(f)
        if len(sys.argv) > 4 and m_dump(f):
            np.save('diff_%d.npy' % f, np.stack([img, cidx]))
        m = (img == cidx).mean()
        # the palette the main loop writes after the retrace of interval f (order >= 4): weights after that
        # interval's callback (callback number c0 + 1)
        sp = copy.deepcopy(st)
        for j in range(done, min(r['irq_copy'] - 617, len(cbs))):   # the callbacks before the palette write
            callback(sp, *cbs[j])
        if r['bg']['b12b75'] >= 4 or sp['palw1']:
            pal_ok = (palette(sp) == cpal.reshape(-1)).all()
        else:
            pal_ok = (P0.astype(np.uint8) == cpal.reshape(-1)).all()
        if not pal_ok:
            print('palette differs at', f, sp['palw0'], sp['palw1'])
        results.append((f, m))
        ncarry += carried
        if carried:
            carried_frames.append(f)
        if m < 1:
            nbad += 1
            print('frame', f, 'draw', r['draw'], 'c', c0, 'order', order, 'match %.6f' % m, flush=True)
print('frames with dword carries', ncarry, carried_frames)
print('variables mismatches', var_bad, '; frames rendered', len(results), 'not bit-exact', nbad)
