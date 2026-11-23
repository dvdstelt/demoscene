"""Frame rates of the parts2d-a entries in the capture: drawn frames per refresh, from full.ticks.

usage: python3 rates.py
Prints, per entry, the refresh its retrace routine was set at, how many refreshes and drawn frames it lasted and
the histogram of the drawn-frame counter's increments per refresh.
"""
import collections
import re

from common import RE, ticks
import os

ENTRIES = (0, 5, 6, 7, 9, 10, 12, 13, 15)


def starts():
    s = {}
    for line in open(os.path.join(RE, 'full.log')):
        m = re.match(r'entry (\d+): retrace routine set at frame (\d+)', line)
        if m:
            s[int(m.group(1))] = int(m.group(2))
    return s


def main():
    t = ticks()
    s = starts()
    last = max(t)
    for e in ENTRIES:
        if e not in s:
            print('entry %d: not in the capture yet' % e)
            continue
        a = s[e]
        b = min(s.get(e + 1, last), last)
        d = [int(t[k + 1]['drawn']) - int(t[k]['drawn']) for k in range(a, b)]
        h = collections.Counter(d)
        n = int(t[b]['drawn']) - int(t[a]['drawn'])
        print('entry %2d: refreshes %d..%d (%d), drawn %d, %.2f frames/refresh, increments %s' % (
            e, a, b, b - a, n, n / max(1, b - a), dict(sorted(h.items()))))


if __name__ == '__main__':
    main()
