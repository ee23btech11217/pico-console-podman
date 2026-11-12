# Figure 22: the same temperature run measured two ways (README Round 9f), plot only.
# usage: python3 temp_fix_chart.py <out.png> <matplotlib dir> <log, regulator in power-save> <log, regulator in PWM for the read>
import re
import sys
sys.path.insert(0, sys.argv[2])
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

fig, ax = plt.subplots(figsize=(11, 4.2))
for path, c, lab in ((sys.argv[3], "#e03131", "3.3 V regulator in power-save (wrong)"),
                     (sys.argv[4], "#2f9e44", "regulator forced to PWM for the read")):
    p = [(int(m.group(1)) / 1000.0, float(m.group(2))) for m in re.finditer(r"TMP (\d+) ms phase \w+ temp ([\d.]+)", open(path, errors="replace").read())]
    ax.plot([a for a, _ in p], [b for _, b in p], color=c, lw=1.6, label=lab)
ax.axvspan(125, 215, color="#63e6be", alpha=0.2, lw=0)
ax.text(170, 0.97, "sleep", transform=ax.get_xaxis_transform(), ha="center", va="top", fontsize=9)
ax.set_xlabel("s since boot"); ax.set_ylabel("chip °C"); ax.legend(frameon=False, fontsize=9, loc="upper left")
for s in ("top", "right"):
    ax.spines[s].set_visible(False)
fig.tight_layout()
fig.savefig(sys.argv[1], dpi=120)
