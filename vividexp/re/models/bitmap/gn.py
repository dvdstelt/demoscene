# tiny Gauss-Newton / Levenberg-Marquardt (no scipy in the toolbox)
import numpy as np
def lm(f,p0,iters=200,lam=1e-3):
    p=np.array(p0,float); r=f(p); c=r@r
    for it in range(iters):
        J=np.empty((r.size,p.size))
        for k in range(p.size):
            dp=np.zeros_like(p); dp[k]=1e-6*max(1,abs(p[k]))
            J[:,k]=(f(p+dp)-r)/dp[k]
        A=J.T@J; g=J.T@r
        while True:
            step=np.linalg.solve(A+lam*np.diag(np.diag(A)+1e-12),-g)
            pn=p+step; rn=f(pn); cn=rn@rn
            if cn<c: p,r,c=pn,rn,cn; lam=max(lam/3,1e-12); break
            lam*=4
            if lam>1e12: return p
        if np.abs(step).max()<1e-10: break
    return p
