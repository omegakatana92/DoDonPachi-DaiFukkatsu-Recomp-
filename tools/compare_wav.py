#!/usr/bin/env python3
"""Compare the runtime's WAV output with a MAME -wavwrite recording (stereo or mono): linear resampling to
MAME's rate, best alignment within +-200 samples, correlation, residual and per-second level ratio.
Usage: compare_wav.py ours.wav mame.wav"""
import sys, wave, numpy as np
def load(p):
    w = wave.open(p); n = w.getnframes(); ch = w.getnchannels(); r = w.getframerate()
    a = np.frombuffer(w.readframes(n), dtype='<i2').astype(np.float64).reshape(-1, ch) / 32768.0
    return a, r
o, ro = load(sys.argv[1]); m, rm = load(sys.argv[2])
t = np.arange(int(len(o) * rm / ro)) * (ro / rm)
o2 = np.stack([np.interp(t, np.arange(len(o)), o[:, c]) for c in range(o.shape[1])], 1)
n = min(len(o2), len(m)) - 400
best = None
for lag in range(-200, 201):
    a = o2[200 + lag:200 + lag + n, 0]; b = m[200:200 + n, 0]
    c = np.dot(a, b)
    if best is None or c > best[0]: best = (c, lag)
lag = best[1]
a = o2[200 + lag:200 + lag + n]; b = m[200:200 + n]
for c in range(a.shape[1]):
    corr = np.corrcoef(a[:, c], b[:, c])[0, 1]
    res = np.sqrt(np.mean((a[:, c] - b[:, c]) ** 2)) / np.sqrt(np.mean(b[:, c] ** 2))
    ratios = []
    for s in range(0, n - rm, rm):
        rb = np.sqrt(np.mean(b[s:s + rm, c] ** 2)); ra = np.sqrt(np.mean(a[s:s + rm, c] ** 2))
        if rb > 0.01: ratios.append(ra / rb)
    print("channel %d: lag %d, correlation %.4f, residual %.1f%%, level ratio %.3f-%.3f over %d s" % (c, lag, corr, res * 100, min(ratios), max(ratios), len(ratios)))
