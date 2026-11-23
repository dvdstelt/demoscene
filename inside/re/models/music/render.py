"""Render the music to a WAV through the GF1 model (gf1.py), with the DSMI port (dsmi.py) driving it as on hardware
(the driver's volume reads see the ramped volume, its ramp waits take GF1 time).

Timing as in the capture (notes-music.md section 8): the music callback runs in timer interrupts 91.. (one callback
lost at interrupt 12117, --skip); interrupt i = 4m + k (k = 1..4) happens at PIT clock
  (15 + m) * FRAME - 37 * LINE + 4085 * k     for k = 1..3
  (16 + m) * FRAME - 37 * LINE                for k = 4 (the retrace interrupt, just before record 16 + m)
relative to record 0 of full.frames (FRAME = 1,193,182 / 70.086 PIT clocks, LINE = FRAME / 449). The WAV starts at
record 0, so it lines up with frames2mp4.py's video of full.frames.

usage: python3 render.py out.wav [--until IRQ] [--rate 44100] [--skip 12117]
--rate: resample the GF1's 25,725 Hz output (linear interpolation) for players that dislike odd rates (default: keep).
"""
import argparse
import os
import sys
import wave

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import amf   # noqa: E402
import dsmi  # noqa: E402
import gf1   # noqa: E402

PIT_HZ = 1193182
FRAME = PIT_HZ / (25175000 / (800 * 449))
LINE = FRAME / 449
PERIOD = 4085                        # [0x4b8f00]


def irq_time(i, first_record=15):
    """PIT clock of timer interrupt i relative to record 0 of the capture."""
    m, k = divmod(i - 1, 4)
    k += 1
    if k == 4:
        return (first_record + 1 + m) * FRAME - 37 * LINE
    return (first_record + m) * FRAME - 37 * LINE + PERIOD * k


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('wav')
    ap.add_argument('--amf', default=os.path.join(dsmi.ORIG, 'EYE.AMF'))
    ap.add_argument('--first-irq', type=int, default=91)
    ap.add_argument('--until', type=int, default=52262)
    ap.add_argument('--skip', default='12117')
    ap.add_argument('--rate', type=int, default=0)
    a = ap.parse_args()

    m = amf.AMF(a.amf)
    card = gf1.GF1()
    ds = dsmi.Dsmi(m, card)
    ds.start()
    card.prepare_render()
    rate = card.frame_rate()                      # 617,400 / 24
    skip = {int(x) for x in a.skip.split(',') if x}
    # silence from record 0 to the first music callback
    t0 = irq_time(a.first_irq)
    acc = t0 * rate / PIT_HZ
    n = int(acc)
    acc -= n
    card.run(n)
    for i in range(a.first_irq, a.until + 1):
        card.stamp = i
        card.waited = 0
        if i not in skip:
            ds.interrupt()
        acc += (irq_time(i + 1) - irq_time(i)) * rate / PIT_HZ - card.waited
        n = int(acc)
        acc -= n
        card.run(n)
    pcm = np.concatenate(card.out)
    peak = np.abs(pcm).max()
    out_rate = rate
    if a.rate:
        t = np.arange(int(len(pcm) * a.rate / rate)) * (rate / a.rate)
        pcm = np.stack([np.interp(t, np.arange(len(pcm)), pcm[:, c]) for c in (0, 1)], 1)
        out_rate = a.rate
    print('rendered %d frames at %.1f Hz (%.2f s, from record 0 of the capture); peak %.3f (1.0 = one voice at '
          'full scale and volume)' % (len(pcm), out_rate, len(pcm) / out_rate, peak))
    with wave.open(a.wav, 'wb') as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(int(round(out_rate)))
        w.writeframes((np.clip(pcm / max(peak, 1e-9) * 0.9, -1, 1) * 32767).astype('<i2').tobytes())


if __name__ == '__main__':
    main()
