import sys, pickle, numpy as np
R='/tmp/claude-1000/-home-dennis--config-Claude-scratch-workspaces-e97b6b66-a77a-46ac-a12a-f43ab86d2564-b079650b-97d8-49d5-bd62-940ddce6ee3d-scratch-2026-11-10-9f02fa/0a51be05-fc78-48c4-9845-e78eac5ea96a/scratchpad/re-water/'
D='/tmp/claude-1000/-home-dennis--config-Claude-scratch-workspaces-e97b6b66-a77a-46ac-a12a-f43ab86d2564-b079650b-97d8-49d5-bd62-940ddce6ee3d-scratch-2026-11-10-9f02fa/0a51be05-fc78-48c4-9845-e78eac5ea96a/scratchpad/part-greetings/dump/'
sys.path.insert(0, R+'py'); import model
SIN=pickle.load(open(R+'sin.pkl','rb'))
t=int(sys.argv[1]) if len(sys.argv)>1 else 7997
h=np.fromfile(D+'h_%d.f32'%t,np.float32).reshape(200,320)[::-1]
ix=np.fromfile(D+'i_%d.f32'%t,np.float32).reshape(200,320)[::-1]
hm=np.load(R+'mem8000_hmap.npy').astype(np.float64); vga=np.load(R+'mem8000_vga.npy').astype(np.float64)
lm=np.load(R+'mem8000_lightmap.npy')
H=hm[:200]
d=h-H
txt=H>160   # letter plateau / letters above the background maximum
print('height: mean |d| %.2f  mean d %.2f  rms %.2f  corr %.4f  p95 |d| %.1f'%(np.abs(d).mean(), d.mean(), np.sqrt((d**2).mean()), np.corrcoef(h.ravel(),H.ravel())[0,1], np.percentile(np.abs(d),95)))
print('  bg only (orig<=159): mean|d| %.2f, letters (>160): mean|d| %.2f n=%d'%(np.abs(d[~txt]).mean(), np.abs(d[txt]).mean(), txt.sum()))
# gradients
def gx(a): return (a[1:-1,:-2]-a[1:-1,2:])/2
def gy(a): return (a[:-2,1:-1]-a[2:,1:-1])/2
for nm,f in (('gx',gx),('gy',gy)):
    a=f(h); b=f(H)
    print('%s: orig rms %.2f mine rms %.2f  corr %.4f  mean|d| %.2f'%(nm, b.std(), a.std(), np.corrcoef(a.ravel(),b.ravel())[0,1], np.abs(a-b).mean()))
# index, valid region rows 1..179 (bump was still drawing rows 180+)
V=vga[1:180,1:319]; I=ix[1:180,1:319]
di=I-V
print('index: mean |d| %.2f  mean d %.2f  corr %.4f  p50 %.1f p90 %.1f'%(np.abs(di).mean(), di.mean(), np.corrcoef(I.ravel(),V.ravel())[0,1], np.percentile(np.abs(di),50), np.percentile(np.abs(di),90)))
# 2x2 box-averaged (original reads the light map at half resolution)
def box(a): a=a[:a.shape[0]//2*2,:a.shape[1]//2*2]; return a.reshape(a.shape[0]//2,2,a.shape[1]//2,2).mean((1,3))
print('index 2x2 avg: mean|d| %.2f corr %.4f'%(np.abs(box(I)-box(V)).mean(), np.corrcoef(box(I).ravel(),box(V).ravel())[0,1]))
# lighting formula alone: my continuous lighting on the ORIGINAL height map vs the VGA
ang=342
A=64*np.sin(2*np.pi*ang/1024); E=16*np.cos(2*np.pi*ang/1024)
tex=np.load(R+'gen_tex.npy').astype(np.float64).reshape(-1)[:65536].reshape(256,256)
def radial(r):
    v=np.clip(300-32*np.sqrt(r),0,255)
    return np.where(v>192.5,1.75*(v-128)+0.375,np.maximum(0,0.58333333*v-0.79166667))
def lmc(u,v):  # continuous LM with the exact texture (nearest, index as original)
    i=np.floor(u+256+0.5).astype(int); j=np.floor(v+128+0.5).astype(int)
    tt=tex[(-j)&255,(-i)&255]
    return np.minimum(255, radial(np.hypot(u,v))+np.floor(tt/2))
ys,xs=np.mgrid[1:199,1:319]
X=xs+0.5; Y=ys+0.5
g1=(H[1:199,0:318]-H[1:199,2:320])/2; g2=(H[0:198,1:319]-H[2:200,1:319])/2
u=X/2+A-81.25+g1; v=Y/2+E-51.25+g2
L=lmc(u,v)[:179]; Vv=vga[1:180,1:319]
print('my lighting on the original heights: mean|d| %.2f  mean d %.2f  exact-equal %.1f%%'%(np.abs(L-Vv).mean(), (L-Vv).mean(), 100*(L==Vv).mean()))
# exact LM vs my radial at integer coords
jj,ii=np.mgrid[0:256,0:512]
uu=ii-256; vv=jj-128
mine=np.minimum(255, radial(np.hypot(uu,vv))+tex[(-jj)&255,(-ii)&255]/2-0.25)
dl=mine-lm
print('light map (exact texture term): mean d %.3f mean|d| %.3f max|d| %.2f'%(dl.mean(),np.abs(dl).mean(),np.abs(dl).max()))
