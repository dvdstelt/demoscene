"""Dump recursive-descent listings of the given functions (core scope reading aid)."""
import sys, os
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
import rep
for a in sys.argv[1:]:
    print('=' * 20, a)
    print(rep.func(int(a, 16)))
