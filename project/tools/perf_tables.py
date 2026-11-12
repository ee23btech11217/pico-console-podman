#!/usr/bin/env python3
"""Markdown tables from docs/perf/results.json (used to write the README results section)."""
import json, os
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DB = json.load(open(os.path.join(ROOT, "docs/perf/results.json")))


def row(label, n, extra=()):
    r = DB[n]; b = r["bench"]; d = r.get("demo", {})
    f = lambda v: "%.1f" % v
    cells = [label, "/".join("%.0f" % x for x in b["FPS"]), f(b["TCK"][1]), f(b["BGN"][1]), f(b["RAS"][1]), f(r["xfr_ms"])]
    if d.get("lat_us"):
        cells += ["%.0f" % d["fps"], "%.0f" % ((d["lat_us"] + d["per_us"] / 2) / 1000)]
    else:
        cells += ["-", "-"]
    return "| " + " | ".join(cells) + " |"


HEAD = ("| Configuration | Benchmark FPS (QUIET/CROWD/FLIGHT) | Sim tick ms | beginFrame ms | Raster ms | Full-screen SPI ms | Real-time FPS | Press-to-screen ms |\n"
        "|---|---|---|---|---|---|---|---|")


def table(title, items):
    print("\n**%s**\n\n%s" % (title, HEAD))
    for lab, n in items:
        if n in DB and "bench" in DB[n]:
            print(row(lab, n))


table("Stage 1 - where the code runs (150 MHz, 30 Hz ticks, SPI 37.5 MHz)", [
    ("original (XIP, flash clk 50 MHz)", "base_150_xip"), ("XIP, flash clk 75 MHz", "s1_flashdiv2"),
    ("XIP, single core", "s1_single"), ("sim library in RAM", "s1_ram_sim"),
    ("sim+render+gfx in RAM", "s1_ram_libs"), ("libs + rodata in RAM", "s1_ram_libs_ro"),
    ("whole image in RAM (copy_to_ram)", "s1_c2r")])
table("Stage 2 - system clock (code in RAM, SPI request 40 MHz, so the SPI actually moves with the divider)", [
    ("150 MHz (SPI 37.5)", "s1_c2r"), ("200 MHz (SPI 33.3)", "s2_c2r_200"), ("250 MHz (SPI 31.25)", "s2_c2r_250"),
    ("300 MHz (SPI 37.5)", "s2_c2r_300"), ("400 MHz (SPI 40)", "s2_c2r_400")])
table("Stage 3 - SPI rate (the clean/garbled column is from looking at the LCD)", [
    ("400 MHz, SPI 40 - clean", "s2_c2r_400"), ("400 MHz, SPI 50 - clean", "s3_400_spi50"),
    ("264 MHz, SPI 66 - clean", "s6_264_66_t60"), ("400 MHz, SPI 66.7 - clean (final)", "f_final_late2"),
    ("300 MHz, SPI 75 - garbled", "s3_300_spi75"), ("400 MHz, SPI 100 - garbled", "s3_400_spi100")])
table("Stage 4 - final configuration against variants (400 MHz core, SPI 66.7, 60 Hz ticks)", [
    ("old setup (`make safe`)", "f_final_safe"), ("code in flash (XIP)", "f_final_xip"),
    ("single core", "f_final_single"), ("libs in RAM, rest XIP", "f_final_xip_ramlibs"),
    ("sim at -O2", "f_final_sim_O2"), ("gfx fixed-point", "f_final_gfx_fixed"),
    ("30 Hz ticks", "f_final_late_t30"), ("launch batch early (no late launch)", "f_final"),
    ("**FINAL** (late launch)", "f_final_late2")])
print("\n**Joystick (stick at rest, peak-to-peak raw ADC counts per second)**\n\n| Oversampling / poll | X p-p | Y p-p | us per sample |\n|---|---|---|---|")
for lab, n in [("1x, 2 ms (old)", "s6_264_66_t60"), ("4x, 2 ms", "s5_joy_os4"), ("16x, 2 ms", "s5_joy_os16"), ("4x, 1 ms (final)", "s5_joy_os4_poll1000")]:
    d = DB[n]["demo"]
    print("| %s | %.0f | %.0f | %.0f |" % (lab, d["jppx"], d["jppy"], d["jus"]))


# ------------------------------------------------------------------ round 2 (music + world textures)
def round2():
    print("\n### Round 2: textures, before and after\n")
    print("| Configuration | FPS QUIET/CROWD/FLIGHT | Real-time FPS | Press-to-screen ms | beginFrame ms | Raster ms (incl. planet) | Planet pass ms | Blocks/frame | Longest frame FLIGHT ms |")
    print("|---|---|---|---|---|---|---|---|---|")
    for lab, n in [("before: whole image in RAM, no textures", "f_final_late2"), ("before: libraries in RAM, no textures", "m4_tex_off"),
                   ("textures v1: 2x2 blocks everywhere", "m2_tex_on"), ("textures v2: 4x2 beyond 60 FU", "m2_tex_on2"),
                   ("textures v3: 4x2 beyond 30 FU, world built in the foreground", "m4_tex_on"),
                   ("**after: v3 + world built in the background**", "m5_async")]:
        r = DB[n]; b = r["bench"]; d = r["demo"]
        pl = r.get("planet", {}).get("P1")
        print("| %s | %s | %.0f | %.0f | %.1f | %.1f | %s | %s | %.0f |" % (
            lab, "/".join("%.1f" % x for x in b["FPS"]), d["fps"], (d["lat_us"] + d["per_us"] / 2) / 1000,
            b["BGN"][1], b["RAS"][1], "%.1f" % (pl["paint_us"] / 1000) if pl else "-", "%d" % pl["blocks"] if pl else "-", b["FRM^"][2]))
    print("\n**What the benchmark counts per scene (QUIET / CROWD / FLIGHT), identical with and without textures**\n")
    b = DB["m5_async"]["bench"]
    print("| Counter | Meaning | QUIET | CROWD | FLIGHT |\n|---|---|---|---|---|")
    for k, m in [("TRI", "triangles drawn (peak per frame)"), ("TRIK", "triangle memory, KB"), ("SPN", "spans in the span buffer (peak)"),
                 ("ARNK", "scratch arena in use, KB"), ("LIN", "sphere wire segments built per frame"), ("DRP", "primitives dropped for lack of memory")]:
        print("| %s | %s | %s |" % (k, m, " | ".join("%g" % x for x in b[k])))


round2()
