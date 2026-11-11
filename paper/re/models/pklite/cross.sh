#!/bin/sh
# Cross-check unpklite against PKLITE's stub run in unicorn (paper/re/unpklite.py, unchanged) on synthetic files.
S=$(dirname "$0"); P=/home/dennis/Downloads/acme_303/paper
mkdir -p $S/cross
fails=0
for v in normal early loose; do
  for seed in 1 2 3 4 5 6; do
    x=$S/cross/$v$seed
    python3 -B $S/gen.py $P/PAPER.EXE $seed $v $x.exe || exit 1
    python3 -B $P/re/unpklite.py $x.exe $x > $x.log 2>&1 || { echo "emulator failed"; cat $x.log; exit 1; }
    $S/dump $x.exe $x.img || { fails=$((fails+1)); continue; }
    python3 -B - $x <<'PY' || fails=$((fails+1))
import sys
x = sys.argv[1]
img = open(x + '.img', 'rb').read(); mem = open(x + '.mem', 'rb').read()
ref = mem[0x100:0x100 + len(img)]
oep = open(x + '.txt').read().split()[3]
if img == ref: print('   identical (%05x bytes), %s' % (len(img), oep))
else:
    i = next(i for i in range(len(img)) if img[i] != ref[i])
    print('   MISMATCH at %05x: got %02x, emulator %02x' % (i, img[i], ref[i])); sys.exit(1)
PY
  done
done
echo "failures: $fails"
