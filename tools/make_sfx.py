#!/usr/bin/env python3
"""Synthesises the game's sound effects (original, made by this script, CC0): mono 48 kHz 16-bit WAV files.
Usage: make_sfx.py <output folder>"""
import math, os, struct, sys, wave
import numpy as np

SR = 48000
rng = np.random.default_rng(7)

def t(sec): return np.arange(int(sec * SR)) / SR
def env(n, attack=0.004, decay=None, power=2.0):
    x = np.arange(n) / SR
    d = decay if decay else n / SR
    a = np.minimum(1, x / max(attack, 1e-4))
    return a * np.exp(-power * x / d * 2.0)
def sine(f, sec): tt = t(sec); return np.sin(2 * math.pi * f * tt)
def sweep(f0, f1, sec):
    tt = t(sec); f = f0 + (f1 - f0) * tt / sec
    return np.sin(2 * math.pi * np.cumsum(f) / SR)
def tri(f, sec): tt = t(sec); return 2 / math.pi * np.arcsin(np.sin(2 * math.pi * f * tt))
def square(f, sec): return np.sign(np.sin(2 * math.pi * f * t(sec))) * 0.6
def saw(f, sec): tt = t(sec); return 2 * ((f * tt) % 1.0) - 1
def noise(sec): return rng.uniform(-1, 1, int(sec * SR))
def lowpass(x, k):
    out = np.cumsum(x); out[k:] = out[k:] - out[:-k]; return out / k
def seq(parts, gap=0.0):
    out = np.zeros(0)
    for p in parts:
        out = np.concatenate([out, p, np.zeros(int(gap * SR))])
    return out
def mix(*xs):
    n = max(len(x) for x in xs); out = np.zeros(n)
    for x in xs: out[:len(x)] += x
    return out
def note(f, sec, wave=tri, decay=None): x = wave(f, sec); return x * env(len(x), 0.004, decay or sec * 0.7)
def fade(x, sec=0.01):
    n = int(sec * SR); x = x.copy(); x[-n:] *= np.linspace(1, 0, n); return x

C5, E5, G5, C6 = 523.25, 659.25, 784.0, 1046.5
fx = {}
fx['build'] = (mix(sine(100, 0.2) * env(int(0.2 * SR), 0.002, 0.06), seq([note(392, 0.1), note(587, 0.18)])), -9)
fx['error'] = (square(150, 0.16) * env(int(0.16 * SR), 0.002, 0.1), -11)
fx['arrow'] = (mix(sweep(2200, 700, 0.09) * 0.5, lowpass(noise(0.09), 3) * 0.6) * env(int(0.09 * SR), 0.002, 0.05), -15)
fx['catapult'] = (mix(sweep(150, 55, 0.22) * env(int(0.22 * SR), 0.002, 0.1), noise(0.03) * env(int(0.03 * SR), 0.001, 0.015) * 0.7), -9)
fx['freeze'] = (mix(sine(1600, 0.2), sine(2400, 0.2) * 0.6) * env(int(0.2 * SR), 0.002, 0.1), -13)
fx['die'] = (mix(sweep(520, 140, 0.1) * env(int(0.1 * SR), 0.001, 0.05), noise(0.02) * 0.5), -12)
fx['lifelost'] = (saw(0, 0.01), -8)
x = sweep(330, 110, 0.32); fx['lifelost'] = ((x * 0.6 + np.sign(x) * 0.2) * env(len(x), 0.004, 0.2), -8)
fx['upgrade'] = (seq([note(C5, 0.09), note(E5, 0.09), note(G5, 0.09), note(C6, 0.22)]), -9)
fx['sell'] = (mix(note(1318.5, 0.2, sine, 0.07), np.concatenate([np.zeros(int(0.06 * SR)), note(1760, 0.2, sine, 0.07)])), -10)
tick = lambda: noise(0.018) * env(int(0.018 * SR), 0.001, 0.008)
fx['repair'] = (mix(seq([tick(), np.zeros(int(0.04 * SR)), tick(), np.zeros(int(0.04 * SR)), tick()]), sweep(400, 760, 0.2) * 0.35 * env(int(0.2 * SR), 0.01, 0.12)), -12)
fx['crumble'] = (lowpass(noise(0.45), 6) * env(int(0.45 * SR), 0.003, 0.22), -10)
h = mix(saw(220, 0.5), saw(331, 0.5) * 0.8); h = lowpass(h, 8); fx['wave'] = (h * env(len(h), 0.05, 0.4, 1.0), -9)
fx['tick'] = (sine(880, 0.07) * env(int(0.07 * SR), 0.002, 0.03), -10)
fx['go'] = (mix(sine(1320, 0.28), sine(1980, 0.28) * 0.4) * env(int(0.28 * SR), 0.003, 0.14), -8)
fx['win'] = (seq([note(C5, 0.16), note(E5, 0.16), note(G5, 0.16), mix(note(C6, 0.9, tri, 0.5), note(G5, 0.9, tri, 0.5) * 0.6)]), -7)
fx['lose'] = (seq([note(220, 0.25, tri), note(174.6, 0.25, tri), note(146.8, 0.25, tri), note(110, 0.6, tri, 0.4)]), -8)
fx['move'] = (square(1100, 0.025) * env(int(0.025 * SR), 0.001, 0.015), -21)
fx['select'] = (sine(1500, 0.07) * env(int(0.07 * SR), 0.002, 0.035), -16)

out = sys.argv[1]
os.makedirs(out, exist_ok=True)
for name, (x, peak_db) in fx.items():
    x = fade(x)
    x = x / (np.max(np.abs(x)) + 1e-9) * (10 ** (peak_db / 20.0))
    pcm = (np.clip(x, -1, 1) * 32767).astype('<i2')
    with wave.open(os.path.join(out, name + '.wav'), 'wb') as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(SR); w.writeframes(pcm.tobytes())
print('wrote', len(fx), 'effects to', out)
