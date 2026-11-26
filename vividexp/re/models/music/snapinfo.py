"""Print MIDAS's timer, GMP and GUS-device globals from a snapshot (run inside the toolbox: unicorn must be importable).

usage: python3 snapinfo.py ../../snaps/f10221.snap
"""
import pickle
import struct
import sys

import unicorn  # noqa: F401  (the pickled context needs it)

TIMER = [('sync [0x825c60]', 0x825c60), ('sysCount 0x8231d6', 0x8231d6), ('playCount 0x8231da', 0x8231da),
         ('playTmrCnt 0x8231de', 0x8231de), ('SD 0x8231e2', 0x8231e2), ('playSD 0x823226', 0x823226),
         ('inPlayer 0x82322a', 0x82322a), ('scrCount 0x823236', 0x823236), ('scrTmrCnt 0x82323a', 0x82323a),
         ('scrRem 0x82323e', 0x82323e), ('syncScr 0x82324e', 0x82324e), ('inRetrace 0x823252', 0x823252),
         ('tempoFlag 0x823256', 0x823256), ('mode 0x82325a', 0x82325a),
         ('GUS outrate 0x82395c', 0x82395c), ('GUS updrate 0x823960', 0x823960), ('GUS channels 0x823950', 0x823950),
         ('GUS master 0x823970', 0x823970), ('GUS maxhandle 0x823978', 0x823978), ('GUS mem 0x82397c', 0x82397c),
         ('GUS free 0x823980', 0x823980), ('surround 0x846e48', 0x846e48), ('GMP tempo 0x844cc0', 0x844cc0),
         ('GMP handle 0x844cc4', 0x844cc4), ('frames 0x825c5c', 0x825c5c), ('order 0x825c48', 0x825c48),
         ('row 0x825c50', 0x825c50), ('syncval 0x825c54', 0x825c54)]


def main():
    s = pickle.load(open(sys.argv[1], 'rb'))
    mem = s['mem']
    d = lambda a: struct.unpack_from('<i', mem, a)[0]
    for name, a in TIMER:
        print('%-24s %d (%#x)' % (name, d(a), d(a) & 0xffffffff))
    print('st:', {k: v for k, v in s['st'].items() if not isinstance(v, (bytes, bytearray, list, dict))})


if __name__ == '__main__':
    main()
