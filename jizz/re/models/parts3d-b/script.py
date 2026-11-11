"""Disassembler for Nix's object scripts (interpreter 0x14394, opcode table 0x143af), read from jizz_img.bin.

usage: python3 script.py 0x157fa [0x15973 ...]
A script = float time scale (x 1e-9 -> [0x1579c]), float camera distance ([0x154b4]), then opcodes:
  0  n, centre xyz, R, A1, F1, A2, F2         sphere (0x1488d): 2n x (n+1) vertices, 4n(n-1) faces
  1  t                                        texture index for following faces (face+0xe) [0x1487d]
  2  us, vs (bytes)                           texture repeat factors [0x14885] / [0x14889]
  3                                           end
  4  N, M, P0 xyz, D xyz, r0, r1              tube/cone along D (0x14b8a): N x (M+1) vertices, 2NM faces
  5  obj, dx dy dz                            translate object
  6  obj, k                                   twist: rotate about z by k*y (0x15217)
  7                                           merge the last two objects
  8  obj, a                                   rotate about x by a
  9  obj, a                                   rotate about y by a
  10 obj, a                                   rotate about z by a
  11 obj                                      duplicate object obj (faces get texture [0x1487d])
  12                                          per-object normals etc (0x1a089 for every object)
  13 m                                        face mode (face+0xd) and vertex flag (m>>1 -> vertex+0x3c) [0x14881]
  14 N, M, R, r, A1, F1, A2, F2, T            torus (0x14e7c): N x M vertices, 2NM faces
  15                                          drop every other pair of faces of the last object (0x15174)
"""
import os, struct, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
import rep

def f(a): return rep.rdf(a)

def parse(a):
    ops = []
    ts, dist = f(a), f(a + 4); a += 8
    while True:
        op = rep.SEG[a]; p = rep.SEG[a + 1] if a + 1 < len(rep.SEG) else None
        if op == 0:
            vals = [f(a + 2 + 4 * i) for i in range(8)]
            ops.append((a, 'sphere', dict(n=p, c=vals[0:3], R=vals[3], A1=vals[4], F1=vals[5], A2=vals[6], F2=vals[7]))); a += 34
        elif op == 1: ops.append((a, 'tex', dict(t=p))); a += 2
        elif op == 2: ops.append((a, 'uvscale', dict(us=p, vs=rep.SEG[a + 2]))); a += 3
        elif op == 3: ops.append((a, 'end', {})); break
        elif op == 4:
            q = rep.SEG[a + 2]; vals = [f(a + 3 + 4 * i) for i in range(8)]
            ops.append((a, 'tube', dict(N=p, M=q, P0=vals[0:3], D=vals[3:6], r0=vals[6], r1=vals[7]))); a += 35
        elif op == 5: ops.append((a, 'translate', dict(obj=p, d=[f(a + 2 + 4 * i) for i in range(3)]))); a += 14
        elif op == 6: ops.append((a, 'twist', dict(obj=p, k=f(a + 2)))); a += 6
        elif op == 7: ops.append((a, 'merge', {})); a += 1
        elif op in (8, 9, 10): ops.append((a, 'rot' + 'xyz'[op - 8], dict(obj=p, a=f(a + 2)))); a += 6
        elif op == 11: ops.append((a, 'dup', dict(obj=p))); a += 2
        elif op == 12: ops.append((a, 'normals', {})); a += 1
        elif op == 13: ops.append((a, 'mode', dict(m=p))); a += 2
        elif op == 14:
            q = rep.SEG[a + 2]; vals = [f(a + 3 + 4 * i) for i in range(7)]
            ops.append((a, 'torus', dict(N=p, M=q, R=vals[0], r=vals[1], A1=vals[2], F1=vals[3], A2=vals[4], F2=vals[5], T=vals[6]))); a += 31
        elif op == 15: ops.append((a, 'halve', {})); a += 1
        else: raise ValueError(f'bad op {op} at {a:#x}')
    return ts, dist, ops

if __name__ == '__main__':
    for s in sys.argv[1:]:
        ts, dist, ops = parse(int(s, 16))
        print(f'script {s}: time scale {ts} (x1e-9), distance {dist}')
        for a, name, args in ops:
            print(f'  {a:#07x} {name:9s} ' + ', '.join(f'{k}={[round(x, 6) for x in v] if isinstance(v, list) else (round(v, 7) if isinstance(v, float) else v)}' for k, v in args.items()))
