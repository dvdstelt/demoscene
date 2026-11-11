import numpy as np, pickle
fr = pickle.load(open('flock_frames.pkl', 'rb'))
bad = 0
for line in open('flock_dump.txt'):
    a = list(map(int, line.split())); n = a[0]
    planes, V, cam, _ = fr[n]
    want = V + cam
    for m, wp in planes: want += wp + m
    if a[1:] != want: bad += 1; print('frame', n, 'differs') if bad < 5 else None
print('flock frames compared', n, 'bad', bad)
T = {}
for line in open('teal_dump.txt'):
    a = list(map(int, line.split())); T[a[0]] = a
print('6099 pos', T[6099][1:3], 'want (8970153, 6028530)'); print('6999 pos', T[6999][1:3], 'want (27759069, 16514932)')
tr = np.load('../re-planes/teal_traj.npy')
ok = all(T[int(r[0])][1] >> 10 == r[1] and T[int(r[0])][2] >> 10 == r[2] for r in tr)
print('teal traj pos match all ticks:', ok)
