"""Which word (text entry) the particle parts draw in which frame, from the tick model (as particles_model --ticks)."""
import pickle, sys
import snap
import particles_model as pm

WORDS = {0: 'NOOON', 7680: 'BRING', 15360: 'YOUR', 22720: 'BEAUTY', 29760: 'FAMILY'}
for path, variant, sn in (('runs/p1.pkl', 1, 'f18451_133ad.snap'), ('runs/p2.pkl', 2, 'f21956_133f1.snap')):
    ev = pickle.load(open(path, 'rb'))['events']
    sm = snap.Mem(snap.load(sn)['mem'])
    n, since, timer, idx = sm.u32(0x13044), sm.u32(0x13539), 0x14, 0
    base = 0x13178 if variant == 1 else 0x13278
    runs = []; pos = None
    for e in ev:
        if e[0] == 'T':
            n += 1; since += 1; pos = (e[2], e[3])
            if e[3] in (0, 0x1e): timer = 0x14
        elif e[0] == 'F':
            fr = e[1]
        elif e[0] == 't170' and timer != 0:
            timer -= since
            if timer < 0:
                timer = 0; idx = (idx + 1) % (0x11 if variant == 2 else 0xd)
            since = 0
            w, hh = pm.IMG.u32(base + 16 * idx + 8), pm.IMG.u32(base + 16 * idx + 12)
            word = WORDS.get(pm.IMG.u32(base + 16 * idx + 4), '?') if w > 1 else '-'
            if runs and runs[-1][1] == idx and runs[-1][3] == fr - 1:
                runs[-1][3] = fr
            else:
                runs.append([word, idx, fr, fr, pos])
    print('variant %d (n at the start %d, ticks since the last word %d):' % (variant, sm.u32(0x13044), sm.u32(0x13539)))
    for word, i, a, b, p in runs:
        print('   entry %2d %-6s frames %d..%d (%d)  pos %s' % (i, word, a, b, b - a + 1, '%02x:%02x' % p if p else '-'))
