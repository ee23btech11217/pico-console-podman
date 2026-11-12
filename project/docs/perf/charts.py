# Executed by tools/perf_plot.py (it provides DB, OUT, plt, lab, ok and the chart helpers).
import json as _json

# 1. Where the code runs (stock 150 MHz, 30 Hz ticks, 37.5 MHz SPI)
PL = ["base_150_xip", "s1_flashdiv2", "s1_single", "s1_ram_sim", "s1_ram_libs", "s1_ram_libs_ro", "s1_c2r"]
fps_chart("01_code_placement_fps.png", "Where the code runs - benchmark FPS (150 MHz, SPI 37.5 MHz)", PL)
breakdown_chart("02_code_placement_breakdown.png", "Where the code runs - frame time (CROWD scene)", PL)

# 2. Clock sweep (code in RAM). CPU cost per frame vs system clock; hung points marked.
fig, ax = plt.subplots(figsize=(8.5, 4.6))
pts = [(150, "s1_c2r"), (200, "s2_c2r_200"), (250, "s2_c2r_250"), (300, "s2_c2r_300"), (400, "s2_c2r_400")]
xs = [p[0] for p in pts]
for i, sc in enumerate(SCENES):
    ax.plot(xs, [DB[n]["bench"]["TCK"][i] for _, n in pts], "o-", color=COL[sc], label=sc + " sim tick (core1)")
ax.plot(xs, [DB[n]["bench"]["RAS"][1] + DB[n]["bench"]["BGN"][1] for _, n in pts], "s--", color="#444", label="CROWD beginFrame+raster (core0)")
for c in (420, 450, 480, 500):
    ax.axvline(c, color="#c00", alpha=.25, lw=6)
ax.text(0.87, 0.6, "420-500 MHz:\nhang ->\nwatchdog\nfallback", color="#c00", ha="center", fontsize=8, transform=ax.transAxes)
ax.set_xlabel("system clock MHz (1.30 V; 350 is not reachable with the 12 MHz crystal PLL)")
ax.set_ylabel("ms")
ax.set_title("CPU cost vs clock, code in RAM (SPI-limited FPS is NOT shown here)")
ax.grid(alpha=.3); ax.legend(fontsize=8)
fig.tight_layout(); fig.savefig(OUT + "/03_clock_sweep_cpu.png", dpi=130); plt.close(fig)

# 3. SPI rate vs FPS (400 MHz core): the display is the bottleneck. Clean region from the on-screen test.
fig, ax = plt.subplots(figsize=(8.5, 4.6))
sp = [(40.0, "s2_c2r_400"), (50.0, "s3_400_spi50"), (66.7, "f_final_late2"), (75.0, "s3_300_spi75"), (100.0, "s3_400_spi100")]
sp = [(x, n) for x, n in sp if n in DB and "bench" in DB[n]]
for i, sc in enumerate(SCENES):
    ax.plot([p[0] for p in sp], [DB[n]["bench"]["FPS"][i] for _, n in sp], "o-", color=COL[sc], label=sc)
ax.plot([p[0] for p in sp], [1000 / DB[n]["xfr_ms"] for _, n in sp], "k:", label="SPI-only limit (1/transfer time)")
ax.axvspan(0, 67.5, color="#2a2", alpha=.08)
ax.axvspan(67.5, 105, color="#c22", alpha=.08)
ax.text(0.03, 0.55, "clean picture\n(checked on the LCD)", color="#161", fontsize=9, transform=ax.transAxes)
ax.text(0.74, 0.3, "garbled picture\n(75, 80, 90, 100 MHz)", color="#811", fontsize=9, transform=ax.transAxes)
ax.set_xlabel("LCD SPI clock MHz"); ax.set_ylabel("benchmark FPS (unpaced)")
ax.set_title("The panel's SPI limit sets the FPS ceiling (core at 300-400 MHz, code in RAM)")
ax.grid(alpha=.3); ax.legend(fontsize=8, loc="upper left")
fig.tight_layout(); fig.savefig(OUT + "/04_spi_sweep.png", dpi=130); plt.close(fig)

# 4. Final-configuration matrix
FM = ["f_final_safe", "f_final_xip", "f_final_single", "f_final_xip_ramlibs", "f_final_sim_O2", "f_final_gfx_fixed", "f_final_late2"]
fps_chart("05_final_matrix_fps.png", "Final configuration vs variants - benchmark FPS (400 MHz core, SPI 66.7 MHz, 60 Hz ticks)", FM)
breakdown_chart("06_final_matrix_breakdown.png", "Final configuration vs variants - frame time (CROWD scene)", FM)

# 5. Latency + paced FPS
LT = ["base_150_xip", "s1_c2r", "f_final_t30", "f_final", "f_final_single", "f_final_late_t30", "f_final_late2"]
latency_chart("07_latency_and_paced_fps.png", "Input latency and real-time FPS (AI demo, paced)", LT)

# 6. Joystick noise
JN = [("s6_264_66_t60", "1x, 2 ms"), ("s5_joy_os4", "4x, 2 ms"), ("s5_joy_os16", "16x, 2 ms"), ("s5_joy_os4_poll1000", "4x, 1 ms")]
JN = [(n, l) for n, l in JN if n in DB and "demo" in DB[n]]
fig, ax = plt.subplots(1, 2, figsize=(10, 4))
x = range(len(JN))
ax[0].bar([i - .18 for i in x], [DB[n]["demo"]["jppx"] for n, _ in JN], .36, label="X axis", color="#4C78A8")
ax[0].bar([i + .18 for i in x], [DB[n]["demo"]["jppy"] for n, _ in JN], .36, label="Y axis", color="#F58518")
for i, (n, _) in enumerate(JN):
    ax[0].text(i - .18, DB[n]["demo"]["jppx"] + .8, f'{DB[n]["demo"]["jppx"]:.0f}', ha="center", fontsize=8)
    ax[0].text(i + .18, DB[n]["demo"]["jppy"] + .8, f'{DB[n]["demo"]["jppy"]:.0f}', ha="center", fontsize=8)
ax[0].set_xticks(list(x)); ax[0].set_xticklabels([l for _, l in JN], fontsize=8)
ax[0].set_ylabel("raw ADC counts, peak-to-peak per second (stick at rest)")
ax[0].set_title("Stick noise (lower is better)"); ax[0].legend(fontsize=8); ax[0].grid(axis="y", alpha=.3)
ax[1].bar(list(x), [DB[n]["demo"]["jus"] for n, _ in JN], .5, color="#54A24B")
for i, (n, _) in enumerate(JN):
    ax[1].text(i, DB[n]["demo"]["jus"] + 1, f'{DB[n]["demo"]["jus"]:.0f} us', ha="center", fontsize=8)
ax[1].set_xticks(list(x)); ax[1].set_xticklabels([l for _, l in JN], fontsize=8)
ax[1].set_ylabel("microseconds per stick sample"); ax[1].set_title("Cost of one sample (oversampling x, poll interval)")
ax[1].grid(axis="y", alpha=.3)
fig.tight_layout(); fig.savefig(OUT + "/08_joystick_noise.png", dpi=130); plt.close(fig)

# 7. Before / after summary
B, A = DB["base_150_xip"], DB["f_final_late2"]
fig, ax = plt.subplots(1, 4, figsize=(12, 3.8))
def pair(a, title, vb, va, unit, lower_better=False):
    a.bar(["before", "after"], [vb, va], color=["#999", "#2a7"])
    for i, v in enumerate([vb, va]):
        a.text(i, v * 1.01, f"{v:.1f}", ha="center", fontsize=9)
    a.set_title(title + ("\n(lower is better)" if lower_better else "\n(higher is better)"), fontsize=9)
    a.set_ylabel(unit); a.grid(axis="y", alpha=.3)
pair(ax[0], "Benchmark FPS (CROWD)", B["bench"]["FPS"][1], A["bench"]["FPS"][1], "FPS")
pair(ax[1], "Real-time FPS (AI demo)", B["demo"]["fps"], A["demo"]["fps"], "FPS")
pair(ax[2], "Input to screen (avg)", (B["demo"]["lat_us"] + B["demo"]["per_us"] / 2) / 1000, (A["demo"]["lat_us"] + A["demo"]["per_us"] / 2) / 1000, "ms", True)
pair(ax[3], "Sim tick (CROWD)", B["bench"]["TCK"][1], A["bench"]["TCK"][1], "ms", True)
fig.tight_layout(); fig.savefig(OUT + "/00_before_after.png", dpi=130); plt.close(fig)
print("charts done")

# ---------------------------------------------------------------- round 2: music + world textures
import re as _re
R2 = ["f_final_late2", "m4_tex_off", "m2_tex_on", "m2_tex_on2", "m4_tex_on", "m5_async"]
fps_chart("10_textures_fps.png", "World textures: benchmark FPS before / after (400 MHz core, SPI 66.7 MHz)", R2)
breakdown_chart("11_textures_breakdown.png", "World textures: frame time (CROWD scene)", R2)

# Scene complexity (the benchmark's own counters): textures do not change any of them
fig, ax = plt.subplots(1, 3, figsize=(12, 4))
A = DB["m5_async"]["bench"]; B = DB["m4_tex_off"]["bench"]
xs = range(3)
def two(a, rows, title, ylabel):
    w = 0.38
    for j, (key, col) in enumerate(rows):
        a.bar([x + (j - (len(rows) - 1) / 2) * w / len(rows) * 2 for x in xs], A[key], w * 2 / len(rows) * 0.9, label=key, color=col)
        for x, v in zip(xs, A[key]):
            a.text(x + (j - (len(rows) - 1) / 2) * w / len(rows) * 2, v + max(A[key]) * 0.01, "%g" % v, ha="center", fontsize=7)
    a.set_xticks(list(xs)); a.set_xticklabels(SCENES); a.set_title(title, fontsize=10); a.set_ylabel(ylabel); a.grid(axis="y", alpha=.3); a.legend(fontsize=8)
two(ax[0], [("TRI", "#4C78A8"), ("SPN", "#F58518")], "Triangles and spans (peak per frame)", "count")
two(ax[1], [("LIN", "#54A24B")], "Wireframe lines (still built; not drawn with textures)", "lines")
two(ax[2], [("TRIK", "#B279A2"), ("ARNK", "#E45756")], "Triangle memory / scratch arena used (KB)", "KB")
same = all(A[k] == B[k] for k in ("TRI", "TRIK", "SPN", "ARNK", "LIN", "DRP"))
fig.suptitle("Benchmark scene complexity (identical before and after the textures: %s; dropped primitives DRP = %s)" % ("yes" if same else "NO", A["DRP"]), fontsize=10)
fig.tight_layout(); fig.savefig(OUT + "/12_scene_complexity.png", dpi=130); plt.close(fig)

# Cost of the planet pass and the world-change hitch
fig, ax = plt.subplots(1, 3, figsize=(12.5, 4))
PV = [("m2_tex_on", "v1\n2x2 blocks"), ("m2_tex_on2", "v2\nwide >60 FU"), ("m4_tex_on", "v3\nwide >30 FU")]
ax[0].bar([l for _, l in PV], [DB[n]["planet"]["P1"]["paint_us"] / 1000 for n, _ in PV], 0.5, color="#4C78A8")
for i, (n, _) in enumerate(PV):
    v = DB[n]["planet"]["P1"]["paint_us"] / 1000
    ax[0].text(i, v + .2, "%.1f ms\n%d blocks" % (v, DB[n]["planet"]["P1"]["blocks"]), ha="center", fontsize=8)
ax[0].set_ylabel("ms per frame"); ax[0].set_title("Planet pass cost (benchmark scenes)", fontsize=10); ax[0].grid(axis="y", alpha=.3)
ax[1].bar(["textures off", "v1", "v2", "v3"], [B["FPS"][1]] + [DB[n]["bench"]["FPS"][1] for n, _ in PV], 0.5, color=["#999", "#4C78A8", "#4C78A8", "#2a7"])
for i, v in enumerate([B["FPS"][1]] + [DB[n]["bench"]["FPS"][1] for n, _ in PV]):
    ax[1].text(i, v + .3, "%.1f" % v, ha="center", fontsize=8)
ax[1].set_ylabel("benchmark FPS (CROWD)"); ax[1].set_title("What the pass costs in FPS", fontsize=10); ax[1].grid(axis="y", alpha=.3)
hit = [("sync build\n(v3)", DB["m4_tex_on"]["bench"]["FRM^"][2]), ("background\nbuild", DB["m5_async"]["bench"]["FRM^"][2]), ("no textures", B["FRM^"][2])]
ax[2].bar([l for l, _ in hit], [v for _, v in hit], 0.5, color=["#E45756", "#2a7", "#999"])
for i, (_, v) in enumerate(hit):
    ax[2].text(i, v + 1, "%.0f ms" % v, ha="center", fontsize=8)
ax[2].set_ylabel("ms"); ax[2].set_title("Longest frame in FLIGHT (a new world is built)", fontsize=10); ax[2].grid(axis="y", alpha=.3)
fig.tight_layout(); fig.savefig(OUT + "/13_planet_pass.png", dpi=130); plt.close(fig)

# Music: before and after, parsed from the two sound.cpp versions
def _music(path):
    s = open(path).read()
    enum = {k: int(v) for k, v in _re.findall(r"(\b[A-G]s?\d) = (\d+)", s)}
    hz = [int(x) for x in _re.search(r"NOTE_HZ\[\] = \{([^}]*)\}", s).group(1).replace("\n", " ").split(",") if x.strip()]
    nmin = enum["G3"] if "G3" in enum else enum["A3"]
    steps = [int(x) for x in _re.findall(r"\{(\d+), \d+, \d+, PROG\d?, ARP\d, MEL\d, BARS\d\}", s)]
    mx = []
    for i in range(4):
        m = _re.search(r"MEL%d\[\] = \{(.*?)\n\};" % i, s, _re.S)
        notes = [enum[a] for a, _ in _re.findall(r"\{(\w+), (\d+)\}", m.group(1))]
        mx.append(hz[max(notes) - nmin])
    return steps, mx
OLD = _music(os.path.join(ROOT, "docs/music/sound_before_rework.cpp.txt"))
NEW = _music(os.path.join(ROOT, "src/audio/sound.cpp"))
fig, ax = plt.subplots(1, 3, figsize=(12.5, 4))
t = list(range(4)); w = 0.38
bpm = lambda st: [60000 / (4 * x) for x in st]
ax[0].bar([i - w / 2 for i in t], bpm(OLD[0]), w, label="before", color="#999")
ax[0].bar([i + w / 2 for i in t], bpm(NEW[0]), w, label="after", color="#2a7")
for i in t:
    ax[0].text(i - w / 2, bpm(OLD[0])[i] + 3, "%.0f" % bpm(OLD[0])[i], ha="center", fontsize=8)
    ax[0].text(i + w / 2, bpm(NEW[0])[i] + 3, "%.0f" % bpm(NEW[0])[i], ha="center", fontsize=8)
ax[0].set_title("Tempo (beats per minute) by intensity tier", fontsize=10); ax[0].set_xticks(t); ax[0].set_xticklabels(["tier %d" % i for i in t]); ax[0].legend(fontsize=8); ax[0].grid(axis="y", alpha=.3)
nps = lambda st, div: [1000 / (x * div) for x in st]
ax[1].bar([i - w / 2 for i in t], nps(OLD[0], 1), w, label="before (16th arpeggio)", color="#999")
ax[1].bar([i + w / 2 for i in t], nps(NEW[0], 2), w, label="after (8th arpeggio)", color="#2a7")
for i in t:
    ax[1].text(i - w / 2, nps(OLD[0], 1)[i] + .2, "%.1f" % nps(OLD[0], 1)[i], ha="center", fontsize=8)
    ax[1].text(i + w / 2, nps(NEW[0], 2)[i] + .2, "%.1f" % nps(NEW[0], 2)[i], ha="center", fontsize=8)
ax[1].set_title("Arpeggio speed (notes per second)", fontsize=10); ax[1].set_xticks(t); ax[1].set_xticklabels(["tier %d" % i for i in t]); ax[1].legend(fontsize=8); ax[1].grid(axis="y", alpha=.3)
ax[2].bar([i - w / 2 for i in t], OLD[1], w, label="before", color="#999")
ax[2].bar([i + w / 2 for i in t], NEW[1], w, label="after", color="#2a7")
for i in t:
    ax[2].text(i - w / 2, OLD[1][i] + 15, "%d" % OLD[1][i], ha="center", fontsize=8)
    ax[2].text(i + w / 2, NEW[1][i] + 15, "%d" % NEW[1][i], ha="center", fontsize=8)
ax[2].set_title("Highest melody note (Hz)", fontsize=10); ax[2].set_xticks(t); ax[2].set_xticklabels(["tier %d" % i for i in t]); ax[2].legend(fontsize=8); ax[2].grid(axis="y", alpha=.3)
fig.suptitle("Background music: slower, calmer arpeggio, melody an octave lower", fontsize=11)
fig.tight_layout(); fig.savefig(OUT + "/09_music_before_after.png", dpi=130); plt.close(fig)
print("round 2 charts done", OLD, NEW)

# ---------------------------------------------------------------- round 4: intro (world select + cut scene)
import re as _re4
_log = open(os.path.join(ROOT, "docs/perf/raw/intro_test.log")).read().splitlines()
_fp = [_re4.search(r"INTRO stars (\d+) = (\d+) bytes of (\d+) free \(reserve (\d+)\)", l) for l in _log]
_fp = [m for m in _fp if m][0]
stars, used, free_b, reserve = (int(_fp.group(i)) for i in (1, 2, 3, 4))
rows = []
for l in _log:
    m = _re4.match(r"P0 FPS ([\d.]+).*raster (\d+)us.*? ui (\d) .*per (\d+)/", l)
    if m: rows.append((float(m.group(1)), int(m.group(2)) / 1000, int(m.group(3)), int(m.group(4)) / 1000))
fig, ax = plt.subplots(1, 2, figsize=(11.5, 4))
ax[0].barh(["RAM left after the game"], [used], color="#4C78A8", label="star field: %d stars x 8 B = %d B" % (stars, used))
ax[0].barh(["RAM left after the game"], [reserve], left=[used], color="#F58518", label="kept for the C library and stacks: %d B" % reserve)
ax[0].barh(["RAM left after the game"], [free_b - used - reserve], left=[used + reserve], color="#999", label="malloc bookkeeping: %d B" % (free_b - used - reserve))
ax[0].set_xlim(0, free_b * 1.02); ax[0].set_xlabel("bytes (of %d free)" % free_b)
ax[0].set_title("Cut scene footprint: the star field takes what is left", fontsize=10); ax[0].legend(fontsize=8, loc="lower center", bbox_to_anchor=(0.5, -0.95))
sel = [r for r in rows if r[2] == 1][1:]; cut = [r for r in rows if r[2] == 2]
labels = ["select %d" % i for i in range(len(sel[:4]))] + ["cut %d" % i for i in range(len(cut))]
vals = [r[1] for r in sel[:4]] + [r[1] for r in cut]
cols = ["#2a7"] * len(sel[:4]) + ["#4C78A8"] * len(cut)
ax[1].bar(range(len(vals)), vals, color=cols)
ax[1].axhline(20.25, color="#c22", ls="--", lw=1); ax[1].text(0, 20.7, "frame period 20.25 ms (SPI-bound): 49.3 FPS, never below it", color="#c22", fontsize=8)
for i, v in enumerate(vals): ax[1].text(i, v + .3, "%.1f" % v, ha="center", fontsize=8)
ax[1].set_xticks(range(len(vals))); ax[1].set_xticklabels(labels, rotation=30, fontsize=8)
ax[1].set_ylabel("CPU time to paint a frame (ms)"); ax[1].set_ylim(0, 24)
ax[1].set_title("World select and cut scene: painting cost per frame\n(cut scene seconds 1..5: warp, drop-out, approach, dive)", fontsize=10)
fig.tight_layout(); fig.savefig(OUT + "/14_intro_footprint.png", dpi=130, bbox_inches="tight"); plt.close(fig)
print("round 4 charts done", stars, used, free_b, reserve, len(sel), len(cut))

# ---------------------------------------------------------------- round 6: stored music
fig, ax = plt.subplots(1, 3, figsize=(12.5, 3.8))
ax[0].bar(["both tracks\nin full", "75 s of each\n(used)", "flash area\navailable"], [3281, 1161, 3580], color=["#999", "#2a7", "#4C78A8"])
for i, v in enumerate([3281, 1161, 3580]): ax[0].text(i, v + 50, "%d KB" % v, ha="center", fontsize=9)
ax[0].set_ylabel("KB of flash"); ax[0].set_title("Music footprint", fontsize=10); ax[0].grid(axis="y", alpha=.3)
ax[1].bar(["interrupt, silent", "interrupt, playing"], [0.5, 0.9], color=["#4C78A8", "#F58518"])
for i, v in enumerate([0.5, 0.9]): ax[1].text(i, v + .03, "%.1f %%" % v, ha="center", fontsize=9)
ax[1].set_ylabel("% of core 0"); ax[1].set_title("DAC interrupt (31.25 kHz, 117 cycles each)", fontsize=10); ax[1].grid(axis="y", alpha=.3)
A, B = DB["m6_skin"]["bench"]["FPS"], DB["m7_audio"]["bench"]["FPS"]
w = .36
ax[2].bar([i - w / 2 for i in range(3)], A, w, label="before (synth tune)", color="#999")
ax[2].bar([i + w / 2 for i in range(3)], B, w, label="after (stored music engine)", color="#2a7")
for i in range(3):
    ax[2].text(i - w / 2, A[i] + .3, "%.1f" % A[i], ha="center", fontsize=8); ax[2].text(i + w / 2, B[i] + .3, "%.1f" % B[i], ha="center", fontsize=8)
ax[2].set_xticks(range(3)); ax[2].set_xticklabels(SCENES); ax[2].set_ylim(0, 58); ax[2].set_ylabel("benchmark FPS"); ax[2].legend(fontsize=8, loc="lower right")
ax[2].set_title("Frame rate: no change", fontsize=10); ax[2].grid(axis="y", alpha=.3)
fig.tight_layout(); fig.savefig(OUT + "/15_music_pack.png", dpi=130); plt.close(fig)
print("round 6 charts done")
