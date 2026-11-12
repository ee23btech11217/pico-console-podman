# Figures 16 (sleep) and 17 (why 400 MHz crashed), README Round 9. Data: board log runs of this round.
# usage: python3 crash_sleep_charts.py <out dir> <matplotlib dir>
import sys
sys.path.insert(0, sys.argv[2])
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import FancyBboxPatch

OK, BAD, WARN, GREY, INK = "#2a9d8f", "#d1495b", "#e9a23b", "#8d99ae", "#222"
out = sys.argv[1]

# ---------------- Figure 17: why 400 MHz crashed ----------------
fig = plt.figure(figsize=(14, 9))
fig.suptitle("Why the fast (400 MHz) version crashed", fontsize=17, weight="bold", x=0.02, ha="left")
fig.text(0.02, 0.915, "Short answer: the chip was pushed past its speed limit. At 400 MHz it makes small calculation mistakes, "
         "one of them breaks a pointer in the drawing code, and the board resets.", fontsize=11, color=INK)

ax = fig.add_axes([0.06, 0.40, 0.50, 0.46])
runs = [  # MHz, volts, ok, label
    (264, 1.10, True, "60 s"), (300, 1.10, True, "3 runs"), (300, 1.30, True, ""), (360, 1.30, True, ""),
    (384, 1.30, True, "2 runs"), (384, 1.25, False, "crash"), (384, 1.20, False, "crash"), (400, 1.30, False, "crash\nevery run"),
]
for mhz, v, ok, lab in runs:
    ax.scatter(mhz, v, s=420, color=OK if ok else BAD, marker="o" if ok else "X", zorder=3, edgecolor="white", lw=1.5)
    if lab:
        ax.annotate(lab, (mhz, v), (8, 12), textcoords="offset points", fontsize=8.5, color=INK)
ax.fill_between([380, 410], 1.075, 1.325, color=BAD, alpha=0.07)
ax.text(392, 1.085, "too fast", ha="center", color=BAD, fontsize=9)
ax.set_xlim(250, 415); ax.set_ylim(1.07, 1.33)
ax.set_xlabel("CPU speed (MHz), both cores working"); ax.set_ylabel("core voltage (V)")
ax.set_title("Each dot is one test on your board (green = fine, red = crashed)", fontsize=10, loc="left")
ax.grid(alpha=0.25)
ax.annotate("same speed, less voltage -> crash:\nthe classic sign of a chip at its limit", (384, 1.22), (300, 1.18), fontsize=9,
            color=INK, arrowprops=dict(arrowstyle="->", color=GREY))

# right: ruled out
rx = fig.add_axes([0.60, 0.40, 0.38, 0.46]); rx.axis("off")
rx.set_title("Things we checked and ruled out", fontsize=11, loc="left", weight="bold")
items = [
    ("A bug in the other core writing over memory", "Made the memory read-only for core 1 (hardware guard).\nNo trap at 400 MHz. Self-test write: traps, so the guard works."),
    ("LCD SPI speed", "400 MHz crashes with slow SPI (50 MHz).\n384 MHz is fine with fast SPI (64 MHz)."),
    ("Flash memory speed", "Halving the flash clock did not stop it."),
    ("Auto-sleep", "Crash comes ~5 s after boot; auto-sleep needs 180 s."),
]
for i, (t, d) in enumerate(items):
    y = 0.92 - i * 0.245
    rx.add_patch(FancyBboxPatch((0, y - 0.17), 1, 0.2, boxstyle="round,pad=0.01", fc="#f1f3f5", ec="none", transform=rx.transAxes))
    rx.text(0.03, y, "x  " + t, fontsize=10, weight="bold", color=INK, transform=rx.transAxes, va="top")
    rx.text(0.07, y - 0.06, d, fontsize=8.6, color=INK, transform=rx.transAxes, va="top")

# bottom: chain + fix
bx = fig.add_axes([0.02, 0.03, 0.96, 0.30]); bx.axis("off"); bx.set_xlim(0, 14); bx.set_ylim(0, 3)
steps = [("400 MHz:\nchip at its limit", BAD), ("a tiny mistake\nin a number", BAD), ("drawing code follows\na broken pointer", BAD),
         ("bus fault, board\nfreezes", WARN), ("watchdog resets\nafter 3 s", WARN), ("safe mode:\n150 MHz, 23 FPS", GREY)]
for i, (t, c) in enumerate(steps):
    x = 0.1 + i * 2.32
    bx.add_patch(FancyBboxPatch((x, 1.5), 2.05, 1.15, boxstyle="round,pad=0.03", fc=c, ec="none"))
    bx.text(x + 1.02, 2.07, t, ha="center", va="center", color="white", fontsize=9.5, weight="bold")
    if i < len(steps) - 1:
        bx.annotate("", (x + 2.27, 2.07), (x + 2.08, 2.07), arrowprops=dict(arrowstyle="->", color=INK, lw=1.6))
bx.text(0.1, 0.95, "The fix: 264 MHz at 1.15 V (was 400 MHz at 1.30 V), LCD SPI 66 MHz, both cores.\n"
        "Measured: 43.5 FPS, no fault in about 3 minutes of logs. Less voltage = less heat.", fontsize=10.5, weight="bold", color=OK)
bx.text(0.1, 0.42, "Why always the same spot? It is the busiest memory loop of the whole game, so a mistake shows up there first. "
        "The broken values were different every time: random, not a fixed bug.", fontsize=9, color=INK)
bx.text(0.1, 0.05, "Limits: each red/green dot is a short run (15-60 s). 400 MHz with one core passed once (5 s); "
        "we did not soak it. 264 MHz was tested at 1.10 V and ships at 1.15 V for extra margin.", fontsize=8.5, color=GREY)
fig.savefig(out + "/17_why_400mhz_crashed.png", dpi=120)

# ---------------- Figure 16: sleep ----------------
fig, ax = plt.subplots(figsize=(13.5, 8))
fig.suptitle("Sleep mode: what went wrong and what changed", fontsize=16, weight="bold", x=0.02, ha="left")
rows = [
    ("Hold B a long time\n(264 MHz)", BAD, "Rebooted instead of sleeping.  FIXED",
     "Cause found: after 2 s the game waits for you to let go of B. It drew no pictures meanwhile, so the 3 s\n"
     "watchdog (which expects a picture) reset the board. Log: 'wdt 1, prevcrumb 13'. Now it is fed while waiting."),
    ("Long sleep, deep\n(old: DORMANT, then light)", BAD, "Did not wake, board very hot (also on battery).",
     "Both versions switched the chip to the bare 12 MHz crystal, stopped the PLLs and the ring oscillator and lowered\n"
     "the voltage. Short sleeps woke, long ones did not. Even the watchdog never fired: the chip's clock stopped.\n"
     "Which of those steps exactly is still unknown, so this sleep is no longer used."),
    ("Long sleep, screen-only\n(NEW default)", OK, "4.5+ min asleep, woke, stayed cool (you checked).",
     "Only the screen and the speaker are switched off; clock, voltage and USB stay as in the game.\n"
     "The log keeps running while asleep: chip temperature 61-66 C, no fault. Same game state after waking."),
    ("Auto-sleep after\n3 min idle", WARN, "Off.",
     "It used the deep sleep above, which most likely explains the 'dark and hot after a long run' report. Off by default\n"
     "(GAME_SLEEP_IDLE_S=0); could come back on top of the screen-only sleep after a long test."),
]
ax.axis("off"); ax.set_xlim(0, 13); ax.set_ylim(-0.6, 3.6)
for i, (t, c, s, d) in enumerate(rows):
    y = 3.0 - i * 1.0
    ax.add_patch(FancyBboxPatch((0.05, y - 0.32), 2.6, 0.75, boxstyle="round,pad=0.03", fc=c, ec="none"))
    ax.text(1.35, y + 0.05, t, ha="center", va="center", color="white", fontsize=10, weight="bold")
    ax.text(2.95, y + 0.25, s, fontsize=11, weight="bold", color=INK, va="center")
    ax.text(2.95, y - 0.12, d, fontsize=8.8, color=INK, va="center")
fig.text(0.02, 0.03, "Cost of the safe choice: the chip keeps its clock while asleep, so sleep uses more power than the deep version would (not measured).", fontsize=9.5, color=GREY)
fig.savefig(out + "/16_sleep_investigation.png", dpi=120)
