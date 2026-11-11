#!/bin/bash
# usage: grab.sh out.jpg t1 t2 ...   (4 columns, 480px frames)
S=${TMPDIR:-/tmp}
HERE=$(cd "$(dirname "$0")" && pwd)
V=${CAPTURE:?set CAPTURE to a video capture of the original 303 (960x720 or larger)}
out=$1; shift; tmp=$(mktemp -d -p $S); i=0
for t in "$@"; do ffmpeg -hide_banner -loglevel error -y -ss $t -i "$V" -frames:v 1 -vf scale=480:-2 $tmp/$(printf %03d $i)_$t.png; i=$((i+1)); done
python3 -I -c "
from PIL import Image, ImageDraw
import glob,sys,os
fs=sorted(glob.glob('$tmp/*.png')); ims=[Image.open(f) for f in fs]; W,H=ims[0].size; C=4
s=Image.new('RGB',(W*C,H*((len(ims)+C-1)//C))); d=ImageDraw.Draw(s)
for i,(f,im) in enumerate(zip(fs,ims)): s.paste(im,((i%C)*W,(i//C)*H)); d.rectangle(((i%C)*W,(i//C)*H,(i%C)*W+60,(i//C)*H+12),fill=0); d.text(((i%C)*W+3,(i//C)*H+1),os.path.basename(f)[4:-4],fill=(255,255,0))
s.save('$out',quality=85)"
rm -rf $tmp
