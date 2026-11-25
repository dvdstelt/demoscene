"""With proper near clipping (depth >= 1), would the faces the original drops (any vertex at depth <= 280) cover
screen pixels? Counts the screen area (pixels) of the dropped, front-facing parts per t."""
import sys
import numpy as np
import render


def clip(poly, inside, inter):
    out = []
    for i in range(len(poly)):
        p, q = poly[i - 1], poly[i]
        if inside(q):
            if not inside(p):
                out.append(inter(p, q))
            out.append(q)
        elif inside(p):
            out.append(inter(p, q))
    return out


def area(poly):
    return 0.5 * sum(poly[i - 1][0] * poly[i][1] - poly[i][0] * poly[i - 1][1] for i in range(len(poly)))


def main():
    ini, t0, t1 = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
    sc = render.Scene(ini)
    tot = 0
    for t in range(t0, t1):
        s = 0
        for o in sc.objs:
            pos, vals = sc.state(o, t)
            v, nrm = render.transform(o, vals)
            X, Y, Z = vals[:3]
            P = np.stack([v[0] + X, v[1] + Y, v[2] + Z + 7000], 1).astype(float)
            if P[:, 2].min() > 280:
                continue
            for fc in o.m3d.groups[0][1]:
                tri = [P[fc[k]] for k in range(3)]
                if min(p[2] for p in tri) > 280:
                    continue
                poly = clip(tri, lambda p: p[2] >= 1.0,
                            lambda p, q: p + (q - p) * ((1.0 - p[2]) / (q[2] - p[2])))
                if len(poly) < 3:
                    continue
                scr = [(160 + 371 * p[0] / p[2], 100 - 309 * p[1] / p[2]) for p in poly]
                if area(scr) >= 0:            # positive shoelace (screen y down) = the original's hidden side
                    continue
                for k, (inside, inter) in enumerate([
                        (lambda p: p[0] >= 0, lambda p, q: (0.0, p[1] + (q[1] - p[1]) * (0 - p[0]) / (q[0] - p[0]))),
                        (lambda p: p[0] <= 320, lambda p, q: (320.0, p[1] + (q[1] - p[1]) * (320 - p[0]) / (q[0] - p[0]))),
                        (lambda p: p[1] >= 0, lambda p, q: (p[0] + (q[0] - p[0]) * (0 - p[1]) / (q[1] - p[1]), 0.0)),
                        (lambda p: p[1] <= 200, lambda p, q: (p[0] + (q[0] - p[0]) * (200 - p[1]) / (q[1] - p[1]), 200.0))]):
                    scr = clip(scr, inside, inter)
                    if len(scr) < 3:
                        break
                if len(scr) >= 3:
                    s += abs(area(scr))
        if s > 0:
            print('t %d: dropped faces would cover %.0f pixels' % (t, s))
        tot += s
    print('total', tot)


if __name__ == "__main__":
    main()
