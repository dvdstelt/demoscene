"""Which rotozoom layer each pixel shows in entry 7 (and how old it is), from the write pattern of the four-call cycle.

usage: python3 layers.py
Runs layer1 / layer2 on a tag buffer for 8 frames from the image's initial cycle state and prints, per pixel class
(column parity, row parity), the layer it shows after each frame and the frame it was written in.
"""
import numpy as np

from roto import RotoState, layer1, layer2


def main():
    st = RotoState(None)
    tag = np.zeros((200, 320), np.uint8)
    age = np.zeros((200, 320), np.int64)
    t1 = np.full(256, 1, np.uint8)
    t2 = np.full(4096, 2, np.uint8)
    for f in range(8):
        before = tag.copy()
        layer1(tag, st, 0, t1)
        layer2(tag, st, 0, t2)
        age[tag != before] = f
        w1 = tag == 1
        cells = {(cx, ry): 'L%d' % tag[ry, cx] for ry in (0, 1) for cx in (0, 1)}
        print('frame %d: (x even, y even) %s, (x even, y odd) %s, (x odd, y even) %s, (x odd, y odd) %s; layer 1 '
              'on %.0f%% of the pixels' % (f, cells[(0, 0)], cells[(0, 1)], cells[(1, 0)], cells[(1, 1)],
                                          100 * w1.mean()))


if __name__ == '__main__':
    main()
