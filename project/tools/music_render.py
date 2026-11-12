#!/usr/bin/env python3
"""Render the firmware's background music to WAV files on the PC (a preview, not the real speaker).

    tools/music_render.py [seconds] [outdir]

Parses the note tables and tier settings out of src/audio/sound.cpp and plays them through the same logic as
music_tick(): 1 ms steps, note gaps, swell / decay envelope, vibrato, square wave. The real thing is a PWM square
wave through an 8 ohm speaker, so the PC will sound cleaner, but the tempo, notes and structure are identical.
"""
import math, os, re, struct, sys, wave

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
src = open(os.path.join(ROOT, "src/audio/sound.cpp")).read()

enum = dict(re.findall(r"(\b[A-G]s?\d) = (\d+)", src))
notes = {k: int(v) for k, v in enum.items()}
hz_tab = [int(x) for x in re.search(r"NOTE_HZ\[\] = \{([^}]*)\}", src).group(1).replace("\n", " ").split(",") if x.strip()]
note_min = notes["G3"] if "G3" in notes else notes["A3"]
sec = int(sys.argv[1]) if len(sys.argv) > 1 else 40
outdir = sys.argv[2] if len(sys.argv) > 2 else os.path.join(ROOT, "docs/music")
os.makedirs(outdir, exist_ok=True)

def table(name):
    m = re.search(name + r"\[\] = \{(.*?)\n\};", src, re.S)
    return [(notes[a], int(b)) for a, b in re.findall(r"\{(\w+), (\d+)\}", m.group(1))]

def arr(name):
    return [int(x) for x in re.search(name + r"\[\d*\] = \{([^}]*)\}", src).group(1).split(",")]

voice = [[notes[n.strip()] for n in row.split(",")] for row in re.findall(r"\{([A-G]s?\d, [A-G]s?\d, [A-G]s?\d, [A-G]s?\d)\}", re.search(r"VOICE\[6\]\[4\] = \{(.*?)\};", src, re.S).group(1))]
prog = arr("PROG")
arps = [arr("ARP%d" % i) for i in range(4)]
mels = [table("MEL%d" % i) for i in range(4)]
tiers = re.findall(r"\{(\d+), (\d+), (\d+), PROG, ARP(\d), MEL(\d), BARS\d\}", src)
tempo = 100
m = re.search(r"define MUSIC_TEMPO_PCT (\d+)", src)
if m: tempo = int(m.group(1))
LOOP, ARP_HEAD, MEL_STEPS, BAR = 256, 64, 128, 16
GAP = 12
RATE = 11025

def events(tier):
    stepms, off, vol, _, a, _, = tiers[tier][0], tiers[tier][1], tiers[tier][2], tiers[tier][3], int(tiers[tier][3]), 0
    stepms = int(tiers[tier][0]) * 100 // tempo
    arp, mel = arps[int(tiers[tier][3])], mels[int(tiers[tier][4])]
    pos, mi, out = 0, 0, []
    while True:
        melody_bar = ARP_HEAD <= pos < ARP_HEAD + MEL_STEPS
        if pos % BAR == 0 and melody_bar:
            mi = sum(l for _, l in mel[:0])  # recomputed below
            acc, idx = 0, 0
            for i, (_, l) in enumerate(mel):
                if acc == pos - ARP_HEAD: idx = i; break
                acc += l
            mi = idx
        if not melody_bar:
            s = (pos & 15) // 2
            note, ln, accent = voice[prog[(pos >> 4) & 7]][arp[s]], 2, (100 if s % 2 == 0 else int(tiers[tier][1]))
        else:
            note, ln = mel[mi]; mi += 1; accent = 100
        out.append((note, ln * stepms, accent))
        pos += ln
        if pos >= LOOP: pos = 0
        yield out[-1]

def render(tier, seconds):
    vol_pct = int(tiers[tier][2])
    samples, t_ms = [], 0
    phase = 0.0
    gen = events(tier)
    while t_ms < seconds * 1000:
        note, dur, accent = next(gen)
        long_note = dur >= 700
        for t in range(dur):
            if t + GAP >= dur: f = 0
            else:
                f = hz_tab[note - note_min]
                vf = 250 if long_note else 120
                if dur >= 280 and t >= vf:
                    vib = [0, 7, 12, 7, 0, -7, -12, -7][((t - vf) // 25) & 7]
                    if not long_note: vib = vib * 3 // 4
                    f = f * (1000 + vib) / 1000
                if long_note:
                    up, down = dur // 4, dur // 8
                    env = 45 + 55 * t // up if t < up else (100 * (dur - t) // down if t + down >= dur else 100)
                    env = max(env, 15)
                else:
                    att = 100 if t >= 7 else 30 + 10 * t
                    env = (100 - 35 * t // dur) * att // 100
            v = 0 if f == 0 else min(100, 40 * accent // 100 * env // 100 * vol_pct // 100) / 100.0
            for _ in range(RATE // 1000):
                phase += f / RATE if f else 0
                samples.append(int(12000 * v * (1 if (phase % 1.0) < 0.5 else -1)))
            t_ms += 1
    return samples

for tier in range(4):
    smp = render(tier, sec)
    path = os.path.join(outdir, "music_tier%d.wav" % tier)
    with wave.open(path, "wb") as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(RATE)
        w.writeframes(struct.pack("<%dh" % len(smp), *smp))
    print(path, "%.0f BPM" % (60000 / (4 * int(tiers[tier][0]) * 100 / tempo)))
