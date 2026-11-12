#!/usr/bin/env python3
"""Charts from docs/perf/results.json (all numbers come from the USB log).

    PYTHONPATH=<dir with matplotlib> tools/perf_plot.py

Chart definitions are the CHARTS list below: (file, title, [config names]).
"""
import json, os, sys
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DB = json.load(open(os.path.join(ROOT, "docs/perf/results.json")))
OUT = os.path.join(ROOT, "docs/perf")
SCENES = ["QUIET", "CROWD", "FLIGHT"]
COL = {"QUIET": "#4C78A8", "CROWD": "#E45756", "FLIGHT": "#54A24B"}
LABELS = json.load(open(os.path.join(ROOT, "docs/perf/labels.json"))) if os.path.exists(os.path.join(ROOT, "docs/perf/labels.json")) else {}


def lab(n):
    return LABELS.get(n, n)


def ok(n):
    r = DB.get(n)
    return r is not None and "bench" in r and not r.get("fallback")


def fps_chart(fname, title, names, ylabel="benchmark FPS (unpaced, higher is better)"):
    names = [n for n in names if ok(n)]
    fig, ax = plt.subplots(figsize=(max(7, 1.25 * len(names) + 2), 4.6))
    w = 0.26
    for i, sc in enumerate(SCENES):
        xs = [k + (i - 1) * w for k in range(len(names))]
        vs = [DB[n]["bench"]["FPS"][i] for n in names]
        bars = ax.bar(xs, vs, w, label=sc, color=COL[sc])
        for x, v in zip(xs, vs):
            ax.text(x, v + 0.3, f"{v:.0f}", ha="center", fontsize=7)
    ax.set_xticks(range(len(names)))
    ax.set_xticklabels([lab(n) for n in names], rotation=18, ha="right", fontsize=8)
    ax.set_ylabel(ylabel)
    ax.set_title(title)
    ax.grid(axis="y", alpha=.3)
    ax.legend(ncols=3, fontsize=8)
    fig.tight_layout()
    fig.savefig(os.path.join(OUT, fname), dpi=130)
    plt.close(fig)


def breakdown_chart(fname, title, names, scene=1):
    """Where a frame's time goes (CROWD scene by default): begin / raster-wait / DMA wait vs the SPI floor."""
    names = [n for n in names if ok(n)]
    fig, ax = plt.subplots(figsize=(max(7, 1.25 * len(names) + 2), 4.6))
    bgn = [DB[n]["bench"]["BGN"][scene] for n in names]
    ras = [DB[n]["bench"]["RAS"][scene] for n in names]
    dma = [DB[n]["bench"]["DMA"][scene] for n in names]
    tck = [DB[n]["bench"]["TCK"][scene] for n in names]
    x = range(len(names))
    ax.bar(x, bgn, 0.55, label="beginFrame (scene build)", color="#F58518")
    ax.bar(x, ras, 0.55, bottom=bgn, label="rasterize (CPU)", color="#4C78A8")
    ax.bar(x, dma, 0.55, bottom=[a + b for a, b in zip(bgn, ras)], label="waiting for SPI/DMA", color="#B279A2")
    ax.plot(x, tck, "k_", markersize=26, mew=2, label="sim tick (core1, parallel)")
    for xi, n in zip(x, names):
        if "xfr_ms" in DB[n]:
            ax.plot([xi - .3, xi + .3], [DB[n]["xfr_ms"]] * 2, color="#888", ls=":", lw=1)
    ax.set_xticks(list(x))
    ax.set_xticklabels([lab(n) for n in names], rotation=18, ha="right", fontsize=8)
    ax.set_ylabel("ms per frame (%s scene)" % SCENES[scene])
    ax.set_title(title + "  (dotted = full-screen SPI transfer time)")
    ax.grid(axis="y", alpha=.3)
    ax.legend(fontsize=8)
    fig.tight_layout()
    fig.savefig(os.path.join(OUT, fname), dpi=130)
    plt.close(fig)


def latency_chart(fname, title, names):
    names = [n for n in names if ok(n) and "demo" in DB[n] and DB[n]["demo"].get("lat_us")]
    fig, ax = plt.subplots(1, 2, figsize=(max(8, 1.2 * len(names) + 3), 4.4))
    x = range(len(names))
    lat = [DB[n]["demo"]["lat_us"] / 1000 for n in names]
    per = [DB[n]["demo"]["per_us"] / 1000 for n in names]
    avg = [l + p / 2 for l, p in zip(lat, per)]
    worst = [DB[n]["demo"]["lat_max_us"] / 1000 + DB[n]["demo"]["per_max_us"] / 1000 for n in names]
    ax[0].bar(x, avg, 0.5, color="#4C78A8", label="average press-to-screen")
    ax[0].plot(x, worst, "r_", markersize=22, mew=2, label="worst frame seen")
    for xi, v in zip(x, avg):
        ax[0].text(xi, v + 1, f"{v:.0f}", ha="center", fontsize=8)
    ax[0].set_ylabel("ms (lower is better)")
    ax[0].set_title("Input latency")
    fp = [DB[n]["demo"]["fps"] for n in names]
    ax[1].bar(x, fp, 0.5, color="#54A24B")
    for xi, v in zip(x, fp):
        ax[1].text(xi, v + .5, f"{v:.0f}", ha="center", fontsize=8)
    ax[1].set_ylabel("paced real-time FPS")
    ax[1].set_title("Paced gameplay FPS (AI demo)")
    for a in ax:
        a.set_xticks(list(x))
        a.set_xticklabels([lab(n) for n in names], rotation=18, ha="right", fontsize=8)
        a.grid(axis="y", alpha=.3)
    ax[0].legend(fontsize=8)
    fig.suptitle(title)
    fig.tight_layout()
    fig.savefig(os.path.join(OUT, fname), dpi=130)
    plt.close(fig)


def sweep_chart(fname, title, names, xs, xlabel):
    """FPS (3 scenes) vs a swept numeric parameter, plus the sim tick cost."""
    pts = [(x, n) for x, n in zip(xs, names) if ok(n)]
    fig, ax = plt.subplots(1, 2, figsize=(10, 4.2))
    for i, sc in enumerate(SCENES):
        ax[0].plot([p[0] for p in pts], [DB[n]["bench"]["FPS"][i] for _, n in pts], "o-", label=sc, color=COL[sc])
    ax[0].set_xlabel(xlabel)
    ax[0].set_ylabel("benchmark FPS")
    ax[0].grid(alpha=.3)
    ax[0].legend(fontsize=8)
    for i, sc in enumerate(SCENES):
        ax[1].plot([p[0] for p in pts], [DB[n]["bench"]["TCK"][i] for _, n in pts], "o-", label=sc + " tick", color=COL[sc])
    ax[1].plot([p[0] for p in pts], [DB[n]["bench"]["RAS"][1] for _, n in pts], "s--", label="CROWD raster", color="#888")
    ax[1].set_xlabel(xlabel)
    ax[1].set_ylabel("ms")
    ax[1].grid(alpha=.3)
    ax[1].legend(fontsize=8)
    fig.suptitle(title)
    fig.tight_layout()
    fig.savefig(os.path.join(OUT, fname), dpi=130)
    plt.close(fig)


if __name__ == "__main__":
    exec(open(os.path.join(ROOT, "docs/perf/charts.py")).read())
