import sys
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import FancyBboxPatch

OUT = sys.argv[1]
SURF, INK, INK2, MUTED, GRID = "#fcfcfb", "#0b0b0b", "#52514e", "#898781", "#e1e0d9"
BLUE, ORANGE, AQUA, VIOLET = "#2a78d6", "#eb6834", "#1baf7a", "#6250d6"
plt.rcParams.update({"font.family": "sans-serif", "font.size": 11, "text.color": INK})


def block(ax, x, w, y, color, label=None, h=0.62, tc="#ffffff"):
    ax.add_patch(FancyBboxPatch((x + 0.06, y - h / 2), max(w - 0.12, 0.05), h, boxstyle="round,pad=0,rounding_size=0.12",
                                facecolor=color, edgecolor="none"))
    if label:
        ax.text(x + w / 2, y, label, ha="center", va="center", fontsize=9, color=tc)


# ---------- frame timeline: Round 11 work sharing vs the current build (logged averages, schematic)
# Round 11: r11_share4_late (begin 3.4 ms, 22.2 ms/frame). Current: r13_ship (begin 3.7 ms, sim 6.2 ms, 22.6 ms/frame);
# core 1 sleeps in WFE during core 0's scene setup and animates the surface in its remaining gaps.
RED, LIGHT = "#e34948", "#c3c2b7"
fig, axes = plt.subplots(2, 1, figsize=(11, 4.6), dpi=150, facecolor=SURF, sharex=True)
cases = [("Round 11: work sharing   (22.2 ms / frame)", 3.4, 2.1, 6.6, False, 22.2),
         ("Current (Round 13): + surface animation   (22.6 ms / frame)", 3.7, 2.1, 6.2, True, 22.6)]
XFER = 20.5 / 6
for ax, (title, begin, band, sim, anim, per) in zip(axes, cases):
    ax.set_facecolor(SURF)
    block(ax, 0, begin, 2, INK2, "scene")
    if anim:
        block(ax, 0, begin, 1, LIGHT, "asleep (WFE)", tc=INK)
    t = begin
    spi_free = 0.0
    busy = []
    for b in range(6):
        block(ax, t, band, 2, BLUE, f"band {b + 1}")
        if b < 4:
            block(ax, t, band, 1, AQUA, "help")
            busy.append((t, t + band))
        start = max(t + band, spi_free)
        block(ax, start, XFER, 0, ORANGE, f"{b + 1}")
        spi_free = start + XFER
        t = max(t + band, spi_free - XFER) if b < 5 else t + band
    sim0 = per - sim - 1.0
    block(ax, sim0, sim, 1, VIOLET, "simulation")
    busy.append((sim0, sim0 + sim))
    if anim:  # the animation fills core 1's gaps (schematic: its measured total is 3.0 ms of them)
        busy.sort()
        edge = begin
        for a0, a1 in busy:
            if a0 - edge > 0.45:
                block(ax, edge, a0 - edge, 1, RED, "anim" if a0 - edge > 0.95 else None)
            edge = max(edge, a1)
    ax.set_yticks([0, 1, 2])
    ax.set_yticklabels(["SPI DMA", "Core 1", "Core 0"], color=INK2)
    ax.set_ylim(-0.6, 2.6)
    ax.axvline(per, color=MUTED, lw=1, ls=(0, (4, 3)))
    ax.text(0, 2.75, title, fontsize=11.5, fontweight="bold", va="bottom")
    for sp in ("top", "right", "left"):
        ax.spines[sp].set_visible(False)
    ax.spines["bottom"].set_color("#c3c2b7")
    ax.tick_params(axis="y", length=0)
    ax.grid(axis="x", color=GRID, lw=0.7)
    ax.set_axisbelow(True)
axes[1].set_xlim(0, 27.2)
axes[1].set_xlabel("ms within one frame (schematic, from logged averages)", color=INK2)
fig.tight_layout()
fig.savefig(f"{OUT}/timeline.png", facecolor=SURF)

# ---------- flash map (4 MB QSPI)
fig, ax = plt.subplots(figsize=(10, 2.0), dpi=150, facecolor=SURF)
ax.set_facecolor(SURF)
parts = [(0, 0.5, BLUE, ""),
         (0.5, 3.496, AQUA, "music area   0x080000   3,281 KB used: 2 whole tracks, 424 s, 4-bit IMA ADPCM, kept by make flash"),
         (3.996, 0.004, ORANGE, "")]
for x, w, c, lab in parts:
    ax.add_patch(FancyBboxPatch((x, 0), w, 1, boxstyle="round,pad=0,rounding_size=0.04", facecolor=c, edgecolor=SURF, lw=2))
    if lab:
        ax.text(x + 0.05, 0.5, lab, va="center", ha="left", fontsize=10, color="#ffffff" if c != AQUA else INK)
ax.annotate("firmware  0x000000\n277 KB, build fails > 512 KB", xy=(0.25, 1.0), xytext=(0.25, 1.55), fontsize=9.5, ha="center", color=INK, arrowprops=dict(arrowstyle="-", color=INK2, lw=1))
ax.annotate("high scores\nlast 4 KB sector\n0x3FF000 (CRC)", xy=(4.0, 1.0), xytext=(3.55, 1.55), fontsize=9.5, ha="center",
            color=INK, arrowprops=dict(arrowstyle="-", color=INK2, lw=1))
ax.set_xlim(-0.02, 4.05)
ax.set_ylim(-0.1, 2.3)
ax.axis("off")
ax.text(0, -0.08, "0 MB", fontsize=9, color=MUTED, va="top")
ax.text(4.0, -0.08, "4 MB", fontsize=9, color=MUTED, va="top", ha="right")
fig.tight_layout()
fig.savefig(f"{OUT}/flashmap.png", facecolor=SURF)
print("ok")
