"""Find SDIGUS.DSD (and other things) in a snapshot's memory: python3 snapfind.py ../../snaps/f38.snap"""
import pickle, sys, os
import unicorn  # noqa: F401  (the pickle needs it)
snap = pickle.load(open(sys.argv[1], 'rb'))
mem = snap['mem']
dsd = open(os.path.join(os.path.dirname(__file__), '../../../original/SDIGUS.DSD'), 'rb').read()
for off in range(0, len(dsd) - 64, 0x400):
    chunk = dsd[off:off + 32]
    if chunk.count(0) > 24:
        continue
    i = mem.find(chunk)
    hits = []
    while i >= 0 and len(hits) < 4:
        hits.append(i)
        i = mem.find(chunk, i + 1)
    print('dsd+%05x -> %s' % (off, ' '.join('%x' % h for h in hits)))
