import numpy as np
def cr(t):
    t=abs(t)
    return np.where(t<1, 1.5*t**3-2.5*t**2+1, np.where(t<2, -0.5*t**3+2.5*t**2-4*t+2, 0))
def val(x, sig):
    v=0
    for c in range(-4,6):
        s = 1.0 if c<=0 else (sig if c==1 else 0.0)
        v += cr(x-c)*s
    return v
xs=np.linspace(0,2,20001)
def X(sig):
    v=val(xs,sig)
    i=np.nonzero(v<0.5)[0][0]
    return xs[i]
sigs=np.linspace(0,1,41)
Xs=np.array([X(s) for s in sigs])
for s,x in zip(sigs[::4],Xs[::4]): print('%.3f %.4f'%(s,x))
# invert: want X = 0.5 + tp
tp=Xs-0.5
# fit sigma as polynomial of tp
p=np.polyfit(tp,sigs,5); print('poly',p)
print('max err', np.abs(np.polyval(p,tp)-sigs).max())
