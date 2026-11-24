"""The ending (order 27): per callback count, the fall, the fade level, the stamps on screen."""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sheet_model as sm
from sheet_model import Tables, State, heights
from stats import stamp_positions
T = Tables()
for c in range(1136, 1202, 3):
    st = State(T, c, max(0, c - 1137))
    pts = sm.project(T, st, heights(T, st))
    n = sum(len(stamp_positions(*pts[a], *pts[b])) for a, b in T.edges)
    ys = sorted(y for x, y in pts)
    print('c %d refresh %d  b %2d  ydrop %5d  stamps %4d  highest vertex y %d' % (c, 6896 + c, st.b, st.ydrop, n, ys[0]))
