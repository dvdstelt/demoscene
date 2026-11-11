import glob
rows = {}
for f in sorted(glob.glob('fade/*.fade')):
    for line in open(f):
        t, lv, tg, st, cnt = map(int, line.split())
        if t in rows and rows[t] != (lv, tg, st, cnt):
            print('mismatch at', t, rows[t], (lv, tg, st, cnt), f)
        rows[t] = (lv, tg, st, cnt)
T = max(rows)
print('ticks', min(rows), T, 'missing', [t for t in range(0, T+1) if t not in rows][:10])
# events: target/step changes, and level changes not explained by the IRQ step
prev = (0, 0, 0, 0)
for t in range(0, T+1):
    lv, tg, st, cnt = rows[t]
    plv, ptg, pst, _ = prev
    # IRQ model: level moves by st towards tg
    if plv < tg: e = min(plv + st, tg)
    elif plv > tg: e = max(plv - st, tg)
    else: e = plv
    if tg != ptg or st != pst or e != lv:
        print('t %5d cnt %5d level %6d target %6d step %5d   (prev level %6d target %6d step %5d) model %6d %s' % (t, cnt, lv, tg, st, plv, ptg, pst, e, '' if e == lv else 'LEVEL SET'))
    prev = rows[t]
import json
json.dump({t: rows[t] for t in rows}, open('fade/all.json', 'w'))
