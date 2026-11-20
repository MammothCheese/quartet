/* SPDX-License-Identifier: GPL-3.0-only */
/* Tracks 1-3, the synth slots: each a LEAD (4 voices), BASS (1 voice) or PAD (4 voices) (set.styp; 9 voices shared,
 * trk_setup). One subtractive engine for all, its
 * settings per track (set.snd[track], snd_t in quartet.c):
 *   2 oscillators, each SINE / TRIANGLE / SAW / SQUARE; osc 2 tuned -24..+24 semitones (and ~7 cents apart, for
 *   width), mixed 0 (osc 1 only) .. 127 (mostly osc 2)
 *   a resonant low-pass filter (cutoff, resonance), opened by the envelope (filter env amount)
 *   one ADSR envelope (attack, decay, sustain, release) for the level and the filter
 *   one LFO per track (rate 0.05-20 Hz, depth; to PITCH, FILTER or AMP; TRIANGLE / SQUARE / SAW / RANDOM)
 *   the sound library below (banks ORIGINAL, CLASSIC, EXPERIMENTAL, and USER in set.mu.ub; chosen with SELECT or the
 *   SOUND page's INSTR); picking one replaces the slot's settings and glide
 * Included by quartet.c, after gb_drums.h. Runs in the audio interrupt.
 *
 * Patterns: step_note() / step_len() (quartet.c): note 0 is a rest; LEAD and BASS hold one note a step, the PAD up to 4;
 * len = how many steps the notes last (1..NSTEP). Each track plays
 * play_pat[track] at trk_pos[track] (gb_clock.h moves them).
 * Live notes (the keys, MIDI) come from the main loop through live_q (main writes, the interrupt reads).
 *
 * The cost, per voice and sample: 1-2 oscillators, the filter once (pad) or twice (lead, bass), a few adds.
 * Everything else (envelope, LFO, filter coefficients) runs once per 16 samples. */

enum { W_SIN, W_TRI, W_SAW, W_SQR, W_NOISE, W_FM, W_PULSE, W_SYNC, NWAVE };   /* FM (osc 1): a sine whose pitch osc 2
                                                   bends, fast. PULSE: a square whose width the LFO can move (PWM).
                                                   SYNC (osc 2): a saw restarted with each cycle of osc 1 (hard sync;
                                                   FILTER ENV sweeps its pitch: the sync scream) */
enum { L_PITCH, L_FILT, L_AMP, L_PWM, NLTGT };   /* (PWM: the pulse width) */
enum { LW_TRI, LW_SQR, LW_SAW, LW_RND, NLWAVE };
#define NPRESET 10u                             /* the ORIGINAL bank's sounds per synth (ids 0..9: every older save's) */

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

/* ---- the sound library: banks of sounds for each instrument type, each with an id that never changes (so a saved
 * song always finds its sound): ORIGINAL 0..9 (QUARTET's first ten: every older save points here), CLASSIC 32..,
 * EXPERIMENTAL 64.., USER 96.. (sounds you saved: set.mu.ub). A sound is the engine's settings and its glide:
 * {cut, res, fenv, wave1, wave2, tune (24 = 0), mix, A, D, S, R, lfo rate, depth, target, wave} + glide 0..127.
 * (Envelope times: 0 = instant, 60 ~ 36 ms, 72 ~ 80 ms, 88 ~ 0.25 s, 96 ~ 0.4 s, 100 ~ 0.6 s.) */
typedef struct { const char *name; uint8_t id, glide; snd_t s; } sdef_t;
#define SD(nm, id, gl, cu, re, fe, w1, w2, tu, mx, a, d, su, r, lr, ld, lt, lw) {nm, id, gl, {cu, re, fe, w1, w2, tu, mx, a, d, su, r, lr, ld, lt, lw, id}}
#define NBANK 5u                                /* the groups a type's sounds are browsed in: four, then USER */
#define BK_USER 4u
static const char *const BANK_NAME[NSYN][NBANK] = {   /* by type: T_LEAD, T_BASS, T_PAD */
    {"KEYS", "BAND", "SYNTH", "ODD", "USER"}, {"ACOUSTIC", "ANALOG", "DIGITAL", "WILD", "USER"},
    {"STRINGS", "KEYS", "SYNTH", "ATMOS", "USER"}};
/* the sounds offered, group by group (0xFF: the end). The library's others are not offered any more but stay here,
 * loadable, so an older save sounds as it did (its ids never change) */
#define GX 0xFFu
static const uint8_t SND_GRP[NSYN][4][6] = {
    {{0, 1, 3, 38, 40, 41}, {5, 2, 4, 36, 42, 43}, {32, 33, 34, 35, 9, 39}, {6, 64, 67, 68, 66, 69}},
    /* LEAD: KEYS piano, xylophone, music box, FM bell, tine keys, clavinet; BAND Hammond, brass, electric guitar, synth
       brass, flute, harmonica; SYNTH mini, sync, PWM, acid lead, chiptune, porta lead; ODD theremin, glass harp,
       laser, bowed metal, sine swarm, stepper */
    {{0, 2, 3, 1, 40, 41}, {32, 33, 36, 5, 38, 68}, {34, 8, 39, 6, 35, 37}, {4, 9, 64, 66, 69, 67}},
    /* BASS: ACOUSTIC upright, slap, plucked, tuba, fretless, picked; ANALOG Moog, 303 line, sub, synth bass, rubber,
       slide sub; DIGITAL DX bass, Reese, electro bass, FM, deep Reese, synth pluck; WILD fuzz, wobble, growl, bubble,
       womp, dirt bass */
    {{0, 34, 1, 2, 40, 39}, {4, 37, 3, 5, 41, 42}, {32, 35, 36, 33, 38, 6}, {8, 64, 67, 9, 7, 65}}};
    /* PAD: STRINGS strings, Solina, choir, acoustic guitar, harp, digital choir; KEYS electric piano, DX piano, organ,
       handpan, vibes, marimba; SYNTH Juno strings, poly synth, PWM pad, Prophet brass, warm analog, warm synth; ATMOS
       drone, frozen, cathedral, sweep, glass pad, tidal strings, poly synth, PWM pad, Prophet brass; ATMOS drone, frozen, cathedral, sweep */
#define UID(u) (96u + (u))                      /* a USER sound's id */
static const sdef_t SND_LEAD[] = {
    /* ORIGINAL */
    SD("PIANO", 0, 0, 95, 0, 45, W_TRI, W_SAW, 36, 20, 0, 96, 0, 80, 95, 0, L_PITCH, LW_TRI),          /* a struck, bright attack that fades */
    SD("XYLOPHONE", 1, 0, 127, 0, 0, W_SIN, W_SIN, 43, 40, 0, 72, 0, 62, 95, 0, L_PITCH, LW_TRI),      /* sine + its 3rd harmonic, a short knock */
    SD("BRASS", 2, 0, 45, 20, 85, W_SAW, W_SAW, 24, 50, 55, 85, 100, 60, 95, 8, L_PITCH, LW_TRI),      /* two saws, a 25 ms swell, the filter opening */
    SD("MUSIC BOX", 3, 0, 112, 0, 0, W_SIN, W_SIN, 48, 30, 0, 92, 0, 90, 95, 0, L_PITCH, LW_TRI),      /* a sine + two octaves up, a tinkling pluck */
    SD("ELEC GUITAR", 4, 0, 70, 35, 55, W_SAW, W_SQR, 24, 45, 0, 92, 60, 62, 95, 0, L_PITCH, LW_TRI),  /* a picked, bright string that rings on */
    SD("HAMMOND", 5, 0, 80, 10, 25, W_SQR, W_SIN, 43, 45, 0, 60, 127, 30, 105, 10, L_PITCH, LW_TRI),   /* a drawbar organ, a fast Leslie wobble */
    SD("THEREMIN", 6, 0, 112, 0, 0, W_SIN, W_SIN, 24, 0, 70, 80, 127, 80, 90, 30, L_PITCH, LW_TRI),    /* a pure sine, a wide singing vibrato */
    SD("SYNTH", 7, 0, 85, 25, 30, W_SAW, W_SAW, 24, 50, 5, 70, 110, 55, 95, 0, L_PITCH, LW_TRI),       /* two saws, wide, held */
    SD("SQUARE LEAD", 8, 0, 80, 40, 40, W_SQR, W_SQR, 24, 50, 0, 80, 110, 60, 95, 6, L_PITCH, LW_TRI), /* two detuned squares, a touch of vibrato */
    SD("CHIPTUNE", 9, 0, 112, 0, 0, W_SQR, W_SQR, 36, 15, 0, 60, 127, 20, 95, 0, L_PITCH, LW_TRI),     /* a raw square, on and off at once */
    /* CLASSIC */
    SD("MINI LEAD", 32, 8, 72, 60, 55, W_SAW, W_SQR, 12, 40, 0, 74, 100, 52, 96, 5, L_PITCH, LW_TRI),  /* saw + a square an octave down, a fat resonant filter */
    SD("SYNC LEAD", 33, 0, 100, 25, 90, W_SAW, W_SYNC, 36, 100, 0, 88, 90, 50, 95, 0, L_PITCH, LW_TRI),/* hard sync, the envelope sweeping it: the scream */
    SD("PWM LEAD", 34, 0, 88, 20, 30, W_PULSE, W_PULSE, 24, 35, 2, 80, 110, 55, 74, 70, L_PWM, LW_TRI),/* two pulses, their width swaying: alive */
    SD("ACID LEAD", 35, 45, 34, 108, 100, W_SAW, W_SAW, 24, 0, 0, 66, 25, 40, 60, 0, L_FILT, LW_TRI),  /* one saw, a screaming snappy filter, slides */
    SD("SYNTH BRASS", 36, 0, 38, 25, 100, W_SAW, W_PULSE, 24, 55, 62, 88, 95, 62, 95, 5, L_PITCH, LW_TRI),   /* a slow filter swell on saw + pulse */
    SD("MONO SQUARE", 37, 30, 76, 60, 40, W_SQR, W_SQR, 36, 25, 0, 82, 110, 50, 96, 8, L_PITCH, LW_TRI),     /* a square and its octave, sliding */
    SD("FM BELL", 38, 0, 127, 0, 60, W_FM, W_SIN, 48, 70, 0, 102, 0, 100, 70, 10, L_AMP, LW_TRI),      /* FM 4:1: a struck bell that rings long */
    SD("PORTA LEAD", 39, 72, 82, 40, 35, W_SAW, W_SAW, 24, 55, 5, 80, 115, 60, 95, 12, L_PITCH, LW_TRI),/* two saws gliding from note to note */
    /* EXPERIMENTAL */
    SD("GLASS HARP", 64, 0, 110, 0, 30, W_FM, W_SIN, 43, 30, 30, 100, 60, 100, 40, 12, L_AMP, LW_TRI), /* FM 3:1, bowed glass, a slow shimmer */
    SD("BROKEN RADIO", 65, 0, 60, 112, 20, W_SQR, W_NOISE, 24, 50, 0, 80, 100, 50, 100, 60, L_FILT, LW_RND),   /* a square and hiss, the filter jumping */
    SD("SINE SWARM", 66, 0, 127, 0, 0, W_SIN, W_SIN, 25, 64, 40, 90, 110, 90, 110, 26, L_PITCH, LW_RND),/* two sines a semitone apart, wandering */
    SD("LASER", 67, 0, 124, 60, 127, W_SAW, W_SYNC, 48, 100, 0, 70, 0, 40, 95, 0, L_PITCH, LW_TRI),    /* sync, swept fast and short: pew */
    SD("BOWED METAL", 68, 0, 92, 30, 20, W_FM, W_SIN, 31, 50, 82, 100, 100, 95, 60, 10, L_PITCH, LW_TRI),   /* FM at an odd ratio, slowly bowed */
    SD("STEPPER", 69, 0, 80, 40, 30, W_SQR, W_SAW, 24, 30, 0, 80, 110, 50, 104, 42, L_PITCH, LW_SQR),  /* a fast trill: the LFO steps the pitch */
    /* (later: more for the groups) */
    SD("TINE KEYS", 40, 0, 112, 0, 30, W_FM, W_SIN, 36, 30, 0, 94, 20, 75, 95, 0, L_PITCH, LW_TRI),   /* FM tines: an electric piano's bell */
    SD("CLAVINET", 41, 0, 70, 55, 85, W_PULSE, W_SAW, 36, 30, 0, 70, 0, 35, 95, 0, L_PITCH, LW_TRI),  /* a funky, snappy plucked string */
    SD("FLUTE", 42, 0, 92, 10, 10, W_SIN, W_NOISE, 24, 12, 40, 80, 110, 55, 95, 10, L_PITCH, LW_TRI), /* a breathy sine with vibrato */
    SD("HARMONICA", 43, 0, 72, 30, 25, W_PULSE, W_SQR, 24, 45, 25, 80, 100, 45, 90, 14, L_AMP, LW_TRI),   /* reedy, a little tremble */
};
#define SND_REESE 8u                            /* the bass a new project starts with */
static const sdef_t SND_BASS[] = {
    /* ORIGINAL */
    SD("UPRIGHT", 0, 0, 72, 25, 40, W_SAW, W_TRI, 24, 40, 0, 92, 85, 50, 60, 0, L_FILT, LW_TRI),       /* round and woody, a thump that holds */
    SD("TUBA", 1, 0, 32, 20, 60, W_SAW, W_SQR, 24, 30, 50, 85, 100, 50, 85, 6, L_PITCH, LW_TRI),       /* a dark brass note that swells in */
    SD("SLAP", 2, 0, 55, 60, 100, W_SAW, W_SQR, 36, 35, 0, 70, 50, 45, 60, 0, L_FILT, LW_TRI),         /* a bright snap and pop */
    SD("PLUCKED", 3, 0, 25, 40, 110, W_SAW, W_SQR, 24, 30, 0, 60, 0, 40, 60, 0, L_FILT, LW_TRI),       /* a short dark pluck */
    SD("FUZZ", 4, 0, 110, 120, 0, W_SQR, W_SAW, 24, 64, 0, 80, 120, 45, 60, 0, L_FILT, LW_TRI),        /* buzzy, the resonance clips */
    SD("SYNTH BASS", 5, 0, 50, 50, 70, W_SQR, W_SAW, 24, 50, 0, 75, 60, 45, 60, 0, L_FILT, LW_TRI),    /* square + saw, a snappy filter */
    SD("FM", 6, 0, 127, 0, 70, W_FM, W_SIN, 24, 40, 0, 80, 50, 45, 60, 0, L_FILT, LW_TRI),             /* a sine bent by a sine, brightest at the attack */
    SD("ACID", 7, 0, 30, 100, 95, W_SAW, W_SAW, 24, 0, 0, 66, 30, 40, 60, 0, L_FILT, LW_TRI),          /* squelchy, the filter snaps */
    SD("REESE", 8, 0, 60, 30, 20, W_SAW, W_SAW, 24, 64, 0, 80, 127, 50, 30, 4, L_PITCH, LW_TRI),       /* two saws a little apart, beating */
    SD("WOBBLE", 9, 0, 40, 90, 0, W_SAW, W_SQR, 24, 40, 0, 80, 127, 45, 80, 100, L_FILT, LW_TRI),      /* the LFO opens and closes the filter */
    /* CLASSIC */
    SD("MOOG BASS", 32, 0, 38, 58, 72, W_SAW, W_SQR, 12, 50, 0, 76, 80, 45, 60, 0, L_FILT, LW_TRI),    /* saw + a square an octave down, the ladder's growl */
    SD("303 LINE", 33, 45, 30, 112, 100, W_SAW, W_SAW, 24, 0, 0, 64, 20, 40, 60, 0, L_FILT, LW_TRI),   /* one saw, a squelching filter, sliding notes */
    SD("DX BASS", 34, 0, 127, 0, 100, W_FM, W_SIN, 36, 80, 0, 78, 40, 45, 60, 0, L_FILT, LW_TRI),      /* FM 2:1, a hard metallic pluck */
    SD("DEEP REESE", 35, 0, 52, 40, 10, W_SAW, W_PULSE, 24, 64, 0, 80, 127, 50, 40, 50, L_PWM, LW_TRI),/* saw against a slowly widening pulse */
    SD("SUB BASS", 36, 0, 127, 0, 0, W_SIN, W_TRI, 24, 20, 0, 80, 127, 40, 60, 0, L_FILT, LW_TRI),     /* a pure low sine: felt more than heard */
    SD("SYNTH PLUCK", 37, 0, 45, 50, 112, W_SQR, W_SAW, 36, 30, 0, 62, 0, 40, 60, 0, L_FILT, LW_TRI),  /* a short bright pluck that closes at once */
    SD("RUBBER BASS", 38, 20, 34, 72, 90, W_SQR, W_SIN, 24, 50, 0, 78, 60, 45, 60, 0, L_FILT, LW_TRI), /* a bouncy rounded square, slides a little */
    SD("ELECTRO BASS", 39, 0, 62, 40, 60, W_PULSE, W_SAW, 12, 50, 0, 70, 90, 40, 60, 0, L_FILT, LW_TRI),/* pulse + saw an octave down, tight */
    /* EXPERIMENTAL */
    SD("GROWL", 64, 0, 70, 92, 30, W_FM, W_SIN, 31, 60, 0, 80, 120, 45, 85, 70, L_FILT, LW_SAW),       /* FM through a filter the LFO saws at */
    SD("SYNC BASS", 65, 0, 72, 40, 92, W_SAW, W_SYNC, 31, 80, 0, 70, 80, 40, 60, 0, L_FILT, LW_TRI),   /* a synced snarl, swept by the envelope */
    SD("BUBBLE", 66, 0, 100, 30, 40, W_SIN, W_TRI, 24, 30, 0, 80, 100, 45, 110, 30, L_PITCH, LW_SQR),  /* a sine trilling fast: bubbles */
    SD("DIRT BASS", 67, 0, 55, 120, 50, W_SAW, W_NOISE, 24, 25, 0, 75, 90, 40, 60, 0, L_FILT, LW_TRI), /* a saw with grit, the resonance on edge */
    SD("SLIDE SUB", 68, 92, 127, 0, 0, W_SIN, W_SIN, 36, 20, 0, 80, 127, 50, 60, 0, L_FILT, LW_TRI),   /* a sub that slides long between notes */
    SD("WOMP", 69, 0, 40, 92, 0, W_SAW, W_PULSE, 24, 50, 0, 80, 127, 45, 75, 100, L_FILT, LW_SQR),     /* the filter gated open and shut on the beat */
    /* (later: more for the groups) */
    SD("FRETLESS", 40, 30, 48, 30, 30, W_SAW, W_TRI, 24, 55, 10, 85, 90, 50, 80, 6, L_PITCH, LW_TRI),   /* warm and sliding, a singing tone */
    SD("PICKED", 41, 0, 62, 25, 90, W_SAW, W_SQR, 24, 30, 0, 74, 60, 40, 60, 0, L_FILT, LW_TRI),       /* a plectrum's bright attack */
};
static const sdef_t SND_PAD[] = {
    /* ORIGINAL */
    SD("STRINGS", 0, 0, 80, 0, 0, W_SAW, W_SAW, 36, 40, 85, 100, 120, 90, 92, 12, L_PITCH, LW_TRI),    /* brighter, a little vibrato */
    SD("CHOIR", 1, 0, 72, 80, 0, W_SAW, W_NOISE, 24, 35, 88, 100, 120, 92, 92, 14, L_PITCH, LW_TRI),   /* saw + breath through an "aah" resonance */
    SD("AC GUITAR", 2, 0, 88, 15, 40, W_SAW, W_TRI, 36, 40, 0, 96, 0, 84, 60, 0, L_PITCH, LW_TRI),     /* strummed steel strings */
    SD("ORGAN", 3, 0, 100, 0, 0, W_TRI, W_SIN, 36, 55, 2, 60, 127, 40, 96, 18, L_AMP, LW_TRI),         /* two drawbars, a slight wobble */
    SD("ELEC PIANO", 4, 0, 100, 0, 50, W_FM, W_SIN, 36, 30, 0, 100, 40, 85, 85, 20, L_AMP, LW_TRI),    /* a bell-like FM tine, tremolo */
    SD("HANDPAN", 5, 0, 90, 0, 40, W_FM, W_SIN, 43, 15, 2, 100, 0, 100, 60, 0, L_PITCH, LW_TRI),       /* a warm metal note, ringing long */
    SD("WARM SYNTH", 6, 0, 50, 10, 10, W_SAW, W_SAW, 24, 64, 92, 100, 115, 95, 60, 0, L_PITCH, LW_TRI),/* soft saws, slow in and out */
    SD("GLASS PAD", 7, 0, 100, 10, 20, W_FM, W_SIN, 48, 20, 85, 100, 110, 95, 70, 10, L_AMP, LW_TRI),  /* a soft, glassy FM shimmer */
    SD("DRONE", 8, 0, 40, 50, 0, W_SAW, W_SAW, 12, 50, 100, 100, 127, 110, 40, 40, L_FILT, LW_TRI),    /* dark saws, the filter breathing */
    SD("SWEEP", 9, 0, 50, 70, 0, W_SAW, W_SAW, 24, 50, 80, 100, 120, 95, 50, 80, L_FILT, LW_TRI),      /* a slow LFO sweeps the resonant filter */
    /* CLASSIC */
    SD("JUNO STRINGS", 32, 0, 70, 10, 12, W_SAW, W_PULSE, 24, 55, 82, 100, 120, 95, 66, 40, L_PWM, LW_TRI),   /* saw + a swaying pulse (add CHORUS) */
    SD("PROPHET BRASS", 33, 0, 44, 28, 82, W_SAW, W_PULSE, 24, 50, 62, 90, 100, 82, 95, 5, L_PITCH, LW_TRI),  /* poly brass, the filter swelling open */
    SD("SOLINA", 34, 0, 86, 0, 0, W_SAW, W_SAW, 36, 50, 90, 100, 127, 98, 100, 22, L_PITCH, LW_TRI),   /* string machine: a saw and its octave, ensemble shimmer */
    SD("POLY SYNTH", 35, 0, 74, 30, 48, W_SAW, W_SQR, 24, 40, 10, 90, 90, 80, 95, 0, L_PITCH, LW_TRI), /* bright chords that bloom and settle */
    SD("PWM PAD", 36, 0, 74, 10, 0, W_PULSE, W_PULSE, 24, 40, 86, 100, 120, 96, 58, 92, L_PWM, LW_TRI),/* two pulses, their widths slowly swirling */
    SD("DX PIANO", 37, 0, 122, 0, 100, W_FM, W_SIN, 48, 25, 0, 96, 30, 82, 80, 16, L_AMP, LW_TRI),     /* FM 4:1: a bright, chiming electric piano */
    SD("WARM ANALOG", 38, 0, 46, 20, 22, W_SAW, W_TRI, 12, 50, 72, 100, 115, 100, 40, 8, L_FILT, LW_TRI),     /* a saw over a triangle an octave down, slow */
    SD("DIGITAL CHOIR", 39, 0, 80, 72, 0, W_PULSE, W_NOISE, 24, 25, 86, 100, 120, 95, 92, 12, L_PITCH, LW_TRI),/* a pulse and breath, a vowel's ring */
    /* EXPERIMENTAL */
    SD("FROZEN", 64, 0, 100, 10, 0, W_FM, W_SIN, 48, 15, 100, 100, 127, 110, 30, 30, L_AMP, LW_RND),   /* glassy FM that flickers, slow as ice */
    SD("TIDAL", 65, 0, 70, 40, 0, W_SAW, W_SYNC, 36, 80, 90, 100, 120, 100, 20, 50, L_FILT, LW_TRI),     /* synced saws under a filter like a tide */
    SD("DUST", 66, 0, 60, 116, 0, W_NOISE, W_SIN, 24, 40, 90, 100, 120, 100, 30, 40, L_FILT, LW_TRI),  /* air through a singing resonance */
    SD("CATHEDRAL", 67, 0, 55, 10, 0, W_TRI, W_SAW, 12, 40, 110, 110, 127, 115, 50, 6, L_PITCH, LW_TRI),      /* huge and slow, an octave below */
    SD("PULSAR", 68, 0, 80, 30, 0, W_PULSE, W_PULSE, 31, 40, 20, 90, 110, 80, 105, 80, L_PWM, LW_SQR), /* the pulse's width flicking on the beat */
    SD("SPECTRAL", 69, 0, 90, 40, 0, W_FM, W_SIN, 29, 40, 95, 100, 120, 100, 25, 60, L_FILT, LW_TRI), /* odd-ratio FM, a slow filter drift */
    /* (later: more for the groups) */
    SD("HARP", 40, 0, 104, 0, 25, W_TRI, W_SIN, 36, 30, 0, 98, 0, 96, 95, 0, L_PITCH, LW_TRI),       /* plucked strings that ring on */
    SD("VIBES", 41, 0, 127, 0, 0, W_SIN, W_SIN, 48, 25, 0, 100, 0, 98, 92, 30, L_AMP, LW_TRI),       /* soft mallets, the motor's tremolo */
    SD("MARIMBA", 42, 0, 110, 0, 10, W_SIN, W_TRI, 43, 20, 0, 80, 0, 64, 95, 0, L_PITCH, LW_TRI),     /* wooden bars, a short warm knock */
};
static const sdef_t *const SND_LIB[NSYN] = {SND_LEAD, SND_BASS, SND_PAD};   /* by type: T_LEAD, T_BASS, T_PAD */
static const uint8_t SND_N[NSYN] = {sizeof SND_LEAD / sizeof SND_LEAD[0], sizeof SND_BASS / sizeof SND_BASS[0],
                                    sizeof SND_PAD / sizeof SND_PAD[0]};
static const sdef_t *sound_def(uint32_t ty, uint32_t id)   /* a library sound by its id (none: 0; a USER id: 0) */
{
    uint32_t i;
    for (i = 0; i < SND_N[ty % NSYN]; i++) if (SND_LIB[ty % NSYN][i].id == id) return &SND_LIB[ty % NSYN][i];
    return 0;
}
static int sound_ok(uint32_t ty, uint32_t id)   /* a sound that exists: in the library, or a USER slot in use */
{
    if (id >= UID(0) && id < UID(NUSND)) return set.mu.ub.snd[ty % NSYN][id - UID(0)].preset != 0;
    return sound_def(ty, id) != 0;
}
static uint32_t sound_group(uint32_t ty, uint32_t id)   /* a sound's group (0-3), USER 4; one no longer offered: GX */
{
    uint32_t g, i;
    if (id >= UID(0)) return BK_USER;
    for (g = 0; g < 4u; g++) for (i = 0; i < 6u; i++) if (SND_GRP[ty % NSYN][g][i] == id) return g;
    return GX;
}
static uint32_t sound_list(uint32_t ty, uint8_t *ids, uint32_t cur)   /* the sounds offered, group by group, then USER
                                                   (cur first, if it is one no longer offered: an older save's) */
{
    uint32_t g, i, n = 0;
    if (sound_group(ty, cur) == GX && sound_def(ty, cur)) ids[n++] = (uint8_t)cur;
    for (g = 0; g < 4u; g++) for (i = 0; i < 6u && SND_GRP[ty % NSYN][g][i] != GX; i++) ids[n++] = SND_GRP[ty % NSYN][g][i];
    for (i = 0; i < NUSND; i++) if (set.mu.ub.snd[ty % NSYN][i].preset) ids[n++] = (uint8_t)UID(i);
    return n;
}
static const char *sound_name(uint32_t ty, uint32_t id)
{
    static char un[8] = "USER 1";
    const sdef_t *d = sound_def(ty, id);
    if (d) return d->name;
    un[5] = (char)('1' + (id - UID(0)) % 10u);
    return un;
}
static void sound_load(uint32_t t, uint32_t id) /* slot t takes sound id of its type: its settings, its glide */
{
    uint32_t ty = set.styp[t] % NSYN;
    const sdef_t *d = sound_def(ty, id);
    if (d) { set.snd[t] = d->s; set.glide[t] = d->glide; }
    else if (sound_ok(ty, id)) {
        set.snd[t] = set.mu.ub.snd[ty][id - UID(0)];
        set.glide[t] = set.mu.ub.glide[ty][id - UID(0)];
    } else { set.snd[t] = SND_LIB[ty][0].s; set.glide[t] = SND_LIB[ty][0].glide; return; }
    set.snd[t].preset = (uint8_t)id;
}

typedef struct {
    uint8_t first, count;                       /* its voices in vx[] */
    int32_t level;                              /* loudness of one voice, Q15 */
    int8_t transpose;                           /* semitones from the key you press */
    uint8_t over;                               /* filter runs per sample: 2 (to 8 kHz) or 1 (cheaper, to ~4.3 kHz) */
    uint8_t cmax;                               /* the highest cutoff index it may reach */
} trkd_t;
#define NVOX 9u                                 /* voices, shared by the three synth slots (trk_setup) */
static const trkd_t TYPD[NSYN] = {              /* each instrument type, as it was its slot's (first, count: below) */
    {0, 4, 7000, 12, 1, 112},                   /* LEAD: 4 voices, an octave up (the 1x filter, as the pad: CPU) */
    {0, 1, 16000, -24, 2, 127},                 /* BASS: one voice, two octaves down */
    {0, 4, 5000, 0, 1, 112}};                   /* PAD: 4 voices */
static trkd_t TRK[NSYN];                        /* each synth slot now (trk_setup: from its type; its voices out of 9) */
static volatile uint32_t trk_req, trk_done = ~0u;   /* main loop: +1 = a slot's type changed (the interrupt re-deals) */
static void trk_setup(void)                     /* the slots' types -> their settings and voices: LEAD and PAD 4 each,
                                                   BASS 1; more than the 9 there are: the biggest gives one up */
{
    uint32_t t, c[NSYN], f = 0, sum = 0;
    for (t = 0; t < NSYN; t++) { TRK[t] = TYPD[set.styp[t] % NSYN]; c[t] = TRK[t].count; sum += c[t]; }
    while (sum > NVOX) {
        uint32_t big = 0;
        for (t = 1; t < NSYN; t++) if (c[t] > c[big]) big = t;
        c[big]--; sum--;
    }
    for (t = 0; t < NSYN; t++) { TRK[t].first = (uint8_t)f; TRK[t].count = (uint8_t)c[t]; f += c[t]; }
}

static struct {
    uint32_t ph, ph2, inc, inc2, age;
    int32_t amp;                                /* the level now, Q23 (level << 8 at full); ramps to env*lfo each block */
    int32_t dstep;                              /* amp's step per sample in this block */
    int32_t env;                                /* the ADSR, Q23 */
    int32_t low, band, f, q;
    uint8_t pizz;                               /* a pizzicato note: plucked, it dies away by itself */
    uint8_t gate, note, seq, stage, blk, fmi;   /* seq: started by the sequencer; stage: 0 attack, 1 decay/sustain, 2 release; fmi: FM depth */
    uint32_t tinc;                              /* glide: the pitch it slides to (inc: where it is now) */
    int32_t vel;                                /* how hard: Q15 (32767 = as ever; an accent ~+3 dB) */
    uint8_t acc;                                /* an accent: the filter a little more open too */
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

static const int32_t DYN_G15[NDYN] = {32767, 25500, 19500, 14000, 46286, 39000, 32767, 32767, 29000};   /* NORMAL, the SOFTs
                                                   (mf, mp, p), ACCENT (+3 dB), LOUD (+1.5 dB), STACCATO, LEGATO, PIZZICATO */
static uint8_t leg_on[NSYN];                    /* interrupt: the track's notes playing now are legato (the next glides on) */
static uint32_t trk_last[NSYN];                 /* each slot's last note's pitch (a glide starts from it) */
static uint32_t inc2_of(uint32_t inc, uint32_t tune)   /* osc 2's pitch: tuned, and ~7 cents above (two oscillators that
                                                   beat a little sound wider) */
{
    uint32_t i2 = (uint32_t)(((uint64_t)inc * SEMI[tune > 48u ? 48u : tune]) >> 16);
    return i2 + (i2 >> 8);
}
static void voice_on(uint32_t trk, uint32_t note, uint32_t seq, uint32_t dyn)
{
    uint32_t v, pick = TRK[trk].first, best = 0xFFFFFFFFu, tune = set.snd[trk].tune;
    if (!note || note > 127u) return;
    for (v = TRK[trk].first; v < TRK[trk].first + TRK[trk].count; v++) {   /* the same note, a free voice, the oldest */
        uint32_t score = vx[v].note == note && vx[v].gate ? 0u
                       : vx[v].gate ? 0x80000000u + (vx_ages - vx[v].age > 0x7FFFFFFFu ? 0u : 0x7FFFFFFFu - (vx_ages - vx[v].age))
                       : (uint32_t)(vx[v].env >> 8) + 1u;
        if (score < best) { best = score; pick = v; }
    }
    vx[pick].tinc = INC_C9[note % 12u] >> (10u - note / 12u);
    vx[pick].inc = set.glide[trk] && trk_last[trk] ? trk_last[trk] : vx[pick].tinc;   /* glide: from the last note */
    trk_last[trk] = vx[pick].tinc;
    vx[pick].inc2 = inc2_of(vx[pick].inc, tune);
    vx[pick].note = (uint8_t)note;
    vx[pick].seq = (uint8_t)seq;
    vx[pick].vel = DYN_G15[dyn % NDYN];
    vx[pick].acc = (uint8_t)(dyn % NDYN == 4u);
    vx[pick].pizz = (uint8_t)(dyn % NDYN == 8u);
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
static uint8_t arp_dyn[NSYN];                   /* interrupt: the dynamic of the step the arpeggiator plays from */
static uint8_t arp_held[NSYN][ARP_MAX], arp_n[NSYN], arp_cur[NSYN];   /* interrupt only: the keys held, low to high */
static uint32_t arp_idx[NSYN], arp_gate[NSYN];
static uint8_t seq_arp[NSYN][4];                /* interrupt only: the pattern's notes the arpeggiator holds now (0: none) */

static void arp_add(uint32_t t, uint32_t note)
{
    uint32_t i, j;
    for (i = 0; i < arp_n[t]; i++) if (arp_held[t][i] == note) return;
    if (arp_n[t] >= ARP_MAX) return;
    if (!arp_n[t]) arp_idx[t] = 0;              /* (the first key: the arpeggio starts from it, on the next beat of its rate) */
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
static void arp_tick(uint32_t tick)             /* every clock tick: tick, counted from the start (step 1) */
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
        if (tick % rate == 0u) {                /* on the beat of its rate: the next note, through the keys, over 1..4 octaves */
            static uint32_t rnd = 77777u;
            uint32_t span = arp_n[t] * ((set.arp_oct[t] & 3u) + 1u), i, note;
            if (set.arp_dir[t] == 2u) { rnd ^= rnd << 13; rnd ^= rnd >> 17; rnd ^= rnd << 5; i = rnd % span; }   /* RANDOM */
            else { i = arp_idx[t]++ % span; if (set.arp_dir[t] == 1u) i = span - 1u - i; }   /* UP, DOWN */
            note = arp_held[t][i % arp_n[t]] + 12u * (i / arp_n[t]);
            if (note > 127u) note = 127u;
            if (arp_cur[t]) voice_off(t, arp_cur[t], 0);
            voice_on(t, note, 0, arp_dyn[t]);
            arp_cur[t] = (uint8_t)note;
            arp_gate[t] = rate / 2u ? rate / 2u : 1u;
        }
    }
}

static void synth_step_t(uint32_t t)            /* one track's step (BROKEN CLOCK may play it late: gb_clock.h) */
{
    uint32_t i;
    {
        const uint8_t *nt = step_note(t, play_pat[t], trk_pos[t]);
        uint32_t ln = *step_len(t, play_pat[t], trk_pos[t]), dy = sdyn_get(t, play_pat[t], trk_pos[t]);
        if ((trk_mute | chain_rest) >> t & 1u || !nt[0]) return;   /* (muted, or a SUPER SEQ rest) */
        if (leg_on[t] && !(set.arp_on >> t & 1u)) {   /* after a legato note: these glide on from the notes still
                                                   sounding, without a new attack; any left over end */
            uint32_t v, used = 0;
            for (i = 0; i < STEP_NN(t); i++) {
                if (!nt[i] || nt[i] > 127u) continue;
                for (v = TRK[t].first; v < TRK[t].first + TRK[t].count; v++)
                    if (vx[v].gate && vx[v].seq && !(used >> v & 1u)) break;
                if (v < TRK[t].first + TRK[t].count) {
                    used |= 1u << v;
                    vx[v].tinc = INC_C9[nt[i] % 12u] >> (10u - nt[i] / 12u);
                    trk_last[t] = vx[v].tinc;
                    vx[v].note = nt[i]; vx[v].vel = DYN_G15[dy % NDYN]; vx[v].acc = (uint8_t)(dy == 4u); vx[v].pizz = (uint8_t)(dy == 8u);
                } else voice_on(t, nt[i], 1, dy);
            }
            for (v = TRK[t].first; v < TRK[t].first + TRK[t].count; v++)
                if (vx[v].gate && vx[v].seq && !(used >> v & 1u)) { vx[v].gate = 0; vx[v].stage = 2; }
            leg_on[t] = (uint8_t)(dy == 7u);
            syn_left[t] = dy == 6u ? TPS / 2u : (ln ? ln : 1u) * TPS - (dy == 7u ? 0u : 1u) + (dy == 7u ? 2u : 0u);
            return;
        }
        voice_off(t, 0, 1);                     /* (the last step's notes end: a new one starts) */
        leg_on[t] = (uint8_t)(dy == 7u);
        if (set.arp_on >> t & 1u) {             /* the arpeggiator on: it takes the notes, as if held on the keys */
            seq_arp_drop(t);
            arp_dyn[t] = (uint8_t)dy;
            for (i = 0; i < STEP_NN(t); i++)
                if (nt[i] && nt[i] < 128u) { arp_add(t, nt[i]); seq_arp[t][i] = nt[i]; }
        } else
            for (i = 0; i < STEP_NN(t); i++)
                if (nt[i] && nt[i] < 128u) voice_on(t, nt[i], 1, dy);
        syn_left[t] = dy == 6u ? TPS / 2u   /* staccato: half a step */
                    : dy == 7u ? (ln ? ln : 1u) * TPS + 2u   /* legato: on past the next step's start (it glides on) */
                    : (ln ? ln : 1u) * TPS - 1u;   /* the notes end a tick before their last step does */
    }
}
static void synth_step(void) { uint32_t t; for (t = 0; t < NSYN; t++) synth_step_t(t); }

static void synth_tick(void)
{
    uint32_t t;
    for (t = 0; t < NSYN; t++) {
        if ((trk_mute | chain_rest) >> t & 1u && syn_left[t]) syn_left[t] = 1;   /* muted (or resting): its notes end now */
        if (syn_left[t] && --syn_left[t] == 0u) { voice_off(t, 0, 1); seq_arp_drop(t); leg_on[t] = 0; }
    }
}
static void synth_stop(void)
{
    uint32_t t;
    for (t = 0; t < NSYN; t++) { syn_left[t] = 0; voice_off(t, 0, 1); seq_arp_drop(t); leg_on[t] = 0; }
}
static int synth_quiet(void)
{
    uint32_t v;
    for (v = 0; v < NVOX; v++)
        if (vx[v].gate || vx[v].amp || vx[v].env) return 0;
    return 1;
}

static uint32_t osc_pw = 0x80000000u;           /* the pulse's width now (of a cycle: 2^31 = half, a square) */
/* a jump in a wave, rounded over the sample before it and the one after (PolyBLEP): p is the phase since the jump
 * (2^32 a cycle), inc the step a sample. 0 away from it; near it -(1-t)^2 after, (1-t)^2 before, t the fraction of a
 * step from the jump, Q15. A raw jump has every harmonic, and the ones above half the sample rate come back as
 * whistles out of tune with the note; rounded, they mostly go */
static inline int32_t osc_blep(uint32_t p, uint32_t inc)
{
    uint32_t b = inc >> 16, d, t, r;
    if (!b) return 0;                           /* (a very low note: its jumps are already gentle) */
    if (p < inc) { d = p >> 16; t = d >= b ? 32767u : (d << 15) / b; r = 32768u - t; return -(int32_t)((r * r) >> 15); }
    d = 0u - p;
    if (d < inc) { d >>= 16; t = d >= b ? 32767u : (d << 15) / b; r = 32768u - t; return (int32_t)((r * r) >> 15); }
    return 0;
}
static inline int32_t osc_wave(uint32_t w, uint32_t ph, uint32_t inc)   /* one cycle over 2^32 -> -32767..32767; inc:
                                                   its step a sample (for the rounded jumps) */
{
    static uint32_t nz = 24680u;
    int32_t s = (int32_t)ph, v;
    switch (w) {
    case W_TRI: return ((s ^ (s >> 31)) >> 15) - 32768;   /* |s| rising and falling */
    case W_SAW: v = (s >> 16) - osc_blep(ph + 0x80000000u, inc); return v > 32767 ? 32767 : v < -32767 ? -32767 : v;   /* (its jump: half way) */
    case W_SQR: v = (s < 0 ? -32767 : 32767) + osc_blep(ph, inc) - osc_blep(ph + 0x80000000u, inc);
        return v > 32767 ? 32767 : v < -32767 ? -32767 : v;
    case W_PULSE: v = (ph < osc_pw ? 32767 : -32767) + osc_blep(ph, inc) - osc_blep(ph - osc_pw, inc);   /* (its width:
                                                   osc_pw, the LFO's with PWM) */
        return v > 32767 ? 32767 : v < -32767 ? -32767 : v;
    case W_SYNC: return s >> 16;                /* (a saw: restarted by osc 1, in the voice loop) */
    case W_NOISE: nz ^= nz << 13; nz ^= nz >> 17; nz ^= nz << 5; return (int32_t)nz >> 16;   /* breath, hiss */
    default: return sine(ph);                   /* SINE (and FM, as osc 2: the sine that does the bending) */
    }
}

static void synth_render(int32_t (*tbuf)[FXN], uint32_t off, uint32_t n)   /* into each track's mono buffer, from off */
{
    static uint32_t lfo_ph[NSYN], rnd = 12345u;
    static int32_t lfo_rnd[NSYN];
    uint32_t v, i, t;
    if (trk_done != trk_req) {                  /* a slot's instrument changed: deal the voices again, from silence */
        trk_done = trk_req;
        for (v = 0; v < NVOX; v++) { vx[v].gate = 0; vx[v].amp = 0; vx[v].env = 0; vx[v].dstep = 0; vx[v].stage = 2; }
        for (t = 0; t < NSYN; t++) trk_last[t] = 0;
        trk_setup();
    }
    while (live_r != live_w) {                  /* the live notes first */
        uint32_t r = live_r;
        if (live_q[r % LIVE_N].trk < NSYN) {
            uint32_t lt = live_q[r % LIVE_N].trk, ln = live_q[r % LIVE_N].note;
            if (set.arp_on >> lt & 1u) {        /* the arpeggiator takes the keys */
                if (live_q[r % LIVE_N].on) arp_add(lt, ln);
                else { arp_del(lt, ln); voice_off(lt, ln, 0); }   /* (a note held from before the arp went on: let go) */
            } else if (live_q[r % LIVE_N].on) { voice_on(lt, ln, 0, 0); fx_quiet = 0; }
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
        osc_pw = 0x80000000u + (sp->ltgt == L_PWM ? (uint32_t)(lmod * 50000) : 0u);   /* PWM: the width 12 .. 88 % */
        for (v = TRK[t].first; v < TRK[t].first + TRK[t].count; v++) {
            int32_t amp = vx[v].amp, dstep = vx[v].dstep, low = vx[v].low, band = vx[v].band, f = vx[v].f, q = vx[v].q;
            uint32_t ph = vx[v].ph, ph2 = vx[v].ph2, i1 = vx[v].inc, i2 = vx[v].inc2, blk = vx[v].blk, fmi = vx[v].fmi, i2s;
            int32_t *o = &tbuf[t][off], pm = sp->ltgt == L_PITCH && lmod ? (lmod * 3874) >> 15 : 0;
            if (!vx[v].gate && !amp && !vx[v].env) continue;   /* silent: costs nothing */
            if (pm) {                           /* vibrato: up to +-1 semitone */
                i1 += (uint32_t)(int32_t)(((int64_t)i1 * pm) >> 16);
                i2 += (uint32_t)(int32_t)(((int64_t)i2 * pm) >> 16);
            }
            i2s = i2;
            for (i = 0; i < n; ) {
                uint32_t m, j;
                if (blk == 0u) {                /* every 16 samples: the envelope, the LFO, the filter's coefficients */
                    int32_t env = vx[v].env, eff, en, c;
                    if (vx[v].inc != vx[v].tinc) {   /* glide: a little nearer the note (8 ms .. 0.6 s, set.glide) */
                        int32_t d = (int32_t)(vx[v].tinc - vx[v].inc), gk = ENV_K[40u + (set.glide[t] & 127u) * 60u / 127u];
                        int32_t stp = (int32_t)(((int64_t)d * gk) >> 15);
                        vx[v].inc = stp ? vx[v].inc + (uint32_t)stp : vx[v].tinc;
                        vx[v].inc2 = inc2_of(vx[v].inc, sp->tune);
                        i1 = vx[v].inc; i2 = vx[v].inc2;
                        if (pm) { i1 += (uint32_t)(int32_t)(((int64_t)i1 * pm) >> 16); i2 += (uint32_t)(int32_t)(((int64_t)i2 * pm) >> 16); }
                    }
                    if (vx[v].stage == 0u) {    /* attack: towards 1.3 x full, done at full (pizzicato: at once) */
                        env += (int32_t)(((int64_t)(full + (full >> 2) + (full >> 4) - env) * ENV_K[vx[v].pizz && sp->a > 20u ? 20u : sp->a & 127u]) >> 15);
                        if (env >= full) { env = full; vx[v].stage = 1; }
                    } else if (vx[v].stage == 1u && vx[v].pizz)   /* pizzicato: on down to nothing, soon */
                        env += (int32_t)(((int64_t)(0 - env) * ENV_K[sp->d > 84u ? 84u : sp->d & 127u]) >> 15);
                    else if (vx[v].stage == 1u)   /* decay to the sustain level, and stay */
                        env += (int32_t)(((int64_t)(sus - env) * ENV_K[sp->d & 127u]) >> 15);
                    else {                      /* release */
                        env -= (int32_t)(((int64_t)env * ENV_K[sp->r & 127u]) >> 15);
                        if (env < 4096) env = 0;
                    }
                    vx[v].env = env;
                    eff = env;
                    if (sp->ltgt == L_AMP)      /* tremolo: down to ~1 % at full depth */
                        eff = (int32_t)(((int64_t)eff * (32768 - ((((lv + 32768) >> 1) * (int32_t)(sp->ldepth & 127u)) >> 7))) >> 15);
                    eff = (int32_t)(((int64_t)eff * vx[v].vel) >> 15);   /* the note's dynamic */
                    if (!env && vx[v].stage == 2u) { amp = 0; dstep = 0; }   /* the release has ended: silence */
                    else dstep = (eff - amp) >> 4;   /* a ramp over the block: no steps you could hear */
                    en = ((env >> 8) * 32767) / TRK[t].level;   /* the envelope 0..32767 for the filter */
                    c = (int32_t)sp->cut + ((en * (int32_t)sp->fenv) >> 15);
                    if (w1 == W_FM) {           /* FM: how hard osc 2 bends: MIX, plus the envelope times FILTER ENV */
                        int32_t fx = (int32_t)mix + ((en * (int32_t)sp->fenv) >> 15);
                        fmi = (uint32_t)(fx > 127 ? 127 : fx);
                    }
                    if (sp->ltgt == L_FILT) c += (lmod * 64) >> 15;   /* wah: +-64 cutoff steps */
                    if (vx[v].acc) c += 10;     /* (an accent: brighter) */
                    i2s = w2 == W_SYNC ? i2 + (uint32_t)(((uint64_t)i2 * (uint32_t)((en * (int32_t)sp->fenv) >> 7)) >> 15) : i2;
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
                    if (w2 == W_SYNC) {         /* hard sync: osc 2 starts again with each cycle of osc 1 */
                        uint32_t o1 = ph;
                        ph += i1;
                        if (ph < o1) ph2 = 0;
                        ph2 += i2s;
                        x = (osc_wave(w1, ph, i1) * (int32_t)(128u - mix) + (((int32_t)ph2 >> 16) * (int32_t)mix)) >> 8;
                    } else {
                    ph += i1;
                    if (w1 == W_FM) {           /* FM: osc 2 (a sine) pushes osc 1's phase back and forth, up to ~half a cycle */
                        ph2 += i2;
                        x = sine(ph + ((uint32_t)(sine(ph2) * (int32_t)fmi) << 9)) >> 1;
                    } else if (mix) {           /* two oscillators, mixed, half scale (room for the resonance) */
                        ph2 += i2;
                        x = (osc_wave(w1, ph, i1) * (int32_t)(128u - mix) + osc_wave(w2, ph2, i2) * (int32_t)mix) >> 8;
                    } else
                        x = osc_wave(w1, ph, i1) >> 1;
                    }
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
