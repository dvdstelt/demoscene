import sys
sys.path.insert(0, '/home/dennis/Downloads/acme_303/paper/re')
import rep
def find(hit, v):
    cands = []
    for k in range(1, 11):
        st = hit - k
        ins = next(rep.md.disasm(rep.SEG[st:st+15], st), None)
        if ins and st + ins.size >= hit + 4 and hex(v) in ins.op_str:
            score = (('ptr [' in ins.op_str) * 2 + (st + ins.size == hit + 4))
            cands.append((score, k, ins))
    if not cands: return None
    cands.sort(key=lambda c: (-c[0], c[1]))
    return cands[0][2]
if __name__ == '__main__':
    for arg in sys.argv[1:]:
        v = int(arg, 16)
        print('==', hex(v))
        for hit in rep.xref(v):
            ins = find(hit, v)
            print('  ' + (rep.annotate(ins) if ins else '?? %x' % hit))
