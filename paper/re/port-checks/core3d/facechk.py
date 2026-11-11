# face colours and geometry of the 4500 frame: original = integer-exact model render (with paper), lines removed
# from both comparisons by masking pixels near any line pixel of the original or the remake
import pickle, sys, numpy as np
from PIL import Image
S='/tmp/claude-1000/-home-dennis--config-Claude-scratch-workspaces-e97b6b66-a77a-46ac-a12a-f43ab86d2564-b079650b-97d8-49d5-bd62-940ddce6ee3d-scratch-2026-11-10-9f02fa/0a51be05-fc78-48c4-9845-e78eac5ea96a/scratchpad'
sys.path.insert(0, S+'/re-scene3d')
import model
cap=pickle.load(open(S+'/re-scene3d/c4500.cap0.pkl','rb'))
R=model.Renderer(cap['memA']); full=R.scene(0); fullc=R.combine(full, R.M.u32(0x1edf5))
R2=model.Renderer(cap['memA']); R2.line18b7=lambda *a: None; nl=R2.scene(0); nlc=R2.combine(nl, R2.M.u32(0x1edf5))
img=open('/home/dennis/Downloads/acme_303/paper/re/paper_img.bin','rb').read()
pal=np.frombuffer(img[0xd00+0x169a3:0xd00+0x169a3+768],np.uint8).reshape(256,3).astype(np.float32)*255/63
A=np.frombuffer(bytes(fullc),np.uint8).reshape(200,320); B=np.frombuffer(bytes(nlc),np.uint8).reshape(200,320)
lm=A!=B
def dil(m,r):
    o=m.copy()
    for dy in range(-r,r+1):
        for dx in range(-r,r+1): o|=np.roll(np.roll(m,dy,0),dx,1)
    return o
mine=np.asarray(Image.open(S+'/core3d/'+sys.argv[1]).convert('RGB'),np.float32)
orig_nl=pal[B]
# remake line pixels: where mine is notably darker than the no-line original
d=np.abs(mine-orig_nl).mean(axis=2)
mask=~dil(lm,3)
print('face pixels (>=3 px from any original line): %d  mean diff %.2f  median %.2f  >16: %.2f%%' % (mask.sum(), d[mask].mean(), np.median(d[mask]), (d[mask]>16).mean()*100))
# where are the big face errors?
big=(d>16)&mask
print('big diff count', big.sum())
ys,xs=np.nonzero(big); print(list(zip(xs[:20],ys[:20])))
out=np.clip(d*4,0,255).astype(np.uint8); out[~mask]=40
Image.fromarray(out).resize((960,600),Image.NEAREST).save(S+'/core3d/facediff.png')
# line footprints: darkening relative to the no-line original
lum=lambda x: x.mean(axis=2)
do=lum(orig_nl)-lum(pal[A]); dr=lum(orig_nl)-lum(mine)
# exclude the banner handwriting rows? (not drawn by either: model has none) ; sky band row 0..1
mo=do>4; mr=dr>12
print('line px orig %d remake %d both %d IoU %.2f; darkness sum orig %.0f remake %.0f' % (mo.sum(), mr.sum(), (mo&mr).sum(), (mo&mr).sum()/(mo|mr).sum(), np.clip(do,0,None).sum(), np.clip(dr,0,None)[~(d>60)|mo].sum()))
o=np.zeros((200,320,3),np.uint8); o[...,0]=np.clip(do*3,0,255); o[...,1]=np.clip(dr*3,0,255)
Image.fromarray(o).resize((960,600),Image.NEAREST).save(S+'/core3d/edgecmp.png')
zone=dil(mo,3)
print('darkness within 3 px of the original lines: orig %.0f remake %.0f (ratio %.2f); off-line remake residual mean %.2f' % (do[zone].sum(), dr[zone].sum(), dr[zone].sum()/do[zone].sum(), dr[~zone].mean()))
