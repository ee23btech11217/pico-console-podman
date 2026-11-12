#!/usr/bin/env python3
"""Loudness of two music packs, old vs new (README Round 12), on the same passages.

    python3 tools/music_levels.py FFMPEG OLD.bin NEW.bin OUT.json

Decodes both packs with the packer's reference decoder and measures RMS with ffmpeg's astats, full band and
"audible" (4-pole high-pass at 200 Hz: about what the small speaker plays). The old pack holds 75 s windows
starting at 75 s / 179 s (what `--window 75` chose); the new pack is cut to the same passages for the comparison.
"""
import os, struct, subprocess, sys, json
_HERE = os.path.dirname(os.path.abspath(__file__))
src = open(os.path.join(_HERE, "music_pack.py")).read().replace("\nmain()\n", "\n")
mp = {}; exec(compile(src, "music_pack", "exec"), mp)
FF = sys.argv[1]
def tracks(path):
    d = open(path, "rb").read(); n = d[5]; out = []
    for i in range(n):
        off, blocks, chk, _ = struct.unpack_from("<IIII", d, 16 + 40 * i)
        out.append(mp["decode"](d[off:off + blocks * 256], blocks))
    return out
def rms(pcm, hp):
    af = ("highpass=f=200:poles=2,highpass=f=200:poles=2," if hp else "") + "astats=metadata=0"
    r = subprocess.run([FF, "-hide_banner", "-nostats", "-f", "s16le", "-ar", "15625", "-ac", "1", "-i", "-", "-af", af, "-f", "null", "-"],
                       input=struct.pack("<%dh" % len(pcm), *pcm), capture_output=True).stderr.decode()
    return float([l for l in r.splitlines() if "RMS level dB" in l][0].split()[-1])
old, new = tracks(sys.argv[2]), tracks(sys.argv[3])
starts = [75, 179]
res = {}
for i in range(2):
    s = starts[i] * 15625
    seg = new[i][s:s + 75 * 15625]
    res[i] = {"old_full": rms(old[i], 0), "old_aud": rms(old[i], 1), "new_full": rms(seg, 0), "new_aud": rms(seg, 1),
              "whole_aud": rms(new[i], 1), "old_s": len(old[i]) / 15625, "new_s": len(new[i]) / 15625}
    print(i, {k: round(v, 2) for k, v in res[i].items()})
json.dump(res, open(sys.argv[4], "w"), indent=1)
