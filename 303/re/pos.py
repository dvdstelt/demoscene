import sys
rows = {}
for l in open(__file__.rsplit('/',1)[0] + '/rows.txt'):
    o, r, p, t = l.split(); rows[int(p)] = float(t)
ps = sorted(rows)
def ptime(P):
    """music time when counter 2*(o*64+r)-2 first reaches >= P"""
    for p in ps:
        if p - 2 >= P and rows[p] < 244.8: return rows[p]
    return None
def VT(T): return T * 1.00126 - 1.395
if __name__ == '__main__':
    for a in sys.argv[1:]:
        P = int(a, 0); t = ptime(P)
        print(f'{a:>7} = {P:5d}: music {t:8.3f}  video {VT(t):8.3f}')
# NOTE (added later): the capture's picture follows video = 1.00643 * music - 1.953 for exe-scheduled
# events (fitted on palette flashes); 'video' above uses the audio relation and is ~0.25-0.5 s late.
