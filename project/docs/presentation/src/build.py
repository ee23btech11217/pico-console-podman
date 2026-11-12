import sys
import pymupdf

S = sys.argv[1]           # deck work dir (scratchpad/deck)
OUT = sys.argv[2]         # output pdf
W, H = 960, 540
DS = f"{S}/ds"
PERF = "/workspace/docs/perf"
WORLDS = "/workspace/docs/worlds"

INK, INK2, MUTED, ACC, BOX = "#0b0b0b", "#52514e", "#898781", "#2a78d6", "#f1f0ec"
CSS = f"""
* {{ font-family: sans-serif; color: {INK}; }}
h1 {{ font-size: 25px; font-weight: bold; margin: 0; }}
.k {{ font-size: 11px; color: {ACC}; font-weight: bold; letter-spacing: 1px; margin: 0 0 3px 0; }}
ul {{ margin: 0; padding-left: 16px; }}
li {{ font-size: 15px; margin: 0 0 9px 0; color: {INK}; }}
.s {{ color: {INK2}; }}
.d {{ font-size: 12.5px; color: {INK}; }}
.dt {{ font-size: 10.5px; font-weight: bold; color: {ACC}; letter-spacing: 1px; }}
.cap {{ font-size: 9px; color: {MUTED}; }}
.big {{ font-size: 34px; font-weight: bold; color: {INK}; margin: 0; }}
.lab {{ font-size: 12px; color: {INK2}; margin: 0 0 14px 0; }}
td {{ font-size: 17px; padding: 3px 10px 3px 0; vertical-align: top; }}
.r {{ font-weight: bold; color: {ACC}; }}
.t1 td {{ font-size: 14.5px; padding: 4px 14px 4px 0; }}
.t2 td {{ font-size: 13px; padding: 3px 8px 3px 0; }}
"""

doc = pymupdf.open()
n = [0]


def html(page, rect, body):
    page.insert_htmlbox(pymupdf.Rect(*rect), body, css=CSS)


def img(page, rect, path, cap=None, border=False):
    r = pymupdf.Rect(*rect)
    page.insert_image(r, filename=path, keep_proportion=True)
    if cap:
        html(page, (r.x0, r.y1 + 2, r.x1, r.y1 + 16), f'<p class="cap">{cap}</p>')


def slide(kicker, title):
    p = doc.new_page(width=W, height=H)
    p.draw_rect(pymupdf.Rect(0, 0, W, H), color=None, fill=pymupdf.utils.getColor("white") if False else (0.988, 0.988, 0.984))
    n[0] += 1
    html(p, (40, 26, 920, 90), f'<p class="k">{kicker}</p><h1>{title}</h1>')
    p.draw_line((40, 86), (920, 86), color=(0.88, 0.878, 0.85), width=0.8)
    html(p, (860, 515, 920, 532), f'<p class="cap" style="text-align:right">{n[0]}</p>')
    return p


def bullets(p, rect, items):
    html(p, rect, "<ul>" + "".join(f"<li>{i}</li>" for i in items) + "</ul>")


def debug(p, rect, text):
    r = pymupdf.Rect(*rect)
    p.draw_rect(r, color=None, fill=(0.945, 0.941, 0.925), radius=0.06)
    p.draw_rect(pymupdf.Rect(r.x0, r.y0, r.x0 + 3, r.y1), color=None, fill=(0.165, 0.47, 0.84))
    html(p, (r.x0 + 12, r.y0 + 8, r.x1 - 10, r.y1 - 6), f'<p class="dt">DEBUG DETAIL</p><p class="d">{text}</p>')


DSC = "RP2350 datasheet, "

# 1 ---------------------------------------------------------------- title
p = doc.new_page(width=W, height=H)
p.draw_rect(pymupdf.Rect(0, 0, W, H), color=None, fill=(0.988, 0.988, 0.984))
n[0] += 1
html(p, (60, 70, 900, 200), '<p class="k">PICO 2 GAME CONSOLE</p>'
     '<p style="font-size:40px;font-weight:bold;margin:0">Devour Sphere on RP2350</p>'
     f'<p style="font-size:18px;color:{INK2};margin:8px 0 0 0">Hardware and software choices, round by round</p>')
img(p, (60, 215, 900, 470), f"{S}/hud_cmp.png")
html(p, (60, 485, 900, 520), f'<p style="font-size:14px;color:{INK2}">RP2350 A2 · 264 MHz · ST7789 320×240 SPI · 2 cores · '
     '42-45 FPS · 38 ms input-to-screen</p>')

# 2 ---------------------------------------------------------------- build-up
p = slide("BUILD-UP", "Seventeen rounds to the current revision")
rows = [("R1", "Speed: code in RAM, clock, SPI rate, sim on core 1, late launch"),
        ("R2", "Epic original music, textured world surface"),
        ("R3", "Player skinned with the world, HUD legend"),
        ("R4", "World select screen and cut scene"),
        ("R5", "Button names, per-weapon high scores, stronger upgrades"),
        ("R6", "Stored music: ADPCM from flash through a PWM DAC"),
        ("R7", "Sleep mode on B, resume at the same frame"),
        ("R8-9", "264 MHz @ 1.15 V, screen-only sleep with cores asleep"),
        ("R10", "Datasheet errata fixes, core temperature indicator"),
        ("R11", "Work-sharing scheduler across both cores"),
        ("R12", "Colour-table animation, whole and louder music"),
        ("R13", "Height map + ground animation on core 1, yield + WFE: no FPS cost"),
        ("R14-15", "Pin probe tried and reverted; Makefile pins the shipping build"),
        ("R16", "New board: ST7789 panel, battery current measured, synthwave music"),
        ("R17", "Bodies lit by the world's sun (ShapoGFX lights)")]
html(p, (60, 98, 900, 520), '<table class="t1">' + "".join(f'<tr><td class="r">{a}</td><td>{b}</td></tr>' for a, b in rows) + "</table>")

# 3 ---------------------------------------------------------------- hardware
p = slide("HARDWARE", "The console")
img(p, (40, 100, 520, 500), "/workspace/board.png", "Pico 2 pinout (board.png)")
bullets(p, (545, 105, 920, 360), [
    "Pico 2, RP2350 <b>A2</b>, QFN-60",
    "ST7789 320×240 on SPI0: GP16-21 (R16)",
    "Analog stick: GP26/27 = ADC0/1",
    "Two buttons: GP8 = A, GP9 = B",
    "Speaker: GP15 PWM → NPN → 8 Ω",
    "Power: 3× AA into VSYS",
    "All pins in one file: <i>board_pins.h</i>"])
debug(p, (545, 380, 920, 495), "The once-a-second log prints the button mask (<b>btn 080</b>). On the first board it showed "
      "GP10 floats and GP11 reads pressed for good, so game logic uses only GP8 and GP9.")

# 4 ---------------------------------------------------------------- chip
p = slide("HARDWARE", "What we use on the chip")
img(p, (40, 98, 470, 500), f"{DS}/fig1.png", DSC + "Fig. 1, system overview")
bullets(p, (500, 105, 920, 400), [
    "Two Cortex-M33 cores: render | simulate",
    "SPI0 + DMA: display bands",
    "PWM slice: 31.25 kHz audio DAC",
    "ADC: stick + die temperature",
    "QSPI XIP: firmware, music, scores",
    "Watchdog + scratch: clock guard, crumbs"])

# 4b -------------------------------------------------------------- boot flow
p = slide("HARDWARE + SOFTWARE", "Power-up to the first frame")
img(p, (40, 96, 920, 395), f"{S}/bootflow.png")
debug(p, (40, 410, 920, 505), "Each step writes a breadcrumb to watchdog <b>scratch1</b>; <b>scratch0</b> holds the clock-guard "
      "flag. Both survive the watchdog's system reset (datasheet §7.2, §12.9.5), so <b>prevcrumb</b> in the CFG log says where "
      "a boot stopped. The bootrom owns scratch4-7 (watchdog boot, §5.2.4): we never touch them.")

# 4c -------------------------------------------------------------- boot steps vs datasheet
p = slide("HARDWARE + SOFTWARE", "Our boot steps, and the rule behind each")
img(p, (40, 110, 400, 470), f"{DS}/fig30.png", DSC + "Fig. 30, power-on state machine")
html(p, (420, 100, 920, 520), '<table class="t2">'
     '<tr><td class="cap">STEP</td><td class="cap">WHAT</td><td class="cap">WHY</td></tr>'
     '<tr><td class="r">Guard</td><td>read watchdog REASON + scratch, arm 3 s watchdog</td><td class="s">hang → 150 MHz fallback</td></tr>'
     '<tr><td class="r">Flash</td><td>QSPI divider 3 before the PLL moves</td><td class="s">flash ≤ 100 MHz (88 MHz)</td></tr>'
     '<tr><td class="r">Voltage</td><td>1.15 V, 10 ms settle, then PLL</td><td class="s">voltage up before clock up</td></tr>'
     '<tr><td class="r">PLL</td><td>264 MHz from 12 MHz XOSC, clk_peri = clk_sys</td><td class="s">SPI 66 MHz = /4</td></tr>'
     '<tr><td class="r">Pins</td><td>backlight off, CS high, RST high first</td><td class="s">no garbage on the panel</td></tr>'
     '<tr><td class="r">Inputs</td><td>pull-ups on buttons, ADC on GP26/27</td><td class="s">pressed = low; ADC pads IE off</td></tr>'
     '<tr><td class="r">Audio</td><td>pack header + 1 s decode self-test</td><td class="s">synth fallback if not bit-exact</td></tr>'
     '<tr><td class="r">Chirp</td><td>short tone at power-on</td><td class="s">speaker wiring check</td></tr>'
     '<tr><td class="r">SPI floor</td><td>time one full screen, nothing else running</td><td class="s">20.5 ms, logged as xfer</td></tr>'
     '<tr><td class="r">Core 1</td><td>launch, flash_safe_execute_core_init</td><td class="s">safe high-score writes</td></tr>'
     '<tr><td class="r">Proven</td><td>5 s of frames clears scratch0</td><td class="s">later resets keep 264 MHz</td></tr>'
     '</table>')

# 4d -------------------------------------------------------------- panel power-up timing
p = slide("HARDWARE (ILI9341, BOARD UNTIL R15)", "Panel power-up and sleep timing")
img(p, (40, 96, 470, 300), f"{DS}/ili_reset.png", "ILI9341 datasheet V1.11, §15.4 reset timing")
html(p, (500, 100, 920, 420), '<table class="t2">'
     '<tr><td class="cap">STEP</td><td class="cap">OURS</td><td class="cap">DATASHEET MIN</td></tr>'
     '<tr><td class="r">RESX low</td><td>20 ms</td><td class="s">10 µs (tRW)</td></tr>'
     '<tr><td class="r">After RESX</td><td>120 ms</td><td class="s">5 ms, 120 ms before SLPOUT</td></tr>'
     '<tr><td class="r">SWRESET</td><td>120 ms</td><td class="s">5 ms (120 in Sleep Out)</td></tr>'
     '<tr><td class="r">SLPOUT</td><td>120 ms</td><td class="s">5 ms, 120 before SLPIN</td></tr>'
     '<tr><td class="r">COLMOD</td><td>0x55</td><td class="s">16 bit/pixel</td></tr>'
     '<tr><td class="r">DISPON</td><td>20 ms</td><td class="s">-</td></tr>'
     '<tr><td class="r">Sleep</td><td>DISPOFF, 5 ms, SLPIN, 6 ms</td><td class="s">5 ms after SLPIN</td></tr>'
     '<tr><td class="r">Wake</td><td>SLPOUT, 120 ms, DISPON</td><td class="s">120 ms after SLPIN</td></tr>'
     '</table>')
bullets(p, (40, 330, 470, 440), [
    "Backlight off while asleep; GRAM keeps the frame",
    "Init at 40 MHz SPI, then 66 MHz for pixels"])
debug(p, (40, 440, 920, 510), "Every delay meets the ILI9341 rules (§8.2.2 SWRESET, §8.2.11 SLPIN, §8.2.12 SLPOUT, §15.4 reset). "
      "The wake shows the old frame at once: the panel's GRAM is kept through SLPIN.")

# 5 ---------------------------------------------------------------- display
p = slide("HARDWARE + SOFTWARE", "Display link: the frame-rate ceiling")
img(p, (40, 100, 470, 260), f"{DS}/fig98.png", DSC + "Fig. 98, SPI mode 0 (SPO=0, SPH=0)")
img(p, (40, 290, 470, 500), f"{PERF}/04_spi_sweep.png", "SPI rate sweep (docs/perf/04)")
bullets(p, (500, 105, 920, 330), [
    "RGB565, byte-swapped buffers",
    "40-row bands, DMA double buffer",
    "66 MHz = 264 MHz / 4",
    "Full frame 20.5 ms → max 48.8 FPS"])
debug(p, (500, 350, 920, 495), "The log cannot see a garbled picture. <i>make spitest</i> draws a test pattern; the panel was "
      "checked by eye: clean up to 66.7 MHz. That set the SPI limit.")

# 5b -------------------------------------------------------------- ILI9341 limits
p = slide("HARDWARE LIMITS (BOARD UNTIL R15)", "ILI9341: what the panel allowed")
img(p, (40, 96, 330, 400), f"{DS}/ili_block.png", "ILI9341 datasheet V1.11, §3 block diagram")
img(p, (350, 96, 920, 300), f"{DS}/ili_timing_table.png", "ILI9341 datasheet V1.11, §18.3.4, 4-line SPI timing")
html(p, (350, 320, 920, 500), '<table class="t2">'
     '<tr><td class="r">Write clock</td><td>spec ≥ 100 ns (10 MHz); run at 15 ns (66 MHz), clean by eye</td></tr>'
     '<tr><td class="r">Link only</td><td>4-line SPI: 153.6 KB per frame → 20.5 ms → 48.8 FPS max</td></tr>'
     '<tr><td class="r">GRAM</td><td>172,800 B on the panel: bands are enough; image kept in sleep</td></tr>'
     '<tr><td class="r">No TE pin</td><td>not wired: no sync to the ~70 Hz refresh (FRMCTR1 default)</td></tr>'
     '<tr><td class="r">No MISO</td><td>write-only panel, nothing read back</td></tr></table>')
debug(p, (40, 425, 330, 500), "6.6× the rated clock works on this panel; 75 MHz garbles. <i>make spitest</i> decides.")

# 5c -------------------------------------------------------------- ST7789
p = slide("HARDWARE + SOFTWARE", "New board: ST7789 panel (Round 16)")
html(p, (40, 100, 560, 330), '<table class="t1">'
     '<tr><td class="cap">SETTING</td><td class="cap">ILI9341</td><td class="cap">ST7789</td><td class="cap">WHY</td></tr>'
     '<tr><td class="r">MADCTL</td><td>0xE8</td><td>0xA0</td><td class="s">MX off: vertical flip; BGR off</td></tr>'
     '<tr><td class="r">INVON</td><td>no</td><td>no</td><td class="s">tried: inverted colours</td></tr>'
     '<tr><td class="r">DMA byte swap</td><td>on</td><td>on</td><td class="s">tried off: noise</td></tr>'
     '<tr><td class="r">Joystick U/D</td><td>as wired</td><td>flipped</td><td class="s">JOY_INVERT_UD</td></tr>'
     '</table>')
bullets(p, (590, 105, 920, 330), [
    "Same commands, same 66 MHz SPI",
    "Three settings, all in <i>SHIP_ARGS</i>",
    "Root cause of the colours: RGB vs BGR order"])
debug(p, (40, 350, 920, 470), "Symptom: blue sea orange, Mars bluish, green and black/white unchanged. Only red and blue had traded "
      "places, which is MADCTL bit 3 (BGR), not inversion or byte order. Two wrong guesses first (INVON, DMA swap off) were each "
      "flashed and judged on the LCD by the user, and reverted.")

# 6 ---------------------------------------------------------------- RAM code
p = slide("SOFTWARE", "Hot code out of flash")
img(p, (40, 100, 450, 330), f"{DS}/fig16.png", DSC + "Fig. 16, XIP subsystem (16 KB cache)")
img(p, (470, 100, 920, 330), f"{PERF}/02_code_placement_breakdown.png", "Frame time by code placement (docs/perf/02)")
bullets(p, (40, 365, 450, 500), [
    "sim + render + gfx run from SRAM",
    "Sim tick 23.7 → 13.9 ms (150 MHz)",
    "Raster 18.9 → 9.3 ms"])
debug(p, (470, 365, 920, 495), "Both cores shared one 16 KB XIP cache at a 50 MHz flash clock. Moving three libraries to "
      "RAM (<i>GAME_RAM_CODE</i>) matched a full copy-to-RAM and left RAM for textures.")

# 7 ---------------------------------------------------------------- clock
p = slide("HARDWARE", "Clock and core voltage: 264 MHz @ 1.15 V")
img(p, (40, 100, 450, 330), f"{DS}/fig156.png", DSC + "Fig. 156, DVDD current vs core frequency")
img(p, (470, 100, 920, 330), f"{S}/03_crop.png", "CPU cost vs clock, 150-400 MHz (docs/perf/03)")
bullets(p, (40, 365, 450, 500), [
    "264 MHz gives SPI divider 4 = 66 MHz",
    "1.15 V; flash clock kept ≤ 100 MHz",
    "Current rises with clock: no extra MHz"])
debug(p, (470, 365, 920, 495), "Clock guard: a 3 s watchdog over the first 5 s of frames; a hang reboots into 150 MHz "
      "(<b>fallback 1</b>). Breadcrumbs in watchdog scratch show how far the last boot got (<b>prevcrumb</b>).")

# 8 ---------------------------------------------------------------- flash
p = slide("SOFTWARE", "Flash map and safe writes")
img(p, (40, 105, 920, 280), f"{S}/flashmap.png")
bullets(p, (40, 300, 450, 500), [
    "High scores: CRC'd record, 3 weapons",
    "Written only off-play (title, game over)",
    "<i>flash_safe_execute</i> pauses core 1",
    "Music pack survives <i>make flash</i>"])
debug(p, (470, 300, 920, 495), "The ROM flash routines reset the QSPI divider to a 150 MHz value. <i>high_score.cpp</i> "
      "restores it, and the code between ROM call and restore runs from RAM (<i>__not_in_flash_func</i>). "
      "Checked by the scripted game-over test (<b>HS save</b>).")

# 8b -------------------------------------------------------------- memory footprint
p = slide("SOFTWARE", "RAM and flash footprint")
img(p, (40, 96, 920, 395), f"{PERF}/24_memory_footprint.png")
bullets(p, (40, 410, 470, 520), [
    "No framebuffer: 150 KB won't fit; 2 × 25 KB bands",
    "178 KB of hot code in SRAM",
    "Star field takes the free heap, keeps 2 KB"])
debug(p, (490, 410, 920, 505), "<i>sbrk(0)</i> misleads: the SDK hands the whole heap to malloc at the first printf. "
      "Free RAM is read with <i>mallinfo().fordblks</i>; <b>__bss_end__</b> is checked against 0x20080000 after every change.")

# 9 ---------------------------------------------------------------- two cores
p = slide("SOFTWARE", "Two cores: simulation on core 1, started late")
img(p, (40, 100, 470, 350), f"{DS}/fig5.png", DSC + "Fig. 5, bus fabric (global exclusive monitor)")
img(p, (490, 100, 920, 350), f"{PERF}/07_latency_and_paced_fps.png", "Latency and paced FPS (docs/perf/07)")
bullets(p, (40, 385, 470, 500), [
    "Hand-off: one atomic state word",
    "Batch starts when bands left ≈ sim time",
    "Press-to-screen 104 → 34-39 ms"])
debug(p, (490, 385, 920, 495), "Latency is measured on the board: input sample → last band to DMA + its transfer. "
      "The stick is sampled right at launch (<i>buttons_read(true)</i>).")

# 10 --------------------------------------------------------------- scheduler
p = slide("SOFTWARE", "Work-sharing scheduler (current)")
img(p, (40, 96, 920, 330), f"{S}/timeline.png")
bullets(p, (40, 350, 470, 500), [
    "Planet rows cut into 4-row chunks",
    "Both cores claim chunks, sim first",
    "Core 1 paints ~37 % of the planet",
    "Now SPI-bound: 22.6 vs 20.5 ms"])
debug(p, (490, 350, 920, 495), "Claim word = (job serial « 8) | chunk; a CAS only succeeds for the current job, index 255 "
      "closes it before the next band is written. Safe across cores per datasheet §2.1.6 (all SRAM).")

# 10b -------------------------------------------------------------- benchmark
p = slide("SOFTWARE", "Seven schedulers measured on the board")
img(p, (40, 96, 920, 445), f"{PERF}/23_scheduling.png", "docs/perf/23, tools/perf_run.py, hands-free, 90 s per variant")
bullets(p, (40, 470, 920, 530), ["Work sharing + late start: <b>+7.5 %</b> FPS in the crowd scene, lowest latency of the dual-core variants"])

# 10c -------------------------------------------------------------- time utilisation
p = slide("SOFTWARE", "Time utilisation: where each frame goes")
img(p, (40, 94, 640, 392), f"{PERF}/25_time_utilisation.png", "docs/perf/25, P + PLN lines of the current build (r13_ship), AI demo")
bullets(p, (660, 105, 920, 398), [
    "Core 0 busy <b>77 %</b>",
    "Core 0 waits on SPI: 23 %",
    "Planet pass: 40 % of core 0",
    "Core 1 busy <b>55 %</b>",
    "Sim 28 %, planet 15 %, anim 13 %",
    "Rest: asleep in beginFrame, or idle"])
debug(p, (40, 418, 920, 510), "Every <i>make log</i> prints this once a second: the <b>P</b> line gives per-frame averages of "
      "<b>begin raster wait cmd</b> on core 0, <b>sim</b> on core 1 and the frame period <b>per</b>; the <b>PLN</b> line gives the "
      "planet time inside raster, core 1's <b>help</b> % and its surface animation time <b>anim</b>. Core 1's planet slice is paint × help %, an estimate. "
      "Chart: <i>docs/perf/time_util_chart.py [log] [phase]</i>.")

# 11 --------------------------------------------------------------- input
p = slide("HARDWARE + SOFTWARE", "Analog stick")
img(p, (40, 98, 330, 500), f"{DS}/fig111.png", DSC + "Fig. 111, ADC inputs QFN-60")
img(p, (350, 100, 920, 310), f"{PERF}/08_joystick_noise.png", "Stick noise vs oversampling (docs/perf/08)")
bullets(p, (350, 335, 620, 500), [
    "4× oversampling, 1 ms rate limit",
    "Deadzone + full scale, −127..127",
    "Noise 32-35 → 7-16 counts"])
debug(p, (640, 335, 920, 495), "The log line carries raw ADC, peak-to-peak and µs per sample (<b>joy jpp jus</b>): "
      "16× oversampling cost 3.4× the time (85 vs 25 µs) for no gain, so 4× stays.")

# 12 --------------------------------------------------------------- audio
p = slide("HARDWARE + SOFTWARE", "Audio: PWM DAC + stored music")
img(p, (40, 94, 920, 335), f"{PERF}/15_music_pack.png", "Music footprint, interrupt cost at 264 MHz (docs/perf/15)")
bullets(p, (40, 352, 470, 530), [
    "GP15 → 1 kΩ → NPN → 8 Ω speaker",
    "31.25 kHz 8-bit PWM DAC interrupt",
    "4-bit IMA ADPCM, 15.6 kHz, from flash",
    "1.6 % of core 0 while playing",
    "Tempo follows the sphere level",
    "Now: one synthwave track, 134 s (R16)"])
debug(p, (490, 352, 920, 505), "Decoder self-test at boot: the first second of track 1 is decoded on the board and its "
      "checksum compared with the packer's (<b>selftest 1</b> = bit-exact). Loudness measured in the interrupt on the real "
      "output (<b>music-rms 40/127</b>), and a DWT cycle counter gives the interrupt's cost (<b>137 cycles</b>).")

# 12b -------------------------------------------------------------- music round 12
p = slide("SOFTWARE (ROUND 12)", "Music: whole tracks, louder where the speaker plays")
img(p, (40, 96, 920, 390), f"{PERF}/26_music_round12.png", "docs/perf/26, packs decoded on the PC (tools/music_levels.py)")
bullets(p, (40, 410, 470, 520), [
    "Whole tracks: 75 s → 160 s and 264 s",
    "3,281 KB of the 3,580 KB music area",
    "R16: synthwave only, 1,034 KB; 3.5 kHz 4-pole low-pass, no 2 kHz lift (highs distorted)"])
debug(p, (490, 410, 920, 505), "The tracks already hit the limiter: +7 dB of drive gave only +0.7 dB RMS. The headroom was in "
      "bass the 8 Ω speaker cannot play: a 4-pole high-pass at 180 Hz and +3 dB at 2 kHz free the limiter for the "
      "audible band. On the board: <b>SND ... music-rms</b>.")

# 13 --------------------------------------------------------------- worlds
p = slide("SOFTWARE", "World surface")
img(p, (40, 98, 520, 338), f"{WORLDS}/worlds_sheet.png", "PC renders of the board shader with logged cameras")
img(p, (540, 98, 920, 288), f"{WORLDS}/world_select.png", "World select")
bullets(p, (540, 300, 920, 420), [
    "Ray-sphere per 2×2 block",
    "Cube height map + hills + grain",
    "Next world built on core 1 in slices"])
debug(p, (40, 360, 520, 495), "Same C++ shader builds on the PC (<i>planet_preview</i>), fed with the board's <b>CAM</b> "
      "log lines: every look change is checked before flashing. Background build removed a 106 ms frame per world change.")

# 13b -------------------------------------------------------------- animated surface
p = slide("SOFTWARE", "Animated surface from core 1's idle time")
img(p, (40, 96, 920, 360), f"{WORLDS}/surface_anim.png", "PC renders of the board code (tools/anim_preview.cpp), two moments 0.9 s apart")
bullets(p, (40, 385, 470, 520), [
    "Height map: waves, beach wash, creeping lava",
    "Ground tiles: wind, dunes, snow, flowing lumps",
    "Colour table: surf, glints, glow, embers"])
debug(p, (490, 385, 920, 505), "All integer, 50 passes/s, in units of a few µs. The still copies live in the idle back "
      "world set: no new buffer. Core 1 yields between units to a sim batch, an open chunk job and core 0's "
      "<i>beginFrame</i>. Log: <b>PLN ... anim n us/frame</b>.")

# 13c -------------------------------------------------------------- animation cost
p = slide("SOFTWARE", "Four times the animation, no FPS cost")
img(p, (40, 96, 920, 400), f"{PERF}/27_anim_round13.png", "docs/perf/27, board logs r12_anim_on2, r13_anim_final, r13_ship, r13_terra_on3/off3")
bullets(p, (40, 420, 470, 525), [
    "Core 1: 0.8 → 3.0-3.3 ms per frame",
    "Benchmark off 44.4/41.9/45.4, on 44.4/41.8/45.4",
    "Core 1 busy 41 % (R11) → 55 % of a frame"])
debug(p, (490, 420, 920, 510), "First try, long slices: 0.8 FPS lower in the AI demo. Now the work is cut into µs units "
      "with a yield check. Core 1 sleeps in <b>WFE</b> during <i>beginFrame</i> (core 0 <b>SEV</b>s): +0.1 FPS in the benchmark "
      "with or without the animation. The demo varies ±0.5 FPS run to run, so the fixed benchmark decides.")

# 13d -------------------------------------------------------------- body lighting
p = slide("SOFTWARE", "Bodies lit by the world's sun (Round 17)")
img(p, (40, 94, 520, 420), f"{PERF}/29_body_light.png", "PC render of the board code (tools/light_preview.cpp), same frame")
img(p, (540, 94, 920, 332), f"{PERF}/30_light_r17.png", "Benchmark on the board, docs/perf/30")
bullets(p, (540, 350, 920, 470), [
    "ShapoGFX ambient + parallel light",
    "Same sun vector as the ground",
    "−0.1 to −0.2 FPS, same triangle count"])
debug(p, (40, 440, 520, 510), "Kites carried a dummy normal (0, 1, 0). Now cross(dir, perp), turned to the eye (double-sided "
      "material). One normal per kite keeps it a flat 60 B record. Fork patch 10; levels 45 % + 80 %.")

# 14 --------------------------------------------------------------- UI
p = slide("SOFTWARE", "Screens and player skin")
img(p, (40, 98, 520, 338), f"{WORLDS}/screens_round5.png", "Title, weapon select, play, game over (PC renders)")
img(p, (540, 98, 920, 338), f"{WORLDS}/player_skin.png", "Player skinned with the world's terrain")
img(p, (40, 360, 520, 500), f"{WORLDS}/cutscene_storyboard.png", "Cut scene storyboard")
bullets(p, (540, 365, 920, 500), [
    "Renderer patched via small hooks",
    "Star field sized from free heap",
    "UI uses the console's names: A, B"])

# 15 --------------------------------------------------------------- sleep
p = slide("HARDWARE + SOFTWARE", "Sleep: hold B, wake on B at the same frame")
img(p, (40, 98, 330, 380), f"{DS}/fig21.png", DSC + "Fig. 21, core power domains")
img(p, (345, 98, 600, 380), f"{DS}/fig36.png", DSC + "Fig. 36, clocks")
bullets(p, (620, 105, 920, 380), [
    "Screen + speaker off",
    "Both cores in deep sleep (WFI/WFE)",
    "64 MHz via PLL, then 1.10 V",
    "Unused peripheral clocks gated",
    "B = GPIO IRQ; 1 s alarm feeds watchdog"])
debug(p, (40, 405, 920, 495), "Order matters: clock down → voltage down; on wake voltage up, 1 ms, clock up, then sound and "
      "LCD. Log lines <b>SLP wake 1/2/3</b> and <b>WAKE</b> at the same tick prove each step; USB stays up through the sleep.")

# 16 --------------------------------------------------------------- E9
p = slide("DATASHEET ERRATA", "RP2350-E9 and E12 applied to the sleep")
img(p, (40, 98, 470, 380), f"{DS}/fig157.png", DSC + "Fig. 157, pad leakage (E9, chip A2)")
bullets(p, (500, 105, 920, 340), [
    "E9: input pad between VIL/VIH leaks ~120 µA",
    "Speaker pin: driven low, input off",
    "All other pads: input off, pulls off",
    "E12: clk_sys ≥ 1.1 × clk_usb → 64 MHz",
    "Die temp falls in sleep: 60.2 → 57.1 °C"])
debug(p, (40, 405, 920, 495), "The log prints the stepping (<b>chip A2</b>) and how many pads changed (<b>pads IE-off 9</b>). "
      "A <i>static_assert</i> pins the E12 rule. Every datasheet-derived change is listed in "
      "<i>docs/datasheet/IMPLEMENTATION.md</i>.")

# 16b -------------------------------------------------------------- power, measured
p = slide("HARDWARE + SOFTWARE", "Battery current, measured")
img(p, (40, 94, 920, 384), f"{PERF}/28_power_measured.png",
    "User's meter, 3× AA (≈ 4.5 V). Life = 200 mAh / current. Split and hatched bars are estimates")
bullets(p, (40, 400, 470, 515), [
    "200 mAh: <b>1 h 40 min</b> playing, <b>20 h</b> asleep",
    "Sleep ≈ 1/12 of play: backlight + speaker off",
    "Measured values sit inside the estimate ranges"])
debug(p, (490, 392, 920, 505), "Sleep path: R9 screen + speaker off → R9d cores in WFI/WFE → R9e 48 MHz via PLL, "
      "peripheral clocks gated, 1.10 V → R10 E9 pads input-off, 64 MHz for E12. "
      "The old board's 380/200 mA was a board fault (> 2 A in BOOTSEL).")

# 17 --------------------------------------------------------------- temperature measurement
p = slide("HARDWARE", "Measuring die temperature correctly")
img(p, (40, 98, 560, 360), f"{PERF}/22_temp_measure_fix.png", "Same run read both ways (docs/perf/22)")
bullets(p, (580, 105, 920, 360), [
    "Sensor: ADC ch. 4, 0.706 V @ 27 °C",
    "−1.721 mV/°C (datasheet §12.4.6)",
    "GPIO23 high: regulator in PWM mode",
    "Up to +11 °C error removed"])
debug(p, (40, 385, 920, 495), "At low load the Pico 2's 3.3 V regulator switches to power-save mode; its ripple moved the "
      "ADC reference. Holding GPIO23 high for 2 ms around each read gave steady values (60.5-62.7 °C asleep).")

# 18 --------------------------------------------------------------- indicator
p = slide("SOFTWARE", "Core temperature under the FPS")
img(p, (40, 98, 600, 310), f"{S}/hud_cmp.png", "PC render: WARM (amber) and HOT (red, blinking)")
html(p, (620, 105, 920, 320), "<table>"
     '<tr><td class="r">&lt; 60 °C</td><td>CORE nnC, green</td></tr>'
     '<tr><td class="r">≥ 60 °C</td><td>WARM, amber</td></tr>'
     '<tr><td class="r">≥ 70 °C</td><td>HOT!, red, blinking</td></tr>'
     '<tr><td class="r">125 °C</td><td>junction max (Table 1680)</td></tr>'
     '<tr><td class="r">85 °C</td><td>ambient max (Table 1682)</td></tr></table>')
debug(p, (40, 335, 920, 450), "Non-blocking: frame N sets GPIO23, frame N+1 (≥ 2 ms later) waits 50 µs and reads 20 conversions (~0.1 ms). "
      "FPS unchanged. Levels leave 2 °C below their threshold; <b>TEMP level</b> log lines record each change.")

# 18b ------------------------------------------------------------- other hardware limits
p = slide("HARDWARE LIMITS", "Board and chip constraints, and the answer to each")
html(p, (40, 100, 920, 520), '<table class="t1">'
     '<tr><td class="cap">LIMIT</td><td class="cap">EFFECT</td><td class="cap">ANSWER</td></tr>'
     '<tr><td class="r">RP2350 A2</td><td>E9 pad leakage</td><td class="s">inputs off on idle pads in sleep</td></tr>'
     '<tr><td class="r">12 MHz crystal</td><td>PLL steps only (350 MHz not reachable)</td><td class="s">264 MHz / 4 = 66 MHz SPI</td></tr>'
     '<tr><td class="r">520 KB SRAM, no PSRAM</td><td>no framebuffer, no full copy-to-RAM</td><td class="s">bands, 3 libraries in RAM</td></tr>'
     '<tr><td class="r">4 MB flash</td><td>music competes with code</td><td class="s">134 s synthwave in 1,034 KB of 3,580 KB</td></tr>'
     '<tr><td class="r">2 buttons used</td><td>GP10 floats, GP11 tied low (first board)</td><td class="s">all UI on A + B + stick</td></tr>'
     '<tr><td class="r">Analog stick</td><td>12-bit ADC noise at rest</td><td class="s">4× oversampling, deadzone</td></tr>'
     '<tr><td class="r">Speaker</td><td>one PWM pin, NPN, 8 Ω</td><td class="s">31.25 kHz 8-bit DAC in one ISR</td></tr>'
     '<tr><td class="r">3.3 V regulator</td><td>power-save ripple shifts the ADC</td><td class="s">GPIO23 high during temp reads</td></tr>'
     '<tr><td class="r">GPIO24</td><td>no usable VBUS sense</td><td class="s">USB clocks kept in sleep</td></tr>'
     '<tr><td class="r">No current sensor</td><td>firmware cannot read power</td><td class="s">user\'s meter at the battery; die temperature</td></tr>'
     '</table>')

# 19 --------------------------------------------------------------- toolbox
p = slide("DEBUG", "The toolbox behind every number")
html(p, (40, 105, 920, 500), "<table>"
     '<tr><td class="r">USB log</td><td>P-line: FPS, sim/begin/raster/wait µs, latency, state, button mask, stick</td></tr>'
     '<tr><td class="r">PLN</td><td>planet paint µs, core 1 help %, surface animation µs, world style</td></tr>'
     '<tr><td class="r">CFG</td><td>real clocks, SPI rate, fallback, breadcrumbs, scheduler mode</td></tr>'
     '<tr><td class="r">perf_run.py</td><td>build → flash → log → results.json, hands-free, 90 s per variant</td></tr>'
     '<tr><td class="r">Benchmark</td><td>3 fixed scenes + paced AI demo inside the firmware</td></tr>'
     '<tr><td class="r">Scripted tests</td><td>GAME_INPUT_TEST (pause, restart, deaths), GAME_TEMP_TEST</td></tr>'
     '<tr><td class="r">FAULT</td><td>HardFault PC/LR/CFSR kept in .noinit RAM, printed after reset</td></tr>'
     '<tr><td class="r">PC previews</td><td>planet, HUD, intro, skin: the board\'s own code on the PC</td></tr>'
     '<tr><td class="r">Datasheet</td><td>tools/ds_text.py + IMPLEMENTATION.md: section → code → status</td></tr>'
     "</table>")

# 20 --------------------------------------------------------------- current revision
p = slide("CURRENT REVISION", "Where it stands")
cells = [("42-45", "FPS (benchmark 44.3 / 41.6 / 45.3)"), ("38 ms", "input to screen (AI demo)"),
         ("264 MHz", "@ 1.15 V, SPI 66 MHz"), ("36 %", "of the planet painted by core 1"),
         ("278 KB", "firmware; 134 s of music at 512 KB"), ("120 mA", "playing on 3× AA; 10 mA asleep")]
for i, (big, lab) in enumerate(cells):
    x = 60 + (i % 3) * 290
    y = 120 + (i // 3) * 150
    html(p, (x, y, x + 270, y + 130), f'<p class="big">{big}</p><p class="lab">{lab}</p>')
html(p, (60, 405, 900, 515), f'<p style="font-size:13px;color:{INK2}">On the board with this build (log, Round 17): '
     'benchmark and AI-demo frame rate, latency, planet sharing, music found and decoded bit-exact. Measured by the user: battery '
     'current (idle 100, play 120, sleep 10 mA). Checked by the user on the LCD: ST7789 colours and orientation, the synthwave '
     'music. Not yet checked: the lit bodies on the LCD, the joystick up/down fix in play, and sleep/wake on the new board.</p>')

# 21 --------------------------------------------------------------- thank you + credits
p = doc.new_page(width=W, height=H)
p.draw_rect(pymupdf.Rect(0, 0, W, H), color=None, fill=(0.988, 0.988, 0.984))
n[0] += 1
html(p, (60, 70, 900, 170), '<p class="k">CREDITS</p>'
     '<p style="font-size:44px;font-weight:bold;margin:0">Thank you</p>'
     f'<p style="font-size:18px;color:{INK2};margin:8px 0 0 0">Devour Sphere on a Pico 2, built on the work of others</p>')
r = pymupdf.Rect(60, 200, 900, 300)
p.draw_rect(r, color=None, fill=(0.945, 0.941, 0.925), radius=0.08)
p.draw_rect(pymupdf.Rect(r.x0, r.y0, r.x0 + 4, r.y1), color=None, fill=(0.165, 0.47, 0.84))
html(p, (r.x0 + 20, r.y0 + 14, r.x1 - 16, r.y1 - 8),
     '<p class="dt">GRAPHICS</p>'
     '<p style="font-size:24px;font-weight:bold;margin:2px 0 4px 0">ShapoGFX by Shapoco</p>'
     f'<p style="font-size:14px;color:{INK2};margin:0">2D/3D software renderer for microcontrollers: the scanline 3D '
     'scene into band buffers, the HUD, fonts and screens.<br>MIT License · github.com/shapoco/shapo-gfx</p>')
html(p, (60, 322, 900, 500), '<table class="t1">'
     '<tr><td class="r">SDK</td><td>Raspberry Pi pico-sdk and picotool (BSD-3-Clause)</td></tr>'
     '<tr><td class="r">Chip</td><td>RP2350 datasheet (Raspberry Pi Ltd), quoted figures and tables</td></tr>'
     '<tr><td class="r">Music</td><td>synthwave track, packed as 4-bit ADPCM from flash</td></tr>'
     '</table>')
html(p, (860, 515, 920, 532), f'<p class="cap" style="text-align:right">{n[0]}</p>')

doc.save(OUT, garbage=4, deflate=True)
print(OUT, doc.page_count, "pages")
