#!/usr/bin/env python3
"""Figure 25: where each core's frame time goes, from the P / PLN lines that every `make log` prints.

    PYTHONPATH=<matplotlib dir> python3 docs/perf/time_util_chart.py [log] [phase]

Default: docs/perf/raw/r13_ship.log (the current build: 264 MHz, SPI 66 MHz, work sharing, late launch, Round 13
animation; the demo happened to run on Alien),
phase P3 (paced AI demo = real play). Per-frame averages of the 1 s log lines, first two lines skipped.
Core 0 slices: begin / raster (minus the planet pass) / planet pass / DMA wait / band start / rest of the period.
Core 1 slices: sim batch / planet chunks (paint time x help %, an estimate) / surface animation (PLN anim, Round 12)
/ idle (rest of the period).
"""
import os, re, sys

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

HERE = os.path.dirname(os.path.abspath(__file__))
LOG = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "raw", "r13_ship.log")
PHASE = sys.argv[2] if len(sys.argv) > 2 else "P3"
OUT = os.path.join(HERE, "25_time_utilisation.png")

SERIES = ["#2a78d6", "#eb6834", "#1baf7a", "#eda100", "#e87ba4", "#008300", "#6250d6", "#e34948"]  # categorical slots 1-8, fixed order
IDLE = "#c3c2b7"                                                              # neutral: not work
SURFACE, INK, INK2, MUTED = "#fcfcfb", "#0b0b0b", "#52514e", "#898781"

rows, cur = [], None
for line in open(LOG, errors="replace"):
    m = re.match(r"(P\d) FPS ([\d.]+)", line)
    if m:
        cur = None
        if m.group(1) == PHASE:
            g = lambda k: float(re.search(k + r" (\d+)", line).group(1))
            cur = {"fps": float(m.group(2)), "sim": g("sim"), "begin": g("begin"), "raster": g("raster"),
                   "wait": g("wait"), "cmd": g("cmd"), "per": float(re.search(r"per (\d+)/", line).group(1)),
                   "paint": 0.0, "help": 0.0, "anim": 0.0}
            rows.append(cur)
        continue
    m = re.match(r"PLN paint (\d+) us/frame.*help ([\d.]+)%", line)
    if m and cur is not None:
        cur["paint"], cur["help"] = float(m.group(1)), float(m.group(2)) / 100.0
        a = re.search(r"anim (\d+) us/frame", line)
        if a:
            cur["anim"] = float(a.group(1))
rows = rows[2:]
if not rows:
    sys.exit(f"no {PHASE} lines in {LOG}")
avg = {k: sum(r[k] for r in rows) / len(rows) for k in rows[0]}
per = avg["per"]

core0 = [("Prepare frame (begin)", avg["begin"]),
         ("3D + HUD raster", avg["raster"] - avg["paint"]),
         ("Planet surface", avg["paint"]),
         ("Wait for SPI DMA", avg["wait"]),
         ("Band start (cmd)", avg["cmd"])]
core0.append(("Loop / present", max(0.0, per - sum(v for _, v in core0))))
help_us = avg["paint"] * avg["help"]
core1 = [("Simulation batch", avg["sim"]), ("Planet chunks (est.)", help_us)]
if avg["anim"] > 0:
    core1.append(("Surface animation", avg["anim"]))
core1.append(("Idle / asleep / world build", max(0.0, per - sum(v for _, v in core1))))

plt.rcParams.update({"font.family": "sans-serif", "font.size": 10, "text.color": INK})
fig, axes = plt.subplots(1, 2, figsize=(12.5, 6.4), dpi=110, facecolor=SURFACE)


def pie(ax, parts, colors, title):
    vals = [v for _, v in parts]
    wedges, _ = ax.pie(vals, colors=colors, startangle=90, counterclock=False,
                       wedgeprops={"width": 0.42, "edgecolor": SURFACE, "linewidth": 2})
    ax.set_title(title, loc="center", fontsize=15, fontweight="bold", color=INK, pad=6)
    busy = 100.0 * (1 - parts[-1][1] / per) if parts is core1 else 100.0 * (per - avg["wait"]) / per
    ax.text(0, 0.06, f"{per / 1000:.1f} ms", ha="center", va="center", fontsize=15, fontweight="bold", color=INK)
    ax.text(0, -0.14, "per frame", ha="center", va="center", fontsize=9, color=INK2)
    ax.legend(wedges, [f"{n}  {v / 1000:.1f} ms  ({100 * v / per:.0f} %)" for n, v in parts],
              loc="upper center", bbox_to_anchor=(0.5, -0.02), frameon=False, fontsize=12.5, labelcolor=INK, ncol=1,
              columnspacing=1.6, handlelength=1.2)
    return busy


b0 = pie(axes[0], core0, SERIES[:len(core0)], "Core 0: display")
b1 = pie(axes[1], core1, [SERIES[6], SERIES[2], SERIES[7]][:len(core1) - 1] + [IDLE], "Core 1: simulation + helper")
print(f"{os.path.basename(LOG)} {PHASE}: {len(rows)} lines, {avg['fps']:.1f} FPS, core 0 computes {b0:.0f} %, core 1 busy {b1:.0f} %")
fig.subplots_adjust(left=0.03, right=0.97, wspace=0.25, top=0.93, bottom=0.33)
fig.savefig(OUT, facecolor=SURFACE)
print(OUT, {k: round(v) for k, v in avg.items()})
