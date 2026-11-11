"""Finds the full.frames records that equal the target page of a render dump (r<N>.post), i.e. shows that the
renderer's output is what the capture shows. usage: python3 match_capture.py DUMPDIR N [N ...]"""
import os, sys, pickle
HERE = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
P = 128000
d = sys.argv[1]
f = open(os.path.join(HERE, 'full.frames'), 'rb')
for n in map(int, sys.argv[2:]):
    post = pickle.load(open(os.path.join(d, 'r%04d.post' % n), 'rb'))
    t0 = post['tick']
    hits = []
    for t in range(t0 - 2, t0 + 30):
        f.seek(32 + t * P)
        if f.read(P) == post['pagebytes']: hits.append(t)
    print('render %d (drawn during tick %d): identical to full.frames records %s' % (n, t0, hits))
