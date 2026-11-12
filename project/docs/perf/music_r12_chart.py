#!/usr/bin/env python3
"""Figure 26: Round 12 music pack, old (75 s windows, Round 6 chain) vs new (whole tracks, bass cut + presence).
Data: docs/perf/raw/r12_music_levels.json (tools/music_levels.py) and the packer's sizes.

    PYTHONPATH=<matplotlib dir> python3 docs/perf/music_r12_chart.py
"""
import json, os

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

HERE = os.path.dirname(os.path.abspath(__file__))
L = json.load(open(os.path.join(HERE, "raw", "r12_music_levels.json")))
OLD, NEW = "#898781", "#2a78d6"   # neutral = before, categorical slot 1 = after
SURF, INK, INK2, MUTED, GRID, AXIS = "#fcfcfb", "#0b0b0b", "#52514e", "#898781", "#e1e0d9", "#c3c2b7"
TR = ["Track 1 (tiers 0-1)", "Track 2 (tiers 2-3)"]
plt.rcParams.update({"font.family": "sans-serif", "font.size": 10, "text.color": INK})
fig, ax = plt.subplots(1, 3, figsize=(13, 4.3), dpi=130, facecolor=SURF)


def style(a, title, ylabel):
    a.set_facecolor(SURF)
    a.set_title(title, loc="left", fontsize=11, fontweight="bold")
    a.set_ylabel(ylabel, color=INK2)
    for s in ("top", "right"):
        a.spines[s].set_visible(False)
    for s in ("left", "bottom"):
        a.spines[s].set_color(AXIS)
    a.tick_params(colors=INK2)
    a.grid(axis="y", color=GRID, lw=0.7)
    a.set_axisbelow(True)


w = 0.36
x = [0, 1]
old_s = [L[k]["old_s"] for k in ("0", "1")]
new_s = [L[k]["new_s"] for k in ("0", "1")]
ax[0].bar([i - w / 2 for i in x], old_s, w - 0.03, color=OLD, label="Round 6: 75 s window")
ax[0].bar([i + w / 2 for i in x], new_s, w - 0.03, color=NEW, label="Round 12: whole track")
for i in x:
    ax[0].text(i - w / 2, old_s[i] + 4, "%.0f s" % old_s[i], ha="center", fontsize=9, color=INK2)
    ax[0].text(i + w / 2, new_s[i] + 4, "%.0f s" % new_s[i], ha="center", fontsize=9, color=INK2)
ax[0].set_xticks(x, TR)
style(ax[0], "Music length before it loops", "seconds")
ax[0].legend(frameon=False, fontsize=9, loc="upper left")

# Loudness in the band the speaker plays, same passage. Bars start at -16 dBFS (a level axis, not a quantity)
FLOOR = -16
old_a = [L[k]["old_aud"] for k in ("0", "1")]
new_a = [L[k]["new_aud"] for k in ("0", "1")]
ax[1].bar([i - w / 2 for i in x], [v - FLOOR for v in old_a], w - 0.03, bottom=FLOOR, color=OLD, label="Round 6 chain")
ax[1].bar([i + w / 2 for i in x], [v - FLOOR for v in new_a], w - 0.03, bottom=FLOOR, color=NEW, label="Round 12 chain")
for i in x:
    ax[1].text(i - w / 2, old_a[i] + 0.15, "%.1f" % old_a[i], ha="center", fontsize=9, color=INK2)
    ax[1].text(i + w / 2, new_a[i] + 0.15, "%.1f" % new_a[i], ha="center", fontsize=9, color=INK2)
    ax[1].text(i, max(old_a[i], new_a[i]) + 0.9, "+%.1f dB" % (new_a[i] - old_a[i]), ha="center", fontsize=10,
               fontweight="bold", color=INK)
ax[1].set_ylim(FLOOR, -8)
ax[1].set_xticks(x, TR)
style(ax[1], "Loudness above 200 Hz, same passage", "RMS, dBFS")
ax[1].legend(frameon=False, fontsize=9, loc="upper right")

sizes = [("Round 6 pack", 1161, OLD), ("Round 12 pack", 3281, NEW)]
ax[2].bar([0, 1], [s[1] for s in sizes], 0.55, color=[s[2] for s in sizes])
ax[2].axhline(3580, color=INK2, lw=1, ls="--")
ax[2].text(-0.27, 3580 + 60, "music area 3,580 KB", ha="left", va="bottom", fontsize=9, color=INK2)
for i, s in enumerate(sizes):
    ax[2].text(i, s[1] + 50, "{:,} KB".format(s[1]), ha="center", fontsize=9, color=INK2)
ax[2].set_xticks([0, 1], [s[0] for s in sizes])
ax[2].set_ylim(0, 4000)
style(ax[2], "Flash used by the music", "KB")

fig.text(0.01, 0.01, "Packer: 4-pole high-pass 180 Hz + 3 dB at 2 kHz + 8 dB into the limiter (was 2-pole 90 Hz, flat, 5 dB). "
         "Peaks unchanged (0 dBFS). Loudness from the packs decoded on the PC (tools/music_levels.py); on the board: SND music-rms.",
         fontsize=8, color=MUTED)
fig.tight_layout(rect=(0, 0.05, 1, 1))
fig.savefig(os.path.join(HERE, "26_music_round12.png"), facecolor=SURF)
print("ok")
