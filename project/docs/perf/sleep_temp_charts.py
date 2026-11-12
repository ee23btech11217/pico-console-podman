# Figures 19/20: die temperature during sleep, measured ("SLP alive <s> s temp <C>"), plot only.
# 19 = screen-only sleep (cores running), 20 = screen-only + both cores asleep (WFI/WFE). Same axes for comparison.
# usage: python3 sleep_temp_charts.py <out dir> <matplotlib dir> <old log> <new log>
import re
import sys
sys.path.insert(0, sys.argv[2])
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt


def pts(path):
    return [(int(m.group(1)), float(m.group(2))) for m in re.finditer(r"SLP alive (\d+) s temp ([\d.]+) C", open(path).read())]


for name, path, color in (("19_temp_sleep_cores_running", sys.argv[3], "#4c6ef5"),
                          ("20_temp_sleep_cores_asleep", sys.argv[4], "#2a9d8f")):
    p = pts(path)
    fig, ax = plt.subplots(figsize=(7, 3.8))
    ax.plot([a for a, _ in p], [b for _, b in p], color=color, lw=2, marker="o" if len(p) < 30 else None, ms=4)
    ax.set_xlim(0, 300); ax.set_ylim(55, 70)
    ax.set_xlabel("s asleep"); ax.set_ylabel("chip °C")
    for s in ("top", "right"):
        ax.spines[s].set_visible(False)
    ax.grid(alpha=0.25)
    fig.tight_layout()
    fig.savefig(f"{sys.argv[1]}/{name}.png", dpi=120)
    plt.close(fig)
    print(name, "n", len(p), "first", p[0], "last", p[-1], "mean", round(sum(b for _, b in p) / len(p), 2))
