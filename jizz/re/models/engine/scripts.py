"""Parses the five object scripts that 0x15bb0 hands to the interpreter 0x14394 (run from anywhere).
usage: python3 scripts.py            prints every script with decoded operands"""
import os, sys, struct
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
import rep

SCRIPTS = {0: 0x157a4, 1: 0x157fa, 2: 0x15945, 3: 0x15973, 4: 0x159cb}   # scene index (eax) -> script address
NAMES = {0x157a4: '0x17003 ribbons', 0x157fa: '0x15c8b tube', 0x15945: '0x169e7 tunnel',
         0x15973: '0x1212a/0x15d20 spiky object', 0x159cb: '0x10fda metaballs'}


def f(a): return rep.rdf(a)


def parse(a):
    """yields (address, opcode, mnemonic, operands) until opcode 3 / > 15; header = 2 floats"""
    out = [(a, None, 'header', {'time_scale*1e9': f(a), 'focal': f(a + 4)})]
    p = a + 8
    while True:
        op = rep.SEG[p]
        b = rep.SEG[p + 1]
        if op > 15 or op == 3:
            out.append((p, op, 'end', {})); return out, p + 1
        if op == 0:
            v = [f(p + 2 + 4 * i) for i in range(8)]
            out.append((p, op, 'sphere', dict(M=b, S=2 * b, centre=v[0:3], R=v[3], A_phi=v[4], F_phi=v[5],
                                                   A_theta=v[6], F_theta=v[7]))); p += 2 + 32
        elif op == 1:
            out.append((p, op, 'texture', dict(index=b))); p += 2
        elif op == 2:
            out.append((p, op, 'uvscale', dict(u=b, v=rep.SEG[p + 2]))); p += 3
        elif op == 4:
            m = rep.SEG[p + 2]
            v = [f(p + 3 + 4 * i) for i in range(8)]
            out.append((p, op, 'cylinder', dict(N=b, M=m, start=v[0:3], axis=v[3:6], R0=v[6], R1=v[7]))); p += 3 + 32
        elif op == 5:
            v = [f(p + 2 + 4 * i) for i in range(3)]
            out.append((p, op, 'translate', dict(obj=b, d=v))); p += 2 + 12
        elif op in (6, 8, 9, 10):
            out.append((p, op, {6: 'twist_z_by_y', 8: 'rot_x', 9: 'rot_y', 10: 'rot_z'}[op], dict(obj=b, k=f(p + 2)))); p += 6
        elif op == 7:
            out.append((p, op, 'merge_last_two', {})); p += 1
        elif op == 11:
            out.append((p, op, 'duplicate', dict(obj=b))); p += 2
        elif op == 12:
            out.append((p, op, 'normals', {})); p += 1
        elif op == 13:
            out.append((p, op, 'facetype', dict(type=b))); p += 2
        elif op == 14:
            m = rep.SEG[p + 2]
            v = [f(p + 3 + 4 * i) for i in range(7)]
            out.append((p, op, 'torus', dict(N=b, M=m, R=v[0], r=v[1], A1=v[2], F1=v[3], A2=v[4], F2=v[5],
                                                  twist=v[6]))); p += 3 + 28
        elif op == 15:
            out.append((p, op, 'halve_faces', {})); p += 1


if __name__ == '__main__':
    for s, a in SCRIPTS.items():
        ops, end = parse(a)
        print(f'scene {s}: script {a:#x}..{end:#x} ({end - a} bytes)  {NAMES[a]}')
        for p, op, n, d in ops:
            dd = {k: ([round(x, 4) for x in v] if isinstance(v, list) else (round(v, 6) if isinstance(v, float) else v)) for k, v in d.items()}
            print(f'  {p:#x} {n:16s} {dd}')
