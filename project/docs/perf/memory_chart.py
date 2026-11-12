#!/usr/bin/env python3
"""Figure 24: RAM and flash footprint of the shipping build (numbers from arm-none-eabi-size/nm of out/game.elf,
Round 13 build, and the whole-track music pack of Round 12).  PYTHONPATH=<matplotlib dir> python3 docs/perf/memory_chart.py"""
import os
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

HERE = os.path.dirname(os.path.abspath(__file__))
SURF, INK, INK2, MUTED, GRID, AXIS, BLUE, FREE = "#fcfcfb", "#0b0b0b", "#52514e", "#898781", "#e1e0d9", "#c3c2b7", "#2a78d6", "#c3c2b7"

# KB. RAM: 520 KB = 512 KB main SRAM + 8 KB scratch (stacks)
RAM = [("Code run from RAM (sim, render, gfx, planet)", 177.9), ("Game state (sim::Game)", 81.9),
       ("Two world surfaces (planet sets)", 64.0), ("Renderer arena", 64.0), ("LCD band buffers (2 × 40 rows)", 50.0),
       ("Renderer object", 36.0), ("Star field (free heap at run time)", 13.5), ("Planet shading LUT", 16.0),
       ("Stacks (scratch X/Y, both cores)", 8.0), ("Other data + bss", 6.7)]
FLASH = [("Free", 534.2), ("Music pack (2 whole tracks, 160 s + 264 s)", 3281.0), ("RAM-code load image", 178.6),
         ("Code run from flash (XIP)", 68.7), ("Read-only data (fonts, tables)", 29.5), ("High scores (last sector)", 4.0)]

plt.rcParams.update({"font.family": "sans-serif", "font.size": 10, "text.color": INK})
fig, (a1, a2) = plt.subplots(1, 2, figsize=(13, 4.8), dpi=130, facecolor=SURF)


def panel(ax, rows, title, xmax, free_label=None):
    rows = sorted(rows, key=lambda r: -r[1])
    ax.set_facecolor(SURF)
    for i, (lab, v) in enumerate(rows):
        c = FREE if lab == free_label else BLUE
        ax.barh(i, v, height=0.56, color=c, edgecolor=SURF, linewidth=1)
        ax.text(v + xmax * 0.012, i, f"{v:,.0f} KB" if v >= 10 else f"{v:.0f} KB", va="center", fontsize=9, color=INK2)
    ax.set_yticks(range(len(rows)))
    ax.set_yticklabels([r[0] for r in rows], color=INK2)
    ax.set_ylim(len(rows) - 0.5, -0.6)
    ax.set_xlim(0, xmax)
    for s in ("top", "right", "left"):
        ax.spines[s].set_visible(False)
    ax.spines["bottom"].set_color(AXIS)
    ax.tick_params(axis="y", length=0)
    ax.tick_params(axis="x", colors=MUTED)
    ax.grid(axis="x", color=GRID, lw=0.7)
    ax.set_axisbelow(True)
    ax.set_title(title, loc="left", fontsize=11.5, fontweight="bold", color=INK)
    ax.set_xlabel("KB", color=INK2)


panel(a1, RAM, "SRAM 520 KB: all in use (heap given to the star field)", 205)
panel(a2, FLASH, "QSPI flash 4 MB: 3.5 MB used", 3800, free_label="Free")
fig.text(0.01, 0.01, "From arm-none-eabi-size / nm of out/game.elf (Round 13 shipping build) and the Round 12 music pack. "
         "\nStar field from the INTRO log line (1,725 stars = 13,800 B of 15,916 B free, r13_input_test). Firmware image 277 KB = XIP code + read-only data + RAM-code load image.", fontsize=8, color=MUTED)
fig.tight_layout(rect=(0, 0.07, 1, 1))
fig.savefig(os.path.join(HERE, "24_memory_footprint.png"), facecolor=SURF)
print("ok")
