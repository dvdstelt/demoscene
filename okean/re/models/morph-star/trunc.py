"""How much the slow path's 8-bit step counter (dec cl: n mod 256 blobs) changes the parts' frames: per frame the
lines with more than 255 steps that reach the screen, and the on-screen blobs they lose (or gain) compared with
drawing all n steps.

usage: python3 trunc.py 4|6 [--every N]
"""
import argparse

import common
import hooks


def full_positions(x1, y1, x2, y2, reject_x=False):
    """line_positions without the 8-bit counter (all n steps, each blob checked)"""
    if not (y1 > 0) and y2 < 0:
        return []
    if reject_x:
        if (not (x1 > 0) and x2 < 0) or (x1 >= 0x140 and x2 > 0x140) or (y1 >= 0xc8 and y2 > 0xc8):
            return []
    if common.u32(y1) >= common.u32(y2):
        x1, y1, x2, y2 = x2, y2, x1, y1
    dx, dy = x2 - x1, y2 - y1
    n1 = common.u32(dx + 1 if dx >= 0 else -dx)
    n2 = common.u32(dy + 1 if dy >= 0 else -dy)
    n = max(n1, n2) >> 1
    if n == 0:
        return []
    sx, sy = common.idiv(dx * 256, n), common.idiv(dy * 256, n)
    ex, ey = x1 * 256, y1 * 256
    out = []
    for _ in range(n):
        x, y = ex >> 8, ey >> 8
        if 0 <= y <= 0xbf and 0 <= x <= 0x137:
            out.append((x, y))
        ex += sx
        ey += sy
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('part', type=int)
    ap.add_argument('--every', type=int, default=25)
    a = ap.parse_args()
    if a.part == 4:
        import morph as M
        h = hooks.parse('p4_hooks.log')[::a.every]
    else:
        import star as M
        h = hooks.parse('p6_hooks.log')[::a.every]
    for x in h:
        st = M.state(x['cbs'])
        if a.part == 4:
            pts = M.project(st, M.deform(st))
            edges = M.EDGES[:st['nedge']]
        else:
            pts = M.project(st, M.tube(st))
            edges = M.EDGES
        lost = gained = nlong = 0
        for i, j in edges:
            p = common.line_positions(pts[i][0], pts[i][1], pts[j][0], pts[j][1], reject_x=a.part == 6)
            q = full_positions(pts[i][0], pts[i][1], pts[j][0], pts[j][1], reject_x=a.part == 6)
            if p != q:
                nlong += 1
                lost += len(set(q) - set(p))
                gained += len(set(p) - set(q))
        print('record %d c %d: %d lines truncated, %d on-screen blobs lost, %d gained' % (
            x['record'], x['cbs'], nlong, lost, gained), flush=True)


if __name__ == '__main__':
    main()
