exec(open('check.py').read().split("# lighting formula alone")[0].split("h=np.fromfile")[0])
import numpy as np
hm=np.load(R+'mem8000_hmap.npy').astype(np.float64); vga=np.load(R+'mem8000_vga.npy').astype(np.float64)
H=hm[:200]
tex=np.load(R+'gen_tex.npy').astype(np.float64).reshape(-1)[:65536].reshape(256,256)
ang=342; A=64*np.sin(2*np.pi*ang/1024); E=16*np.cos(2*np.pi*ang/1024)
def radial(r):
    v=np.clip(300-32*np.sqrt(r),0,255)
    return np.where(v>192.5,1.75*(v-128)+0.375,np.maximum(0,0.58333333*v-0.79166667))
def lmc(u,v):
    i=np.floor(u+256+0.5).astype(int); j=np.floor(v+128+0.5).astype(int)
    return np.minimum(255, radial(np.hypot(u,v))+np.floor(tex[(-j)&255,(-i)&255]/2))
ys,xs=np.mgrid[1:180,1:319]; X=xs+0.5; Y=ys+0.5
g1=(H[1:180,0:318]-H[1:180,2:320])/2; g2=(H[0:179,1:319]-H[2:181,1:319])/2
V=vga[1:180,1:319]
for ox in (-80.75,-81.0,-81.25,-81.5):
    print(ox, ' '.join('%.3f'%np.abs(lmc(X/2+A+ox+g1, Y/2+E+oy+g2)-V).mean() for oy in (-50.75,-51.0,-51.25,-51.5)))
