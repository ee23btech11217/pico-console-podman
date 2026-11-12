#!/usr/bin/env python3
"""Figure 23: two-core scheduling benchmark (README Round 11), from docs/perf/results.json (r11_* runs).

    PYTHONPATH=<matplotlib dir> python3 docs/perf/sched_chart.py
"""
import json, os

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.path import Path
from matplotlib.patches import PathPatch

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "23_scheduling.png")

# (run name, label, is the new default)
RUNS = [
    ("r11_single", "Single core", False),
    ("r11_pipe_early", "Pipeline, early start", False),
    ("r11_pipe_late", "Pipeline, late start (old default)", False),
    ("r11_share4_late", "Sharing 4 rows, late start (new default)", True),
    ("r11_share4_early", "Sharing 4 rows, early start", False),
    ("r11_share2_late", "Sharing 2 rows, late start", False),
    ("r11_share8_late", "Sharing 8 rows, late start", False),
]
SCENES = ["Quiet", "Crowd", "Flight"]          # benchmark columns, in this order
SERIES = ["#2a78d6", "#eb6834", "#1baf7a"]      # categorical slots 1-3 (validated light mode)
SURFACE, INK, INK2, MUTED, GRID, AXIS = "#fcfcfb", "#0b0b0b", "#52514e", "#898781", "#e1e0d9", "#c3c2b7"
NEUTRAL = "#52514e"                             # single-series panel: no categorical identity
SPI_FLOOR_MS = 20.5                             # one full-screen transfer at 66 MHz (XFR in the logs)

db = json.load(open(os.path.join(HERE, "results.json")))
fps = [db[r]["bench"]["FPS"] for r, _, _ in RUNS]
lat = [db[r]["demo"]["lat_us"] / 1000.0 for r, _, _ in RUNS]

plt.rcParams.update({"font.family": "sans-serif", "font.size": 10, "text.color": INK, "axes.labelcolor": INK2,
                     "xtick.color": MUTED, "ytick.color": INK2})
fig, (a1, a2) = plt.subplots(1, 2, figsize=(12.5, 5.2), dpi=110, gridspec_kw={"width_ratios": [2.1, 1]},
                             facecolor=SURFACE)


def hbar(ax, y, w, h, color, r_px=4):
    """Horizontal bar from 0 to w, square at the baseline, 4 px rounded data end."""
    fig.canvas.draw()
    bb = ax.get_window_extent()
    x0, x1 = ax.get_xlim()
    y0, y1 = ax.get_ylim()
    rx = r_px * (x1 - x0) / bb.width
    ry = min(r_px * abs(y1 - y0) / bb.height, h / 2)
    rx = min(rx, w)
    yt, yb = y - h / 2, y + h / 2
    k = 0.5523  # cubic approximation of a quarter circle
    verts = [(0, yt), (w - rx, yt), (w - rx + k * rx, yt), (w, yt + ry - k * ry), (w, yt + ry),
             (w, yb - ry), (w, yb - ry + k * ry), (w - rx + k * rx, yb), (w - rx, yb), (0, yb), (0, yt)]
    codes = [Path.MOVETO, Path.LINETO, Path.CURVE4, Path.CURVE4, Path.CURVE4, Path.LINETO, Path.CURVE4,
             Path.CURVE4, Path.CURVE4, Path.LINETO, Path.CLOSEPOLY]
    ax.add_patch(PathPatch(Path(verts, codes), facecolor=color, edgecolor=SURFACE, linewidth=1.0))


def style(ax, xmax, xlabel):
    ax.set_facecolor(SURFACE)
    ax.set_xlim(0, xmax)
    ax.set_ylim(len(RUNS) - 0.5, -0.5)
    for s in ("top", "right", "left"):
        ax.spines[s].set_visible(False)
    ax.spines["bottom"].set_color(AXIS)
    ax.tick_params(axis="y", length=0)
    ax.tick_params(axis="x", color=AXIS)
    ax.grid(axis="x", color=GRID, linewidth=0.8)
    ax.set_axisbelow(True)
    ax.set_xlabel(xlabel)


# --- panel 1: benchmark FPS per scene
ceiling = 1000.0 / SPI_FLOOR_MS
style(a1, 52, "Benchmark FPS (higher is better)")
bh = 0.24
for i, vals in enumerate(fps):
    for j, v in enumerate(vals):
        y = i + (j - 1) * (bh + 0.02)
        hbar(a1, y, v, bh, SERIES[j])
        if RUNS[i][2]:  # direct labels only on the new default
            a1.text(v + 0.5, y, f"{v:.1f}", va="center", ha="left", fontsize=8.5, color=INK2)
a1.axvline(ceiling, color=MUTED, linewidth=1.2, linestyle=(0, (4, 3)))
a1.text(ceiling - 0.4, -0.62, f"SPI ceiling {ceiling:.1f} FPS", ha="right", va="bottom", fontsize=8.5, color=INK2)
a1.set_yticks(range(len(RUNS)))
a1.set_yticklabels([lab for _, lab, _ in RUNS])
for t, (_, _, best) in zip(a1.get_yticklabels(), RUNS):
    if best:
        t.set_fontweight("bold")
        t.set_color(INK)
handles = [plt.Rectangle((0, 0), 1, 1, color=c) for c in SERIES]
a1.legend(handles, SCENES, title="Benchmark scene", loc="lower left", bbox_to_anchor=(0.0, 1.04), ncol=3,
          frameon=False, fontsize=9, title_fontsize=9, alignment="left")

# --- panel 2: input-to-screen latency in the paced AI demo
style(a2, 52, "Input-to-screen latency, ms (lower is better)")
for i, v in enumerate(lat):
    hbar(a2, i, v, 0.42, NEUTRAL)
    a2.text(v + 0.8, i, f"{v:.1f}", va="center", ha="left", fontsize=8.5,
            color=INK if RUNS[i][2] else INK2, fontweight="bold" if RUNS[i][2] else "normal")
a2.set_yticks(range(len(RUNS)))
a2.set_yticklabels([])
a2.set_title("Mean latency, AI demo", loc="left", fontsize=9, color=INK2, pad=24)

fig.suptitle("Two-core scheduling on RP2350 at 264 MHz, LCD SPI 66 MHz", x=0.012, ha="left", fontsize=13,
             fontweight="bold", color=INK)
fig.text(0.012, 0.012, "Sharing = both cores paint the planet surface in chunks; late/early = when the simulation "
         "batch starts in the frame. Source: tools/perf_run.py, docs/perf/results.json (r11_*).",
         fontsize=8, color=MUTED, ha="left")
fig.tight_layout(rect=(0, 0.04, 1, 0.975))
# bars were placed before tight_layout moved the axes: redraw them with the final geometry
for ax in (a1, a2):
    for p in list(ax.patches):
        p.remove()
for i, vals in enumerate(fps):
    for j, v in enumerate(vals):
        hbar(a1, i + (j - 1) * (bh + 0.02), v, bh, SERIES[j])
for i, v in enumerate(lat):
    hbar(a2, i, v, 0.42, NEUTRAL)
fig.savefig(OUT, facecolor=SURFACE)
print(OUT)
