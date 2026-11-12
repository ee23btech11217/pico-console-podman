# Figure 28: battery life on 200 mAh and a guesstimated power split (left), from the current measured by the user (Round 16, new board, 3x AA into VSYS, meter in series) and the
# sleep revisions that brought it down. Measured = user's meter, battery side. Earlier bars are the Round 9 estimates
# (3.3 V rail, figure 18), converted to mW so both share one unit; battery power uses 4.5 V (3 x 1.5 V nominal).
# usage: python3 power_measured_chart.py <out.png> <matplotlib dir>
import sys
sys.path.insert(0, sys.argv[2])
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

INK, INK2, MUTED, GRID = "#0b0b0b", "#52514e", "#898781", "#e4e2dc"
MEAS, EST = "#2a78d6", "#a9c6ec"
VBAT = 4.5

CAP_MAH = 200                     # battery capacity the user asked to plan with
# Guesstimated split of the battery power per mode (%), not measured: backlight ~50 mA and the RP2350 ~40-70 mA at
# 3.3 V (README Round 9c), speaker average while music plays, ILI/ST controller ~6 mA, the rest = regulator loss,
# flash, stick potentiometers (0.7 mA). In sleep the backlight and speaker are off, the panel is in SLPIN.
PARTS = [("RP2350", "#2a78d6"), ("backlight", "#eb6834"), ("speaker", "#1baf7a"), ("LCD controller", "#eda100"),
         ("regulator + other", "#e87ba4")]
modes = [("playing", 120, (35, 30, 12, 5, 18)), ("idle", 100, (32, 36, 9, 5, 18)), ("sleep", 10, (50, 0, 0, 5, 45))]

fig = plt.figure(figsize=(12.5, 4.9))
gs = fig.add_gridspec(2, 2, width_ratios=[1.15, 1], height_ratios=[1, 1], hspace=0.75, wspace=0.22)
ax, cx, bx = fig.add_subplot(gs[0, 0]), fig.add_subplot(gs[1, 0]), fig.add_subplot(gs[:, 1])
fig.patch.set_facecolor("#fcfcfb")

rows = list(range(len(modes)))[::-1]
for r, (name, ma, _) in zip(rows, modes):
    h = CAP_MAH / ma
    ax.barh(r, h, height=0.55, color=MEAS)
    txt = f"{int(h)} h {round((h - int(h)) * 60):02d} min" if h < 10 else f"{h:.0f} h"
    ax.text(h + 0.3, r, f"{txt}   ({ma} mA, {ma * VBAT / 1000:.2f} W)", va="center", fontsize=10, color=INK)
ax.set_yticks(rows); ax.set_yticklabels([m[0] for m in modes], fontsize=10)
ax.set_xlim(0, 30); ax.set_xlabel("hours on a 200 mAh battery (measured current, ideal capacity)", color=INK2, fontsize=9)
ax.set_title(f"Battery life: {CAP_MAH} mAh, 3× AA ≈ 4.5 V into VSYS", fontsize=11, color=INK, loc="left")

for r, (name, ma, shares) in zip(rows, modes):
    left = 0
    for (part, col), sh in zip(PARTS, shares):
        if sh:
            cx.barh(r, sh, left=left, height=0.55, color=col, edgecolor="#fcfcfb", lw=2,
                    label=part if r == rows[0] else None)
            if sh >= 5:
                cx.text(left + sh / 2, r, f"{sh}%", ha="center", va="center", fontsize=9, color=INK)
        left += sh
cx.set_yticks(rows); cx.set_yticklabels([m[0] for m in modes], fontsize=10)
cx.set_xlim(0, 100); cx.set_xlabel("share of battery power, % (guesstimate)", color=INK2, fontsize=9)
cx.set_title("Where the power goes (guesstimate)", fontsize=11, color=INK, loc="left")
cx.legend(ncol=5, frameon=False, fontsize=8, loc="upper left", bbox_to_anchor=(0, -0.42), handlelength=1.2,
          columnspacing=1.0)

# (label, mW, measured?, (low, high) mA of the Round 9 estimate range)
revs = [("R9\nscreen off,\n264 MHz", 55 * 3.3, False, (40, 70)), ("R9d\n+ cores\nasleep", 45 * 3.3, False, (35, 60)),
        ("R9e\n48 MHz, gated,\n1.10 V", 8 * 3.3, False, (5, 15)), ("R10 (now)\nE9 pads, E12\n64 MHz", 10 * VBAT, True, None)]
for i, (lab, mw, real, rng) in enumerate(revs):
    if rng:
        bx.errorbar(i, mw, yerr=[[mw - rng[0] * 3.3], [rng[1] * 3.3 - mw]], color=MUTED, capsize=5, lw=1)
    bx.bar(i, mw, width=0.55, color=MEAS if real else EST, hatch=None if real else "//", edgecolor="white", lw=0)
    bx.text(i, (rng[1] * 3.3 if rng else mw) + 4, f"{mw:.0f} mW" + ("" if real else " (est.)"), ha="center", fontsize=10,
            weight="bold" if real else "normal", color=INK if real else INK2)
bx.set_xticks(range(4)); bx.set_xticklabels([r[0] for r in revs], fontsize=9)
bx.set_ylabel("sleep power, mW", color=INK2); bx.set_ylim(0, 250)
bx.set_title("Sleep revisions: estimate ranges (hatched) → measured", fontsize=11, color=INK, loc="left")

for a in (ax, cx, bx):
    a.set_facecolor("#fcfcfb")
    for s in ("top", "right"):
        a.spines[s].set_visible(False)
    for s in ("left", "bottom"):
        a.spines[s].set_color(GRID)
    a.tick_params(colors=INK2)
    (a.xaxis if a in (ax, cx) else a.yaxis).grid(True, color=GRID, lw=0.6); a.set_axisbelow(True)
fig.subplots_adjust(left=0.07, right=0.99, top=0.93, bottom=0.2)
fig.savefig(sys.argv[1], dpi=130, facecolor=fig.get_facecolor())
