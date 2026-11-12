#!/usr/bin/env python3
"""Measure one firmware configuration on the Pico through the USB log only.

    tools/perf_run.py NAME [KEY=VALUE ...]

Builds + flashes with `make flash CMAKE_ARGS=...`, reads the USB printf log with
`make log`, keeps the raw log in docs/perf/raw/NAME.log and appends the parsed
numbers to docs/perf/results.json. KEYs are the CMake cache variables without the
GAME_ prefix (SYS_CLOCK_KHZ, LCD_SPI_HZ, TICK_RATE, SIM_ON_CORE1, RAM_CODE, ...).
The firmware must be built with BENCH_AUTORUN=ON (the default here): it then runs
the benchmark and a paced AI demo by itself.
"""
import json, os, re, subprocess, sys, time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RAW = os.path.join(ROOT, "docs/perf/raw")
RES = os.path.join(ROOT, "docs/perf/results.json")

DEFAULTS = {
    # 264 MHz / 66 MHz SPI = the shipping setup since Round 9 (400 MHz crashes and heats the board: never default to it)
    "PLANET_TEX": "ON", "PLANET_DEFS": "", "SYS_CLOCK_KHZ": "264000", "LCD_SPI_HZ": "66000000", "TICK_RATE": "60",
    "PLANET_SHARE": "ON", "PLANET_SHARE_ROWS": "4", "PLANET_ANIM": "ON", "TEMP_TEST": "OFF", "ARENA_GUARD": "0",
    "SIM_ON_CORE1": "ON", "RAM_CODE": "sim+render+gfx", "RAM_RODATA": "", "COPY_TO_RAM": "OFF",
    "GFX_HOT": "OFF", "GFX_FIXED": "OFF", "SIM_OPT": "", "RENDER_OPT": "", "GFX_OPT": "",
    "BAND_H": "40", "BENCH_AUTORUN": "ON", "SPI_TEST": "OFF", "SKIP_STATIC": "OFF", "BENCH_ENABLE": "OFF", "INPUT_TEST": "OFF", "LATE_LAUNCH": "ON", "JOY_OVERSAMPLE": "4", "JOY_POLL_US": "1000",
    "VREG_OVERRIDE": "0", "FLASH_DIV": "0", "BODY_LIGHT": "ON",
}


def sh(cmd, **kw):
    return subprocess.run(cmd, shell=True, cwd=ROOT, capture_output=True, text=True, **kw)


def parse(text):
    out = {"fallback": None}
    lines = text.splitlines()
    # last complete benchmark table
    blocks, cur = [], None
    for l in lines:
        if l.startswith("BENCH| BENCHMARK"):
            cur = [l[7:]]
        elif cur is not None and l.startswith("BENCH| end"):
            blocks.append(cur); cur = None
        elif cur is not None and l.startswith("BENCH| "):
            cur.append(l[7:])
    if blocks:
        b = blocks[-1]
        out["header"] = b[0].strip()
        m = re.search(r"XFR ([\d.]+)", b[0])
        if m: out["xfr_ms"] = float(m.group(1))
        tab = {}
        for l in b[2:]:
            t = l.split()
            if len(t) == 4:
                try: tab[t[0]] = [float(x) for x in t[1:]]
                except ValueError: pass
        out["bench"] = tab
    # planet pass statistics (PLN lines follow the P<phase> line of the same second)
    pl = {}
    gens = []
    phase = None
    for l in lines:
        m = re.match(r"P(\d) FPS", l)
        if m:
            phase = "P" + m.group(1)
        m = re.match(r"PLN paint (\d+) us/frame\s+blocks (\d+)/frame\s+gen (\d+) us\s+palette (\d)(?:\s+help ([\d.]+)%)?", l)
        if m and phase:
            if int(m.group(1)) > 0:
                pl.setdefault(phase, []).append((int(m.group(1)), int(m.group(2)), float(m.group(5) or 0)))
            if int(m.group(3)) > 0:
                gens.append(int(m.group(3)))
    if pl:
        out["planet"] = {ph: {"paint_us": sum(x[0] for x in v) / len(v), "blocks": sum(x[1] for x in v) / len(v),
                              "help_pct": sum(x[2] for x in v) / len(v), "n": len(v)}
                         for ph, v in pl.items()}
        if gens:
            out["planet_gen_us"] = sum(gens) / len(gens)
    cfg = [l for l in lines if l.startswith("CFG ")]
    if cfg:
        d = dict(re.findall(r"(\w+) (\S+)", cfg[-1][4:]))
        out["meas"] = {k: d.get(k) for k in ("sys", "peri", "spi", "core1", "tick", "band", "c2r", "qmi", "wdt", "fallback", "xfer", "share", "late")}
        out["fallback"] = d.get("fallback") == "1"
    def avg(pfx, skip=2):
        rows = [re.findall(r"(\w+) (-?[\d.]+)(?:/(-?\d+)us)?", l) for l in lines if l.startswith(pfx + " FPS")]
        return rows[skip:]
    for ph, name in (("P3", "demo"), ("P0", "idle")):
        ls = [l for l in lines if l.startswith(ph + " FPS")][2:]
        if not ls: continue
        def f(rx):
            v = [float(m.group(1)) for l in ls if (m := re.search(rx, l))]
            return sum(v) / len(v) if v else None
        def fmax(rx):
            v = [float(m.group(1)) for l in ls if (m := re.search(rx, l))]
            return max(v) if v else None
        out[name] = {
            "n": len(ls), "fps": f(r"FPS ([\d.]+)"), "skip": f(r"skip ([\d.]+)"), "ticks": f(r"ticks ([\d.]+)"),
            "sim_us": f(r"sim (\d+)us"), "begin_us": f(r"begin (\d+)us"),
            "raster_us": f(r"raster (\d+)us"), "wait_us": f(r"wait (\d+)us"),
            "lat_us": f(r"lat (\d+)/"), "lat_max_us": fmax(r"lat \d+/(\d+)us"),
            "per_us": f(r"per (\d+)/"), "per_max_us": fmax(r"per \d+/(\d+)us"),
            "jppx": f(r"jpp (\d+),"), "jppy": f(r"jpp \d+,(\d+)"), "jus": f(r"jus (\d+)"),
        }
    return out


def reparse():
    db = json.load(open(RES))
    for n, rec in db.items():
        f = os.path.join(RAW, n + ".log")
        if os.path.exists(f):
            for k in ("bench", "meas", "demo", "idle", "header", "xfr_ms", "fallback", "planet", "planet_gen_us"):
                rec.pop(k, None)
            rec.update(parse(open(f).read()))
    json.dump(db, open(RES, "w"), indent=1)


def main():
    if sys.argv[1] == "--reparse":
        return reparse()
    name = sys.argv[1]
    cfg = dict(DEFAULTS)
    for kv in sys.argv[2:]:
        k, v = kv.split("=", 1)
        assert k in cfg, k
        cfg[k] = v
    args = " ".join(f"-DGAME_{k}={v}" if v != "" else f"-DGAME_{k}=" for k, v in cfg.items())
    args = args.replace("GAME_SYS_CLOCK_KHZ", "GAME_SYS_CLOCK_KHZ")
    t0 = time.time()
    r = sh(f'make flash CMAKE_ARGS="{args}"')
    log = r.stdout + r.stderr
    rec = {"name": name, "cfg": cfg}
    if r.returncode != 0:
        rec["error"] = "build/flash failed"
        rec["build_log_tail"] = log[-1500:]
        print(log[-1500:])
    else:
        text = ""
        for chunk in range(8):          # up to ~320 s of log
            c = sh("make log LOG_SECS=40")
            text += c.stdout
            if len(re.findall(r"^P3 FPS", text, re.M)) >= 12:
                break
            if chunk >= 2 and "P" not in text:
                break
        os.makedirs(RAW, exist_ok=True)
        open(os.path.join(RAW, name + ".log"), "w").write(text)
        rec.update(parse(text))
        if "bench" not in rec:
            rec["error"] = "no benchmark table in log"
    rec["wall_s"] = round(time.time() - t0)
    db = json.load(open(RES)) if os.path.exists(RES) else {}
    db[name] = rec
    json.dump(db, open(RES, "w"), indent=1)
    b = rec.get("bench", {})
    print(name, "ERR" if "error" in rec else "ok", "FPS", b.get("FPS"), "TCK", b.get("TCK"), "RAS", b.get("RAS"),
          "BGN", b.get("BGN"), "DMA", b.get("DMA"), "XFR", rec.get("xfr_ms"), "fallback", rec.get("fallback"),
          "demo", {k: round(v, 1) if isinstance(v, float) else v for k, v in rec.get("demo", {}).items() if k in ("fps", "lat_us", "per_us")},
          "cfg", rec.get("meas", {}).get("sys"), rec.get("meas", {}).get("spi"))


main()
