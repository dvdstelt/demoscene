# Helpers to test table formulas: evaluate in float32 (or float64), convert like C's (int) cast, store as a byte.
import numpy as np
T1='unpacked/files/1/tabs/'; T2='unpacked/files/2/tabs/'
def tab(n):
    for d in (T1,T2):
        try:
            t=np.frombuffer(open(d+n,'rb').read(),np.uint8)
            return t.reshape(100,160).astype(int) if len(t)==16000 else t.reshape(200,320).astype(int)
        except FileNotFoundError: pass
def grid(w=320,h=200,cx=160,cy=100,dt=np.float32,sx=1,sy=1):
    y,x=np.mgrid[0:h,0:w]
    dx=((x-cx)*sx).astype(dt); dy=((y-cy)*sy).astype(dt)
    r=np.sqrt(dx*dx+dy*dy).astype(dt); th=np.arctan2(dy,dx).astype(dt)
    return dx,dy,r,th
def tobyte(v,mode='trunc'):
    if mode=='trunc': i=np.trunc(v)
    elif mode=='floor': i=np.floor(v)
    else: i=np.floor(v+0.5)
    return i.astype(np.int64)&255
def score(v,t,mode='trunc'):
    b=tobyte(v,mode); e=(b-t+128)%256-128
    return int((e==0).sum()), int(np.abs(e).max()), e
