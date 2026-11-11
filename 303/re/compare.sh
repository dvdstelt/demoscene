#!/bin/bash
# usage: compare.sh out.jpg vt1 vt2 ...  -> original (left) vs remake (right) per row pair
S=${TMPDIR:-/tmp}
HERE=$(cd "$(dirname "$0")" && pwd)
V=${CAPTURE:?set CAPTURE to a video capture of the original 303 (960x720 or larger)}
R=$HERE/../remake
BIN=${BIN:-$R/build/acme303}
out=$1; shift; tmp=$(mktemp -d -p $S); mt=""
for t in "$@"; do
  ffmpeg -hide_banner -loglevel error -y -ss $t -i "$V" -frames:v 1 -vf scale=400:300 $tmp/v_$t.png
  m=$(python3 -c "print('%.3f'%(($t+1.395)/1.00126))"); mt="$mt,$m"
done
toolbox run -c acme303 $BIN --data $HERE/../original --shots ${mt:1} --shotdir $tmp --shotsize 800x600 2>&1 | grep -v "^wrote" | grep -v "^$" | head -5
python3 -I - "$tmp" "$out" "$@" <<'PY'
import sys,glob,os
from PIL import Image, ImageDraw
tmp,out=sys.argv[1],sys.argv[2]; ts=sys.argv[3:]
shots=sorted(glob.glob(tmp+'/shot_*.png'))
pairs=[]
for t,s in zip(ts,shots):
    a=Image.open(f'{tmp}/v_{t}.png').convert('RGB'); b=Image.open(s).convert('RGB').resize((400,300),Image.LANCZOS)
    pairs.append((t,a,b))
C=2; W,H=800,300
im=Image.new('RGB',(W*C,H*((len(pairs)+C-1)//C)),(40,40,40)); d=ImageDraw.Draw(im)
for i,(t,a,b) in enumerate(pairs):
    x,y=(i%C)*W,(i//C)*H; im.paste(a,(x,y)); im.paste(b,(x+400,y))
    d.rectangle((x,y,x+70,y+12),fill=0); d.text((x+2,y),f'orig {t}',fill=(255,255,0)); d.text((x+402,y),'remake',fill=(0,255,255))
    d.line((x+800-1,y,x+800-1,y+300),fill=(255,0,255))
im.save(out,quality=85)
PY
rm -rf $tmp
