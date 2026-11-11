import sys
sys.path.insert(0, '/home/dennis/Downloads/acme_303/paper/re')
import rep
ROOTS = {
 'init': [0x5b7d], 'setup': [0x3cb7, 0x3a8c, 0x39a4, 0x1943],
 'title/end-text': [0x2829, 0x27ae],
 'sketch': [0x5e7f, 0x7554, 0x74e8, 0x603f, 0x13af, 0x61ee, 0x6267],
 'scene3d': [0x5eec, 0x7207, 0x726d, 0x72c4, 0x3c3e, 0x73a6, 0x6199, 0x620b],
 'peel': [0x5623, 0x2b55, 0x2b6c, 0x6170],
 'logo': [0x572f, 0x561c],
 'teal': [0x687a, 0x6434, 0x6439],
 'water': [0x24df, 0x26aa],
 'planes': [0x388b, 0x3884],
 'end': [0x13d6, 0x136c],
 'music': [0x1c24, 0x1e45, 0x1c8e],
 'frame': [0x7a64],
}
def body(start):
    seen, todo, calls = set(), [start], set()
    while todo:
        a = todo.pop()
        while a not in seen and 0 <= a < len(rep.SEG):
            seen.add(a)
            ins = next(rep.md.disasm(rep.SEG[a:a+15], a), None)
            if ins is None: break
            mn = ins.mnemonic
            if mn == 'call':
                try: calls.add(int(ins.op_str, 16))
                except ValueError: pass
            elif mn.startswith('j') or mn.startswith('loop'):
                try:
                    t = int(ins.op_str, 16)
                    todo.append(t)
                except ValueError: pass
                if mn == 'jmp': break
            if mn in ('ret', 'retf', 'iretd', 'iret'): break
            a += ins.size
    return seen, calls
cache = {}
def reach(root, acc):
    if root in acc: return
    acc.add(root)
    if root not in cache: cache[root] = body(root)
    for c in cache[root][1]: reach(c, acc)
by = {}
for part, rs in ROOTS.items():
    acc = set()
    for r in rs: reach(r, acc)
    for f in acc: by.setdefault(f, set()).add(part)
callers = rep.call_targets()
for f in sorted(by):
    parts = sorted(by[f])
    n = len(cache[f][0]) if f in cache else 0
    print('%06x  n=%4d  parts=%s  callsites=%s' % (f, n, ','.join(parts), ' '.join('%x' % c for c in callers.get(f, []))))
