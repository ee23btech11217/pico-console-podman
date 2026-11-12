#!/usr/bin/env python3
"""Figure 15 (redrawn in Round 13 for the current build): stored music footprint, the DAC interrupt's cost on core 0
at the shipping clock, and the frame rate before/after the stored-music engine (Round 6 measurement).
Data: pack sizes from tools/music_pack.py; interrupt cost (median of the SND lines) while playing at 100 % tempo from
PLAY_LOG (default the current scripted run, r13_input_test) and while silent from SILENT_LOG (default the current
benchmark run, r13_ship: no music there); FPS from docs/perf/results.json (m6_skin, m7_audio: Round 6, 400 MHz).

    PYTHONPATH=<matplotlib dir> python3 docs/perf/music_pack_chart.py [PLAY_LOG [SILENT_LOG]]
"""
import json, os, re, sys

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

HERE = os.path.dirname(os.path.abspath(__file__))
LOG = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "raw", "r13_input_test.log")
SILENT_LOG = sys.argv[2] if len(sys.argv) > 2 else os.path.join(HERE, "raw", "r13_ship.log")
DB = json.load(open(os.path.join(HERE, "results.json")))
OLD, NEW, SLOT2 = "#898781", "#2a78d6", "#eb6834"
SURF, INK, INK2, MUTED, GRID, AXIS = "#fcfcfb", "#0b0b0b", "#52514e", "#898781", "#e1e0d9", "#c3c2b7"

RX = re.compile(r"SND .*playing (\d).*isr ([\d.]+)% \((\d+) cycles each\).*speed (\d+)%")


def lines(path, want_playing):
    out, khz = [], None
    for line in open(path, errors="replace"):
        m = RX.match(line)
        if m and "dac 0/s" not in line and m.group(1) == ("1" if want_playing else "0") and m.group(4) == "100":
            out.append((float(m.group(2)), int(m.group(3))))
        c = re.match(r"CFG sys (\d+)", line)
        if c:
            khz = int(c.group(1))
    return out, khz


playing, sys_khz = lines(LOG, True)
silent, sys2 = lines(SILENT_LOG, False)
assert playing and silent and sys_khz == sys2, (len(playing), len(silent), sys_khz, sys2)
med = lambda v, i: sorted(x[i] for x in v)[len(v) // 2]
isr = [med(silent, 0), med(playing, 0)]
cyc = [med(silent, 1), med(playing, 1)]

plt.rcParams.update({"font.family": "sans-serif", "font.size": 10, "text.color": INK})
fig, ax = plt.subplots(1, 3, figsize=(12.5, 3.9), dpi=130, facecolor=SURF)


def style(a, title, ylabel):
    a.set_facecolor(SURF)
    a.set_title(title, loc="left", fontsize=10.5, fontweight="bold")
    a.set_ylabel(ylabel, color=INK2)
    for s in ("top", "right"):
        a.spines[s].set_visible(False)
    for s in ("left", "bottom"):
        a.spines[s].set_color(AXIS)
    a.tick_params(colors=INK2)
    a.grid(axis="y", color=GRID, lw=0.7)
    a.set_axisbelow(True)


sizes = [("75 s of each\n(Round 6)", 1161, OLD), ("whole tracks\n(used now)", 3281, NEW), ("music area", 3580, AXIS)]
ax[0].bar(range(3), [s[1] for s in sizes], 0.6, color=[s[2] for s in sizes])
for i, s in enumerate(sizes):
    ax[0].text(i, s[1] + 60, "{:,} KB".format(s[1]), ha="center", fontsize=9, color=INK2)
ax[0].set_xticks(range(3), [s[0] for s in sizes])
ax[0].set_ylim(0, 4100)
style(ax[0], "Music footprint in flash", "KB")

ax[1].bar([0, 1], isr, 0.55, color=[OLD, NEW])
for i in range(2):
    ax[1].text(i, isr[i] + 0.04, "%.1f %%\n%.0f cycles" % (isr[i], cyc[i]), ha="center", fontsize=9, color=INK2)
ax[1].set_xticks([0, 1], ["silent", "playing"])
ax[1].set_ylim(0, 2.3)
style(ax[1], "DAC interrupt, 31.25 kHz, at %d MHz" % ((sys_khz or 0) // 1000), "% of core 0")

A, B = DB["m6_skin"]["bench"]["FPS"], DB["m7_audio"]["bench"]["FPS"]
w = 0.36
SC = ["Quiet", "Crowd", "Flight"]
ax[2].bar([i - w / 2 for i in range(3)], A, w - 0.03, color=OLD, label="synth tune")
ax[2].bar([i + w / 2 for i in range(3)], B, w - 0.03, color=SLOT2, label="stored music engine")
for i in range(3):
    ax[2].text(i - w / 2, A[i] + 0.4, "%.1f" % A[i], ha="center", fontsize=8, color=INK2)
    ax[2].text(i + w / 2, B[i] + 0.4, "%.1f" % B[i], ha="center", fontsize=8, color=INK2)
ax[2].set_xticks(range(3), SC)
ax[2].set_ylim(0, 62)
ax[2].legend(frameon=False, fontsize=8.5, loc="upper left", ncol=2)
style(ax[2], "FPS before / after (Round 6, 400 MHz)", "benchmark FPS")
fig.text(0.01, 0.01, "Interrupt cost: medians of the SND lines in %s (playing, tempo 100 %%; +0.1 point at 127 %%) and %s (silent). "
         "Round 6 measured 0.9 %% at 400 MHz." % (os.path.basename(LOG), os.path.basename(SILENT_LOG)),
         fontsize=8, color=MUTED)
fig.tight_layout(rect=(0, 0.05, 1, 1))
fig.savefig(os.path.join(HERE, "15_music_pack.png"), facecolor=SURF)
print("ok isr", [round(v, 2) for v in isr], "cycles", [round(v) for v in cyc], "sys", sys_khz)
