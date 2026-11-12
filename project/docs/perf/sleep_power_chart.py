# Figure 18: sleep power, plot only. Current = estimate (typical published values, no sensor on the board);
# temperature = measured ("SLP alive <s> s temp <C>" log lines). Details in README Round 9c.
# usage: python3 sleep_power_chart.py <out.png> <matplotlib dir> <raw/sleep_screen_only_264mhz.log> <raw/sleep_cores_asleep_264mhz.log>
import re
import sys
sys.path.insert(0, sys.argv[2])
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

C_CHIP, C_BL, C_LCD = "#4c6ef5", "#f59f00", "#ae3ec9"
parts = [("backlight", C_BL, (50, 0, 0, 0)), ("LCD", C_LCD, (6, 0.1, 0.1, 0.1)), ("RP2350", C_CHIP, (55, 55, 45, 8))]
lo, hi = (66, 40, 35, 5), (156, 70, 60, 15)

fig, (ax, bx) = plt.subplots(1, 2, figsize=(12, 4.6), gridspec_kw={"width_ratios": [1.3, 1]})
for col in range(4):
    b = 0
    for name, c, v in parts:
        ax.bar(col, v[col], bottom=b, color=c, width=0.55, label=name if col == 0 else None)
        b += v[col]
    ax.errorbar(col, b, yerr=[[b - lo[col]], [hi[col] - b]], color="#222", capsize=5, lw=1)
    ax.text(col, hi[col] + 4, f"{b:.0f} mA", ha="center", fontsize=10, weight="bold")
ax.set_xticks([0, 1, 2, 3]); ax.set_xticklabels(["playing", "sleep\n264 MHz", "sleep\ncores off", "sleep\n48 MHz, gated,\n1.10 V"], fontsize=9)
ax.set_ylabel("mA (estimate)"); ax.set_ylim(0, 175); ax.legend(frameon=False, fontsize=9)

def pts(path):
    return [(int(m.group(1)), float(m.group(2))) for m in re.finditer(r"SLP alive (\d+) s temp ([\d.]+) C", open(path).read())]
for path, c, lab in ((sys.argv[3], C_CHIP, "cores run"), (sys.argv[4], "#2a9d8f", "cores asleep")):
    p = pts(path)
    bx.plot([a for a, _ in p], [b for _, b in p], color=c, lw=1.8, label=lab)
bx.legend(frameon=False, fontsize=9)
bx.set_xlabel("s asleep"); bx.set_ylabel("chip °C (measured)"); bx.set_ylim(50, 75)
for a in (ax, bx):
    for s in ("top", "right"):
        a.spines[s].set_visible(False)
fig.tight_layout()
fig.savefig(sys.argv[1], dpi=120)
