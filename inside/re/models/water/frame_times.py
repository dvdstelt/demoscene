"""The water part's drawn frames in a run (run2.pkl from run_inside.py --water-dump over the whole part): per frame
the refresh in which 0x4078d4 started, the interrupt counter c = [0x4248c4] it read for fish 0, c - 4 * refresh,
and the refresh at which the frame was shown. usage: frame_times.py [run2] [--list]"""
import collections
import pickle
import sys

run = sys.argv[1] if len(sys.argv) > 1 and not sys.argv[1].startswith('-') else 'run2'
F = pickle.load(open(run + '.pkl', 'rb'))['frames']
off = collections.Counter(f['c'] - 4 * f['refresh'] for f in F)
print('%d frames (plus the first, drawn inside refresh 8467/8468 with c = 33719)' % len(F))
print('c - 4 * refresh:', sorted(off.items()))
gaps = collections.Counter(b['flip_refresh'] - a['flip_refresh'] for a, b in zip(F, F[1:]) if 'flip_refresh' in b)
print('refreshes between shown frames:', sorted(gaps.items()))
print('mean frame rate %.2f per second' % (len(F) / ((F[-1]['refresh'] - F[0]['refresh']) / 70.086)))
if '--list' in sys.argv:
    for k, f in enumerate(F):
        print(k + 2, f['refresh'], f['c'], f.get('flip_refresh'))
