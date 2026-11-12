#include "audio/sound.h"

#include "board_pins.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/pwm.h"
#include "hardware/regs/addressmap.h"
#include "pico/time.h"

#include <cstddef>
#include <cstdio>
#include <cstring>

#ifndef AUDIO_PIN
#define AUDIO_PIN 15
#endif

#ifndef SOUND_VOLUME
#define SOUND_VOLUME 70
#endif

#ifndef MUSIC_VOLUME
#define MUSIC_VOLUME 40
#endif

// Tempo in percent of the default (100 = 75 BPM on the calm tier, 107 BPM on
// the full-voiced one). 80 is slower, 120 faster. Compile-time: -DMUSIC_TEMPO_PCT=80.
#ifndef MUSIC_TEMPO_PCT
#define MUSIC_TEMPO_PCT 100
#endif
static_assert(MUSIC_TEMPO_PCT >= 40 && MUSIC_TEMPO_PCT <= 250, "MUSIC_TEMPO_PCT out of range");

namespace {

// The speaker pin is a 31.25 kHz, 8-bit PWM DAC: one interrupt per carrier period sets the next level. It plays the
// synth voice (effects, and the synthesized tune when no music pack is in flash) and the ADPCM music from flash.
constexpr uint32_t DAC_RATE = 31250;
constexpr uint32_t MUSIC_RATE = 15625;  // stored music: every second DAC sample
constexpr uint32_t MUSIC_FLASH_OFFSET = 0x80000;   // the pack (tools/music_pack.py) sits here, away from the firmware
constexpr uint32_t MUSIC_FLASH_LIMIT = 0x3FF000;   // ... up to the high score sector
constexpr int PACK_MAX_TRACKS = 8;
#ifndef MUSIC_SAMPLE_VOLUME
#define MUSIC_SAMPLE_VOLUME 100  // 0..100: scale of the stored music (100 = the full swing of the DAC; the packer sets the loudness)
#endif
constexpr uint32_t MIN_HZ = 190;
constexpr uint32_t MAX_HZ = 12000;
constexpr uint32_t TICK_US = 1000;

uint g_slice;
uint g_chan;

constexpr uint8_t F_NOISE = 1;

struct Seg {
    uint16_t f0, f1, ms;
    uint8_t vol, flags;
};
struct Effect {
    const Seg *segs;
    uint8_t count;
    uint8_t prio;
};

#define SEGS(name, ...) const Seg name[] = {__VA_ARGS__}

SEGS(s_vulcan, {1800, 900, 40, 50, 0});
SEGS(s_laser, {3200, 700, 90, 55, 0});
SEGS(s_missile, {900, 300, 150, 55, F_NOISE});
SEGS(s_hit_enemy, {1500, 800, 25, 50, F_NOISE});
SEGS(s_hit_player, {800, 400, 100, 70, F_NOISE});
SEGS(s_kill_small, {2200, 600, 90, 60, F_NOISE});
SEGS(s_kill_big, {1800, 400, 220, 70, F_NOISE}, {600, 250, 120, 60, 0});
SEGS(s_player_killed, {1200, 250, 350, 70, 0}, {700, 300, 350, 70, F_NOISE});
SEGS(s_fragment, {1500, 2200, 25, 45, 0});
SEGS(s_upgrade, {880, 880, 60, 60, 0}, {1175, 1175, 60, 60, 0}, {1568, 1568, 60, 60, 0},
     {2093, 2093, 110, 60, 0});
SEGS(s_menu_select, {1500, 1500, 25, 45, 0});
SEGS(s_menu_start, {1000, 1000, 50, 55, 0}, {1500, 1500, 90, 55, 0});
SEGS(s_launch, {400, 3000, 600, 55, 0});
SEGS(s_arrive, {3000, 400, 500, 55, 0});
SEGS(s_alarm, {2000, 2000, 80, 70, 0}, {0, 0, 40, 0, 0}, {2000, 2000, 80, 70, 0});
SEGS(s_dodge, {500, 1500, 70, 45, 0});

#define EFFECT(segs, prio) {segs, (uint8_t)(sizeof(segs) / sizeof(segs[0])), prio}

const Effect EFFECTS[SND_COUNT] = {
    EFFECT(s_vulcan, 1),
    EFFECT(s_laser, 1),
    EFFECT(s_missile, 2),
    EFFECT(s_hit_enemy, 2),
    EFFECT(s_hit_player, 4),
    EFFECT(s_kill_small, 3),
    EFFECT(s_kill_big, 4),
    EFFECT(s_player_killed, 7),
    EFFECT(s_fragment, 2),
    EFFECT(s_upgrade, 5),
    EFFECT(s_menu_select, 3),
    EFFECT(s_menu_start, 3),
    EFFECT(s_launch, 5),
    EFFECT(s_arrive, 5),
    EFFECT(s_alarm, 6),
    EFFECT(s_dodge, 1),
};

enum Note : uint8_t {
    G3 = 55, Gs3 = 56, A3 = 57, B3 = 59, C4 = 60, D4 = 62, E4 = 64, F4 = 65, G4 = 67, Gs4 = 68, A4 = 69, B4 = 71,
    C5 = 72, D5 = 74, E5 = 76, F5 = 77, G5 = 79, Gs5 = 80, A5 = 81, B5 = 83, C6 = 84, D6 = 86,
};
constexpr uint8_t NOTE_MIN = G3;
constexpr uint8_t NOTE_MAX = D6;

const uint16_t NOTE_HZ[] = {196,  208,  220,  233,  247,  262,  277,  294,  311,  330,  349,  370,
                            392,  415,  440,  466,  494,  523,  554,  587,  622,  659,
                            698,  740,  784,  831,  880,  932,  988,  1047, 1109, 1175};
static_assert(sizeof(NOTE_HZ) / sizeof(NOTE_HZ[0]) == NOTE_MAX - NOTE_MIN + 1, "NOTE_HZ size");

constexpr uint32_t GAP_MS = 12;
// One 16 bar loop (a bar is 16 steps = 4 beats): 4 bars of arpeggio, 8 bars of melody,
// 4 bars of arpeggio. The arpeggio notes are 8ths (2 steps).
constexpr uint32_t LOOP_STEPS = 256;
constexpr uint32_t ARP_HEAD = 64;                 // steps of intro arpeggio
constexpr uint32_t MEL_STEPS = 128;               // steps of melody
constexpr uint32_t ARP_STEPS = LOOP_STEPS - MEL_STEPS;  // intro + outro
constexpr uint32_t ARP_NOTE_STEPS = 2;
constexpr uint32_t BAR_STEPS = 16;
constexpr uint32_t TIER_COUNT = 4;

const uint8_t VOICE[6][4] = {
    {A3, C4, E4, A4},
    {A3, C4, F4, A4},
    {C4, E4, G4, C5},
    {B3, D4, G4, B4},
    {A3, D4, F4, A4},
    {B3, E4, Gs4, B4},
};

struct MNote {
    uint8_t note;
    uint8_t len;
};

// An original piece in the spirit of big, slow sci-fi scores: a rolling arpeggio that
// breathes under long, swelling melody notes, in A minor (Am F C G | Am F Dm E).
// The melody sits in the low-mid register (196..587 Hz): a small speaker is harsh above that.
// The four tiers are the same theme with growing motion and register: tier 0 is quiet and
// sustained, tier 3 is the full-voiced version at the highest tempo.
const uint8_t PROG[8] = {0, 1, 2, 3, 0, 1, 4, 5};  // chord of each bar of the arpeggio parts

const uint8_t ARP0[8] = {0, 1, 2, 3, 2, 1, 2, 3};
const uint8_t ARP1[8] = {0, 2, 1, 3, 2, 3, 1, 2};
const uint8_t ARP2[8] = {0, 1, 2, 3, 3, 2, 1, 2};
const uint8_t ARP3[8] = {0, 2, 3, 2, 3, 2, 1, 2};

constexpr MNote MEL0[] = {
    {A3, 4}, {E4, 12},
    {F4, 8}, {E4, 4}, {C4, 4},
    {C4, 4}, {E4, 4}, {G4, 8},
    {D4, 8}, {B3, 4}, {D4, 4},
    {A3, 4}, {E4, 4}, {A4, 8},
    {F4, 4}, {A4, 4}, {C5, 8},
    {A4, 4}, {F4, 4}, {D4, 8},
    {E4, 8}, {B3, 4}, {Gs3, 4},
};
constexpr MNote MEL1[] = {
    {A3, 4}, {C4, 2}, {E4, 10},
    {F4, 6}, {E4, 2}, {C4, 4}, {A3, 4},
    {C4, 2}, {E4, 2}, {G4, 4}, {E4, 2}, {G4, 2}, {C5, 4},
    {D4, 6}, {B3, 2}, {G3, 4}, {B3, 4},
    {A3, 2}, {C4, 2}, {E4, 4}, {A4, 8},
    {F4, 2}, {A4, 2}, {C5, 4}, {A4, 4}, {F4, 4},
    {D5, 4}, {A4, 4}, {F4, 4}, {D4, 4},
    {E4, 4}, {Gs4, 4}, {B4, 4}, {Gs4, 2}, {E4, 2},
};
constexpr MNote MEL2[] = {
    {E4, 4}, {A4, 12},
    {C5, 6}, {A4, 2}, {F4, 4}, {A4, 4},
    {G4, 4}, {E4, 2}, {G4, 2}, {C5, 8},
    {B4, 4}, {G4, 4}, {D4, 4}, {G4, 4},
    {A4, 6}, {E4, 2}, {A4, 4}, {C5, 4},
    {C5, 6}, {A4, 2}, {F4, 4}, {C5, 4},
    {D5, 4}, {F4, 4}, {A4, 4}, {D5, 4},
    {E4, 2}, {Gs4, 2}, {B4, 4}, {Gs4, 2}, {E4, 2}, {B3, 4},
};
constexpr MNote MEL3[] = {
    {A4, 4}, {E4, 2}, {A4, 2}, {C5, 8},
    {C5, 6}, {A4, 2}, {C5, 4}, {F4, 4},
    {G4, 2}, {E4, 2}, {C5, 4}, {E4, 2}, {G4, 2}, {D5, 4},
    {D5, 4}, {B4, 2}, {G4, 2}, {D5, 4}, {B4, 4},
    {A4, 4}, {C5, 4}, {E4, 4}, {A4, 4},
    {C5, 4}, {A4, 4}, {F4, 4}, {C5, 4},
    {D5, 6}, {A4, 2}, {F4, 4}, {D4, 4},
    {E4, 4}, {Gs4, 4}, {B4, 4}, {Gs4, 2}, {E4, 2},
};

struct BarIdx {
    uint8_t idx[8];
    bool ok;
};
template <size_t N>
constexpr BarIdx bar_index(const MNote (&m)[N]) {
    BarIdx r{};
    r.ok = true;
    uint32_t acc = 0, bar = 0;
    for (size_t i = 0; i < N; ++i) {
        if (acc % BAR_STEPS == 0) {
            if (bar >= 8) {
                r.ok = false;
                break;
            }
            r.idx[bar++] = (uint8_t)i;
        }
        if (acc % BAR_STEPS + m[i].len > BAR_STEPS) r.ok = false;
        acc += m[i].len;
    }
    if (acc != LOOP_STEPS - ARP_STEPS || bar != 8) r.ok = false;
    return r;
}

struct Tier {
    uint8_t stepMs;
    uint8_t offAccent;
    uint8_t volPct;
    const uint8_t *prog;
    const uint8_t *arp;
    const MNote *melody;
    BarIdx bars;
};

constexpr BarIdx BARS0 = bar_index(MEL0);
constexpr BarIdx BARS1 = bar_index(MEL1);
constexpr BarIdx BARS2 = bar_index(MEL2);
constexpr BarIdx BARS3 = bar_index(MEL3);
static_assert(BARS0.ok, "MEL0 must fill 8 whole bars");
static_assert(BARS1.ok, "MEL1 must fill 8 whole bars");
static_assert(BARS2.ok, "MEL2 must fill 8 whole bars");
static_assert(BARS3.ok, "MEL3 must fill 8 whole bars");

constexpr Tier TIERS[TIER_COUNT] = {
    // stepMs = one 16th note: 200 ms = 75 BPM, 180 = 83, 160 = 94, 140 = 107
    {200, 70, 85, PROG, ARP0, MEL0, BARS0},
    {180, 75, 95, PROG, ARP1, MEL1, BARS1},
    {160, 80, 105, PROG, ARP2, MEL2, BARS2},
    {140, 85, 115, PROG, ARP3, MEL3, BARS3},
};

uint32_t g_mpos = 0;
uint32_t g_mi = 0;
uint32_t g_mdur = 0;
uint32_t g_mt = 0;
uint8_t g_mnote = A3;
uint8_t g_maccent = 100;
uint8_t g_tier = 0;

volatile bool g_musicOn = false;
volatile uint8_t g_wantTier = 0;

void music_reset() {
    g_mpos = 0;
    g_mi = 0;
    g_mdur = 0;
    g_mt = 0;
}

inline bool music_tick(uint32_t &hz, uint32_t &vol) {
    if (g_mt >= g_mdur) {
        const bool melodyBar = g_mpos >= ARP_HEAD && g_mpos < ARP_HEAD + MEL_STEPS;
        if (g_mpos % BAR_STEPS == 0) {
            // A tier change is applied at the bar line, never mid-note
            uint32_t w = g_wantTier;
            if (w >= TIER_COUNT) w = TIER_COUNT - 1;
            g_tier = (uint8_t)w;
            if (melodyBar) g_mi = TIERS[g_tier].bars.idx[(g_mpos - ARP_HEAD) / BAR_STEPS];
        }
        const Tier &T = TIERS[g_tier];
        uint32_t len;
        if (!melodyBar) {
            const uint32_t s = (g_mpos & 15) / ARP_NOTE_STEPS;  // 0..7 within the bar
            g_mnote = VOICE[T.prog[(g_mpos >> 4) & 7]][T.arp[s]];
            g_maccent = ((s & 1) == 0) ? 100 : T.offAccent;
            len = ARP_NOTE_STEPS;
        } else {
            g_mnote = T.melody[g_mi].note;
            len = T.melody[g_mi].len;
            ++g_mi;
            g_maccent = 100;
        }
        g_mdur = len * (T.stepMs * 100u / MUSIC_TEMPO_PCT);
        g_mt = 0;
        g_mpos += len;
        if (g_mpos >= LOOP_STEPS) g_mpos = 0;
    }
    const uint32_t t = g_mt++;
    if (t + GAP_MS >= g_mdur) return false;
    const bool longNote = g_mdur >= 700;  // melody notes of a beat and a half or more
    uint32_t f = NOTE_HZ[g_mnote - NOTE_MIN];
    // Vibrato on notes that last: 5 Hz, about 20 cents on the long ones, after a short delay
    const uint32_t vibFrom = longNote ? 250 : 120;
    if (g_mdur >= 280 && t >= vibFrom) {
        static const int8_t VIB[8] = {0, 7, 12, 7, 0, -7, -12, -7};  // permille
        int32_t v = VIB[((t - vibFrom) / 25) & 7];
        if (!longNote) v = v * 3 / 4;
        f = (uint32_t)((int32_t)f * (1000 + v) / 1000);
    }
    hz = f;
    uint32_t env;
    if (longNote) {
        // Swell: rise over the first quarter, hold, and let go over the last eighth
        const uint32_t up = g_mdur / 4, down = g_mdur / 8;
        if (t < up) env = 45 + 55 * t / up;
        else if (t + down >= g_mdur) env = 100 * (g_mdur - t) / down;
        else env = 100;
        if (env < 15) env = 15;
    } else {
        // Soft attack (no click), then a slow decay
        const uint32_t att = t >= 7 ? 100u : 30u + 10u * t;
        env = (100 - 35 * t / g_mdur) * att / 100;
    }
    uint32_t v = (uint32_t)MUSIC_VOLUME * g_maccent / 100 * env / 100 * TIERS[g_tier].volPct / 100;
    vol = v > 100 ? 100 : v;
    return true;
}

constexpr uint32_t RING = 32;
uint32_t g_ring[RING];
uint32_t g_head = 0;
uint32_t g_tail = 0;

int g_cur = -1;
uint8_t g_seg = 0;
uint32_t g_t = 0;
uint32_t g_rng = 0x2545F491u;

// ----------------------------------------------------------------------------- the synth voice
// Pulse width tones, set at 1 kHz by sound_tick(), turned into samples by the DAC interrupt.
volatile uint32_t g_vInc = 0;   // phase step per DAC sample (frequency)
volatile uint32_t g_vDuty = 0;  // pulse width as a fraction of the period (x 2^32), 0 = silent
bool g_quiet = true;

inline void silence() {
    g_vDuty = 0;
    g_quiet = true;
}

inline void tone(uint32_t hz, uint32_t vol) {
    if (hz < MIN_HZ) hz = MIN_HZ;
    if (hz > MAX_HZ) hz = MAX_HZ;
    g_vInc = (uint32_t)(((uint64_t)hz << 32) / DAC_RATE);
    g_vDuty = (uint32_t)((((uint64_t)vol * (uint32_t)SOUND_VOLUME) << 32) / 20000u);
    g_quiet = false;
}

// ----------------------------------------------------------------------------- the stored music (4-bit IMA ADPCM)
struct TrackInfo {
    const uint8_t *data;
    uint32_t blocks;
    uint32_t check;
};
TrackInfo g_tracks[PACK_MAX_TRACKS];
int g_nTracks = 0;
bool g_packOk = false;
int g_selftest = 0;  // 0 no pack, 1 the decoder reproduced the packer's checksum, 2 it did not (pack not used)

// Tables in RAM: the interrupt must not wait for flash
int16_t g_step[89] = {7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107,
                      118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963,
                      1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894,
                      6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767};
int8_t g_idxTab[8] = {-1, -1, -1, -1, 2, 4, 6, 8};

struct Dec {
    const uint8_t *start, *next, *np;
    uint32_t blocks, blocksLeft;
    int nibLeft, idx;
    int32_t pred;
    bool hi;
};
Dec g_dec;

inline void dec_start(Dec &d, const TrackInfo &t) {
    d.start = d.next = d.np = t.data;
    d.blocks = d.blocksLeft = t.blocks;
    d.nibLeft = 0;
    d.hi = false;
}

// Next decoded sample of the track (loops at the end). Block: i16 predictor (= first sample), u8 step index, u8 0, 252 bytes of nibbles.
inline int32_t __not_in_flash_func(dec_next)(Dec &d) {
    if (d.nibLeft == 0) {
        if (d.blocksLeft == 0) {
            d.next = d.start;
            d.blocksLeft = d.blocks;
        }
        const uint8_t *b = d.next;
        d.pred = (int16_t)(b[0] | (b[1] << 8));
        const int ix = b[2];
        d.idx = ix > 88 ? 88 : ix;
        d.np = b + 4;
        d.next += 256;
        --d.blocksLeft;
        d.nibLeft = 504;
        d.hi = false;
        return d.pred;
    }
    int code;
    if (!d.hi) {
        code = *d.np & 15;
        d.hi = true;
    } else {
        code = *d.np >> 4;
        ++d.np;
        d.hi = false;
    }
    --d.nibLeft;
    const int step = g_step[d.idx];
    int vp = step >> 3;
    if (code & 4) vp += step;
    if (code & 2) vp += step >> 1;
    if (code & 1) vp += step >> 2;
    int32_t pr = d.pred + ((code & 8) ? -vp : vp);
    d.pred = pr > 32767 ? 32767 : (pr < -32768 ? -32768 : pr);
    int ix = d.idx + g_idxTab[code & 7];
    d.idx = ix < 0 ? 0 : (ix > 88 ? 88 : ix);
    return d.pred;
}

inline uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }

// Look for a music pack in flash and prove the decoder on it: the first second of track 1 must decode to the checksum
// the packer stored. A pack that fails is not used (the synthesized tune plays instead).
void pack_init() {
    const uint8_t *img = reinterpret_cast<const uint8_t *>(XIP_BASE + MUSIC_FLASH_OFFSET);
    if (memcmp(img, "DSMU", 4) != 0 || img[4] != 1) return;
    const int n = img[5];
    const uint32_t rate = (uint32_t)img[6] | ((uint32_t)img[7] << 8);
    const uint32_t blockBytes = (uint32_t)img[8] | ((uint32_t)img[9] << 8);
    if (n < 1 || n > PACK_MAX_TRACKS || rate != MUSIC_RATE || blockBytes != 256) return;
    const uint32_t room = MUSIC_FLASH_LIMIT - MUSIC_FLASH_OFFSET;
    for (int i = 0; i < n; ++i) {
        const uint8_t *e = img + 16 + 40 * i;
        const uint32_t off = rd32(e), blocks = rd32(e + 4);
        if (off < 512 || blocks < 2 || off + blocks * 256u > room) return;
        g_tracks[i] = {img + off, blocks, rd32(e + 8)};
    }
    g_nTracks = n;
    Dec d = {};
    dec_start(d, g_tracks[0]);
    uint32_t h = 2166136261u;
    for (uint32_t i = 0; i < MUSIC_RATE; ++i) h = (h ^ ((uint32_t)dec_next(d) & 0xFFFFu)) * 16777619u;
    g_selftest = h == g_tracks[0].check ? 1 : 2;
    g_packOk = g_selftest == 1;
}

volatile int g_switchTo = -1;      // track the interrupt must start (set while the music is silent)
volatile uint32_t g_gainQ = 0;     // music gain, 0..65535
volatile bool g_musicRun = false;  // the interrupt is decoding
volatile uint32_t g_isrCount = 0;
volatile uint32_t g_speedTarget = 32768, g_speedStep = 32768;  // Q16 samples per DAC sample: 32768 = as recorded
uint32_t g_mAcc = 0;
int32_t g_mPrev = 0, g_mCur = 0;
volatile uint32_t g_lvlSum = 0, g_lvlN = 0;  // music output level: sum of squares of the swing around the middle, and samples
volatile uint32_t g_isrCycles = 0;  // CPU cycles spent in the DAC interrupt (Cortex-M33 cycle counter, core0)
#define DWT_CYCCNT (*(volatile uint32_t *)0xE0001004u)
uint32_t g_phase = 0;
int32_t g_mHold = 0;


// Every DAC sample: the synth voice if it is sounding (it covers the music), else the music; the music is
// decoded either way so that it keeps its place underneath the effects.
void __not_in_flash_func(dac_isr)() {
    const uint32_t c0 = DWT_CYCCNT;
    pwm_hw->intr = 1u << g_slice;
    ++g_isrCount;
    uint32_t out = 0;
    const bool run = g_musicRun;
    if (run) {
        if (g_switchTo >= 0) {
            dec_start(g_dec, g_tracks[g_switchTo]);
            g_switchTo = -1;
            g_mAcc = 0;
            g_mPrev = g_mCur = dec_next(g_dec);
        }
        // Variable-rate playback: 32768 (0.5 stored sample per DAC sample) = as recorded; linear interpolation
        g_mAcc += g_speedStep;
        if (g_mAcc >= 65536u) {
            g_mAcc -= 65536u;
            g_mPrev = g_mCur;
            g_mCur = dec_next(g_dec);
        }
        g_mHold = g_mPrev + (((g_mCur - g_mPrev) * (int32_t)(g_mAcc >> 4)) >> 12);
    }
    g_phase += g_vInc;
    const uint32_t duty = g_vDuty;
    if (duty) {
        out = g_phase < duty ? 255u : 0u;
    } else if (run) {
        const int32_t gain = (int32_t)(g_gainQ >> 8);  // 0..255
        // middle level 128, swing up to +-127 (MUSIC_SAMPLE_VOLUME 100): t = sample * gain, swing = t * K / 32768
        const int32_t t = (g_mHold * gain) >> 8;
        int32_t o = ((128 * gain) >> 8) + ((t * (MUSIC_SAMPLE_VOLUME * 127 / 100)) >> 15);
        out = o < 0 ? 0u : (o > 255 ? 255u : (uint32_t)o);
        {
            const int32_t sw = (int32_t)out - ((128 * gain) >> 8);
            g_lvlSum += (uint32_t)(sw * sw);
            ++g_lvlN;
        }
    }
    const uint32_t cc = pwm_hw->slice[g_slice].cc;
    pwm_hw->slice[g_slice].cc = g_chan ? ((cc & 0xFFFFu) | (out << 16)) : ((cc & 0xFFFF0000u) | out);
    g_isrCycles += DWT_CYCCNT - c0;
}

// 1 kHz: which track plays and how loud. The four intensity tiers are spread over the tracks (2 tracks: tiers 0-1
// play the first, 2-3 the second); a change fades the track out (0.6 s), starts the next one and fades it in.
volatile int g_curTrack = -1;

void sample_music_tick(bool on, int tier) {
    int cur = g_curTrack;
    uint32_t target = on ? 65535u : 0u;
    int want = tier * g_nTracks / (int)TIER_COUNT;
    if (want >= g_nTracks) want = g_nTracks - 1;
    if (on && !g_musicRun) {  // start: from the beginning, fading in
        cur = want;
        g_gainQ = 0;
        g_switchTo = cur;
        g_musicRun = true;
    } else if (on && want != cur) {
        target = 0;
        if (g_gainQ == 0) {
            cur = want;
            g_switchTo = cur;
            target = 65535u;
        }
    }
    const uint32_t stepQ = on ? 110u : 330u;
    uint32_t gq = g_gainQ;
    if (gq < target) gq = (target - gq < stepQ) ? target : gq + stepQ;
    else if (gq > target) gq = (gq - target < stepQ) ? target : gq - stepQ;
    g_gainQ = gq;
    g_curTrack = cur;
    // the speed follows its target by 8/32768 per ms (about 1.2 s from 100 % to 130 %)
    uint32_t sp = g_speedStep;
    if (sp < g_speedTarget) sp = (g_speedTarget - sp < 8) ? g_speedTarget : sp + 8;
    else if (sp > g_speedTarget) sp = (sp - g_speedTarget < 8) ? g_speedTarget : sp - 8;
    g_speedStep = sp;
    if (!on && gq == 0) g_musicRun = false;  // faded out: the next start begins at the top of the track
}

inline uint32_t rnd() {
    g_rng ^= g_rng << 13;
    g_rng ^= g_rng >> 17;
    g_rng ^= g_rng << 5;
    return g_rng;
}

bool __not_in_flash_func(sound_tick)(repeating_timer_t *) {

    uint32_t t = __atomic_load_n(&g_tail, __ATOMIC_RELAXED);
    const uint32_t h = __atomic_load_n(&g_head, __ATOMIC_ACQUIRE);
    uint32_t pending = 0;
    while (t != h) {
        pending |= g_ring[t & (RING - 1)];
        ++t;
    }
    __atomic_store_n(&g_tail, t, __ATOMIC_RELEASE);

    if (pending) {
        int best = -1;
        for (int i = 0; i < SND_COUNT; ++i) {
            if ((pending >> i) & 1u) {
                if (best < 0 || EFFECTS[i].prio > EFFECTS[best].prio) best = i;
            }
        }
        if (best >= 0 && (g_cur < 0 || EFFECTS[best].prio >= EFFECTS[g_cur].prio)) {
            g_cur = best;
            g_seg = 0;
            g_t = 0;
        }
    }

    uint32_t mhz = 0, mvol = 0;
    bool music_sounds = false;
    if (g_packOk) {
        sample_music_tick(g_musicOn, g_wantTier);  // stored music: the DAC interrupt plays it
    } else if (g_musicOn) {
        music_sounds = music_tick(mhz, mvol);
    } else if (g_mpos != 0 || g_mdur != 0) {
        music_reset();
    }

    if (g_cur >= 0) {
        const Effect &e = EFFECTS[g_cur];
        while (g_t >= e.segs[g_seg].ms) {
            ++g_seg;
            g_t = 0;
            if (g_seg >= e.count) {
                g_cur = -1;
                break;
            }
        }
        if (g_cur >= 0) {
            const Seg &s = e.segs[g_seg];
            if (s.f0 == 0) {
                silence();
            } else {
                int32_t f = (int32_t)s.f0 +
                            ((int32_t)s.f1 - (int32_t)s.f0) * (int32_t)g_t / (int32_t)s.ms;
                if (f < 1) f = 1;
                if (s.flags & F_NOISE) {
                    f = f / 2 + (int32_t)(rnd() % (uint32_t)f);
                }
                tone((uint32_t)f, s.vol);
            }
            ++g_t;
            return true;
        }
    }

    if (music_sounds && mvol > 0) {
        tone(mhz, mvol);
    } else if (!g_quiet) {
        silence();
    }
    return true;
}

repeating_timer_t g_timer;

}

void sound_init() {
    // enable the cycle counter of this core (used to measure the interrupt's load)
    *(volatile uint32_t *)0xE000EDFCu |= (1u << 24);
    *(volatile uint32_t *)0xE0001000u |= 1u;
    pack_init();
    g_slice = pwm_gpio_to_slice_num(AUDIO_PIN);
    g_chan = pwm_gpio_to_channel(AUDIO_PIN);

    gpio_set_function(AUDIO_PIN, GPIO_FUNC_PWM);
    pwm_config cfg = pwm_get_default_config();
    pwm_config_set_wrap(&cfg, 255);
    pwm_config_set_clkdiv(&cfg, (float)clock_get_hz(clk_sys) / (256.0f * (float)DAC_RATE));  // one carrier period = one sample
    pwm_init(g_slice, &cfg, false);
    silence();
    pwm_hw->slice[g_slice].cc = 0;
    pwm_clear_irq(g_slice);
    pwm_set_irq_enabled(g_slice, true);
    irq_set_exclusive_handler(PWM_DEFAULT_IRQ_NUM(), dac_isr);
    irq_set_enabled(PWM_DEFAULT_IRQ_NUM(), true);
    pwm_set_enabled(g_slice, true);

    add_repeating_timer_us(-(int32_t)TICK_US, sound_tick, nullptr, &g_timer);
}

void sound_describe(char *buf, size_t n) {
    static uint32_t lastCount = 0, lastUs = 0, lastCyc = 0;
    const uint32_t now = time_us_32(), count = g_isrCount, cyc = g_isrCycles;
    const uint32_t dtUs = now - lastUs;
    const uint32_t rate = lastUs && dtUs ? (uint32_t)((uint64_t)(count - lastCount) * 1000000u / dtUs) : 0;
    const uint32_t loadPermille = lastUs && dtUs ? (uint32_t)((uint64_t)(cyc - lastCyc) * 1000u * 1000000u / ((uint64_t)clock_get_hz(clk_sys) * dtUs)) : 0;
    const uint32_t perIsr = count != lastCount ? (cyc - lastCyc) / (count - lastCount) : 0;
    lastCyc = cyc;
    lastCount = count;
    lastUs = now;
    const uint32_t ls = g_lvlSum, ln = g_lvlN;
    g_lvlSum = 0;
    g_lvlN = 0;
    uint32_t lvl = 0;  // RMS swing of the music, counts of 127
    if (ln) { uint32_t q = ls / ln, r = 0; while ((r + 1) * (r + 1) <= q) ++r; lvl = r; }
    snprintf(buf, n, "pack %s tracks %d selftest %d playing %d track %d gain %u dac %lu/s isr %lu.%lu%% (%lu cycles each) music-rms %lu/127 speed %lu%%", g_packOk ? "yes" : "no", g_nTracks, g_selftest,
             g_musicRun ? 1 : 0, g_curTrack, (unsigned)(g_gainQ >> 8), (unsigned long)rate, (unsigned long)(loadPermille / 10), (unsigned long)(loadPermille % 10), (unsigned long)perIsr, (unsigned long)lvl, (unsigned long)(g_speedStep * 100 / 32768));
}

void sound_request(uint32_t bits) {
    bits &= (1u << SND_COUNT) - 1u;
    if (bits == 0) return;
    const uint32_t h = __atomic_load_n(&g_head, __ATOMIC_RELAXED);
    const uint32_t t = __atomic_load_n(&g_tail, __ATOMIC_ACQUIRE);
    if (h - t >= RING) return;
    g_ring[h & (RING - 1)] = bits;
    __atomic_store_n(&g_head, h + 1, __ATOMIC_RELEASE);
}

void sound_music(bool on) { g_musicOn = on; }

void sound_suspend() {
    cancel_repeating_timer(&g_timer);  // the 1 kHz tick would wake a sleeping core 1000 times a second
    irq_set_enabled(PWM_DEFAULT_IRQ_NUM(), false);
    pwm_set_enabled(g_slice, false);
    pwm_hw->slice[g_slice].cc = 0;
    g_vDuty = 0;
    // Drive the pin low (transistor off) with its input buffer disabled: RP2350-E9 (datasheet App. D.5.1, chip A2):
    // an input-enabled pad between VIL and VIH leaks ~120 uA and holds itself near 2.2 V, which the pull-down cannot
    // beat; the 10 k base resistor (> 8.2 k) cannot either, so the speaker transistor could stay partly on for the
    // whole sleep. Only a <= 8.2 k drive works, and IE off removes the leak. (The Round 9f "a hard low kept the
    // board warm" premise was the regulator measuring artefact, README Round 9f.) sound_resume() sets PWM again.
    gpio_init(AUDIO_PIN);
    gpio_put(AUDIO_PIN, 0);
    gpio_set_dir(AUDIO_PIN, GPIO_OUT);
    gpio_disable_pulls(AUDIO_PIN);
    gpio_set_input_enabled(AUDIO_PIN, false);
}

void sound_resume() {
    add_repeating_timer_us(-(int32_t)TICK_US, sound_tick, nullptr, &g_timer);
    gpio_set_function(AUDIO_PIN, GPIO_FUNC_PWM);
    pwm_set_clkdiv(g_slice, (float)clock_get_hz(clk_sys) / (256.0f * (float)DAC_RATE));
    pwm_clear_irq(g_slice);
    irq_set_enabled(PWM_DEFAULT_IRQ_NUM(), true);
    pwm_set_enabled(g_slice, true);
}

void sound_music_speed(int percent) {
    if (percent < 50) percent = 50;
    if (percent > 150) percent = 150;  // at most 0.75 stored samples per DAC sample: one decode per interrupt
    g_speedTarget = (uint32_t)percent * 32768u / 100u;
}

void sound_music_level(uint8_t level) { g_wantTier = level < TIER_COUNT ? level : (uint8_t)(TIER_COUNT - 1); }
