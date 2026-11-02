#!/usr/bin/env python3
"""Measures every track under a music folder (EBU R128, ffmpeg) and writes music/loudness.txt: one line
"<folder>/<file> = <gain in dB>" per track, so all tracks play at the loudness of the quietest one and none
is boosted (the masters peak at 0 dBFS, so a boost would clip). The game reads it and applies the gain.
Usage: make_loudness.py <music folder>"""
import glob, os, re, subprocess, sys

root = sys.argv[1]
rows = []
for f in sorted(glob.glob(os.path.join(root, '*', '*.ogg'))):
    out = subprocess.run(['ffmpeg', '-nostats', '-hide_banner', '-i', f, '-af', 'ebur128=peak=true', '-f', 'null', '-'],
                         capture_output=True, text=True).stderr
    tail = out[out.rfind('Summary:'):]
    lufs = float(re.search(r'I:\s+(-?[\d.]+) LUFS', tail).group(1))
    rows.append((os.path.relpath(f, root).replace(os.sep, '/'), lufs))
target = min(l for _, l in rows)
with open(os.path.join(root, 'loudness.txt'), 'w') as out:
    out.write('# gain in dB per track, made by tools/make_loudness.py; the quietest track is the reference\n')
    for name, lufs in rows:
        out.write('%s = %.1f\n' % (name, target - lufs))
        print('%-32s %6.1f LUFS  gain %5.1f dB' % (name, lufs, target - lufs))
