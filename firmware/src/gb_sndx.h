/* SPDX-License-Identifier: GPL-3.0-only */
/* each library sound's more (sndx_t, quartet.c): its filter envelope, key tracking, velocity, pitch envelope, vibrato
 * delay and noise layer. A sound not listed plays as it always did (sndx_legacy). Included by gb_synth.h
 *   fa fd fs fr       the filter envelope (it opens the filter as far as MAIN's FILT ENV)
 *   kto kti           key tracking: higher notes brighter; higher notes shorter
 *   vto vat           velocity: harder notes brighter; harder notes a quicker attack
 *   pam pti           the pitch envelope: quarter semitones off at the start, how quickly back
 *   vde               the vibrato's delay
 *   nlv nco nde       the noise layer: level, colour, decay (0: held with the note); off in every library sound (it
 *                     read as hiss and clicks), there for the editor */
#define SXL(ty, id, fa, fd, fs, fr, kto, kti, vto, vat, pam, pti, vde, nlv, nco, nde) \
    {ty, id, {fa, fd, fs, fr, kto, kti, vto, vat, pam, pti, vde, nlv, nco, nde}}
static const struct { uint8_t ty, id; sndx_t x; } SNDX_LIB[] = {
    /* LEAD */
    SXL(T_LEAD,   0,   0,  84,   0,  70,  70,  90, 100,   0,   0,   0,   0,   0,   0,   0), /* PIANO: bright on the hit, darker as it rings; low notes ring longer */
    SXL(T_LEAD,   5,   0,  50, 100,  22,  40,   0,   0,   0,   0,   0,   0,   0,   0,   0), /* TONEWHEEL: a touch of percussion, level while held */
    SXL(T_LEAD,  45,  64,  90, 127,  74,  50,  30,   0,   0,   0,   0,   0,   0,   0,   0), /* CHURCH ORGAN: the pipes speaking slowly */
    SXL(T_LEAD,  46,  22,  60, 127,  24,  40,   0,  40,   0,   0,   0,   0,   0,   0,   0), /* CLAVIOLINE */
    SXL(T_LEAD,  54,   0,  80,  20,  80,  50,  85, 110,   0,   0,   0,   0,   0,   0,   0), /* ELEC PIANO: the tine barks when hit hard */
    SXL(T_LEAD,  55,   0,  78,  10,  76,  60,  90, 110,   0,   0,   0,   0,   0,   0,   0), /* DX PIANO */
    SXL(T_LEAD,   1,   0,  60,   0,  60,  60, 100,  70,   0,   0,   0,   0,   0,   0,   0), /* XYLOPHONE: bright on the hit */
    SXL(T_LEAD,  56,   0,  70,   0,  64,  60, 100,  80,   0,   0,   0,   0,   0,   0,   0), /* MARIMBA: bright on the hit */
    SXL(T_LEAD,   3,   0,  80,   0,  90,  30,  90,  50,   0,   0,   0,   0,   0,   0,   0), /* MUSIC BOX */
    SXL(T_LEAD,  38,   0,  90,   0, 100,  40,  80,  90,   0,   0,   0,   0,   0,   0,   0), /* FM BELL */
    SXL(T_LEAD,  40,   0,  82,  15,  75,  50,  85, 100,   0,   0,   0,   0,   0,   0,   0), /* TINE KEYS */
    SXL(T_LEAD,  41,   0,  62,   0,  35,  60,  70, 110,   0,   0,   0,   0,   0,   0,   0), /* CLAVINET */
    SXL(T_LEAD,   2,  62,  86,  60,  60,  50,  20, 110,  50,  -3,  62,  50,   0,   0,   0), /* BRASS: a scoop up into the note, a bloom on hard notes, vibrato after */
    SXL(T_LEAD,  47,  74,  92,  70,  66,  40,  10, 100,  40,  -2,  66,  60,   0,   0,   0), /* HORN */
    SXL(T_LEAD,   4,   0,  78,  40,  60,  60,  70, 100,   0,   0,   0,   0,   0,   0,   0), /* E GUITAR */
    SXL(T_LEAD,  57,   0,  74,  10,  80,  70,  90, 100,   0,   0,   0,   0,   0,   0,   0), /* AC GUITAR */
    SXL(T_LEAD,  42,  50,  80, 100,  55,  40,   0,  60,  30,  -1,  55,  56,   0,   0,   0), /* FLUTE: the vibrato coming in after the attack */
    SXL(T_LEAD,  43,  25,  80,  90,  45,  40,   0,  70,  30,  -1,  50,  40,   0,   0,   0), /* HARMONICA */
    SXL(T_LEAD,  32,   0,  70,  50,  52,  60,  30,  80,   0,   0,   0,  50,   0,   0,   0), /* MINI LEAD */
    SXL(T_LEAD,  33,   2,  80,  70,  58,  50,  20,  70,   0,   0,   0,   0,   0,   0,   0), /* JUNO LEAD */
    SXL(T_LEAD,  34,  10,  84,  80,  55,  50,   0,  60,   0,   0,   0,   0,   0,   0,   0), /* PWM LEAD */
    SXL(T_LEAD,  35,   0,  64,  10,  40,  40,  20, 120,   0,   0,   0,   0,   0,   0,   0), /* ACID LEAD: accents open the filter */
    SXL(T_LEAD,   9,   0,  60, 127,  20,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0), /* CHIPTUNE: (as it was: pure) */
    SXL(T_LEAD,  36,  30,  84,  60,  62,  40,   0, 110,  60,  -2,  55,  70,   0,   0,   0), /* BRASS: (the synth brass) */
    SXL(T_LEAD,  49,   0,  58,  20,  30,  50,  40, 100,   0,   0,   0,   0,   0,   0,   0), /* PULSE */
    SXL(T_LEAD,  50,  30, 100, 120,  80,  30,   0,   0,   0,   0,   0,  60,   0,   0,   0), /* UNDER: the drift coming in on held notes */
    SXL(T_LEAD,  51,  34,  90,  90,  74,  50,   0,  60,  30,   0,   0,  60,   0,   0,   0), /* SIGNAL: the vibrato after the attack */
    SXL(T_LEAD,  52,   4,  70,   0,  40,  50,  50, 100,  40,   0,   0,   0,   0,   0,   0), /* FAIRLIGHT */
    SXL(T_LEAD,  53,   0,  64,   0,  30,  40,  60,  90,   0,  -5,  50,   0,   0,   0,   0), /* RUBBER: the elastic dip */
    SXL(T_LEAD,   6,  70,  80, 127,  80,   0,   0,   0,   0,   0,   0,  40,   0,   0,   0), /* THEREMIN: the vibrato swelling in */
    SXL(T_LEAD,  64,  30, 100,  60, 100,  30,   0,   0,   0,   0,   0,  30,   0,   0,   0), /* GLASS HARP */
    SXL(T_LEAD,  67,   0,  70,   0,  40,   0,   0,  60,   0,  48,  70,   0,   0,   0,   0), /* LASER: a twelve-semitone zap */
    SXL(T_LEAD,  68,  82, 100, 100,  95,   0,   0,   0,   0,   0,   0,  40,   0,   0,   0), /* BOWED: slow to speak */
    SXL(T_LEAD,  66,  40,  90, 110,  90,   0,   0,   0,   0,   0,   0,  30,   0,   0,   0), /* SINE SWARM */
    SXL(T_LEAD,  69,   0,  80, 110,  50,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0), /* STEPPER: (as it was) */
    /* BASS */
    SXL(T_BASS,   0,   0,  74,  40,  50,  40,  40,  80,   0,   0,   0,   0,   0,   0,   0), /* UPRIGHT */
    SXL(T_BASS,   2,   0,  60,  20,  45,  40,  40, 120,   0,   0,   0,   0,   0,   0,   0), /* SLAP: harder: brighter */
    SXL(T_BASS,   3,   0,  56,   0,  40,  40,  50, 100,   0,   0,   0,   0,   0,   0,   0), /* PLUCKED */
    SXL(T_BASS,   1,  60,  86,  60,  50,   0,   0, 100,  40,  -3,  60,  50,   0,   0,   0), /* TUBA: a scoop up into the note */
    SXL(T_BASS,  40,  10,  80,  60,  80,  30,   0,  80,   0,   0,   0,  60,   0,   0,   0), /* FRETLESS: the vibrato after the attack */
    SXL(T_BASS,  41,   0,  64,  30,  40,  40,   0, 100,   0,   0,   0,   0,   0,   0,   0), /* PICKED */
    SXL(T_BASS,  32,   0,  70,  40,  45,  30,   0,  90,   0,   0,   0,   0,   0,   0,   0), /* MOOG */
    SXL(T_BASS,  33,   0,  62,   0,  40,  20,   0, 120,   0,   0,   0,   0,   0,   0,   0), /* 303 LINE: accents open the filter */
    SXL(T_BASS,  36,   0,  80, 127,  40,   0,   0,  20,   0,   0,   0,   0,   0,   0,   0), /* SUB */
    SXL(T_BASS,   5,   0,  66,  30,  45,  30,   0,  90,   0,   0,   0,   0,   0,   0,   0), /* SYNTH */
    SXL(T_BASS,  38,   0,  70,  30,  45,   0,   0,  90,   0,  -3,  50,   0,   0,   0,   0), /* RUBBER */
    SXL(T_BASS,  68,   0,  80, 127,  50,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0), /* SLIDE SUB: (as it was) */
    SXL(T_BASS,  42,   0,  52,  10,  30,  40,   0, 120,   0,   0,   0,   0,   0,   0,   0), /* BLACKOUT: accents open it */
    SXL(T_BASS,  34,   0,  64,  10,  45,  30,   0, 110,   0,   0,   0,   0,   0,   0,   0), /* DX */
    SXL(T_BASS,   8,   0,  80, 127,  50,  20,   0,   0,   0,   0,   0,   0,   0,   0,   0), /* REESE */
    SXL(T_BASS,  39,   0,  62,  40,  40,   0,   0,  90,   0,   0,   0,   0,   0,   0,   0), /* ELECTRO */
    SXL(T_BASS,   6,   0,  66,  20,  45,   0,   0, 100,   0,   0,   0,   0,   0,   0,   0), /* FM */
    SXL(T_BASS,  35,   0,  80, 127,  50,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0), /* DEEP REESE: (as it was) */
    SXL(T_BASS,  37,   0,  54,   0,  40,  40,  50, 100,   0,   0,   0,   0,   0,   0,   0), /* SYNTH PLUCK */
    SXL(T_BASS,  66,   0,  68,  40,  45,   0,   0,  90,   0,   0,   0,   0,   0,   0,   0), /* J BASS */
    SXL(T_BASS,  43,   0,  52,  10,  30,  30,   0, 110,   0,   0,   0,   0,   0,   0,   0), /* LATELY */
    SXL(T_BASS,   4,   0,  80, 120,  45,   0,   0,  60,   0,   0,   0,   0,   0,   0,   0), /* FUZZ */
    SXL(T_BASS,   9,   0,  80, 127,  45,   0,   0,  60,   0,   0,   0,   0,   0,   0,   0), /* WOBBLE */
    SXL(T_BASS,  64,   0,  80, 120,  45,   0,   0,  60,   0,   0,   0,   0,   0,   0,   0), /* GROWL */
    SXL(T_BASS,  69,   0,  80, 127,  45,   0,   0,  60,   0,   0,   0,   0,   0,   0,   0), /* WOMP */
    SXL(T_BASS,  67,   0,  75,  90,  40,   0,   0,  60,   0,   0,   0,   0,   0,   0,   0), /* DIRT */
    /* PAD */
    SXL(T_PAD,   0,  95, 100, 110,  90,  40,   0,  60,  20,   0,   0,  40,   0,   0,   0),  /* STRINGS: opening slowly, the vibrato after */
    SXL(T_PAD,  34,  90, 100, 127,  98,  30,   0,   0,   0,   0,   0,   0,   0,   0,   0),  /* SOLINA */
    SXL(T_PAD,  43,  40, 100, 120,  76,  40,   0,  60,   0,   0,   0,   0,   0,   0,   0),  /* TREMOLO */
    SXL(T_PAD,  45,  20,  96, 110,  74,  30,   0,  50,   0,   0,   0,   0,   0,   0,   0),  /* TAPE */
    SXL(T_PAD,   1,  88, 100, 120,  92,   0,   0,  40,   0,   0,   0,  40,   0,   0,   0),  /* CHOIR */
    SXL(T_PAD,  44,  66, 100, 120,  84,   0,   0,  40,   0,   0,   0,   0,   0,   0,   0),  /* TAPE CHOIR */
    SXL(T_PAD,  40,   0,  76,   0,  90,  50,  90,  80,   0,   0,   0,   0,   0,   0,   0),  /* HARP */
    SXL(T_PAD,  39,  86, 100, 120,  95,   0,   0,   0,   0,   0,   0,  30,   0,   0,   0),  /* DIGITAL */
    SXL(T_PAD,   3,   2,  60, 127,  40,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0),  /* ORGAN */
    SXL(T_PAD,   5,   2,  90,   0, 100,  40,  80,  80,   0,   0,   0,   0,   0,   0,   0),  /* HANDPAN */
    SXL(T_PAD,  41,   0, 100,   0,  98,  30,  80,  60,   0,   0,   0,   0,   0,   0,   0),  /* VIBES */
    SXL(T_PAD,  32,  98, 100, 110,  95,  30,   0,   0,   0,   0,   0,   0,   0,   0,   0),  /* J STRINGS: opening slowly */
    SXL(T_PAD,  35,  10,  92,  60,  80,  40,   0,  80,   0,   0,   0,   0,   0,   0,   0),  /* POLY: a bloom that settles */
    SXL(T_PAD,  36, 100, 100, 110,  96,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0),  /* PWM PAD: opening slowly */
    SXL(T_PAD,  33,  70,  92,  60,  82,   0,   0, 100,  40,  -2,  62,   0,   0,   0,   0),  /* PROPHET */
    SXL(T_PAD,  38, 100, 100, 100, 100,  30,   0,   0,   0,   0,   0,   0,   0,   0,   0),  /* WARM: opening slowly */
    SXL(T_PAD,   6, 104, 100, 110,  95,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0),  /* WARM SYNTH: opening slowly */
    SXL(T_PAD,  46, 100, 100, 100,  88,   0,   0,  40,   0,   0,   0,   0,   0,   0,   0),  /* DETUNED */
    SXL(T_PAD,   8, 100, 100, 127, 110,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0),  /* DRONE */
    SXL(T_PAD,  64, 100, 100, 127, 110,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0),  /* FROZEN */
    SXL(T_PAD,  67, 110, 110, 127, 115,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0),  /* CHURCH */
    SXL(T_PAD,   9,  80, 100, 120,  95,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0),  /* SWEEP */
    SXL(T_PAD,   7,  85, 100, 110,  95,   0,   0,   0,   0,   0,   0,  20,   0,   0,   0),  /* GLASS */
    SXL(T_PAD,  65,  90, 100, 120, 100,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0),  /* TIDAL */
};
/* the sounds with unison by default (sndx_t uni: how far apart its copies; ~65 is 12 cents). On a pad both oscillators
 * get two copies, on a lead osc 1 only. Saved sounds and USER sounds keep what they had */
static const struct { uint8_t ty, id, uni; } SNDX_UNI[] = {
    {T_PAD, 0, 65}, {T_PAD, 34, 60}, {T_PAD, 32, 65}, {T_PAD, 45, 55}, {T_PAD, 35, 65},    /* STRINGS, SOLINA, J STRINGS, TAPE, POLY */
    {T_PAD, 36, 60}, {T_PAD, 33, 60}, {T_PAD, 38, 65}, {T_PAD, 6, 60}, {T_PAD, 9, 60},     /* PWM PAD, PROPHET, WARM, WARM SYNTH, SWEEP */
    {T_LEAD, 33, 55}, {T_LEAD, 34, 55}, {T_LEAD, 36, 55}, {T_LEAD, 51, 50},              /* JUNO LEAD, PWM LEAD, BRASS (synth), SIGNAL */
    {T_LEAD, 2, 35}, {T_LEAD, 47, 35},                                                   /* BRASS, HORN: a section, lightly */
};
