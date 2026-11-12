import sys
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import FancyBboxPatch

OUT = sys.argv[1]
SURF, INK, INK2, MUTED, BLUE, GRAY = "#fcfcfb", "#0b0b0b", "#52514e", "#898781", "#2a78d6", "#e1e0d9"
plt.rcParams.update({"font.family": "sans-serif", "font.size": 10, "text.color": INK})

# (crumb, title, detail, ours)
ROW1 = [(None, "Power-on reset", "PoR / BOR, RUN pin", False),
        (None, "Power-on state\nmachine", "ROSC, fabric, ROM, OTP,\nXOSC, SRAM, XIP (Fig. 30)", False),
        (None, "Boot ROM", "finds IMAGE_DEF,\nsets up XIP", False),
        (None, "SDK runtime", "XOSC 12 MHz, PLLs,\n150 MHz, 1.10 V", False),
        (1, "Fault + clock guard", "last HardFault from .noinit;\nwatchdog 3 s, scratch0 flag", True),
        (2, "Clock", "QSPI div 3 first, 1.15 V\n+10 ms, PLL 264 MHz", True)]
ROW2 = [(3, "USB stdio", "CDC log port", True),
        (4, "Pins + buses", "backlight off, CS high;\nSPI0 40 MHz mode 0;\npull-ups, ADC0/1", True),
        (5, "Sound", "music pack check +\ndecoder self-test;\nPWM DAC, chirp", True),
        (6, "LCD init", "RESX, SWRESET, SLPOUT,\nRGB565, MADCTL, DISPON;\nSPI 66 MHz (~420 ms)", True),
        (7, "Splash 2 s", "name screen;\nB press = reboot", True),
        (8, "Game init + run", "seed, high scores (CRC),\nSPI floor 20.5 ms;\ncore 1, first world", True)]

fig, ax = plt.subplots(figsize=(13, 5.0), dpi=150, facecolor=SURF)
ax.set_facecolor(SURF)
ax.set_xlim(0, 6)
ax.set_ylim(-0.14, 2.2)
ax.axis("off")
BW, BH = 0.86, 0.78


def row(items, y):
    for i, (crumb, title, detail, ours) in enumerate(items):
        x = i + 0.07
        ax.add_patch(FancyBboxPatch((x, y), BW, BH, boxstyle="round,pad=0,rounding_size=0.05",
                                    facecolor=BLUE if ours else GRAY, edgecolor="none"))
        tc = "#ffffff" if ours else INK
        ax.text(x + BW / 2, y + BH - 0.17, title, ha="center", va="top", fontsize=10.5, fontweight="bold", color=tc)
        ax.text(x + BW / 2, y + 0.07, detail, ha="center", va="bottom", fontsize=8.2, color=tc, linespacing=1.15)
        if crumb is not None:
            ax.text(x + 0.04, y + BH - 0.04, f"crumb {crumb}", fontsize=7.5, color="#d6e6fa", va="top")
        if i < len(items) - 1:
            ax.annotate("", xy=(x + BW + 0.12, y + BH / 2), xytext=(x + BW + 0.005, y + BH / 2),
                        arrowprops=dict(arrowstyle="-|>", color=MUTED, lw=1.2))


row(ROW1, 1.30)
row(ROW2, 0.18)
# wrap arrow from the end of row 1 to the start of row 2
ax.annotate("", xy=(0.5, 0.18 + BH + 0.005), xytext=(5.5, 1.30 - 0.005),
            arrowprops=dict(arrowstyle="-|>", color=MUTED, lw=1.2, connectionstyle="arc3,rad=-0.04"))
ax.add_patch(FancyBboxPatch((0.07, -0.1), 0.18, 0.08, boxstyle="round,pad=0,rounding_size=0.02", facecolor=GRAY, edgecolor="none"))
ax.text(0.29, -0.06, "chip / SDK (stock)", va="center", fontsize=9, color=INK2)
ax.add_patch(FancyBboxPatch((1.4, -0.1), 0.18, 0.08, boxstyle="round,pad=0,rounding_size=0.02", facecolor=BLUE, edgecolor="none"))
ax.text(1.62, -0.06, "our code", va="center", fontsize=9, color=INK2)
fig.tight_layout(pad=0.3)
fig.savefig(f"{OUT}/bootflow.png", facecolor=SURF)
print("ok")
