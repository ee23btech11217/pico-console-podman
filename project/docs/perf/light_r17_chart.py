# Figure 30: Round 17 body lighting, benchmark FPS off vs on (two runs each) from docs/perf/results.json.
# usage: python3 light_r17_chart.py <out.png> <matplotlib dir>
import json
import sys
sys.path.insert(0, sys.argv[2])
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

R = json.load(open(__file__.rsplit("/", 1)[0] + "/results.json"))
runs = R if isinstance(R, list) else list(R.values())
by = {r["name"]: r for r in runs if isinstance(r, dict) and "name" in r}
INK, INK2, GRID, OFF, ON = "#0b0b0b", "#52514e", "#e4e2dc", "#a9a7a0", "#eb6834"
scenes = ["quiet", "crowd", "flight"]
fig, ax = plt.subplots(figsize=(6.4, 4.0))
fig.patch.set_facecolor("#fcfcfb"); ax.set_facecolor("#fcfcfb")
w = 0.16
for k, (tag, col, lab) in enumerate((("off", OFF, "flat"), ("on", ON, "lit"))):
    for j, run in enumerate("ab"):
        f = by[f"r17_light_{tag}_{run}"]["bench"]["FPS"]
        x = [i + (k * 2 + j - 1.5) * w for i in range(3)]
        ax.scatter(x, f, s=70, color=col, edgecolor="#fcfcfb", lw=1.5, zorder=3, label=f"{lab}" if j == 0 else None)
        for xi, v in zip(x, f):
            ax.text(xi, v - 0.55, f"{v:.1f}", ha="center", fontsize=8, color=INK2)
ax.set_xticks(range(3)); ax.set_xticklabels(scenes)
ax.set_ylim(40, 50); ax.set_ylabel("benchmark FPS", color=INK2)
ax.axhline(48.8, color=GRID, lw=1, ls="--"); ax.text(2.45, 49.05, "SPI ceiling 48.8", ha="right", fontsize=8, color=INK2)
ax.set_title("Body lighting: FPS, two runs each", fontsize=11, loc="left", color=INK)
ax.legend(frameon=False, fontsize=9, loc="upper left", ncol=2)
for s in ("top", "right"): ax.spines[s].set_visible(False)
for s in ("left", "bottom"): ax.spines[s].set_color(GRID)
ax.tick_params(colors=INK2); ax.yaxis.grid(True, color=GRID, lw=0.6); ax.set_axisbelow(True)
fig.tight_layout(); fig.savefig(sys.argv[1], dpi=130, facecolor=fig.get_facecolor())
