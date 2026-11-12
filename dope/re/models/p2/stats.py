"""Step starts (measured vs nominal) and drawn frames per second per phase, from out/runs/p2.p2log."""
import os, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import script

log = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, 'out', 'runs', 'p2.p2log')
S, F, Rf = [], [], {}
for line in open(log):
    f = line.split()
    if f[0] == 'S': S.append((int(f[1]), int(f[2]), int(f[3]), int(f[4])))
    elif f[0] == 'F': F.append((int(f[1]), int(f[2])))
    elif f[0] == 'R': Rf[int(f[1])] = int(f[2])
steps = script.parse()
nom, acc = [], 0
for a, n, w in steps:
    nom.append(acc); acc += n
print('retrace 1 at capture frame', Rf.get(1), '; last logged retrace', max(Rf), 'at frame', Rf[max(Rf)])
late = [(i, s[2] - nom[i]) for i, s in enumerate(S) if s[2] != nom[i]]
print('steps:', len(S), 'late steps (step, shift of its start in retraces):', late)
# per phase: drawn frames / retraces
phases = [(0, 7, 'credits 0-6'), (7, 25, 'disc move 7-24'), (25, 32, 'C appears, flight 25-31'),
          (32, 89, 'blur fades, label 32-88'), (89, 149, 'blur grows 89-148'), (149, 151, '149-150'),
          (151, 153, '151-152')]
ends = [s[2] for s in S] + [max(Rf)]
for a, b, name in phases:
    if b > len(S): break
    r0, r1 = S[a][2], (S[b][2] if b < len(S) else max(Rf))
    n = sum(1 for r, fr in F if r0 <= r < r1)
    print('%-28s retraces %5d-%5d: %5d drawn frames, %.1f fps' % (name, r0, r1, n, n / (r1 - r0) * 70.086))
# worst stretches: drawn frames per 70 retraces
import collections
c = collections.Counter(r // 70 for r, fr in F)
low = sorted((v, k) for k, v in c.items())[:8]
print('slowest seconds (drawn frames in retraces 70k..70k+69):', [(k * 70, v) for v, k in low])
# capture frames of the phase starts in section 1 of the notes
marks = [0, 1, 3, 4, 5, 6, 7, 22, 25, 27, 28, 29, 32, 88, 89, 149, 150, 151, 152]
print('step: start retrace -> capture frame of that retrace')
print(', '.join('%d: %d -> %s' % (k, S[k][2], Rf.get(S[k][2] + 1)) for k in marks if k < len(S)))
