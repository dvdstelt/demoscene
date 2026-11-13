"""When does a sync event take effect for the dt-integrated state? (capture full.flips)

A frame is drawn with the timer value T read after the previous frame was shown (.flips column 4); the event check
of a frame runs right after that read, and the frame then integrates dt over the interval since the previous read.
Row p becomes current at xmpGetTimer = 66300 p - 22100 (the player tick that starts the row). So the event frame is
the first frame with T >= that value, and a rate or value set by the event applies from tau = the T of the frame
before it (the last frame timer < the row start): up to one frame interval before the music position.

usage: python3 models/grid/event_lag.py   (from clone/re)
"""
import os
import bisect

RE = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
PIT = 1193182
EVENTS = [('11 (part 11 start)', 0x08, 0x20), ('1101 (part 11 fade-out)', 0x0b, 0), ('2 (part 2 start)', 0x0c, 0),
          ('201 (part 2)', 0x0e, 0), ('5 (part 5 start)', 0x20, 0), ('501', 0x21, 0), ('501', 0x22, 0),
          ('501', 0x23, 0), ('4 (part 4 start)', 0x30, 0x20), ('403', 0x31, 0), ('402 + 401', 0x32, 0),
          ('401', 0x33, 0), ('401', 0x34, 0), ('401', 0x35, 0), ('8 (part 2 end)', 0x10, 0),
          ('7 (part 5 end)', 0x24, 0), ('10 (part 4 end)', 0x36, 0)]


def main():
    T = []
    for line in open(os.path.join(RE, 'full.flips')):
        if line.startswith('#'): continue
        f = line.split()
        if int(f[3]) > -(1 << 39): T.append(int(f[3]))
    print('%-26s %6s %12s %12s %10s %10s' % ('event', 'pos', 'row start s', 'tau s', 'lag ms', 'frame ms'))
    for name, o, r in EVENTS:
        p = o * 64 + r
        pt = 66300 * p - 22100
        k = bisect.bisect_left(T, pt)          # first frame with T >= pt: the event frame
        tau = T[k - 1]
        print('%-26s %02x:%02x %12.4f %12.4f %10.2f %10.2f' % (name, o, r, pt / PIT, tau / PIT, (pt - tau) * 1000 / PIT,
                                                         (T[k] - T[k - 1]) * 1000 / PIT))


if __name__ == '__main__':
    main()
