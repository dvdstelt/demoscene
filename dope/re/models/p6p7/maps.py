"""Look at the p6/p7 bump maps (two 4-bit offsets per byte) and the p7 light spot: is the offset field the gradient
of a height map (curl ~ 0)? does the spot follow a simple radial formula?"""
import numpy as np
from snap import img
b6, b7 = img('p6'), img('p7')
fur = np.frombuffer(b6, np.uint8, 256 * 128, 0x2398 * 16).reshape(128, 256)[:, 128:].astype(int)
wavy = np.frombuffer(b7, np.uint8, 256 * 128, 0x2fe9 * 16).reshape(128, 256)[:, :128].astype(int)
for name, m in (('fur', fur), ('wavy', wavy)):
    hi, lo = (m >> 4) - 8, (m & 15) - 8
    print(name, 'hi nibble range', hi.min(), hi.max(), 'mean', hi.mean().round(2), '| lo', lo.min(), lo.max(), lo.mean().round(2))
    # candidate height maps: does hi ~ d/dy and lo ~ d/dx of some h?  test curl of (lo, hi) as (gx, gy)
    for gx, gy, lab in ((lo, hi, 'lo=x hi=y'), (hi, lo, 'hi=x lo=y')):
        curl = (np.roll(gy, -1, 1) - gy) - (np.roll(gx, -1, 0) - gx)
        div = (np.roll(gx, -1, 1) - gx) + (np.roll(gy, -1, 0) - gy)
        print('   %s: mean |curl| %.2f  mean |div| %.2f  mean |g| %.2f' % (lab, np.abs(curl).mean(), np.abs(div).mean(),
                                                                         np.abs(np.stack([gx, gy])).mean()))
    # correlation with central differences of a smoothed version of itself is not possible without h; try
    # whether the field is the gradient of a height: solve least squares (Poisson) and report the residual
    H, W = m.shape
    fx, fy = np.fft.fftfreq(W), np.fft.fftfreq(H)
    for gx, gy, lab in ((lo, hi, 'lo=x hi=y'), (hi, lo, 'hi=x lo=y')):
        Gx, Gy = np.fft.fft2(gx), np.fft.fft2(gy)
        kx = np.exp(2j * np.pi * fx)[None, :] - 1          # forward difference operators
        ky = np.exp(2j * np.pi * fy)[:, None] - 1
        den = np.abs(kx) ** 2 + np.abs(ky) ** 2
        den[0, 0] = 1
        Hh = (np.conj(kx) * Gx + np.conj(ky) * Gy) / den
        Hh[0, 0] = 0
        h = np.real(np.fft.ifft2(Hh))
        rx = np.real(np.fft.ifft2(kx * Hh)) - gx
        ry = np.real(np.fft.ifft2(ky * Hh)) - gy
        # also centred differences
        kxc = (np.exp(2j * np.pi * fx)[None, :] - np.exp(-2j * np.pi * fx)[None, :]) / 2
        kyc = (np.exp(2j * np.pi * fy)[:, None] - np.exp(-2j * np.pi * fy)[:, None]) / 2
        denc = np.abs(kxc) ** 2 + np.abs(kyc) ** 2; denc[denc < 1e-9] = 1
        Hc = (np.conj(kxc) * Gx + np.conj(kyc) * Gy) / denc; Hc[0, 0] = 0
        rxc = np.real(np.fft.ifft2(kxc * Hc)) - gx; ryc = np.real(np.fft.ifft2(kyc * Hc)) - gy
        print('   %s: gradient fit residual (fwd diff) %.2f, (central diff) %.2f, of mean |g| %.2f' % (
            lab, np.sqrt((rx ** 2 + ry ** 2).mean()), np.sqrt((rxc ** 2 + ryc ** 2).mean()), np.sqrt((gx ** 2 + gy ** 2).mean())))
spot = np.frombuffer(b7, np.uint8, 128 * 128, 0x37ea * 16).reshape(128, 128).astype(float)
yy, xx = np.mgrid[0:128, 0:128]
r = np.hypot(yy - 63, xx - 63)
print('spot symmetric about (63,63)?', bool((spot[63 - 50:63 + 51, 63 - 50:63 + 51] == spot[63 - 50:63 + 51, 63 - 50:63 + 51][::-1, ::-1]).all()))
print('spot radial? max spread of values at equal radius', max(np.ptp(spot[np.abs(r - k) < 0.5]) for k in range(1, 64)))
prof = [spot[np.abs(r - k) < 0.5].mean() for k in range(0, 66, 4)]
print('profile r=0,4,..64:', np.round(prof, 1).tolist())
for name, f in (('63*(1-r/64)^1.5', lambda r: 63 * np.clip(1 - r / 64, 0, 1) ** 1.5),
                ('63*(1-(r/64)^2)^2', lambda r: 63 * np.clip(1 - (r / 64) ** 2, 0, 1) ** 2),
                ('63*cos(pi r/128)^2', lambda r: 63 * np.cos(np.clip(r / 64, 0, 1) * np.pi / 2) ** 2),
                ('63*exp(-(r/36)^2)', lambda r: 63 * np.exp(-(r / 36) ** 2)),
                ('63*(1-r/64)^2*..', lambda r: 63 * np.clip(1 - r / 64, 0, 1) ** 2)):
    m = r <= 64
    print('  fit %-20s rms %.2f max %.1f' % (name, np.sqrt(((f(r) - spot)[m] ** 2).mean()), np.abs(f(r) - spot)[m].max()))
