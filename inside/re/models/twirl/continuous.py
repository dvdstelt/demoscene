"""The twirl evaluated per pixel in continuous form (what the remake does), checked against the capture.

For a screen point (x, y) in original pixel units (pixel n covers [n, n+1)):
  dx = x - 160, dy = y - 100, rho = |(dx, dy)|, theta = atan2(dy, dx) (y down: clockwise, 0 = right)
  q  = rho / 1.72 + QB                     ring (texel row before shifts); QB = 0.315 = bias of the last-write table
  k  = 128 + q                             ring-table index (the original uses floor)
  R  = q + s2c + A sin(p1 + k d18)         texel row
  C  = 128 + theta 256 / 2pi + CB + s58 + B sin(p2 + k d14)   texel column, CB = 0.2
  texel = pic[(256 floor(R) + floor(C)) & 0xffff]    (nearest; column overflow carries into the row)
Prints, for capture records, the share of pixels where nearest sampling at the pixel centre gives the captured index,
and writes models/twirl/cont_<k>.png: capture | continuous at 4x (1280x800) through the record's DAC.
"""
import math, os, sys
import numpy as np
from PIL import Image
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import frame
from sheet import open_frames, indexed

QB, CB = 0.315, 0.2


def render(st, ps, picture, w=320, h=200, exact_trunc=False):
    d18, _, A, _, _, _, B, d14 = ps
    s58, s2c, p1, p2 = st
    y, x = np.mgrid[0:h, 0:w]
    x = (x + 0.5) * 320 / w; y = (y + 0.5) * 200 / h
    dx = x - 160; dy = y - 100
    rho = np.hypot(dx, dy); th = np.mod(np.arctan2(dy, dx), 2 * math.pi)
    q = rho / 1.72 + QB
    if exact_trunc:                       # the original's quantisation, for checking the formula
        qi = np.floor(q)
        k = 128 + qi
        R = qi + math.trunc(s2c) + np.trunc(A * np.sin(p1 + k * d18))
        C = np.floor(128 + th * 128 / math.pi + CB) + math.trunc(s58) + np.trunc(B * np.sin(p2 + k * d14))
    else:
        k = 128 + q
        R = q + s2c + A * np.sin(p1 + k * d18) - 0.5
        C = 128 + th * 128 / math.pi + CB + s58 + B * np.sin(p2 + k * d14) - 0.5
    addr = (256 * np.floor(R + 0.5).astype(np.int64) + np.floor(C + 0.5).astype(np.int64)) & 0xffff
    return picture[addr]


def main():
    raw, _ = open_frames(os.path.join(frame.RE, 'full.frames'))
    states, sets = frame.schedule([(138, 2), (269, 3), (269, 4), (554, 0)])
    picks = {1: 0, 2: 1, 3: 2, 4: 3, 0: 4}
    for k in map(int, sys.argv[1:] or ['10650', '10800', '11000', '11200']):
        n = k - 10575
        s = sets[n] if k not in (10712,) else sets[n + 1]
        P = frame.pic(picks[s])
        px, dac = indexed(raw, k)
        for et in (True, False):
            img = render(states[n], frame.SETS[s], P, exact_trunc=et)
            print(k, 'quantised' if et else 'continuous', f'{(img == px).mean() * 100:.1f}% of pixels equal')
        big = render(states[n], frame.SETS[s], P, 1280, 800)
        lut = (dac.astype(np.uint32) * 255 + 31) // 63
        a = np.asarray(Image.fromarray(lut[px].astype(np.uint8)).resize((1280, 800), Image.NEAREST))
        Image.fromarray(np.concatenate([a, lut[big].astype(np.uint8)], 1)).resize((1280, 400)).save(
            os.path.join(HERE, f'cont_{k}.png'))


if __name__ == '__main__':
    main()
