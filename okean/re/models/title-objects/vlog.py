"""parse vlog files of run_okean.py --vlog: list of (addr, frame, irq, t, {var: value})"""
def parse(path):
    out = []
    for line in open(path):
        f = line.split()
        d = {}
        for kv in f[4:]:
            k, v = kv.split('=')
            d[k] = int(v)
        out.append((f[0], int(f[1][2:]), int(f[2][4:]), float(f[3][2:]), d))
    return out

def draws(ev, start='2e9ca', obj='2e5f5', copy='2eb46', cb='2eb81'):
    """per copy: dict(frame=interval of the copy, draw=index, bg=vars at draw start, objs=[vars per object],
    cb_inside=number of callbacks during the draw, irq at draw start)"""
    res = []
    cur = None
    n = -1
    for a, f, irq, t, d in ev:
        if a == start:
            n += 1
            cur = dict(draw=n, bg=d, objs=[], cb_inside=0, irq0=irq, f0=f)
        elif a == obj and cur is not None:
            cur['objs'].append(dict(d, irq=irq))
        elif a == cb and cur is not None and not cur.get('done'):
            cur['cb_inside'] += 1
        elif a == copy and cur is not None:
            r = dict(cur); r['frame'] = f; r['irq_copy'] = irq
            res.append(r)
            cur['done'] = True
    return res
