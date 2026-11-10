/* SPDX-License-Identifier: GPL-3.0-only */
/* Tracks 1-3, the synths: LEAD (4 voices), BASS (1 voice), PAD (4 voices). One subtractive engine for all, its
 * settings per track (set.snd[track], snd_t in quartet.c):
 *   2 oscillators, each SINE / TRIANGLE / SAW / SQUARE; osc 2 tuned -24..+24 semitones (and ~7 cents apart, for
 *   width), mixed 0 (osc 1 only) .. 127 (mostly osc 2)
 *   a resonant low-pass filter (cutoff, resonance), opened by the envelope (filter env amount)
 *   one ADSR envelope (attack, decay, sustain, release) for the level and the filter
 *   one LFO per track (rate 0.05-20 Hz, depth; to PITCH, FILTER or AMP; TRIANGLE / SQUARE / SAW / RANDOM)
 *   10 sounds per track (PRESETS below, chosen with SELECT); picking one replaces the track's settings
 * Included by quartet.c, after gb_drums.h. Runs in the audio interrupt.
 *
 * Patterns: step_note() / step_len() (quartet.c): note 0 is a rest; LEAD and BASS hold one note a step, the PAD up to 4;
 * len = how many steps the notes last (1..NSTEP). Each track plays
 * play_pat[track] at trk_pos[track] (gb_clock.h moves them).
 * Live notes (the keys, MIDI) come from the main loop through live_q (main writes, the interrupt reads).
 *
 * The cost, per voice and sample: 1-2 oscillators, the filter once (pad) or twice (lead, bass), a few adds.
 * Everything else (envelope, LFO, filter coefficients) runs once per 16 samples. */

enum { W_SIN, W_TRI, W_SAW, W_SQR, W_NOISE, W_FM, NWAVE };   /* FM (osc 1): a sine whose pitch osc 2 bends, fast */
enum { L_PITCH, L_FILT, L_AMP, NLTGT };
enum { LW_TRI, LW_SQR, LW_SAW, LW_RND, NLWAVE };
#define NPRESET 10u                             /* sounds per synth (and drum kits): 2 clicks of SELECT each */

/* the filter's frequency coefficient for cutoff 0..127 = 40 Hz .. 8 kHz (exponential): 2 sin(pi fc / 88200), Q15.
 * LEAD and BASS run the filter twice per sample (88200 Hz: stable up to 8 kHz at any resonance; f = FTAB >> 1 in
 * Q14). PAD runs it once per sample (half the cost; f = FTAB taken as Q14, ~2 sin(pi fc / 44100)), which is stable
 * only below ~5.5 kHz, so the pad's cutoff stops at 112 (~4.3 kHz). Damping in Q13. All 32-bit.
 * The inner values are kept within +-131071 once per 16 samples, not every sample (that was a third of the cost).
 * Modelled, exactly this arithmetic, all cutoffs and resonances, saws 40 Hz - 1.8 kHz, cutoff jumps: the largest
 * product is 4.1e8 of 2.1e9, the inner values stay under 70,000. (The 1x filter blows up at cutoff 127: hence 112.) */
static const uint16_t FTAB[128] = {
    93, 97, 101, 106, 110, 115, 120, 125, 130, 136, 142, 148, 154, 161, 167, 175, 182, 190, 198, 206, 215, 224,
    234, 244, 254, 265, 276, 288, 300, 313, 326, 340, 355, 370, 386, 402, 419, 437, 456, 475, 495, 516, 539, 561,
    585, 610, 636, 663, 692, 721, 752, 784, 817, 852, 888, 926, 966, 1007, 1050, 1094, 1141, 1190, 1240, 1293,
    1348, 1406, 1466, 1528, 1593, 1661, 1732, 1805, 1882, 1962, 2046, 2133, 2224, 2319, 2417, 2520, 2628, 2740,
    2856, 2978, 3105, 3237, 3374, 3518, 3668, 3824, 3987, 4156, 4333, 4517, 4709, 4910, 5118, 5336, 5563, 5799,
    6046, 6302, 6570, 6849, 7139, 7442, 7758, 8087, 8429, 8786, 9158, 9546, 9949, 10370, 10807, 11263, 11738,
    12232, 12747, 13283, 13840, 14420, 15024, 15652, 16305, 16984, 17689, 18423};
#define SVF_LIM 131071

/* envelope speed 0..127: time constant 0.5 ms .. 4 s; per-16-sample step 1 - e^(-16 / (tau * 44100)), Q15
 * (a few: 20 = 2 ms, 40 = 8 ms, 60 = 36 ms, 70 = 70 ms, 80 = 145 ms, 90 = 290 ms, 100 = 0.6 s, 110 = 1.2 s) */
static const uint16_t ENV_K[128] = {
    16908, 16101, 15314, 14546, 13801, 13079, 12382, 11710, 11064, 10444, 9851, 9285,
    8744, 8229, 7739, 7274, 6833, 6416, 6020, 5647, 5294, 4961, 4648, 4352,
    4074, 3813, 3567, 3336, 3120, 2916, 2726, 2547, 2379, 2222, 2075, 1938,
    1809, 1689, 1576, 1471, 1373, 1281, 1195, 1115, 1040, 970, 904, 843,
    787, 733, 684, 638, 594, 554, 517, 482, 449, 418, 390, 363,
    339, 316, 294, 274, 256, 238, 222, 207, 193, 180, 167, 156,
    145, 135, 126, 118, 110, 102, 95, 89, 83, 77, 72, 67,
    62, 58, 54, 50, 47, 44, 41, 38, 35, 33, 31, 29,
    27, 25, 23, 22, 20, 19, 17, 16, 15, 14, 13, 12,
    11, 11, 10, 9, 9, 8, 7, 7, 6, 6, 6, 5,
    5, 5, 4, 4, 4, 3, 3, 3};
/* LFO rate 0..127: 0.05 .. 20 Hz (64 = 1 Hz, 95 = 4.6 Hz), phase step per sample (f * 2^32 / 44100) */
static const uint32_t LFO_INC[128] = {
    4870, 5105, 5351, 5610, 5881, 6165, 6463, 6775,
    7102, 7445, 7805, 8182, 8577, 8992, 9426, 9881,
    10359, 10859, 11384, 11934, 12510, 13115, 13748, 14412,
    15108, 15838, 16603, 17405, 18246, 19128, 20052, 21020,
    22036, 23100, 24216, 25386, 26612, 27898, 29246, 30658,
    32139, 33692, 35320, 37026, 38814, 40689, 42655, 44716,
    46876, 49140, 51514, 54002, 56611, 59346, 62213, 65218,
    68369, 71671, 75134, 78763, 82568, 86556, 90738, 95121,
    99716, 104533, 109583, 114877, 120426, 126243, 132342, 138735,
    145437, 152463, 159828, 167549, 175642, 184127, 193022, 202346,
    212121, 222368, 233110, 244371, 256176, 268551, 281524, 295124,
    309381, 324326, 339994, 356418, 373635, 391685, 410606, 430441,
    451235, 473033, 495884, 519839, 544951, 571276, 598873, 627803,
    658131, 689923, 723252, 758190, 794816, 833212, 873462, 915657,
    959890, 1006260, 1054869, 1105827, 1159247, 1215247, 1273953, 1335494,
    1400008, 1467639, 1538537, 1612860, 1690773, 1772450, 1858072, 1947831};
/* osc 2 tuning, 2^(s / 12) for s = -24..+24 semitones (index s + 24), Q16 */
static const uint32_t SEMI[49] = {
    16384, 17358, 18390, 19484, 20643, 21870, 23170, 24548,
    26008, 27554, 29193, 30929, 32768, 34716, 36781, 38968,
    41285, 43740, 46341, 49097, 52016, 55109, 58386, 61858,
    65536, 69433, 73562, 77936, 82570, 87480, 92682, 98193,
    104032, 110218, 116772, 123715, 131072, 138866, 147123, 155872,
    165140, 174960, 185364, 196386, 208064, 220436, 233544, 247431,
    262144};

/* the ten sounds of each synth (SELECT, 2 clicks each), made from the engine's parts:
 * {cut, res, fenv, wave1, wave2, tune (24 = 0), mix, A, D, S, R, lfo rate, depth, target, wave, index}
 * (Envelope times: 0 = instant, 60 ~ 36 ms, 72 ~ 80 ms, 88 ~ 0.25 s, 96 ~ 0.4 s, 100 ~ 0.6 s.) */
static const snd_t PRESETS[NSYN][NPRESET] = {
    {   /* LEAD */
        {95, 0, 45, W_TRI, W_SAW, 36, 20, 0, 96, 0, 80, 95, 0, L_PITCH, LW_TRI, 0},          /* PIANO: a struck, bright attack that fades */
        {127, 0, 0, W_SIN, W_SIN, 43, 40, 0, 72, 0, 62, 95, 0, L_PITCH, LW_TRI, 1},          /* XYLOPHONE: sine + its 3rd harmonic, a short knock */
        {45, 20, 85, W_SAW, W_SAW, 24, 50, 55, 85, 100, 60, 95, 8, L_PITCH, LW_TRI, 2},      /* BRASS: two saws, a 25 ms swell, the filter opening with it */
        {112, 0, 0, W_SIN, W_SIN, 48, 30, 0, 92, 0, 90, 95, 0, L_PITCH, LW_TRI, 3},          /* MUSIC BOX: a sine + two octaves up, a tinkling pluck that rings out */
        {70, 35, 55, W_SAW, W_SQR, 24, 45, 0, 92, 60, 62, 95, 0, L_PITCH, LW_TRI, 4},        /* ELEC GUITAR: a picked, bright string that rings on (add DIST for crunch) */
        {80, 10, 25, W_SQR, W_SIN, 43, 45, 0, 60, 127, 30, 105, 10, L_PITCH, LW_TRI, 5},     /* HAMMOND: a drawbar organ, + the octave-and-fifth, a fast Leslie wobble */
        {112, 0, 0, W_SIN, W_SIN, 24, 0, 70, 80, 127, 80, 90, 30, L_PITCH, LW_TRI, 6},       /* THEREMIN: a pure sine that swells in, a wide singing vibrato */
        {85, 25, 30, W_SAW, W_SAW, 24, 50, 5, 70, 110, 55, 95, 0, L_PITCH, LW_TRI, 7},       /* SYNTH: two saws, wide, held */
        {80, 40, 40, W_SQR, W_SQR, 24, 50, 0, 80, 110, 60, 95, 6, L_PITCH, LW_TRI, 8},       /* SQUARE LEAD: two detuned squares, filtered, a touch of vibrato */
        {112, 0, 0, W_SQR, W_SQR, 36, 15, 0, 60, 127, 20, 95, 0, L_PITCH, LW_TRI, 9}},       /* CHIPTUNE: a raw square (+ a little of its octave), on and off at once */
    {   /* BASS */
        {72, 25, 40, W_SAW, W_TRI, 24, 40, 0, 92, 85, 50, 60, 0, L_FILT, LW_TRI, 0},         /* UPRIGHT: round and woody, a thump that holds (a saw under it: as loud as the rest) */
        {32, 20, 60, W_SAW, W_SQR, 24, 30, 50, 85, 100, 50, 85, 6, L_PITCH, LW_TRI, 1},      /* TUBA: a dark brass note that swells in (~20 ms), the filter opening with it */
        {55, 60, 100, W_SAW, W_SQR, 36, 35, 0, 70, 50, 45, 60, 0, L_FILT, LW_TRI, 2},        /* SLAP: a bright snap and pop (+ the octave), then a short body */
        {25, 40, 110, W_SAW, W_SQR, 24, 30, 0, 60, 0, 40, 60, 0, L_FILT, LW_TRI, 3},         /* PLUCKED: a short dark pluck */
        {110, 120, 0, W_SQR, W_SAW, 24, 64, 0, 80, 120, 45, 60, 0, L_FILT, LW_TRI, 4},       /* FUZZ: buzzy, the resonance clips */
        {50, 50, 70, W_SQR, W_SAW, 24, 50, 0, 75, 60, 45, 60, 0, L_FILT, LW_TRI, 5},         /* SYNTH BASS: square + saw, a snappy filter, the classic */
        {127, 0, 70, W_FM, W_SIN, 24, 40, 0, 80, 50, 45, 60, 0, L_FILT, LW_TRI, 6},          /* FM: a sine bent by a sine (1:1), brightest at the attack */
        {30, 100, 95, W_SAW, W_SAW, 24, 0, 0, 66, 30, 40, 60, 0, L_FILT, LW_TRI, 7},         /* ACID: squelchy, the filter snaps */
        {60, 30, 20, W_SAW, W_SAW, 24, 64, 0, 80, 127, 50, 30, 4, L_PITCH, LW_TRI, 8},       /* REESE: two saws a little apart, beating, a slow drift */
        {40, 90, 0, W_SAW, W_SQR, 24, 40, 0, 80, 127, 45, 80, 100, L_FILT, LW_TRI, 9}},      /* WOBBLE: the LFO opens and closes the filter (~2 Hz) */
    {   /* PAD */
        {80, 0, 0, W_SAW, W_SAW, 36, 40, 85, 100, 120, 90, 92, 12, L_PITCH, LW_TRI, 0},      /* STRINGS: brighter, a little vibrato */
        {72, 80, 0, W_SAW, W_NOISE, 24, 35, 88, 100, 120, 92, 92, 14, L_PITCH, LW_TRI, 1},   /* CHOIR: saw + breath through an "aah" resonance (~800 Hz), vibrato */
        {88, 15, 40, W_SAW, W_TRI, 36, 40, 0, 96, 0, 84, 60, 0, L_PITCH, LW_TRI, 2},         /* AC GUITAR: strummed steel strings: a bright pluck, no sustain */
        {100, 0, 0, W_TRI, W_SIN, 36, 55, 2, 60, 127, 40, 96, 18, L_AMP, LW_TRI, 3},         /* ORGAN: two drawbars, instant, a slight wobble */
        {100, 0, 50, W_FM, W_SIN, 36, 30, 0, 100, 40, 85, 85, 20, L_AMP, LW_TRI, 4},         /* ELEC PIANO: a bell-like FM tine, brightest as it is struck, tremolo */
        {90, 0, 40, W_FM, W_SIN, 43, 15, 2, 100, 0, 100, 60, 0, L_PITCH, LW_TRI, 5},         /* HANDPAN: a warm metal note (FM 3:1), struck, ringing long */
        {50, 10, 10, W_SAW, W_SAW, 24, 64, 92, 100, 115, 95, 60, 0, L_PITCH, LW_TRI, 6},     /* WARM SYNTH: soft saws, slow in and out */
        {100, 10, 20, W_FM, W_SIN, 48, 20, 85, 100, 110, 95, 70, 10, L_AMP, LW_TRI, 7},      /* GLASS PAD: a soft, glassy FM shimmer (4:1), slow in and out */
        {40, 50, 0, W_SAW, W_SAW, 12, 50, 100, 100, 127, 110, 40, 40, L_FILT, LW_TRI, 8},    /* DRONE: dark saws (+ an octave down), very slow, the filter breathing */
        {50, 70, 0, W_SAW, W_SAW, 24, 50, 80, 100, 120, 95, 50, 80, L_FILT, LW_TRI, 9}}};    /* SWEEP: a slow LFO sweeps the resonant filter up and down */
static const char *const PRESET_NAME[NSYN][NPRESET] = {
    {"PIANO", "XYLOPHONE", "BRASS", "MUSIC BOX", "ELEC GUITAR", "HAMMOND", "THEREMIN", "SYNTH", "SQUARE LEAD", "CHIPTUNE"},
    {"UPRIGHT", "TUBA", "SLAP", "PLUCKED", "FUZZ", "SYNTH BASS", "FM", "ACID", "REESE", "WOBBLE"},
    {"STRINGS", "CHOIR", "AC GUITAR", "ORGAN", "ELEC PIANO", "HANDPAN", "WARM SYNTH", "GLASS PAD", "DRONE", "SWEEP"}};

#define NVOX 9u                                 /* voices: 0..3 lead, 4 bass, 5..8 pad */
/* how each synth track plays: its voices, loudness, where its notes sit, its filter */
static const struct {
    uint8_t first, count;                       /* its voices in vx[] */
    int32_t level;                              /* loudness of one voice, Q15 */
    int8_t transpose;                           /* semitones from the key you press */
    uint8_t over;                               /* filter runs per sample: 2 (to 8 kHz) or 1 (cheaper, to ~4.3 kHz) */
    uint8_t cmax;                               /* the highest cutoff index it may reach */
} TRK[NSYN] = {
    {0, 4, 7000, 12, 1, 112},                   /* LEAD: 4 voices, an octave up (the 1x filter, as the pad: CPU) */
    {4, 1, 16000, -24, 2, 127},                 /* BASS: two octaves down */
    {5, 4, 5000, 0, 1, 112}};                   /* PAD: 4 voices */

static struct {
    uint32_t ph, ph2, inc, inc2, age;
    int32_t amp;                                /* the level now, Q23 (level << 8 at full); ramps to env*lfo each block */
    int32_t dstep;                              /* amp's step per sample in this block */
    int32_t env;                                /* the ADSR, Q23 */
    int32_t low, band, f, q;
    uint8_t gate, note, seq, stage, blk, fmi;   /* seq: started by the sequencer; stage: 0 attack, 1 decay/sustain, 2 release; fmi: FM depth */
} vx[NVOX];                                     /* interrupt only */
static uint32_t syn_left[NSYN];                 /* interrupt: ticks until the sequenced notes of a track end */
static uint32_t vx_ages;

/* main loop -> interrupt: live notes. One writer each: main writes live_w, the interrupt live_r */
#define LIVE_N 32u
static struct { uint8_t trk, note, on; } live_q[LIVE_N];
static volatile uint32_t live_w, live_r;
static void live_note(uint32_t trk, uint32_t note, uint32_t on)   /* main loop */
{
    uint32_t w = live_w;
    if (w - live_r >= LIVE_N) return;           /* full (it never is: the interrupt empties it every 5.8 ms) */
    live_q[w % LIVE_N].trk = (uint8_t)trk;
    live_q[w % LIVE_N].note = (uint8_t)note;
    live_q[w % LIVE_N].on = (uint8_t)on;
    live_w = w + 1u;                            /* (published last: the interrupt sees a whole entry) */
}

static void voice_on(uint32_t trk, uint32_t note, uint32_t seq)
{
    uint32_t v, pick = TRK[trk].first, best = 0xFFFFFFFFu, tune = set.snd[trk].tune;
    if (!note || note > 127u) return;
    for (v = TRK[trk].first; v < TRK[trk].first + TRK[trk].count; v++) {   /* the same note, a free voice, the oldest */
        uint32_t score = vx[v].note == note && vx[v].gate ? 0u
                       : vx[v].gate ? 0x80000000u + (vx_ages - vx[v].age > 0x7FFFFFFFu ? 0u : 0x7FFFFFFFu - (vx_ages - vx[v].age))
                       : (uint32_t)(vx[v].env >> 8) + 1u;
        if (score < best) { best = score; pick = v; }
    }
    vx[pick].inc = INC_C9[note % 12u] >> (10u - note / 12u);
    vx[pick].inc2 = (uint32_t)(((uint64_t)vx[pick].inc * SEMI[tune > 48u ? 48u : tune]) >> 16);
    vx[pick].inc2 += vx[pick].inc2 >> 8;        /* ~7 cents above: two oscillators that beat a little sound wider */
    vx[pick].note = (uint8_t)note;
    vx[pick].seq = (uint8_t)seq;
    vx[pick].age = ++vx_ages;
    vx[pick].stage = 0;                         /* attack, from wherever the envelope is (no click) */
    vx[pick].gate = 1;
}
static void voice_off(uint32_t trk, uint32_t note, int seq_only)   /* note 0: all of the track's notes */
{
    uint32_t v;
    for (v = TRK[trk].first; v < TRK[trk].first + TRK[trk].count; v++)
        if ((!note || vx[v].note == note) && (!seq_only || vx[v].seq) && vx[v].gate) {
            vx[v].gate = 0;
            vx[v].stage = 2;                    /* release */
        }
}

/* ARPEGGIATOR (per synth track: set.arp_on bit t, arp_rate, arp_oct, arp_dir): the keys held on the track, and the
 * notes its pattern plays (each as long as it lasts), play one at a time over 1..4 octaves, UP (low to high), DOWN or RANDOM, at 1/4 .. 1/32 notes, each sounding half its step. It runs on the clock's ticks
 * (gb_clock.h calls arp_tick(): while playing, the very ticks the patterns use, so it cannot drift from them;
 * stopped, ticks at the same tempo). */
#define ARP_MAX 8u
static const uint8_t ARP_TICKS[4] = {16, 8, 4, 2};   /* ticks a note: 1/4, 1/8, 1/16, 1/32 (TPS = 4 a 16th) */
static const char *const ARP_NAME[4] = {"1/4", "1/8", "1/16", "1/32"};
static uint8_t arp_held[NSYN][ARP_MAX], arp_n[NSYN], arp_cur[NSYN];   /* interrupt only: the keys held, low to high */
static uint32_t arp_count[NSYN], arp_idx[NSYN], arp_gate[NSYN];
static uint8_t seq_arp[NSYN][4];                /* interrupt only: the pattern's notes the arpeggiator holds now (0: none) */

static void arp_add(uint32_t t, uint32_t note)
{
    uint32_t i, j;
    for (i = 0; i < arp_n[t]; i++) if (arp_held[t][i] == note) return;
    if (arp_n[t] >= ARP_MAX) return;
    if (!arp_n[t]) { arp_count[t] = 0; arp_idx[t] = 0; }   /* the first key: the arpeggio starts at once */
    for (i = 0; i < arp_n[t] && arp_held[t][i] < note; i++) {}
    for (j = arp_n[t]; j > i; j--) arp_held[t][j] = arp_held[t][j - 1u];
    arp_held[t][i] = (uint8_t)note;
    arp_n[t]++;
}
static void arp_del(uint32_t t, uint32_t note)
{
    uint32_t i;
    for (i = 0; i < arp_n[t] && arp_held[t][i] != note; i++) {}
    if (i == arp_n[t]) return;
    for (; i + 1u < arp_n[t]; i++) arp_held[t][i] = arp_held[t][i + 1u];
    arp_n[t]--;
}
static void seq_arp_drop(uint32_t t)            /* the pattern's notes end: the arpeggiator lets them go */
{
    uint32_t i;
    for (i = 0; i < 4u; i++) if (seq_arp[t][i]) { arp_del(t, seq_arp[t][i]); seq_arp[t][i] = 0; }
}
static void arp_tick(void)                      /* every clock tick */
{
    uint32_t t;
    for (t = 0; t < NSYN; t++) {
        uint32_t rate = ARP_TICKS[set.arp_rate[t] & 3u];
        if (arp_gate[t] && --arp_gate[t] == 0u && arp_cur[t]) { voice_off(t, arp_cur[t], 0); arp_cur[t] = 0; }
        if (!(set.arp_on >> t & 1u)) {          /* off: forget the keys (and the pattern's notes) */
            arp_n[t] = 0;
            seq_arp[t][0] = seq_arp[t][1] = seq_arp[t][2] = seq_arp[t][3] = 0;
            continue;
        }
        if (!arp_n[t]) continue;
        if (arp_count[t]++ % rate == 0u) {      /* the next note: through the keys, over 1..4 octaves */
            static uint32_t rnd = 77777u;
            uint32_t span = arp_n[t] * ((set.arp_oct[t] & 3u) + 1u), i, note;
            if (set.arp_dir[t] == 2u) { rnd ^= rnd << 13; rnd ^= rnd >> 17; rnd ^= rnd << 5; i = rnd % span; }   /* RANDOM */
            else { i = arp_idx[t]++ % span; if (set.arp_dir[t] == 1u) i = span - 1u - i; }   /* UP, DOWN */
            note = arp_held[t][i % arp_n[t]] + 12u * (i / arp_n[t]);
            if (note > 127u) note = 127u;
            if (arp_cur[t]) voice_off(t, arp_cur[t], 0);
            voice_on(t, note, 0);
            arp_cur[t] = (uint8_t)note;
            arp_gate[t] = rate / 2u ? rate / 2u : 1u;
        }
    }
}

static void synth_step(void)
{
    uint32_t t, i;
    for (t = 0; t < NSYN; t++) {
        const uint8_t *nt = step_note(t, play_pat[t], trk_pos[t]);
        uint32_t ln = *step_len(t, play_pat[t], trk_pos[t]);
        if ((trk_mute | chain_rest) >> t & 1u || !nt[0]) continue;   /* (muted, or a SUPER SEQ rest) */
        voice_off(t, 0, 1);                     /* (the last step's notes end: a new one starts) */
        if (set.arp_on >> t & 1u) {             /* the arpeggiator on: it takes the notes, as if held on the keys */
            seq_arp_drop(t);
            for (i = 0; i < STEP_NN(t); i++)
                if (nt[i] && nt[i] < 128u) { arp_add(t, nt[i]); seq_arp[t][i] = nt[i]; }
        } else
            for (i = 0; i < STEP_NN(t); i++)
                if (nt[i] && nt[i] < 128u) voice_on(t, nt[i], 1);
        syn_left[t] = (ln ? ln : 1u) * TPS - 1u;   /* the notes end a tick before their last step does */
    }
}
static void synth_tick(void)
{
    uint32_t t;
    for (t = 0; t < NSYN; t++) {
        if ((trk_mute | chain_rest) >> t & 1u && syn_left[t]) syn_left[t] = 1;   /* muted (or resting): its notes end now */
        if (syn_left[t] && --syn_left[t] == 0u) { voice_off(t, 0, 1); seq_arp_drop(t); }
    }
}
static void synth_stop(void)
{
    uint32_t t;
    for (t = 0; t < NSYN; t++) { syn_left[t] = 0; voice_off(t, 0, 1); seq_arp_drop(t); }
}
static int synth_quiet(void)
{
    uint32_t v;
    for (v = 0; v < NVOX; v++)
        if (vx[v].gate || vx[v].amp || vx[v].env) return 0;
    return 1;
}

static inline int32_t osc_wave(uint32_t w, uint32_t ph)   /* one cycle over 2^32 -> -32767..32767 */
{
    static uint32_t nz = 24680u;
    int32_t s = (int32_t)ph;
    switch (w) {
    case W_TRI: return ((s ^ (s >> 31)) >> 15) - 32768;   /* |s| rising and falling */
    case W_SAW: return s >> 16;
    case W_SQR: return s < 0 ? -32767 : 32767;
    case W_NOISE: nz ^= nz << 13; nz ^= nz >> 17; nz ^= nz << 5; return (int32_t)nz >> 16;   /* breath, hiss */
    default: return sine(ph);                   /* SINE (and FM, as osc 2: the sine that does the bending) */
    }
}

static void synth_render(int32_t (*tbuf)[FXN], uint32_t off, uint32_t n)   /* into each track's mono buffer, from off */
{
    static uint32_t lfo_ph[NSYN], rnd = 12345u;
    static int32_t lfo_rnd[NSYN];
    uint32_t v, i, t;
    while (live_r != live_w) {                  /* the live notes first */
        uint32_t r = live_r;
        if (live_q[r % LIVE_N].trk < NSYN) {
            uint32_t lt = live_q[r % LIVE_N].trk, ln = live_q[r % LIVE_N].note;
            if (set.arp_on >> lt & 1u) {        /* the arpeggiator takes the keys */
                if (live_q[r % LIVE_N].on) arp_add(lt, ln);
                else { arp_del(lt, ln); voice_off(lt, ln, 0); }   /* (a note held from before the arp went on: let go) */
            } else if (live_q[r % LIVE_N].on) voice_on(lt, ln, 0);
            else voice_off(lt, ln, 0);
        }
        live_r = r + 1u;
    }
    for (t = 0; t < NSYN; t++) {
        const snd_t *sp = &set.snd[t];
        int32_t lv, lmod, full = TRK[t].level << 8;
        uint32_t old = lfo_ph[t], w1 = sp->w1 % NWAVE, w2 = sp->w2 % NWAVE, mix = sp->mix & 127u, over = TRK[t].over;
        int32_t sus = (full >> 7) * (int32_t)(sp->s + (sp->s >> 6));   /* ~ full * s / 127 */
        lfo_ph[t] += LFO_INC[sp->lrate & 127u] * n;   /* the track's LFO, one value for this piece */
        if (sus > full) sus = full;
        switch (sp->lwave) {
        case LW_TRI: { int32_t s = (int32_t)lfo_ph[t]; lv = ((s ^ (s >> 31)) >> 15) - 32768; break; }
        case LW_SQR: lv = (int32_t)lfo_ph[t] < 0 ? -32767 : 32767; break;
        case LW_SAW: lv = 32767 - (int32_t)(lfo_ph[t] >> 16); break;   /* falling */
        default:
            if (lfo_ph[t] < old) { rnd ^= rnd << 13; rnd ^= rnd >> 17; rnd ^= rnd << 5; lfo_rnd[t] = (int32_t)rnd >> 16; }
            lv = lfo_rnd[t];
        }
        lmod = (lv * (int32_t)(sp->ldepth & 127u)) >> 7;   /* +-32767 at full depth */
        for (v = TRK[t].first; v < TRK[t].first + TRK[t].count; v++) {
            int32_t amp = vx[v].amp, dstep = vx[v].dstep, low = vx[v].low, band = vx[v].band, f = vx[v].f, q = vx[v].q;
            uint32_t ph = vx[v].ph, ph2 = vx[v].ph2, i1 = vx[v].inc, i2 = vx[v].inc2, blk = vx[v].blk, fmi = vx[v].fmi;
            int32_t *o = &tbuf[t][off];
            if (!vx[v].gate && !amp && !vx[v].env) continue;   /* silent: costs nothing */
            if (sp->ltgt == L_PITCH && lmod) {  /* vibrato: up to +-1 semitone */
                int32_t pm = (lmod * 3874) >> 15;
                i1 += (uint32_t)(int32_t)(((int64_t)i1 * pm) >> 16);
                i2 += (uint32_t)(int32_t)(((int64_t)i2 * pm) >> 16);
            }
            for (i = 0; i < n; ) {
                uint32_t m, j;
                if (blk == 0u) {                /* every 16 samples: the envelope, the LFO, the filter's coefficients */
                    int32_t env = vx[v].env, eff, en, c;
                    if (vx[v].stage == 0u) {    /* attack: towards 1.3 x full, done at full */
                        env += (int32_t)(((int64_t)(full + (full >> 2) + (full >> 4) - env) * ENV_K[sp->a & 127u]) >> 15);
                        if (env >= full) { env = full; vx[v].stage = 1; }
                    } else if (vx[v].stage == 1u)   /* decay to the sustain level, and stay */
                        env += (int32_t)(((int64_t)(sus - env) * ENV_K[sp->d & 127u]) >> 15);
                    else {                      /* release */
                        env -= (int32_t)(((int64_t)env * ENV_K[sp->r & 127u]) >> 15);
                        if (env < 4096) env = 0;
                    }
                    vx[v].env = env;
                    eff = env;
                    if (sp->ltgt == L_AMP)      /* tremolo: down to ~1 % at full depth */
                        eff = (int32_t)(((int64_t)eff * (32768 - ((((lv + 32768) >> 1) * (int32_t)(sp->ldepth & 127u)) >> 7))) >> 15);
                    if (!env && vx[v].stage == 2u) { amp = 0; dstep = 0; }   /* the release has ended: silence */
                    else dstep = (eff - amp) >> 4;   /* a ramp over the block: no steps you could hear */
                    en = ((env >> 8) * 32767) / TRK[t].level;   /* the envelope 0..32767 for the filter */
                    c = (int32_t)sp->cut + ((en * (int32_t)sp->fenv) >> 15);
                    if (w1 == W_FM) {           /* FM: how hard osc 2 bends: MIX, plus the envelope times FILTER ENV */
                        int32_t fx = (int32_t)mix + ((en * (int32_t)sp->fenv) >> 15);
                        fmi = (uint32_t)(fx > 127 ? 127 : fx);
                    }
                    if (sp->ltgt == L_FILT) c += (lmod * 64) >> 15;   /* wah: +-64 cutoff steps */
                    c = c < 0 ? 0 : c > TRK[t].cmax ? TRK[t].cmax : c;
                    f = over == 2u ? FTAB[c] >> 1 : FTAB[c];
                    q = 16384 - (int32_t)(sp->res & 127u) * 119;   /* damping, Q13: 2.0 (none) .. 0.15 (screaming) */
                    if (low > SVF_LIM) low = SVF_LIM; else if (low < -SVF_LIM) low = -SVF_LIM;   /* (a safety net) */
                    if (band > SVF_LIM) band = SVF_LIM; else if (band < -SVF_LIM) band = -SVF_LIM;
                    blk = 16;
                }
                m = n - i < blk ? n - i : blk;  /* up to the next update, or the end of this piece */
                blk -= m;
                i += m;
                for (j = 0; j < m; j++) {
                    int32_t x, high, s;
                    amp += dstep;
                    ph += i1;
                    if (w1 == W_FM) {           /* FM: osc 2 (a sine) pushes osc 1's phase back and forth, up to ~half a cycle */
                        ph2 += i2;
                        x = sine(ph + ((uint32_t)(sine(ph2) * (int32_t)fmi) << 9)) >> 1;
                    } else if (mix) {           /* two oscillators, mixed, half scale (room for the resonance) */
                        ph2 += i2;
                        x = (osc_wave(w1, ph) * (int32_t)(128u - mix) + osc_wave(w2, ph2) * (int32_t)mix) >> 8;
                    } else
                        x = osc_wave(w1, ph) >> 1;
                    low += (f * band) >> 14;    /* Chamberlin state-variable filter */
                    high = x - low - ((q * band) >> 13);
                    band += (f * high) >> 14;
                    if (over == 2u) {           /* (lead, bass: once more, for the high cutoffs) */
                        low += (f * band) >> 14;
                        high = x - low - ((q * band) >> 13);
                        band += (f * high) >> 14;
                    }
                    s = low > 32767 ? 32767 : low < -32767 ? -32767 : low;
                    s = (s * (amp >> 8)) >> 15;
                    *o++ += s;
                }
            }
            vx[v].amp = amp; vx[v].dstep = dstep; vx[v].ph = ph; vx[v].ph2 = ph2; vx[v].fmi = (uint8_t)fmi;
            vx[v].low = low; vx[v].band = band; vx[v].f = f; vx[v].q = q; vx[v].blk = (uint8_t)blk;
        }
    }
}
