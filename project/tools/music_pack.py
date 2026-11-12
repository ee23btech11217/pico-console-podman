#!/usr/bin/env python3
"""Pack music files into the flash image the firmware plays (4-bit IMA ADPCM, mono, 15.625 kHz).

    tools/music_pack.py [-o out/music.bin] [--window N | --max-seconds N] [--ffmpeg PATH] file1.mp3 file2.mp3 ...

The tracks are used in the order given (shell glob order = file name order) for the game's four intensity
tiers: track 1 = calmest ... track 4 = most intense (with fewer tracks the last one repeats; with more than 4
the extras are ignored). Each file is decoded with ffmpeg, mixed to mono, resampled, band limited for the small
speaker (80 Hz - 7 kHz), compressed and limited for loudness, and given a short fade at both ends so the loop does not click.

Image layout (written at flash address 0x10080000 by `make music`, see src/audio/sound.cpp):
    0   "DSMU"  u8 version(1)  u8 tracks  u16 rate  u16 blockBytes  u16 blockSamples
    16  8 x { u32 offset  u32 blocks  u32 check  u32 reserved  char title[24] }   (offset from the image start)
    512 ADPCM blocks of 256 bytes: i16 predictor, u8 stepIndex, u8 0, then 252 bytes = 504 nibbles (low first);
        the predictor is the block's first sample, the nibbles are the 504 after it.
`check` is an FNV-1a hash over the first second of decoded samples (int16, low 16 bits each): the firmware decodes
the same second at start-up and prints whether it matches, which proves the decoder on the board is bit exact.
"""
import argparse, os, struct, subprocess, sys

RATE = 15625
BLOCK_BYTES = 256
BLOCK_SAMPLES = 1 + (BLOCK_BYTES - 4) * 2   # 505
MAX_TRACKS = 8
FLASH_BUDGET = 0x37F000                       # bytes available for the image (0x10080000 .. 0x103FF000)

STEP = [7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107,
        118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963,
        1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894,
        6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767]
IDX = [-1, -1, -1, -1, 2, 4, 6, 8]


def fnv(samples):
    h = 2166136261
    for s in samples:
        h = ((h ^ (s & 0xFFFF)) * 16777619) & 0xFFFFFFFF
    return h


def decode_pcm(path, ffmpeg, max_seconds, window=0, start=None, hp=180, presence=0.0, drive=8.0, lp=3500):
    # Loud on purpose: a 0.5 W speaker on a 5 V rail needs compression and a limiter, not broadcast loudness (the first
    # version used loudnorm to -15 LUFS and was ~23 dB quieter than the sound effects).
    # Round 12: the tracks already hit the limiter, so more drive alone only squashes them (+0.7 dB RMS for +7 dB).
    # The headroom is in the bass the small speaker cannot play: a steeper high-pass at `hp` Hz (4 poles) and a mild
    # presence lift at 2 kHz leave the limiter more room for the audible band (+1.9 dB RMS above 200 Hz, same peaks).
    # Round 6 chain: --hp 90 --presence 0 --drive 5 (with a 2-pole high-pass).
    # Round 16: the user heard distortion in the highs on the new board: presence off by default and a steep (4-pole)
    # low-pass at `lp` Hz before the compressor; lp=0 restores the single 7 kHz stage. Round 12: --presence 3 --lp 0.
    eq = "highpass=f=%g:poles=2,highpass=f=%g:poles=2," % (hp, hp) if hp > 90 else "highpass=f=%g," % hp
    if presence:
        eq += "equalizer=f=2000:t=q:w=1:g=%g," % presence
    eq += ("lowpass=f=%g:poles=2,lowpass=f=%g:poles=2," % (lp, lp)) if lp else "lowpass=f=7000,"
    af = (eq +
          "acompressor=threshold=0.04:ratio=6:attack=3:release=60:makeup=10,"
          "volume=%gdB,alimiter=limit=0.95:attack=2:release=30" % drive)
    cmd = [ffmpeg, "-hide_banner", "-loglevel", "error", "-i", path, "-ac", "1", "-ar", str(RATE), "-af", af, "-f", "s16le", "-"]
    raw = subprocess.run(cmd, capture_output=True, check=True).stdout
    n = len(raw) // 2
    samples = list(struct.unpack("<%dh" % n, raw[: n * 2]))
    if window and n > window * RATE:
        # Only a part of the track is kept (flash): the most energetic `window` seconds, at least 10 s in and 10 s
        # before the end, or --start seconds in when given
        if start is None:
            per_s = [sum(x * x for x in samples[i * RATE:(i + 1) * RATE:16]) for i in range(n // RATE)]
            lo, hi = 10, max(10, len(per_s) - 10 - int(window))
            best, start = -1, lo
            run = sum(per_s[lo:lo + int(window)])
            for st in range(lo, hi + 1):
                if st > lo: run += per_s[st + int(window) - 1] - per_s[st - 1]
                if run > best: best, start = run, st
        samples = samples[int(start * RATE): int((start + window) * RATE)]
        print("  keeping %.0f s starting at %.0f s of %.0f s" % (window, start, n / RATE))
    elif max_seconds and n > max_seconds * RATE:
        samples = samples[: int(max_seconds * RATE)]
    fade = int(0.15 * RATE)
    for i in range(min(fade, len(samples))):                 # fade in / out: the loop point does not click
        g = i / fade
        samples[i] = int(samples[i] * g)
        samples[-1 - i] = int(samples[-1 - i] * g)
    return samples


def encode(samples):
    out = bytearray()
    idx = 0
    pos = 0
    blocks = 0
    n = len(samples)
    while pos < n:
        chunk = samples[pos: pos + BLOCK_SAMPLES]
        chunk = chunk + [0] * (BLOCK_SAMPLES - len(chunk))
        pred = chunk[0]
        out += struct.pack("<hBB", pred, idx, 0)
        nibbles = []
        for s in chunk[1:]:
            step = STEP[idx]
            diff = s - pred
            code = 0
            if diff < 0:
                code = 8
                diff = -diff
            vp = step >> 3
            if diff >= step:
                code |= 4; diff -= step; vp += step
            step >>= 1
            if diff >= step:
                code |= 2; diff -= step; vp += step
            step >>= 1
            if diff >= step:
                code |= 1; vp += step
            pred += -vp if code & 8 else vp
            pred = 32767 if pred > 32767 else (-32768 if pred < -32768 else pred)
            idx += IDX[code & 7]
            idx = 0 if idx < 0 else (88 if idx > 88 else idx)
            nibbles.append(code)
        for i in range(0, len(nibbles), 2):
            out.append(nibbles[i] | (nibbles[i + 1] << 4))
        pos += BLOCK_SAMPLES
        blocks += 1
    return bytes(out), blocks


def decode(data, blocks):
    """Reference decoder (the firmware does the same): returns the samples."""
    out = []
    for b in range(blocks):
        blk = data[b * BLOCK_BYTES: (b + 1) * BLOCK_BYTES]
        pred, idx, _ = struct.unpack("<hBB", blk[:4])
        out.append(pred)
        for byte in blk[4:]:
            for code in (byte & 15, byte >> 4):
                step = STEP[idx]
                vp = step >> 3
                if code & 4: vp += step
                if code & 2: vp += step >> 1
                if code & 1: vp += step >> 2
                pred += -vp if code & 8 else vp
                pred = 32767 if pred > 32767 else (-32768 if pred < -32768 else pred)
                idx += IDX[code & 7]
                idx = 0 if idx < 0 else (88 if idx > 88 else idx)
                out.append(pred)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("files", nargs="+")
    ap.add_argument("-o", "--out", default="out/music.bin")
    ap.add_argument("--window", type=float, default=0, help="keep only the most energetic WINDOW seconds of every track")
    ap.add_argument("--start", type=float, default=None, help="with --window: start at this second instead of choosing")
    ap.add_argument("--max-seconds", type=float, default=0, help="cut every track to this length (0 = whole track)")
    ap.add_argument("--hp", type=float, default=180, help="high-pass corner in Hz (bass the speaker cannot play)")
    ap.add_argument("--presence", type=float, default=0.0, help="lift at 2 kHz in dB (0 = flat; Round 12 used 3)")
    ap.add_argument("--lp", type=float, default=3500, help="steep low-pass corner in Hz (0 = old 7 kHz single stage)")
    ap.add_argument("--drive", type=float, default=8.0, help="gain into the limiter in dB")
    ap.add_argument("--ffmpeg", default=os.environ.get("FFMPEG", "ffmpeg"))
    a = ap.parse_args()
    import shutil
    if shutil.which(a.ffmpeg) is None and not os.path.exists(a.ffmpeg):
        try:
            import imageio_ffmpeg  # pip install imageio-ffmpeg: a static ffmpeg inside the wheel
            a.ffmpeg = imageio_ffmpeg.get_ffmpeg_exe()
        except ImportError:
            sys.exit("ffmpeg not found: install it, or `pip install imageio-ffmpeg`, or pass --ffmpeg PATH")
    files = a.files[:4]
    tracks = []
    for f in files:
        print("decoding", f, flush=True)
        pcm = decode_pcm(f, a.ffmpeg, a.max_seconds, a.window, a.start, a.hp, a.presence, a.drive, a.lp)
        data, blocks = encode(pcm)
        dec = decode(data, blocks)
        err = sum((x - y) ** 2 for x, y in zip(pcm, dec)) / max(1, len(pcm))
        sig = sum(x * x for x in pcm) / max(1, len(pcm))
        snr = 10 * (__import__("math").log10(sig / err) if err > 0 and sig > 0 else 99)
        print("  %.1f s, %d blocks, %.1f KB, ADPCM SNR %.1f dB" % (len(pcm) / RATE, blocks, len(data) / 1024, snr))
        rms = (sum(x * x for x in pcm) / max(1, len(pcm))) ** 0.5
        print("  level: RMS %.1f dBFS, peak %.1f dBFS -> about %.0f counts RMS of 127 on the speaker (sound effects: 100-125)" % (
            20 * __import__("math").log10(max(rms, 1) / 32768), 20 * __import__("math").log10(max(max(abs(x) for x in pcm), 1) / 32768), rms * 127 / 32768))
        tracks.append((os.path.splitext(os.path.basename(f))[0][:23], data, blocks, fnv(dec[:RATE])))
    total = 512 + sum(len(t[1]) for t in tracks)
    if total > FLASH_BUDGET:
        sys.exit("too long: %d bytes of the %d available; use --max-seconds (about %d s per track)" % (total, FLASH_BUDGET, FLASH_BUDGET // len(tracks) // (BLOCK_BYTES * RATE // BLOCK_SAMPLES)))
    head = bytearray(512)
    struct.pack_into("<4sBBHHH", head, 0, b"DSMU", 1, len(tracks), RATE, BLOCK_BYTES, BLOCK_SAMPLES)
    off = 512
    for i, (title, data, blocks, check) in enumerate(tracks):
        struct.pack_into("<IIII24s", head, 16 + 40 * i, off, blocks, check, 0, title.encode("ascii", "replace"))
        off += len(data)
    os.makedirs(os.path.dirname(os.path.abspath(a.out)), exist_ok=True)
    with open(a.out, "wb") as f:
        f.write(head)
        for t in tracks:
            f.write(t[1])
    print("wrote %s: %d tracks, %.0f KB of %.0f KB flash" % (a.out, len(tracks), total / 1024, FLASH_BUDGET / 1024))
    for i, t in enumerate(tracks):
        print("  tier %d: %-23s check %08x" % (i, t[0], t[3]))


main()
