#!/usr/bin/env python3
"""Figure 27: Round 13 surface animation: core 1 time per world (Round 12 colour table only vs Round 13 height map +
ground layers + colour table) and the benchmark FPS with the animation off / on (same build otherwise, Terra forced).
Data: PLN lines (anim us/frame by palette) and BENCH tables of docs/perf/raw/r12_anim_on2.log, r13_anim_final.log,
r13_terra_on3.log, r13_ship.log, r13_terra_off3.log (docs/perf/results.json).

    PYTHONPATH=<matplotlib dir> python3 docs/perf/anim_r13_chart.py
"""
import json, os, re

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

HERE = os.path.dirname(os.path.abspath(__file__))
RAW = os.path.join(HERE, "raw")
DB = json.load(open(os.path.join(HERE, "results.json")))
OLD, NEW = "#898781", "#2a78d6"
SURF, INK, INK2, MUTED, GRID, AXIS = "#fcfcfb", "#0b0b0b", "#52514e", "#898781", "#e1e0d9", "#c3c2b7"
NAMES = ["Terra", "Mars", "Glacier", "Inferno", "Xenos"]  # world select names (palettes terra, mars, ice, lava, alien)


def anim_by_palette(*logs):
    s, c = {}, {}
    for name in logs:
        for line in open(os.path.join(RAW, name + ".log"), errors="replace"):
            m = re.match(r"PLN .*palette (\d).*anim (\d+) us/frame", line)
            if m and int(m.group(2)) > 0:
                p = int(m.group(1))
                s[p] = s.get(p, 0) + int(m.group(2))
                c[p] = c.get(p, 0) + 1
    return {p: s[p] / c[p] / 1000.0 for p in s}


r12 = anim_by_palette("r12_anim_on2")                       # Round 12: one world in that run, the LUT costs the same everywhere
r12_all = sum(r12.values()) / len(r12)
r13 = anim_by_palette("r13_anim_final", "r13_terra_on3", "r13_ship")
plt.rcParams.update({"font.family": "sans-serif", "font.size": 10, "text.color": INK})
fig, ax = plt.subplots(1, 2, figsize=(13, 4.4), dpi=130, facecolor=SURF, gridspec_kw={"width_ratios": [1.25, 1]})


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
x = list(range(len(NAMES)))
v12 = [r12_all] * len(NAMES)
v13 = [r13.get(i, 0) for i in x]
ax[0].bar([i - w / 2 for i in x], v12, w - 0.03, color=OLD, label="Round 12: colour table")
ax[0].bar([i + w / 2 for i in x], v13, w - 0.03, color=NEW, label="Round 13: + height map + ground layers")
for i in x:
    ax[0].text(i - w / 2, v12[i] + 0.06, "%.1f" % v12[i], ha="center", fontsize=9, color=INK2)
    ax[0].text(i + w / 2, v13[i] + 0.06, "%.1f" % v13[i], ha="center", fontsize=9, color=INK2)
ax[0].set_xticks(x, NAMES)
ax[0].set_ylim(0, 4.6)
style(ax[0], "Core 1 time in the animation, per frame", "ms per frame (frame = 22.5 ms)")
ax[0].legend(frameon=False, fontsize=9, loc="upper left")

off, on = DB["r13_terra_off3"]["bench"]["FPS"], DB["r13_terra_on3"]["bench"]["FPS"]
SC = ["Quiet", "Crowd", "Flight"]
ax[1].bar([i - w / 2 for i in range(3)], off, w - 0.03, color=OLD, label="animation off")
ax[1].bar([i + w / 2 for i in range(3)], on, w - 0.03, color=NEW, label="animation on")
for i in range(3):
    ax[1].text(i - w / 2, off[i] + 0.4, "%.1f" % off[i], ha="center", fontsize=9, color=INK2)
    ax[1].text(i + w / 2, on[i] + 0.4, "%.1f" % on[i], ha="center", fontsize=9, color=INK2)
ax[1].set_xticks(range(3), SC)
ax[1].set_ylim(0, 55)
style(ax[1], "Benchmark FPS: no cost", "FPS")
ax[1].legend(frameon=False, fontsize=9, loc="upper left", ncol=2)

fig.text(0.01, 0.01, "Board logs, 264 MHz / SPI 66 MHz. Round 12 measured on Inferno (r12_anim_on2); its colour-table work is the same on every world. "
         "Benchmark A/B with Terra forced (PLANET_FORCE_STYLE=0).",
         fontsize=8, color=MUTED)
fig.tight_layout(rect=(0, 0.05, 1, 1))
fig.savefig(os.path.join(HERE, "27_anim_round13.png"), facecolor=SURF)
print("ok", {NAMES[i]: round(v13[i], 2) for i in x}, round(r12_all, 2))
