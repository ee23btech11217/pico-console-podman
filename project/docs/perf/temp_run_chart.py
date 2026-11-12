# Figure 21: die temperature through quiet play -> crowd -> sleep -> crowd (GAME_TEMP_TEST run, README Round 9f).
# usage: python3 temp_run_chart.py <out.png> <matplotlib dir> <log>
import re
import sys
sys.path.insert(0, sys.argv[2])
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

log = open(sys.argv[3], errors="replace").read()
pts = [(int(m.group(1)) / 1000.0, m.group(2), float(m.group(3)))
       for m in re.finditer(r"TMP (\d+) ms phase (\w+) temp ([\d.]+)", log)]
COL = {"boot": "#adb5bd", "quiet": "#74c0fc", "crowd": "#ff8787", "sleep": "#63e6be", "after": "#ff8787"}
fig, ax = plt.subplots(figsize=(11, 4.4))
# phase bands from consecutive samples
start = 0
for i in range(1, len(pts) + 1):
    if i == len(pts) or pts[i][1] != pts[start][1]:
        x0 = pts[start][0]; x1 = pts[i][0] if i < len(pts) else pts[-1][0]
        ax.axvspan(x0, x1, color=COL.get(pts[start][1], "#eee"), alpha=0.25, lw=0)
        ax.text((x0 + x1) / 2, 0.97, pts[start][1], transform=ax.get_xaxis_transform(), ha="center", va="top", fontsize=9)
        start = i
ax.plot([p[0] for p in pts], [p[2] for p in pts], color="#222", lw=1.6, marker="o", ms=2.5)
ax.set_xlabel("s since boot"); ax.set_ylabel("chip °C")
for s in ("top", "right"):
    ax.spines[s].set_visible(False)
fig.tight_layout()
fig.savefig(sys.argv[1], dpi=120)
by = {}
for t, ph, c in pts:
    by.setdefault(ph, []).append(c)
for ph, v in by.items():
    print(f"{ph:6s} n {len(v):3d} first {v[0]:.1f} last {v[-1]:.1f} min {min(v):.1f} max {max(v):.1f} mean {sum(v)/len(v):.1f}")
