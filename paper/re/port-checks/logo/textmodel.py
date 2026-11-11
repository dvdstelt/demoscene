# Original text buffer T (exact byte pipeline) vs the continuous model (bitmap glyphs at 8x, Gaussian blurs of the
# same variance, box-downsampled) to see whether the model itself darkens the counters.
import pickle, struct, numpy as np
S='/tmp/claude-1000/-home-dennis--config-Claude-scratch-workspaces-e97b6b66-a77a-46ac-a12a-f43ab86d2564-b079650b-97d8-49d5-bd62-940ddce6ee3d-scratch-2026-11-10-9f02fa/0a51be05-fc78-48c4-9845-e78eac5ea96a/scratchpad'
snap=pickle.load(open(S+'/snaps/t5500.snap','rb')); m=snap['mem']
dd=lambda a: struct.unpack_from('<I',m,a)[0]
font=np.frombuffer(m,np.uint8,0xe800,dd(0x1ede9))
def glyph(c):
    o=(ord(c)-0x61)<<11; w=int(font[o]); return w, font[o+1:o+1+32*60].reshape(32,60)[:,:w]
TXT='present the\x01intro'
def draw(buf,pos,stride=320,val=255):
    for ch in TXT:
        if ord(ch)<0x61:
            pos+=10
            if ord(ch)==1: pos+=0x303e
            continue
        w,g=glyph(ch)
        for r in range(32):
            p=pos+r*stride; buf[p:p+w]=np.minimum(buf[p:p+w]+(g[r]>0)*val,255)
        pos+=w+1
def blur(buf):
    n=78*320; s=buf.astype(np.int32); i=np.arange(n); st=320
    tmp=np.zeros(n+1280,np.int32)
    tmp[640+i]=(s[320+i-st]+s[320+i+st]+s[320+i-1]+s[320+i+1])>>2
    buf[320+i]=((tmp[640+i-st]+tmp[640+i+st]+tmp[640+i-1]+tmp[640+i+1])>>2)
bb=np.zeros(64000+2000,np.int32)
draw(bb,0x514)
for k in range(5,0,-1):
    blur(bb)
    if k==2:
        bb[:0x6400]>>=1; draw(bb,0x150)
T=bb[:25600].reshape(80,320).astype(np.float32)
# continuous model at 8x

def gauss1d(a,sigma,axis):
    r=int(np.ceil(4*sigma)); x=np.arange(-r,r+1); k=np.exp(-0.5*x*x/sigma/sigma); k/=k.sum()
    a=np.moveaxis(a,axis,-1); pad=np.pad(a,[(0,0)]*(a.ndim-1)+[(r,r)]); o=np.zeros_like(a)
    for j,kv in enumerate(k): o+=kv*pad[...,j:j+a.shape[-1]]
    return np.moveaxis(o,-1,axis)
def gauss(a,s): return gauss1d(gauss1d(a,s,0),s,1)
U=8
def drawhi(pos_x,pos_y,val):
    b=np.zeros((80*U,320*U),np.float32)
    x=pos_x; y=pos_y
    for ch in TXT:
        if ord(ch)<0x61:
            x+=10
            if ord(ch)==1:
                a=y*320+x+0x303e; y,x=divmod(a,320)
            continue
        w,g=glyph(ch)
        gi=np.kron((g>0).astype(np.float32),np.ones((U,U),np.float32))
        b[y*U:(y+32)*U,x*U:(x+w)*U]=np.maximum(b[y*U:(y+32)*U,x*U:(x+w)*U],gi*val)
        x+=w+1
    return b
sh=drawhi(20,4,0.5); sh=gauss(sh,np.sqrt(4)*U)
t=np.minimum(sh+drawhi(16,1,1.0),1.0); t=gauss(t,1.0*U)
M=t.reshape(80,U,320,U).mean((1,3))*255
print('max |model - original T| over buffer: %.1f  mean %.2f'%(np.abs(M-T).max(),np.abs(M-T).mean()))
for y in (19,29):
    print('row',y,'x 10..36'); print(' orig ',' '.join('%3d'%v for v in T[y,10:36])); print(' model',' '.join('%3d'%v for v in M[y,10:36]))
np.save('T_orig.npy',T); np.save('T_model.npy',M)
