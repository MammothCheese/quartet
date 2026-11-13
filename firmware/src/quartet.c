/* SPDX-License-Identifier: GPL-3.0-only */
/* QUARTET: a 4-track groovebox for the M-VAVE FM-1, built on Dinghy's platform (how to play it: docs/USER_GUIDE.md; how
 * it was planned and built: docs/development/SPEC.md, docs/development/GROOVEBOX_PLAN.md). Four tracks, each with 10
 * patterns (A-J) of 16-64 steps, on one master clock; a 2-oscillator synth engine, 10 sounds per track, 10 drum kits;
 * five pedals, an arpeggiator, a mixer; step and live recording; SUPER SEQ songs; 4 save slots.
 * Everything below this file (platform.c) is done: updates, the USB rescue, MIDI (USB, the TRS jack), USB audio,
 * keys, knobs, LEDs, the screen, flash storage.
 *
 * The modules (each its own file, included below):
 *   gb_clock.h  the master clock: counts samples in the audio interrupt, steps every 16th note, no drift;
 *               moves each track through its pattern and switches patterns when they come round
 *   gb_drums.h  track 4 DRUMS: KICK, SNARE, HAT, OPEN HAT, CLAP, PERC (synthesised; pitch, decay, level each)
 *   gb_synth.h  tracks 1-3 LEAD (4 voices), BASS (1 voice), PAD (4 voices): 2 oscillators (sine, triangle, saw,
 *               square, noise, FM), resonant filter, ADSR, LFO; 10 sounds per track
 *   gb_fx.h     the pedals: distortion and phaser per track; reverb, delay, chorus shared (sends)
 *
 * Sound (outside edit mode): SELECT, by where it is in its turn, picks the chosen track's sound (2 clicks each):
 *   LEAD   PIANO, XYLOPHONE, BRASS, MUSIC BOX, ELEC GUITAR, HAMMOND, THEREMIN, SYNTH, SQUARE LEAD, CHIPTUNE
 *   PAD    STRINGS, CHOIR, AC GUITAR, ORGAN, ELEC PIANO, HANDPAN, WARM SYNTH, GLASS PAD, DRONE, SWEEP
 *   BASS   UPRIGHT, TUBA, SLAP, PLUCKED, FUZZ, SYNTH BASS, FM, ACID, REESE, WOBBLE
 *   DRUMS  the kits ROCK, HIP-HOP, MINI POPS, CR-78, LM-1, 606, 808, 909, LO-FI, INDUSTRIAL
 * (the sound changes only on entering another sound's clicks; its name shows big; stopped, you hear it). KNOB1-4 set the
 * page's four values, the bottom line shows them; hold ARP and turn SELECT for the detailed pages:
 *   MAIN    tempo, cutoff, resonance, filter envelope     OSC     wave 1, wave 2, osc 2 tuning, mix
 *   ENV     attack, decay, sustain, release               LFO     rate, depth, target (pitch/filter/amp), wave
 *   DRUMS   KNOB1 tempo; KNOB2-4 pitch, decay, level of the chosen drum (the last one played or chosen)
 *
 * Pedals (gb_fx.h), for the chosen track. A tap on a top button shows that pedal (the track's name big above it,
 * the ON lamp, three knobs with a ring of dots for their level; the sequence is not shown: HOME, SEQ or SELECT go back);
 * KNOB1 switches it on (3 clicks clockwise) / off (3 clicks back), KNOB2-4 turn its knobs; the button held 0.5 s
 * switches it too (the button lit = on, however it was switched).
 *   FX    REVERB      LEVEL, TONE, TIME               (one reverb for all: LEVEL is this track's, TONE / TIME shared)
 *   SEL   DELAY       REPEAT RATE, ECHO, INTENSITY    (shared; the repeat rate on the tempo, 1/16 .. 1/2)
 *   ENV   DISTORTION  LEVEL, TONE, DIST               (this track's own; the volume follows the drive)
 *   LFO   CHORUS      RATE, DEPTH, LEVEL              (shared, stereo; LEVEL is this track's)
 *   EDIT  PHASER      RATE, DEPTH, FEEDBACK           (this track's own)
 *   GLO   the MIXER (GLO again: back): four channel strips; KNOB1-4 the volume of LEAD, PAD, BASS, DRUMS (100 =
 *         as is); there (only), SAVE / ARP / SEQ / PLAY mute LEAD / PAD / BASS / DRUMS, lit = playing;
 *         the bottom line also shows the CPU load of the sound code (FM-1 only)
 *   ARP   (outside edit mode) a tap shows the ARPEGGIATOR of the chosen synth track, held 0.5 s turns it on / off;
 *         KNOB1 on / off (3 clicks clockwise / back), KNOB2 the rate 1/4 .. 1/32, KNOB3 octaves 1-4, KNOB4 direction
 *         UP / DOWN / RANDOM (three clicks a step).
 *         The keys you hold play one at a time, locked to the tempo
 *
 * The save page (SAVE; SAVE again: back home): 4 slots, SAVE 1-4, each the whole groovebox (patterns, sounds,
 * pedals, mixer, SUPER SEQ). KNOB1 picks the slot. REC saves to it, PLAY loads it: each asks first ("SAVE 2?", the
 * button blinks); the same button again within 3 s does it, any other button or 3 s cancels. Playing stops first
 * (writing the flash would stutter the sound). A load is checked whole before it replaces your work ("LOAD FAILED"
 * leaves everything as it was). Your current work is also kept by itself (saved when stopped and quiet), so
 * switching off loses nothing.
 *
 * Playing:
 *   ALGORITHM  choose the track: 2 clicks right the next one, 2 left the one before (LEAD, PAD, BASS, DRUMS, round)
 *   PRESETS    choose the track's pattern A-J, two clicks each round one turn. While playing it is queued ("A>C") and starts when the playing
 *              pattern comes round to its step 1; stopped, at once.
 *              Hold ARP and turn PRESETS: copy the pattern into the next letter(s)
 *              Hold HOME and turn PRESETS: the MIDI channel (OMNI, 1-16)
 *   keys       play the chosen track live, also while the patterns play (DRUMS: the first 6 white keys)
 *   HOME       always home (the sequencer screen); from there, SUPER SEQ and back. (Muting: on the mixer)
 *
 * SUPER SEQ (HOME from home): songs made of your patterns. A row a track (ALGORITHM chooses it), 8 boxes each; each box
 * plays its pattern once, then the next box, the 8 round and round. A blank box goes on with the pattern before it:
 * A...B.A. plays AAAABBAA. X is a rest (blanks after it rest too); blanks at the start rest (the track comes in
 * later). New: box 1 = A, the rest blank.
 *   KNOB4  SUPER SEQ on / off. On, it plays the songs (each track starts at box 1 when its pattern comes round,
 *          or on PLAY); off, the patterns chosen with PRESETS play as ever
 *   KNOB1  the song's length, 1-8 boxes (the rest blacked out)
 *   KNOB2  the box              KNOB3  its pattern: blank, A-J, X (rest)
 *   PRESETS  the song A-J (10 songs); ARP held + PRESETS: copy the song into the next letter
 *   (Elsewhere, under SUPER SEQ, PRESETS still picks the pattern you edit; SUPER shows on the top bar)
 *   PLAY       start / stop all tracks together (from step 1)
 *   OCT- OCT+  the octave of the keys
 *   REC        hold 1 s: "CLEAR ALL?"; REC again (within 3 s): every pattern empty, every sound, pedal, mixer and
 *              SUPER SEQ song as new, pattern A everywhere (the save slots stay). Another button: no
 *   MASTER     the volume (platform.c)
 *
 * Live recording (SEQ in the step recorder; SEQ blinks; SEQ again: back to the step recorder; HOME: home). The keys
 * play the chosen track as usual (DRUMS: the first 6 white keys); while recording, what you play goes into the
 * pattern playing, each note on the nearest step (its length rounded too), added to what is there:
 *   REC        start recording (and the music, from step 1, if stopped); REC again: stop recording. REC lit = recording.
 *              Hold REC 1 s: clear the pattern
 *   PLAY       stop (the recording too)
 *   KNOB1-3    the pattern's length, the step (synths), the page, as in the step recorder
 *   (each note goes to the closest step, allowing ~20 ms for the sound to reach you; a drum hit just before a step is
 *   heard on that step, not twice; a note is written when you let go of it)
 *
 * Editing, the step recorder (SEQ lit; everything keeps playing). You edit the pattern chosen with PRESETS:
 *   SEQ        from home: the step recorder; in it: live recording (above). HOME: back home
 *   PRESETS    the pattern A-J, as outside edit mode
 *   KNOB1      the pattern's length 16 / 32 / 48 / 64     KNOB2  synths: the step (moves the cursor)
 *   KNOB3      the page of 16 steps                     KNOB4  swing 50 % .. 66 % (the whole song; gb_clock.h)
 *              (the info line under the title shows these; the bottom line says which knob does what)
 *   LEAD BASS  step recording: the lit white key is the cursor (the 16 white keys show the cursor's page of 16)
 *   PAD          a key     writes that note on the cursor's step, the cursor moves on
 *                         (LEAD, PAD: hold several keys together for a chord, up to 4; the cursor moves when you let go)
 *                REC       a rest: clears the cursor's step, the cursor moves on.  Hold REC 1 s: clear the pattern
 *                ARP       a tie: the note before the cursor lasts one step longer, the cursor moves on
 *                SELECT    move the cursor (through all the pattern's pages)
 *   DRUMS      the 16 WHITE keys = 16 steps of the page shown: press one to turn it on / off for the chosen drum;
 *              OCT- / OCT+ turn the page; the first 6 BLACK keys (F#3 G#3 A#3 C#4 D#4 F#4) choose KICK / SNARE /
 *              HAT / OPEN HAT / CLAP / PERC (and play it); hold REC 1 s: clear the pattern
 *   Everything is kept in flash (saved when stopped and quiet).
 *
 * The mixer's bottom bar shows the CPU load of the sound code (on the FM-1 only: "CPU --" in the PC emulator).
 * Rules of the road (the chip has no FPU and no libgcc): integers only, no float, no 64-bit division or shifts.
 * tests/host.c runs this file on a computer: the screen as a PNG, the sound as a WAV.
 * What platform.c offers: docs/development/API.md. The chip's rules: docs/development/RULES.md. */
static void app_idle(void);
#define LCD_IDLE app_idle                       /* while the screen's DMA runs: MIDI in (platform.c) */
#define APP_NAME "QUARTET"                      /* the boot, crash and rescue screens (the installer ID: tools/build.py) */
#define STORE_MAGIC 0x424B4352u                 /* "RCKB": our own flash data; another firmware's is never taken for ours.
                                                   (From QUARTET's first name, ROCKBAND: changing it would wipe every save) */
#define CV_MAX (240u * 128u)                    /* the canvas: the 240 x 128 middle of the screen, 60 KB of the pool */
/* the screen, top to bottom: the top bar (38 px), the info line, the middle (the sequencer view; or a pedal, the
 * arpeggiator, the mixer, which take the info line's place too), the bottom bar */
#define Y_BIG 0
#define H_BIG 38
#define Y_INFO 40
#define H_INFO 22
#define Y_VIEW 64
#define H_VIEW 128
#define Y_PEDAL 44
#define H_PEDAL 164                             /* (drawn in two halves of 82: the canvas holds 128 rows) */
#define Y_BOT 216
#define H_BOT 24
#include "platform.c"

/* ---- shared sound helpers */
static int32_t sine(uint32_t ph)                /* a full cycle over 2^32 -> -32767..32767 (a refined parabola, ~0.1 %) */
{
    int32_t x = (int32_t)ph >> 16, ax = x < 0 ? -x : x, y, ay;
    y = (x * (32768 - ax)) >> 13;               /* 4x(1-|x|), Q15 */
    ay = y < 0 ? -y : y;
    y += ((((y * ay) >> 15) - y) * 7373) >> 15; /* + 0.225 (y|y| - y) */
    return y > 32767 ? 32767 : y < -32767 ? -32767 : y;
}
static const uint32_t INC_C9[12] = {            /* phase steps of MIDI 120..131 (C9..B9); lower octaves shift right */
    815363807u, 863847862u, 915214929u, 969636441u, 1027294024u, 1088380105u,
    1153098554u, 1221665363u, 1294309365u, 1371273005u, 1452813141u, 1539201906u};

/* ---- the groovebox's data: what survives a power-off (and what a save slot keeps) */
enum { T_LEAD, T_BASS, T_PAD, T_DRUMS, NTRK };  /* the tracks; the first NSYN are synths */
#define NSYN 3u
enum { DR_KICK, DR_SNARE, DR_HAT, DR_OPEN, DR_CLAP, DR_PERC, NDR };
#define NPAT 10u                                /* patterns per track, A-J (PRESETS: two clicks each, one turn) */
#define ALG_CLICKS 20u                          /* clicks in one full turn of SELECT and PRESETS (usual for such knobs; if
                                                   their steps feel off, count yours and change this) */
#define NSTEP 64u                               /* the longest pattern, in steps (16ths) */
#define PAGE 16u                                /* steps on the 16 white keys at a time */
#define TPS 4u                                  /* clock ticks per step: note lengths end on a tick */
#define NCHAIN 10u                              /* SUPER SEQ: songs A-J (PRESETS on its page) */
#define NLINK 8u                                /* SUPER SEQ: boxes a track has in a song */
#define CH_REST (NPAT + 1u)                     /* SUPER SEQ: a box value for X, a rest (after blank 0 and A-J 1..10) */
/* a step of a synth pattern: its note(s) (0 = a rest) and how many steps they last (1..NSTEP). BASS plays one
 * note at a time, so its steps keep one note (2 bytes); LEAD's and PAD's keep a chord of 4 (5 bytes). That keeps a
 * whole project inside a 16 KB slot. step_note() / step_len() hide the difference. */
typedef struct { uint8_t note, len; } mono_t;
typedef struct { uint8_t note[4], len; } chord_t;
typedef struct {                                /* a synth's sound (gb_synth.h); 0..127 unless said */
    uint8_t cut, res, fenv;                     /* filter: cutoff, resonance, how far the envelope opens it */
    uint8_t w1, w2, tune, mix;                  /* oscillators: waves 0..3, osc 2 tuning 0..48 (24 = same), mix */
    uint8_t a, d, s, r;                         /* the envelope */
    uint8_t lrate, ldepth, ltgt, lwave;         /* the LFO: rate, depth, target 0..2, wave 0..3 */
    uint8_t preset;                             /* the preset last picked (0..NPRESET-1) */
} snd_t;
typedef struct { uint8_t pitch, decay, level; } dsnd_t;       /* a drum's sound, each 0..127, 64 = as designed */
typedef struct {                                /* a track's pedals (gb_fx.h); 0..127 unless said */
    uint8_t on;                                 /* bit e: pedal e on (E_REV, E_DLY, E_DST, E_CHO, E_PHS) */
    uint8_t rev_send, dly_send, cho_send;       /* REVERB LEVEL, DELAY ECHO, CHORUS LEVEL: what goes to the shared ones */
    uint8_t drive, tone, dlevel;                /* DISTORTION: distortion, tone, level */
    uint8_t prate, pdepth, pfb;                 /* PHASER: rate, depth, feedback */
} tfx_t;
typedef struct {                                /* the shared pedals' knobs */
    uint8_t rtone, rtime;                       /* REVERB: tone, time */
    uint8_t dtime, dfb;                         /* DELAY: repeat rate 0..6 (1/16 .. 1/2), intensity */
    uint8_t crate, cdepth;                      /* CHORUS: rate, depth */
} gfx_t;
#define FXN 256u                                /* frames the effects take at a time (the audio block) */

static struct {
    uint8_t oct, ch, bpm, mute;                 /* mute: bit t = track t muted */
    uint8_t dkit;                               /* the drum kit: 808, CR-78, LM-1, LO-FI (SELECT on DRUMS) */
    uint8_t swing;                              /* 0..127: 50 % (straight) .. 66 % (KNOB4 in edit mode) */
    uint8_t vol[NTRK];                          /* the mixer (GLO): each track's volume, 100 = as is */
    uint8_t arp_on, arp_rate[NSYN];             /* the arpeggiator (ARP): bit t on; 0..3 = 1/4 .. 1/32 */
    uint8_t arp_oct[NSYN], arp_dir[NSYN];       /* its octaves 0..3 = 1..4; direction 0 up, 1 down, 2 random */
    snd_t snd[NSYN];
    dsnd_t dsnd[NDR];
    tfx_t fx[NTRK];
    gfx_t gfx;
    uint8_t sel[NTRK];                          /* the pattern chosen for each track (playing, or queued) */
    uint8_t plen[NTRK][NPAT];                   /* each pattern's length: 16, 32, 48 or 64 */
    uint16_t dpat[NPAT][NDR][NSTEP / PAGE];     /* drums: bit s of page g = step g*16+s */
    mono_t mono[NPAT][NSTEP];                   /* BASS: one note a step */
    chord_t chord[2][NPAT][NSTEP];              /* LEAD (0) and PAD (1): chords of up to 4 */
    /* SUPER SEQ (added after v1.0, so kept last: a v1.0 save is this struct without them, and still loads) */
    uint8_t sseq_on, sseq_sel;                  /* on (KNOB1 on its page); the song played (PRESETS on its page) */
    uint8_t chain[NCHAIN][NTRK][NLINK];         /* each song: per track 8 boxes, 0 = blank (the one before goes on), 1..10 = A..J, 11 = X (rest) */
    uint8_t clen[NCHAIN];                       /* each song's length: boxes 1..8 used (KNOB1 on its page; added later, so last) */
    uint8_t sound_order;                        /* (1: the sounds in QUARTET's order. Only its place matters: a save without
                                                   it is older, and its sound numbers are moved to the new order) */
} set;
#define SET_V10 ((uint32_t)((uint8_t *)&set.sseq_on - (uint8_t *)&set))   /* the size of a v1.0 save */
#define SET_SS1 ((uint32_t)((uint8_t *)&set.clen - (uint8_t *)&set))      /* the size of a save from before song lengths */
#define SET_ORD (((uint32_t)((uint8_t *)&set.sound_order - (uint8_t *)&set) + 1u) & ~1u)   /* ... from before the reorder */
#define STEP_NN(t) ((t) == T_BASS ? 1u : 4u)    /* notes a step of track t can hold */
static uint8_t *step_note(uint32_t t, uint32_t p, uint32_t s)   /* the step's notes: [0 .. STEP_NN(t) - 1] */
{
    p %= NPAT; s %= NSTEP;
    return t == T_BASS ? &set.mono[p][s].note : set.chord[t == T_PAD][p][s].note;
}
static uint8_t *step_len(uint32_t t, uint32_t p, uint32_t s)
{
    p %= NPAT; s %= NSTEP;
    return t == T_BASS ? &set.mono[p][s].len : &set.chord[t == T_PAD][p][s].len;
}
static volatile uint32_t trk_mute;              /* main loop writes, the interrupt reads (saved as set.mute) */
static volatile uint8_t trk_pos[NTRK];          /* gb_clock.h writes: the step each track plays now */
static volatile uint8_t play_pat[NTRK];         /* gb_clock.h writes: the pattern each track plays now */
static volatile uint8_t chain_at[NTRK];         /* gb_clock.h writes: SUPER SEQ, the box each track plays now */
static volatile uint32_t chain_rest;            /* gb_clock.h writes: bit t = track t rests (SUPER SEQ, blank boxes at the start) */
#define NPROJ 4u                                /* save slots (the save page: SAVE 1-4) */
static uint8_t proj_bpm[NPROJ];                 /* each project slot's tempo (0: empty, or not readable) */

static uint32_t dbit(uint32_t p, uint32_t d, uint32_t s)   /* drum d on step s of pattern p? */
{
    return (uint32_t)set.dpat[p][d][(s / PAGE) % (NSTEP / PAGE)] >> (s % PAGE) & 1u;
}

/* The current work is kept in the first few blobs (saved by itself when stopped and quiet: it is what you find
 * after switching on). The save page's 4 slots are slots (16 KB each, store.h), saved and loaded only on request. */
/* the data is bigger than one 4 KB sector: it is kept in the first few blobs (store.h), each with its CRC */
#define NSAVE ((sizeof set + SECTOR_MAX - 1u) / SECTOR_MAX)
typedef char save_fits_in_the_blobs[NSAVE <= NBLOB ? 1 : -1];
static int song_load_n(uint32_t size)           /* the first size bytes of set, from the blobs: 0 = loaded */
{
    uint32_t i;
    for (i = 0; i * SECTOR_MAX < size; i++) {
        uint32_t o = i * SECTOR_MAX, n = size - o > SECTOR_MAX ? SECTOR_MAX : size - o;
        if (blob_load(i, (uint8_t *)&set + o, n)) return -1;
    }
    return 0;
}
static uint32_t song_loaded;                    /* the size song_load found (smaller than set: an older save) */
static int song_load(void)                      /* 0 = loaded (a part that fails: -1, the caller starts afresh) */
{
    /* (an older save is shorter: what it lacks keeps its starting values) */
    if (song_load_n(sizeof set) == 0) song_loaded = sizeof set;
    else if (song_load_n(SET_ORD) == 0) song_loaded = SET_ORD;
    else if (song_load_n(SET_SS1) == 0) song_loaded = SET_SS1;
    else if (song_load_n(SET_V10) == 0) song_loaded = SET_V10;
    else return -1;
    return 0;
}
static void song_save(void)
{
    uint32_t i;
    for (i = 0; i < NSAVE; i++) {
        uint32_t o = i * SECTOR_MAX, n = sizeof set - o > SECTOR_MAX ? SECTOR_MAX : sizeof set - o;
        blob_save(i, (uint8_t *)&set + o, n);
    }
}

#include "gb_drums.h"
#include "gb_synth.h"
#include "gb_fx.h"
#include "gb_clock.h"                           /* (last: it calls the tracks and the effects) */
typedef char kits_as_many_as_sounds[NKIT == NPRESET ? 1 : -1];   /* (SELECT turns through both the same way) */

static void sound_reorder(void)                 /* a save from before QUARTET's order: its sounds and kit to their new places */
{
    static const uint8_t SYN_MAP[NSYN][NPRESET] = {{0, 7, 1, 2, 4, 5, 6, 9, 8, 3}, {6, 7, 3, 4, 0, 2, 1, 8, 9, 5}, {6, 0, 3, 1, 2, 4, 7, 8, 9, 5}};   /* LEAD, BASS, PAD: old number -> new */
    static const uint8_t KIT_MAP[NKIT] = {6, 3, 4, 8, 0, 2, 7, 5, 1, 9};
    uint32_t t;
    for (t = 0; t < NSYN; t++) set.snd[t].preset = SYN_MAP[t][set.snd[t].preset % NPRESET];
    set.dkit = KIT_MAP[set.dkit % NKIT];
    set.sound_order = 1;
}

static void chain_defaults(void)                /* SUPER SEQ off; every song: each track plays A */
{
    uint32_t c, t, i;
    set.sseq_on = 0; set.sseq_sel = 0;
    for (c = 0; c < NCHAIN; c++) for (t = 0; t < NTRK; t++) for (i = 0; i < NLINK; i++) set.chain[c][t][i] = i == 0u;
    for (c = 0; c < NCHAIN; c++) set.clen[c] = NLINK;
}

static void set_defaults(void)                  /* a starting groove in pattern A: C minor, all four tracks */
{
    static const uint8_t LEAD[16][2] = {{72, 2}, {0, 1}, {0, 1}, {75, 1}, {0, 1}, {0, 1}, {79, 2}, {0, 1},
                                        {0, 1}, {0, 1}, {77, 1}, {0, 1}, {75, 3}, {0, 1}, {0, 1}, {0, 1}};
    static const uint8_t BASS[16][2] = {{36, 1}, {0, 1}, {0, 1}, {36, 1}, {0, 1}, {0, 1}, {48, 1}, {0, 1},
                                        {36, 2}, {0, 1}, {0, 1}, {39, 1}, {0, 1}, {0, 1}, {43, 1}, {46, 1}};
    static const uint8_t PAD[4][3] = {{60, 63, 67}, {56, 60, 63}, {55, 58, 63}, {58, 62, 65}};   /* Cm Ab Eb Bb */
    static const uint16_t PAT0[NDR] = {0x1111u, 0x1010u, 0x1555u, 0x4000u, 0, 0};   /* KICK 1 5 9 13, SNARE 5 13, HATS */
    uint8_t *z = (uint8_t *)&set;
    uint32_t i, s, t, p;
    for (i = 0; i < sizeof set; i++) z[i] = 0;
    set.bpm = 120;
    for (i = 0; i < NSYN; i++) set.snd[i] = PRESETS[i][0];   /* each synth starts on its first preset */
    for (i = 0; i < NDR; i++) set.dsnd[i] = KITS[0][i];   /* the 808 kit */
    for (t = 0; t < NTRK; t++) {                /* effects all off, set so that switching one on is heard at once */
        static const tfx_t FX0 = {0, 60, 60, 70, 60, 80, 32, 40, 90, 60};   /* reverb/delay/chorus levels; dist (LEVEL 32); phaser */
        set.fx[t] = FX0;
    }
    {
        static const gfx_t GFX0 = {70, 70, 3, 50, 40, 60};   /* reverb tone, time; delay 1/8 dotted, intensity; chorus */
        set.gfx = GFX0;
    }
    {                                           /* the mixer at 100 (as is); the arpeggiators off, at 1/16 */
        for (t = 0; t < NTRK; t++) set.vol[t] = 100;
        for (t = 0; t < NSYN; t++) set.arp_rate[t] = 2;
    }
    for (t = 0; t < NTRK; t++) for (p = 0; p < NPAT; p++) set.plen[t][p] = 16;
    for (t = 0; t < NSYN; t++) for (p = 0; p < NPAT; p++) for (s = 0; s < NSTEP; s++) *step_len(t, p, s) = 1;
    for (i = 0; i < NDR; i++) set.dpat[0][i][0] = PAT0[i];
    for (s = 0; s < 16u; s++) {
        *step_note(T_LEAD, 0, s) = LEAD[s][0], *step_len(T_LEAD, 0, s) = LEAD[s][1];
        *step_note(T_BASS, 0, s) = BASS[s][0], *step_len(T_BASS, 0, s) = BASS[s][1];
    }
    for (i = 0; i < 4u; i++) {
        for (s = 0; s < 3u; s++) step_note(T_PAD, 0, i * 4u)[s] = PAD[i][s];
        *step_len(T_PAD, 0, i * 4u) = 4;
    }
    chain_defaults();
    set.sound_order = 1;
}

#ifndef DINGHY_HOST
static volatile uint32_t cpu_peak, cpu_reset;   /* the longest app_audio in 24 MHz ticks (interrupt), reset requests (main) */
#endif

static void app_audio(int32_t *out, uint32_t n)
{
#ifndef DINGHY_HOST
    static uint32_t reset_done;
    uint32_t t0 = fm1_ticks(), dt;
#endif
    clock_audio(out, n);                        /* the clock and the four tracks on it */
#ifndef DINGHY_HOST
    dt = fm1_ticks() - t0;
    if (reset_done != cpu_reset) { reset_done = cpu_reset; cpu_peak = 0; }
    if (dt > cpu_peak) cpu_peak = dt;
#endif
}

static int quiet(void)                          /* nothing sounds, not even a release: a flash save would stutter it */
{
    return !clk_run && drums_quiet() && synth_quiet();
}

/* ---- the screen. The look: dark rounded panels on black. Each track has its own colour (red when muted), and the
 * chosen track's colour runs through the screen: the top bar, its steps, the beat light, the SEQ values, the dot on
 * the bottom bar. (Mock-ups: docs/overhaul_mockup.png) */
static const uint8_t TRK_RGB[NTRK][3] = {{255, 184, 36}, {36, 198, 240}, {178, 124, 255}, {92, 226, 104}};   /* LEAD gold, BASS cyan, PAD violet, DRUMS green */
static const uint8_t MUTE_RGB[3] = {238, 56, 46};
#define C_MUTE RGB(238, 56, 46)
#define C_PANEL RGB(20, 20, 24)                 /* the panels */
#define C_PANEL2 RGB(36, 36, 42)                /* the chosen row */
#define C_TXT RGB(228, 228, 236)                /* values */
#define C_SUB RGB(160, 160, 170)                /* labels */
#define C_DIMC RGB(66, 66, 80)                  /* off, unused */
#define C_BOTTXT RGB(196, 196, 210)             /* the bottom bar's text */
#define C_LCD RGB(10, 10, 13)                   /* the little value screens */
#define C_KEYCAP RGB(52, 52, 64)
#define C_PLAYG RGB(92, 232, 112)               /* the play sign */
#define C_EMPTY RGB(32, 32, 42)                 /* an empty step; on a beat: */
#define C_EMPTY4 RGB(50, 50, 64)
#define C_HEADC RGB(124, 124, 138)              /* the step playing, when empty */
static uint32_t live_mode, live_rec;            /* the live recording page is up; it records (main loop) */
static uint32_t scr_home;                       /* the sequencer screen is home (not a recorder): for the top bar */
#define KEY0 53u                                /* key 0 = F3 */
static int black(uint32_t note) { return (0x54Au >> (note % 12u)) & 1u; }
static const char *const TRK_NAME[NTRK] = {"LEAD", "BASS", "PAD", "DRUMS"};
static const char *const TRK_SHORT[NTRK] = {"LD", "BS", "PD", "DR"};
static const uint8_t SEQ_ORDER[NTRK] = {T_LEAD, T_PAD, T_BASS, T_DRUMS};   /* the order on the screen, the mixer, ALGORITHM */
/* on the mixer only: the buttons that mute LEAD, PAD, BASS, DRUMS (lit = playing) */
static const uint8_t MUTE_BTN[NTRK] = {BTN_SAVE, BTN_ARP, BTN_SEQ, BTN_PLAY};
static const char *const MUTE_BTN_NAME[NTRK] = {"SAVE", "ARP", "SEQ", "PLAY"};
static const char *const DR_NAME_ALL[NDR] = {"KICK", "SNARE", "HAT", "OPEN HAT", "CLAP", "PERC"};
static const char *const DR_NAME_LM1[NDR] = {"KICK", "SNARE", "HAT", "OPEN HAT", "TOM L", "TOM H"};   /* (the LM-1's samples) */
#define DR_NAME (set.dkit % NKIT == KIT_LM1 ? DR_NAME_LM1 : DR_NAME_ALL)   /* each drum's name, with this kit */
enum { PG_MAIN, PG_OSC, PG_ENV, PG_LFO, NPAGE,  /* the sound pages (ARP held + SELECT, outside edit mode) */
       PG_FX = NPAGE,                           /* then the effect pages PG_FX + E_REV .. (the top buttons) */
       PG_PROJ = PG_FX + NFX,                   /* the save page (SAVE) */
       PG_ARP, PG_MIX, PG_SSEQ };               /* the arpeggiator (ARP), the mixer (GLO), SUPER SEQ (HOME) */
#define IS_FX_PAGE(p) ((p) >= PG_FX && (p) < PG_PROJ)
static const char *const PAGE_NAME[NPAGE] = {"MAIN", "OSC", "ENV", "LFO"};
static const char *const FX_NAME[NFX] = {"REVERB", "DELAY", "DISTORTION", "CHORUS", "PHASER"};
/* the top buttons: reverb, delay, distortion, chorus, phaser */
static const uint8_t FX_BTN[NFX] = {BTN_FX, BTN_SEL, BTN_ENV, BTN_LFO, BTN_EDIT};   /* (GLO: the mixer) */

/* each pedal's three knobs (KNOB1-3), named as on a pedal: what they set, how far they go */
typedef struct { const char *name; uint8_t *v; uint8_t max; } pknob_t;
static void pedal_knobs(uint32_t trk, uint32_t e, pknob_t k[3])
{
    tfx_t *f = &set.fx[trk];
    gfx_t *g = &set.gfx;
    switch (e) {
    case E_REV: k[0] = (pknob_t){"LEVEL", &f->rev_send, 127}; k[1] = (pknob_t){"TONE", &g->rtone, 127};
                k[2] = (pknob_t){"TIME", &g->rtime, 127}; break;
    case E_DLY: k[0] = (pknob_t){"REPEAT", &g->dtime, NDLY - 1}; k[1] = (pknob_t){"ECHO", &f->dly_send, 127};
                k[2] = (pknob_t){"INTENSITY", &g->dfb, 127}; break;
    case E_DST: k[0] = (pknob_t){"LEVEL", &f->dlevel, 127}; k[1] = (pknob_t){"TONE", &f->tone, 127};
                k[2] = (pknob_t){"DIST", &f->drive, 127}; break;
    case E_CHO: k[0] = (pknob_t){"RATE", &g->crate, 127}; k[1] = (pknob_t){"DEPTH", &g->cdepth, 127};
                k[2] = (pknob_t){"LEVEL", &f->cho_send, 127}; break;
    default:    k[0] = (pknob_t){"RATE", &f->prate, 127}; k[1] = (pknob_t){"DEPTH", &f->pdepth, 127};
                k[2] = (pknob_t){"FEEDBACK", &f->pfb, 127};
    }
}

#define PEDAL_STEPS 20                          /* a pedal knob: 0..20 on the pedal (the value 0..127 underneath, ~6 a step) */
#define PEDAL_CLICKS 1                          /* clicks a step */
static uint32_t pedal_level(uint32_t v) { return (v * PEDAL_STEPS + 63u) / 127u; }   /* 0..127 -> 0..20 (nearest) */
static uint32_t mix(uint32_t h, uint32_t v) { return (h ^ v) * 16777619u; }   /* what a screen part shows, as a number */
static char *put(char *p, const char *a) { while (*a) *p++ = *a++; return p; }
static char *put_num(char *p, uint32_t v, uint32_t digits)   /* v with at least `digits` digits */
{
    char t[10];
    uint32_t i = 0;
    do { t[i++] = (char)('0' + v % 10u); v /= 10u; } while (v || i < digits);
    while (i) *p++ = t[--i];
    return p;
}
static char *put_note(char *p, uint32_t note)  /* C#2 etc. */
{
    static const char *const NAME[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    p = put(p, NAME[note % 12u]);
    if (note >= 12u) *p++ = (char)('0' + note / 12u - 1u);
    return p;
}
static char *put_pat(char *p, uint32_t t)      /* the pattern playing, and the one queued: "A" or "A>C" */
{
    if (set.sseq_on && chain_rest >> t & 1u) { *p++ = '-'; return p; }   /* (SUPER SEQ: resting) */
    *p++ = (char)('A' + play_pat[t] % NPAT);
    if (!set.sseq_on && set.sel[t] != play_pat[t]) { *p++ = '>'; *p++ = (char)('A' + set.sel[t] % NPAT); }
    return p;
}
/* ---- drawing helpers of the look: track colours, shades, a filled circle */
static uint16_t rgbk(const uint8_t *c, uint32_t k)   /* a colour at k % brightness */
{
    return RGB(c[0] * k / 100u, c[1] * k / 100u, c[2] * k / 100u);
}
static uint16_t rgbl(const uint8_t *c, uint32_t k)   /* a colour k % of the way to white */
{
    return RGB(c[0] + (255u - c[0]) * k / 100u, c[1] + (255u - c[1]) * k / 100u, c[2] + (255u - c[2]) * k / 100u);
}
static uint16_t trk_c(uint32_t t, uint32_t k)   /* a track's colour (red when muted) at k % */
{
    return rgbk(trk_mute >> t & 1u ? MUTE_RGB : TRK_RGB[t % NTRK], k);
}
static void disc(int32_t cx, int32_t cy, int32_t r, uint16_t c)   /* a filled circle */
{
    int32_t dy;
    for (dy = -r; dy <= r; dy++) {
        int32_t h = isqrt_small(r * r - dy * dy);
        cv_rect(cx - h, cy + dy, 2 * h + 1, 1, c);
    }
}

/* ---- the top bar: a dark panel, a stripe in the track's colour, the name; on the sequencer the pattern tiles on the
 * right (the pattern playing, and "> the one queued" as an outlined tile); MUTE as a red badge */
static void head_begin(uint16_t tab)
{
    cv_begin(240, H_BIG, C_BLACK);
    cv_rrect(0, 0, 240, H_BIG, 6, C_PANEL);
    cv_rrect(6, 6, 4, 26, 2, tab);
}
static void head_text(const char *s, uint16_t c, uint16_t tab)   /* a sound's name, a message: centred */
{
    head_begin(tab);
    cv_text_bg(124 - text_w(&FONT_L, s) / 2, 3, &FONT_L, s, c, C_PANEL);
    cv_blit(0, Y_BIG);
}
static void head_track(uint32_t trk, const char *name, int chips)
{
    uint16_t col = trk_c(trk, 100);
    uint32_t q = chips && !set.sseq_on && set.sel[trk] != play_pat[trk];
    uint32_t blank = chips && set.sseq_on && scr_home;   /* home under SUPER SEQ: the tile changes nothing heard: blacked out */
    int32_t nx, bx, x = q ? 160 : 202, lim = blank || !chips ? 236 : x - 4;
    char l[2] = {0, 0};
    head_begin(col);
    nx = cv_text_bg(18, 3, &FONT_L, name, col, C_PANEL);
    bx = nx + 6;
    if (trk_mute >> trk & 1u && nx + 50 < lim + 4) {
        cv_rrect(nx + 6, 11, 42, 16, 3, C_MUTE);
        cv_text_bg(nx + 6 + (42 - text_w(&FONT_S, "MUTE")) / 2, 11, &FONT_S, "MUTE", C_BLACK, C_MUTE);
        bx += 48;
    }
    if (chips && live_mode && bx + 42 <= lim) {   /* the live recording page: REC (red) while it records */
        cv_rrect(bx, 11, 42, 16, 3, live_rec ? C_MUTE : C_KEYCAP);
        cv_text_bg(bx + (42 - text_w(&FONT_S, live_rec ? "REC" : "LIVE")) / 2, 11, &FONT_S, live_rec ? "REC" : "LIVE", live_rec ? C_BLACK : C_TXT, live_rec ? C_MUTE : C_KEYCAP);
        bx += 48;
    }
    if (chips && set.sseq_on) {                 /* SUPER SEQ plays the patterns: SUPER SEQ ON (or SUPER, if short of room) */
        const char *bt = bx + 106 <= lim ? "SUPER SEQ ON" : bx + 50 <= lim ? "SUPER" : 0;
        if (bt) {
            cv_rrect(bx, 11, text_w(&FONT_S, bt) + 10, 16, 3, C_TXT);
            cv_text_bg(bx + 5, 11, &FONT_S, bt, C_BLACK, C_TXT);
        }
    }
    if (chips && !blank) {
        l[0] = (char)('A' + (set.sseq_on ? set.sel[trk] : play_pat[trk]) % NPAT);   /* (SUPER SEQ: the one you edit) */
        cv_rrect(x, 4, 32, 30, 5, col);
        cv_text_bg(x + 8, 3, &FONT_L, l, C_BLACK, col);
        if (q) {
            l[0] = (char)('A' + set.sel[trk] % NPAT);
            cv_text_bg(195, 11, &FONT_S, ">", C_SUB, C_PANEL);
            cv_rrect(204, 4, 32, 30, 5, col);
            cv_rrect(206, 6, 28, 26, 4, C_PANEL);
            cv_text_bg(212, 3, &FONT_L, l, col, C_PANEL);
        }
    }
    cv_blit(0, Y_BIG);
}
static void head_mixer(void)                    /* MIXER, the stripe in the four track colours */
{
    uint32_t r;
    cv_begin(240, H_BIG, C_BLACK);
    cv_rrect(0, 0, 240, H_BIG, 6, C_PANEL);
    for (r = 0; r < NTRK; r++) cv_rrect(6, 5 + (int32_t)r * 7, 4, 6, 2, rgbk(TRK_RGB[SEQ_ORDER[r]], 100));
    cv_text_bg(18, 3, &FONT_L, "MIXER", C_TXT, C_PANEL);
    cv_blit(0, Y_BIG);
}

static void draw_big(uint32_t trk, uint32_t drum_edit, uint32_t drum, uint32_t preset_flash)   /* the chosen track (or drum), its pattern */
{
    if (preset_flash) {                         /* a sound (or kit) just picked: its name, big, for a moment */
        head_text(trk < NSYN ? PRESET_NAME[trk][set.snd[trk].preset % NPRESET] : KIT_NAME[set.dkit % NKIT], C_WHITE, trk_c(trk, 100));
        return;
    }
    head_track(trk, drum_edit ? DR_NAME[drum] : TRK_NAME[trk], 1);
}

static char *put_kv(char *p, const char *k, uint32_t v) { *p++ = ' '; p = put(p, k); return put_num(p, v, 1); }

static void bottom_bar(const char *s, uint16_t dot)   /* the bottom bar: the text centred, a dot when there is room */
{
    int32_t w = text_w(&FONT_S, s), x = 120 - w / 2;
    cv_begin(240, H_BOT, C_BLACK);
    cv_rrect(0, 0, 240, H_BOT, 6, C_PANEL);
    if (w <= 208) { x += 5; disc(x - 10, 12, 3, dot); }
    cv_text_bg(x, 4, &FONT_S, s, C_BOTTXT, C_PANEL);
    cv_blit(0, Y_BOT);
}

/* the bottom bar: the sound page and what KNOB1-4 set on it (drums: the chosen drum); HOME + PRESETS: octave, MIDI;
 * a dot in the track's colour when there is room */
static void draw_status(int32_t oct, uint32_t trk, uint32_t show_ch, uint32_t page, uint32_t drum, uint32_t proj,
                        uint32_t cpu)   /* proj: legend (1 synth edit, 2 drum edit, 3 / 4 live synth / drums) << 6; cpu: % (> 100: no meter, the PC) */
{
    static const char *const WAVE[NWAVE] = {"SIN", "TRI", "SAW", "SQR", "NOISE", "FM"};
    static const char *const LTGT[NLTGT] = {"PITCH", "FILT", "AMP"};
    static const char *const LWAVE[NLWAVE] = {"TRI", "SQR", "SAW", "RND"};
    char s[40], *p = s;
    const snd_t *sp = &set.snd[trk < NSYN ? trk : 0];
    if (show_ch) {
        p = put(p, "OCT "); *p++ = oct < 0 ? '-' : '+'; *p++ = (char)('0' + (oct < 0 ? -oct : oct));
        p = put(p, "  MIDI ");
        if (set.ch) { p = put(p, "CH"); p = put_num(p, set.ch, 1); } else p = put(p, "OMNI");
    } else if (proj >> 6) {                     /* the SEQ (edit) screen: what each knob does */
        static const char *const LEG[6] = {"", "K1:LEN K2:STEP K3:PAGE K4:SWG", "K1:LEN  K3:PAGE  K4:SWING",
                                           "K1:LEN K2:STEP K3:PG REC:REC", "K1:LEN  K3:PAGE  REC:RECORD",
                                           "REC AGAIN: CLEAR EVERYTHING"};
        p = put(p, LEG[(proj >> 6) % 6u]);
    } else if (page == PG_PROJ) {               /* the save page: how to work it */
        p = put(p, "K1:SLOT  REC:SAVE  PLAY:LOAD");
    } else if (page == PG_ARP) {                /* the arpeggiator: how to work it */
        p = put(p, trk < NSYN ? "K1/HOLD ARP:ON/OFF  HOME:BACK" : "HOME:BACK");
    } else if (page == PG_MIX) {                /* the mixer; and the CPU load of the sound code */
        p = put(p, "K:VOL  BUTTON:MUTE  CPU ");
        if (cpu > 100u) p = put(p, "--"); else { p = put_num(p, cpu, 1); *p++ = '%'; }
    } else if (IS_FX_PAGE(page)) {              /* a pedal: the values are on the pedal; here, how to work it */
        static const char *const BTN_NAME[NFX] = {"FX", "SEL", "ENV", "LFO", "EDIT"};
        p = put(p, "K1/HOLD "); p = put(p, BTN_NAME[page - PG_FX]); p = put(p, ":ON/OFF  HOME:BACK");
    } else if (trk == T_DRUMS) {                /* 808 KICK P44 D110 L72: the kit, the drum, its knobs */
        const char *kn = KIT_NAME[set.dkit % NKIT];
        uint32_t kl = 0, dl = 0;
        while (kn[kl]) kl++;
        while (DR_NAME[drum][dl]) dl++;
        if (kl + 1u + dl <= 15u) { p = put(p, kn); *p++ = ' '; }   /* (no room: the drum alone; the kit shows when picked) */
        p = put(p, DR_NAME[drum]);
        p = put_kv(p, "P", set.dsnd[drum].pitch); p = put_kv(p, "D", set.dsnd[drum].decay); p = put_kv(p, "L", set.dsnd[drum].level);
    } else {
        p = put(p, page == PG_MAIN ? PRESET_NAME[trk][sp->preset % NPRESET] : PAGE_NAME[page]);   /* MAIN: the sound's name */
        switch (page) {
        case PG_MAIN:                           /* PIANO C95 R0 E45 */
            p = put_kv(p, "C", sp->cut); p = put_kv(p, "R", sp->res); p = put_kv(p, "E", sp->fenv);
            break;
        case PG_OSC: {                          /* OSC SAW SQR +12 M64 */
            int32_t st = (int32_t)sp->tune - 24;
            *p++ = ' '; p = put(p, WAVE[sp->w1 % NWAVE]); *p++ = ' '; p = put(p, WAVE[sp->w2 % NWAVE]);
            *p++ = ' '; *p++ = st < 0 ? '-' : '+'; p = put_num(p, (uint32_t)(st < 0 ? -st : st), 1);
            p = put_kv(p, "M", sp->mix);
            break;
        }
        case PG_ENV:                            /* ENV A10 D70 S100 R60 */
            p = put_kv(p, "A", sp->a); p = put_kv(p, "D", sp->d); p = put_kv(p, "S", sp->s); p = put_kv(p, "R", sp->r);
            break;
        default:                                /* LFO R95 D20 PITCH TRI */
            p = put_kv(p, "R", sp->lrate); p = put_kv(p, "D", sp->ldepth);
            *p++ = ' '; p = put(p, LTGT[sp->ltgt % NLTGT]); *p++ = ' '; p = put(p, LWAVE[sp->lwave % NLWAVE]);
        }
    }
    *p = 0;
    bottom_bar(s, page == PG_MIX ? C_TXT : trk_c(trk, 100));
}

/* the info line: little panels. Home: play / stop, the tempo, four beat lights, the octave. SEQ: one cell above each
 * knob, KNOB1 the length, KNOB2 the step (synths), KNOB3 the page, KNOB4 the swing */
static void info_cell(int32_t x, const char *lab, const char *val, uint16_t lc, uint16_t vc)
{
    int32_t tw = text_w(&FONT_S, lab) + 4 + text_w(&FONT_S, val), tx = x + 30 - tw / 2;
    cv_rrect(x + 1, 1, 58, 20, 4, C_PANEL);
    tx = cv_text_bg(tx, 3, &FONT_S, lab, lc, C_PANEL);
    cv_text_bg(tx + 4, 3, &FONT_S, val, vc, C_PANEL);
}
static void draw_info(uint32_t trk, uint32_t mode, uint32_t cur, uint32_t page, int32_t oct)   /* mode 0 home, 1 synth edit, 2 drum edit, 3 live; page: shown */
{
    char v[8], *p;
    uint32_t len = pat_len(trk, (mode == 3u ? play_pat[trk] : set.sel[trk]) % NPAT), i;   /* (live: the pattern playing) */
    uint16_t col = trk_c(trk, 100);
    cv_begin(240, H_INFO, C_BLACK);
    if (mode) {                                 /* the step recorder, the live page: LEN, STEP, PG; swing (live: REC) */
        uint32_t sy = mode == 1u || (mode == 3u && trk < NSYN), pg = page;   /* (page: the one shown) */
        *put_num(v, len, 2) = 0;
        info_cell(0, "LEN", v, C_SUB, col);
        if (sy) { *put_num(v, cur + 1u, 2) = 0; info_cell(60, "STEP", v, C_SUB, col); }
        else info_cell(60, "STEP", "--", C_DIMC, C_DIMC);   /* (drums: KNOB2 has nothing to do) */
        p = put_num(v, pg + 1u, 1); *p++ = '/'; *put_num(p, len / PAGE, 1) = 0;
        info_cell(120, "PG", v, C_SUB, col);
        if (mode == 3u) info_cell(180, "REC", live_rec ? "ON" : "OFF", live_rec ? C_MUTE : C_SUB, live_rec ? C_MUTE : C_DIMC);
        else {
            p = put_num(v, 50u + (set.swing & 127u) * 16u / 127u, 2); *p++ = '%'; *p = 0;
            info_cell(180, "SW", v, C_SUB, col);
        }
    } else {
        uint32_t beat = (clk_step / 4u) % 4u;
        cv_rrect(0, 1, 30, 20, 4, C_PANEL);
        if (clk_run) for (i = 0; i < 10u; i++) cv_rect(11 + (int32_t)i, 6 + (int32_t)i / 2, 1, 11 - ((int32_t)i / 2) * 2, C_PLAYG);
        else cv_rect(10, 6, 10, 10, C_SUB);
        cv_rrect(34, 1, 84, 20, 4, C_PANEL);
        *put_num(v, set.bpm, 1) = 0;
        cv_text_bg(cv_text_bg(44, 3, &FONT_S, v, C_TXT, C_PANEL) + 6, 3, &FONT_S, "BPM", C_SUB, C_PANEL);
        cv_rrect(122, 1, 58, 20, 4, C_PANEL);
        for (i = 0; i < 4u; i++) disc(132 + (int32_t)i * 13, 11, i ? 3 : 4, clk_run && i == beat ? col : i ? C_DIMC : C_SUB);
        cv_rrect(184, 1, 56, 20, 4, C_PANEL);
        v[0] = oct < 0 ? '-' : '+'; v[1] = (char)('0' + (oct < 0 ? -oct : oct)); v[2] = 0;
        cv_text_bg(cv_text_bg(192, 3, &FONT_S, "OCT", C_SUB, C_PANEL) + 4, 3, &FONT_S, v, C_TXT, C_PANEL);
    }
    cv_blit(0, Y_INFO);
}

static uint32_t syn_covers(uint32_t t, uint32_t pt, uint32_t s, uint32_t len)   /* 2 a note starts, 1 one still sounds, 0 rest */
{
    uint32_t j;
    if (*step_note(t, pt, s)) return 2;
    for (j = 1; j < len; j++) {
        uint32_t q = (s + len - j) % len;
        if (*step_note(t, pt, q)) return *step_len(t, pt, q) > j;
    }
    return 0;
}

/* the home view: a row a track, LEAD PAD BASS DRUMS: a tile in its colour (its short name, its pattern), its steps of
 * the page it is on in groups of four, the step playing white. The chosen track bright, a muted one red. Rows of 32 px */
static void draw_home(uint32_t sel, uint32_t run)
{
    uint32_t r, s;
    cv_begin(240, H_VIEW, C_BLACK);
    for (r = 0; r < NTRK; r++) {
        uint32_t t = SEQ_ORDER[r], me = t == sel;
        int32_t y = (int32_t)r * 32;
        uint32_t pt = play_pat[t] % NPAT, len = pat_len(t, pt), pos = trk_pos[t];
        uint32_t page = run ? pos / PAGE : 0u;
        uint16_t col = trk_c(t, 100), blk = me ? col : trk_c(t, 24);
        uint16_t c_on = me ? col : trk_c(t, 72), c_hold = trk_c(t, me ? 36u : 26u);
        char lt[4], *q = put_pat(lt, t);
        *q = 0;
        cv_rrect(0, y + 1, 240, 30, 5, me ? C_PANEL2 : C_PANEL);
        cv_rrect(0, y + 1, 34, 30, 5, blk);
        cv_text_bg(17 - text_w(&FONT_S, TRK_SHORT[t]) / 2, y + 1, &FONT_S, TRK_SHORT[t], me ? C_BLACK : col, blk);
        cv_text_bg(17 - text_w(&FONT_S, lt) / 2, y + 15, &FONT_S, lt, me ? C_BLACK : C_TXT, blk);
        for (s = 0; s < PAGE; s++) {
            uint32_t st = page * PAGE + s, on = 0;
            uint16_t c;
            if (st >= len) continue;
            if (t == T_DRUMS) { uint32_t d; for (d = 0; d < NDR; d++) on |= dbit(pt, d, st); on *= 2u; }
            else on = syn_covers(t, pt, st, len);
            c = on == 2u ? c_on : on == 1u ? c_hold : s % 4u == 0u ? C_EMPTY4 : C_EMPTY;
            if (run && st == pos) c = on ? C_WHITE : C_HEADC;
            cv_rrect(38 + (int32_t)s * 12 + (int32_t)(s / 4u) * 2, y + 6, 10, 20, 2, c);
        }
    }
    cv_blit(0, Y_VIEW);
}

/* the drums: a row a drum (its short name in a tile, the chosen drum lit), a column a step of the page shown (= a white key) */
static void draw_grid(uint32_t pt, uint32_t drum, uint32_t page, uint32_t play, uint32_t len)   /* play: the step playing, or none */
{
    static const char *const DR_SHORT_ALL[NDR] = {"BD", "SD", "HH", "OH", "CP", "PC"};
    static const char *const DR_SHORT_LM1[NDR] = {"BD", "SD", "HH", "OH", "TL", "TH"};   /* (the LM-1: its two toms) */
    const char *const *DR_SHORT = set.dkit % NKIT == KIT_LM1 ? DR_SHORT_LM1 : DR_SHORT_ALL;
    uint32_t d, s;
    uint16_t col = trk_c(T_DRUMS, 100), col2 = trk_c(T_DRUMS, 62), dim = trk_c(T_DRUMS, 22);
    cv_begin(240, H_VIEW, C_BLACK);
    for (d = 0; d < NDR; d++) {
        int32_t y = (int32_t)d * 21;
        uint32_t me = d == drum;
        cv_rrect(0, y + 1, 240, 20, 4, me ? C_PANEL2 : C_PANEL);
        cv_rrect(0, y + 1, 24, 20, 4, me ? col : dim);
        cv_text_bg(4, y + 3, &FONT_S, DR_SHORT[d], me ? C_BLACK : col, me ? col : dim);
        for (s = 0; s < PAGE; s++) {
            uint32_t st = page * PAGE + s, on;
            uint16_t c;
            if (st >= len) continue;
            on = dbit(pt, d, st);
            c = on ? (me ? col : col2) : s % 4u == 0u ? C_EMPTY4 : C_EMPTY;
            if (st == play) c = on ? C_WHITE : C_HEADC;
            cv_rrect(28 + (int32_t)s * 13, y + 4, 11, 14, 2, c);
        }
    }
    cv_blit(0, Y_VIEW);
}

/* a synth: a piano roll of the cursor's page, higher notes higher (2 px a semitone), a bar as long as the note; a little
 * keyboard on the left, a line at every C, the cursor's column in the track's colour */
static void draw_roll(uint32_t t, uint32_t pt, uint32_t cur, uint32_t play, uint32_t len, uint32_t page)   /* cur ~0: none */
{
    uint32_t s, i;
    int32_t lo = t == T_BASS ? 24 : t == T_LEAD ? 60 : 48, k;   /* the lowest note shown */
    uint16_t col = trk_c(t, 100), tint = trk_c(t, 26);
    cv_begin(240, H_VIEW, C_BLACK);
    for (k = 0; k < 62; k++) {
        uint32_t bk = black((uint32_t)(lo + k));
        cv_rect(0, H_VIEW - 5 - 2 * k, bk ? 7 : 9, 2, bk ? RGB(44, 44, 54) : RGB(176, 176, 188));
    }
    for (s = 0; s < PAGE; s++) {
        uint32_t st = page * PAGE + s;
        uint16_t c;
        if (st >= len) continue;
        c = st == cur ? tint : st == play ? RGB(84, 84, 98) : s % 4u == 0u ? RGB(30, 30, 40) : RGB(18, 18, 25);
        cv_rect(12 + (int32_t)s * 14, 0, 13, H_VIEW, c);
    }
    for (k = 0; k < 62; k++)
        if ((lo + k) % 12 == 0) cv_rect(10, H_VIEW - 5 - 2 * k, 228, 1, RGB(58, 58, 74));
    for (s = 0; s < PAGE; s++) {
        uint32_t st = page * PAGE + s;
        const uint8_t *nt = step_note(t, pt, st);
        if (st >= len) continue;
        for (i = 0; i < STEP_NN(t); i++) {
            int32_t y, w, x = 12 + (int32_t)s * 14;
            if (!nt[i]) continue;
            y = (int32_t)nt[i] - lo;
            y = H_VIEW - 7 - 2 * (y < 0 ? 0 : y > 60 ? 60 : y);
            w = (int32_t)*step_len(t, pt, st) * 14 - 1;
            if (x + w > 236) w = 236 - x;       /* (a note past the page is drawn to its end) */
            cv_rrect(x, y, w, 6, 2, st == cur ? C_WHITE : col);
        }
    }
    cv_blit(0, Y_VIEW);
}

/* ---- a knob: a ring of dots lit up to the value, the knob (a dark rim, a lighter face), its white pointer */
static void knob_xy(int32_t cx, int32_t cy, int32_t deg, int32_t r, int32_t *x, int32_t *y)
{
    uint32_t ph = (uint32_t)(deg * 11930465);   /* degrees -> a phase (2^32 a turn) */
    *x = cx + (r * sine(ph)) / 32767;
    *y = cy - (r * sine(ph + 0x40000000u)) / 32767;
}
static void draw_knob(int32_t cx, int32_t cy, uint32_t v, uint32_t mx, uint16_t ring, uint16_t dim)
{
    int32_t i, x0, y0, x1, y1, deg;
    if (!mx) mx = 1;
    for (i = 0; i <= 18; i++) {                 /* the ring: dots from -135 to +135 degrees */
        knob_xy(cx, cy, -135 + i * 15, 21, &x0, &y0);
        cv_rect(x0 - 1, y0 - 1, 3, 3, (uint32_t)i * mx <= v * 18u && v ? ring : dim);
    }
    disc(cx, cy, 16, RGB(8, 8, 10));
    disc(cx, cy, 14, RGB(70, 70, 78));
    disc(cx, cy, 12, RGB(40, 40, 46));
    deg = -135 + (int32_t)(270u * v / mx);
    knob_xy(cx, cy, deg, 4, &x0, &y0);
    knob_xy(cx, cy, deg, 12, &x1, &y1);
    cv_line(x0, y0, x1, y1, C_WHITE);           /* the pointer, two pixels wide */
    cv_line(x0 + 1, y0, x1 + 1, y1, C_WHITE);
}
static void lcd_value(int32_t cx, int32_t y, int32_t w, const char *s, uint16_t c)   /* a value on a little dark screen */
{
    cv_rrect(cx - w / 2, y, w, 18, 4, C_LCD);
    cv_text_bg(cx - text_w(&FONT_S, s) / 2, y + 1, &FONT_S, s, c, C_LCD);
}

/* ---- a stompbox (the pedals, the arpeggiator): the case with a light top edge and a dark bottom one, four screws,
 * the ON lamp, the name plate, three knobs with their names and values, the footswitch, QUARTET on the case.
 * 240 x 164 under the top bar, drawn in two halves (the canvas holds 240 x 128) */
typedef struct { const char *name; uint32_t v, mx; const char *val; } kview_t;
static void draw_stomp(const uint8_t *rgb, int dark, const char *title, uint32_t on, const kview_t *k, uint32_t nk,
                       const char *msg)
{
    static const int16_t SCREW[4][2] = {{20, 13}, {220, 13}, {20, 149}, {220, 149}};
    uint16_t body = rgbk(rgb, 100), hi = rgbl(rgb, 35), lo = rgbk(rgb, 50), plate = RGB(14, 14, 17);
    uint16_t ink = dark ? RGB(14, 14, 16) : RGB(246, 246, 250), screw = dark ? rgbk(rgb, 70) : rgbl(rgb, 55);
    int32_t pw = text_w(&FONT_S, title) + 16, px = 208 - pw;
    uint32_t half, i;
    for (half = 0; half < 2u; half++) {
        cv_begin(240, H_PEDAL / 2, C_BLACK);
        cv_oy = -(int32_t)half * (H_PEDAL / 2);  /* (drawn in pedal coordinates; the canvas is this half) */
        cv_rrect(6, 0, 228, H_PEDAL, 12, lo);
        cv_rrect(6, 0, 228, H_PEDAL - 3, 12, hi);
        cv_rrect(8, 2, 224, H_PEDAL - 6, 11, body);
        for (i = 0; i < 4u; i++) { disc(SCREW[i][0], SCREW[i][1], 3, screw); cv_rect(SCREW[i][0] - 2, SCREW[i][1], 5, 1, lo); }
        if (on) { disc(40, 22, 7, RGB(115, 18, 18)); disc(40, 22, 5, RGB(255, 70, 60)); cv_rect(38, 20, 2, 2, RGB(255, 200, 190)); }
        else disc(40, 22, 5, RGB(84, 22, 20));
        cv_text_bg(52, 14, &FONT_S, on ? "ON" : "OFF", ink, body);
        cv_rrect(px, 12, pw, 20, 4, plate);
        cv_text_bg(px + 8, 14, &FONT_S, title, RGB(246, 246, 250), plate);
        if (msg) cv_text_bg(120 - text_w(&FONT_S, msg) / 2, 66, &FONT_S, msg, ink, body);
        for (i = 0; i < nk; i++) {
            int32_t cx = 50 + 70 * (int32_t)i;
            draw_knob(cx, 62, k[i].v, k[i].mx, on ? RGB(255, 64, 56) : ink, lo);
            cv_text_bg(cx - text_w(&FONT_S, k[i].name) / 2, 84, &FONT_S, k[i].name, ink, body);
            lcd_value(cx, 103, 52, k[i].val, RGB(240, 240, 246));
        }
        disc(120, 144, 15, RGB(40, 40, 46));    /* the footswitch */
        disc(120, 143, 13, RGB(150, 150, 160));
        disc(120, 143, 9, RGB(206, 206, 216));
        disc(117, 140, 3, RGB(250, 250, 255));
        cv_text_bg(32, 136, &FONT_S, "QUARTET", lo, body);
        cv_oy = 0;
        cv_blit(0, Y_PEDAL + half * (H_PEDAL / 2));
    }
}

/* ---- the pedal screen: the chosen track in the top bar, under it the pedal */
static void draw_pedal_head(uint32_t trk) { head_track(trk, TRK_NAME[trk], 0); }   /* the track whose pedal this is */

static void draw_pedal(uint32_t trk, uint32_t e)
{
    static const uint8_t BODY[NFX][3] = {{30, 80, 160}, {190, 190, 196}, {235, 120, 20}, {70, 180, 210}, {120, 60, 170}};
    static const uint8_t DARK_TEXT = (1u << E_DLY) | (1u << E_DST) | (1u << E_CHO);   /* light pedals: black letters */
    char val[3][8];
    kview_t kv[3];
    pknob_t pk[3];
    uint32_t k;
    pedal_knobs(trk, e, pk);
    for (k = 0; k < 3u; k++) {
        uint32_t v = *pk[k].v;
        if (e == E_DLY && k == 0u) *put(val[k], DLY_NAME[v % NDLY]) = 0;   /* the repeat rate: a note value */
        else *put_num(val[k], pk[k].max == 127u ? pedal_level(v) : v, 1) = 0;   /* (0..127 shown as 0..20) */
        kv[k] = (kview_t){pk[k].name, v, pk[k].max, val[k]};
    }
    draw_stomp(BODY[e % NFX], DARK_TEXT >> e & 1u, FX_NAME[e], set.fx[trk].on >> e & 1u, kv, 3, 0);
}

/* the arpeggiator, a stompbox too: the lamp (hold ARP), RATE, OCTAVES, DIR */
static void draw_arp(uint32_t trk)
{
    static const uint8_t BODY[3] = {20, 110, 120};
    if (trk < NSYN) {
        static const char *const DIR_NAME[3] = {"UP", "DOWN", "RANDOM"};
        static const char *const OCT_NAME[4] = {"1", "2", "3", "4"};
        uint32_t rate = set.arp_rate[trk] & 3u, oct = set.arp_oct[trk] & 3u, dir = set.arp_dir[trk] % 3u;
        kview_t kv[3] = {{"RATE", rate, 3, ARP_NAME[rate]}, {"OCTAVES", oct, 3, OCT_NAME[oct]}, {"DIR", dir, 2, DIR_NAME[dir]}};
        draw_stomp(BODY, 0, "ARPEGGIATOR", set.arp_on >> trk & 1u, kv, 3, 0);
    } else
        draw_stomp(BODY, 0, "ARPEGGIATOR", 0, 0, 0, "FOR LEAD, PAD, BASS");
}

/* the mixer: four channel strips, LEAD PAD BASS DRUMS (KNOB1-4): a header in the track's colour, a knob, its value, a
 * level meter (amber near the top), the button that mutes it with its light (on = playing); muted red */
static void draw_mixer(void)
{
    uint32_t half, r, i;
    for (half = 0; half < 2u; half++) {
        cv_begin(240, H_PEDAL / 2, C_BLACK);
        cv_oy = -(int32_t)half * (H_PEDAL / 2);
        for (r = 0; r < NTRK; r++) {
            uint32_t t = SEQ_ORDER[r], v = set.vol[t], muted = trk_mute >> t & 1u, n = (v * 10u + 63u) / 127u;
            int32_t x = (int32_t)r * 60, cx = x + 30;
            uint16_t col = trk_c(t, 100);
            char val[8];
            *put_num(val, v, 1) = 0;
            cv_rrect(x + 2, 0, 56, H_PEDAL, 6, C_PANEL);
            cv_rrect(x + 2, 0, 56, 20, 6, col);
            cv_rect(x + 2, 12, 56, 8, col);
            cv_text_bg(cx - text_w(&FONT_S, TRK_NAME[t]) / 2, 2, &FONT_S, TRK_NAME[t], C_BLACK, col);
            draw_knob(cx, 48, v, 127, col, C_DIMC);
            lcd_value(cx, 72, 44, muted ? "MUTE" : val, muted ? C_MUTE : RGB(240, 240, 246));
            for (i = 0; i < 10u; i++)
                cv_rect(cx - 10, 134 - (int32_t)i * 4, 20, 3, i >= n ? RGB(34, 34, 44) : muted ? C_MUTE : i >= 8u ? RGB(255, 170, 56) : col);
            cv_rrect(cx - 25, 142, 50, 18, 4, C_KEYCAP);
            disc(cx - 16, 151, 3, muted ? RGB(96, 26, 22) : col);
            cv_text_bg(cx + 6 - text_w(&FONT_S, MUTE_BTN_NAME[r]) / 2, 143, &FONT_S, MUTE_BTN_NAME[r], RGB(214, 214, 226), C_KEYCAP);   /* (centred right of the dot) */
        }
        cv_oy = 0;
        cv_blit(0, Y_PEDAL + half * (H_PEDAL / 2));
    }
}

/* ---- SUPER SEQ (HOME from home): the song chosen with PRESETS (A-J); a row a track, LEAD PAD BASS DRUMS, of 8
 * boxes played in a row (gb_clock.h). A box: its pattern in the track's colour; a blank one shows, faint, the pattern
 * that goes on through it (blanks at the start: "-", a rest). The cursor (KNOB2) framed white on the chosen track;
 * while it plays, a white bar under each track's box. Redrawn whole when anything on it changes */
static void draw_sseq(uint32_t sel, uint32_t cur, uint32_t run)
{
    const uint8_t (*c)[NLINK] = set.chain[set.sseq_sel % NCHAIN];
    uint32_t r, i, on = set.sseq_on, live = on && run, cl = set.clen[set.sseq_sel % NCHAIN];
    uint16_t col = trk_c(sel, 100);
    char v[4] = {0, 0, 0, 0};
    cv_begin(240, H_BIG, C_BLACK);              /* the top bar: SUPER SEQ, the four colours, the song's tile */
    cv_rrect(0, 0, 240, H_BIG, 6, C_PANEL);
    for (r = 0; r < NTRK; r++) cv_rrect(6, 5 + (int32_t)r * 7, 4, 6, 2, rgbk(TRK_RGB[SEQ_ORDER[r]], 100));
    cv_text_bg(18, 3, &FONT_L, "SUPER SEQ", on ? C_TXT : C_SUB, C_PANEL);
    v[0] = (char)('A' + set.sseq_sel % NCHAIN);
    cv_rrect(202, 4, 32, 30, 5, C_TXT);
    cv_text_bg(210, 3, &FONT_L, v, C_BLACK, C_TXT);
    cv_blit(0, Y_BIG);
    if (cl < 1u || cl > NLINK) cl = NLINK;
    cv_begin(240, H_INFO, C_BLACK);             /* a cell above each knob: the length, the box, its pattern, on / off */
    v[0] = (char)('0' + cl);
    info_cell(0, "LEN", v, C_SUB, C_TXT);
    v[0] = (char)('1' + cur % NLINK);
    info_cell(60, "BOX", v, C_SUB, col);
    v[0] = c[sel][cur % NLINK] == CH_REST ? 'X' : c[sel][cur % NLINK] ? (char)('A' + c[sel][cur % NLINK] - 1) : '-';
    info_cell(120, "PAT", v, C_SUB, col);
    info_cell(180, "SEQ", on ? "ON" : "OFF", C_SUB, on ? C_PLAYG : C_DIMC);
    cv_blit(0, Y_INFO);
    cv_begin(240, H_VIEW, C_BLACK);             /* the rows */
    for (r = 0; r < NTRK; r++) {
        uint32_t t = SEQ_ORDER[r], me = t == sel, last = 0;
        int32_t y = (int32_t)r * 32;
        uint16_t tc = trk_c(t, 100), blk = me ? tc : trk_c(t, 24);
        cv_rrect(0, y + 1, 240, 30, 5, me ? C_PANEL2 : C_PANEL);
        cv_rrect(0, y + 1, 34, 30, 5, blk);
        cv_text_bg(17 - text_w(&FONT_S, TRK_SHORT[t]) / 2, y + 1, &FONT_S, TRK_SHORT[t], me ? C_BLACK : tc, blk);
        if (live) {                             /* the pattern it plays now ("-": resting) */
            v[0] = chain_rest >> t & 1u ? '-' : (char)('A' + play_pat[t] % NPAT);
            cv_text_bg(13, y + 15, &FONT_S, v, me ? C_BLACK : C_TXT, blk);
        }
        for (i = 0; i < NLINK; i++) {
            int32_t x = 38 + (int32_t)i * 25;
            uint32_t bv = c[t][i];
            uint16_t bg, ink;
            if (i >= cl) { cv_rrect(x, y + 5, 23, 22, 3, C_BLACK); continue; }   /* past the song's length: blacked out */
            if (bv) last = bv <= NPAT ? bv : 0u; /* (an X: what follows rests) */
            if (bv && bv <= NPAT) { bg = me ? tc : trk_c(t, 62); ink = C_BLACK; v[0] = (char)('A' + bv - 1); }
            else if (bv == CH_REST) { bg = C_EMPTY4; ink = me ? C_TXT : C_SUB; v[0] = 'X'; }   /* a rest */
            else if (last) { bg = trk_c(t, me ? 26 : 18); ink = trk_c(t, me ? 70 : 50); v[0] = (char)('A' + last - 1); }
            else { bg = C_EMPTY; ink = C_DIMC; v[0] = '-'; }
            if (me && i == cur) cv_rrect(x - 2, y + 3, 27, 26, 4, C_WHITE);
            cv_rrect(x, y + 5, 23, 22, 3, bg);
            cv_text_bg(x + 8, y + 8, &FONT_S, v, ink, bg);
            if (live && chain_at[t] % NLINK == i) cv_rect(x + 3, y + 28, 17, 2, C_WHITE);
        }
    }
    cv_blit(0, Y_VIEW);
    bottom_bar("K1:LEN K2:BOX K3:PAT K4:ON/OFF", C_TXT);
}

/* ---- the save page (SAVE): four slots, SAVE 1-4, each with its tempo (or EMPTY), the chosen one framed white; when
 * a save or load waits for "sure?", a box asks for the button again. 164 px under the top bar, in two halves */
static void draw_save(uint32_t slot, uint32_t confirm, const char *msg, uint32_t msg_ok)
{
    static const uint16_t YEL = RGB(255, 200, 60);
    uint32_t half, i;
    char s[16], *p;
    if (msg) head_text(msg, msg_ok ? C_WHITE : C_MUTE, msg_ok ? C_PLAYG : C_MUTE);   /* SAVED 2, LOAD FAILED, EMPTY ... */
    else {
        head_begin(C_TXT);
        cv_text_bg(18, 3, &FONT_L, "SAVE", C_TXT, C_PANEL);
        s[0] = (char)('1' + slot); s[1] = 0;
        cv_rrect(202, 4, 32, 30, 5, C_TXT);
        cv_text_bg(210, 3, &FONT_L, s, C_BLACK, C_TXT);
        cv_blit(0, Y_BIG);
    }
    for (half = 0; half < 2u; half++) {
        cv_begin(240, H_PEDAL / 2, C_BLACK);
        cv_oy = -(int32_t)half * (H_PEDAL / 2);
        for (i = 0; i < NPROJ; i++) {
            int32_t y = (int32_t)i * 41;
            uint32_t me = i == slot, full = proj_bpm[i] != 0u;
            uint16_t card = me ? C_PANEL2 : C_PANEL;
            if (me) { cv_rrect(0, y + 1, 240, 38, 6, C_WHITE); cv_rrect(2, y + 3, 236, 34, 5, card); }
            else cv_rrect(0, y + 1, 240, 38, 6, card);
            cv_rrect(8, y + 6, 28, 28, 5, me ? C_TXT : C_KEYCAP);
            s[0] = (char)('1' + i); s[1] = 0;
            cv_text_bg(14, y + 4, &FONT_L, s, me ? C_BLACK : C_TXT, me ? C_TXT : C_KEYCAP);
            p = put(s, "SAVE "); *p++ = (char)('1' + i); *p = 0;
            cv_text_bg(46, y + 4, &FONT_S, s, me ? C_TXT : C_SUB, card);
            if (full) { p = put_num(s, proj_bpm[i], 1); p = put(p, " BPM"); *p = 0; }
            else *put(s, "EMPTY") = 0;
            cv_text_bg(46, y + 20, &FONT_S, s, full ? (me ? C_TXT : C_SUB) : C_DIMC, card);
            if (full) {                         /* a full slot: the four track colours */
                uint32_t k;
                for (k = 0; k < NTRK; k++) cv_rrect(170 + (int32_t)k * 14, y + 15, 10, 10, 3, rgbk(TRK_RGB[SEQ_ORDER[k]], me ? 100 : 55));
            }
        }
        if (confirm) {                          /* "sure?" */
            const char *q = confirm == 1u ? "SAVE " : "LOAD ";
            p = put(s, q); *p++ = (char)('1' + slot); *p++ = '?'; *p = 0;
            cv_rrect(14, 34, 212, 96, 10, YEL);
            cv_rrect(17, 37, 206, 90, 8, C_PANEL);
            cv_text_bg(120 - text_w(&FONT_L, s) / 2, 42, &FONT_L, s, YEL, C_PANEL);
            q = confirm == 1u ? "PRESS REC AGAIN" : "PRESS PLAY AGAIN";
            cv_text_bg(120 - text_w(&FONT_S, q) / 2, 80, &FONT_S, q, C_TXT, C_PANEL);
            q = confirm == 1u ? (proj_bpm[slot] ? "REPLACES WHAT IS THERE" : "THE WHOLE GROOVEBOX") : "REPLACES YOUR WORK";
            cv_text_bg(120 - text_w(&FONT_S, q) / 2, 98, &FONT_S, q, C_SUB, C_PANEL);
        }
        cv_oy = 0;
        cv_blit(0, Y_PEDAL + half * (H_PEDAL / 2));
    }
}

/* ---- the startup screen: a pedalboard as pixel art, one pedal in each track's colour, under the name with a stripe
 * in the four colours. A chunky grid of 4 x 4 px "pixels" (60 x 60 of them), drawn in two halves */
#define TI_BG RGB(10, 10, 16)
static void ti_px(int32_t ux, int32_t uy, int32_t uw, int32_t uh, uint16_t c) { cv_rect(ux * 4, uy * 4, uw * 4, uh * 4, c); }
static void ti_pedal(int32_t ux, int32_t uy, const uint8_t *rgb, int lit)   /* one pedal, 10 x 16: knobs, lamp, footswitch */
{
    ti_px(ux, uy, 10, 16, RGB(6, 6, 8));        /* the outline */
    ti_px(ux + 1, uy + 1, 8, 14, rgbk(rgb, 100));
    ti_px(ux + 1, uy + 1, 8, 1, rgbl(rgb, 45)); /* a light top edge, a dark bottom one */
    ti_px(ux + 1, uy + 14, 8, 1, rgbk(rgb, 60));
    ti_px(ux + 2, uy + 3, 2, 2, RGB(16, 16, 20));   /* two knobs, their pointers */
    ti_px(ux + 6, uy + 3, 2, 2, RGB(16, 16, 20));
    ti_px(ux + 3, uy + 3, 1, 1, RGB(240, 240, 240));
    ti_px(ux + 6, uy + 3, 1, 1, RGB(240, 240, 240));
    ti_px(ux + 4, uy + 7, 2, 1, lit ? RGB(255, 60, 50) : RGB(90, 24, 20));   /* the lamp */
    ti_px(ux + 2, uy + 10, 6, 4, RGB(16, 16, 20));  /* the footswitch */
    ti_px(ux + 3, uy + 11, 4, 2, RGB(170, 170, 182));
    ti_px(ux + 4, uy + 11, 2, 1, RGB(240, 240, 248));
}
static void draw_title(void)
{
    uint32_t half;
    for (half = 0; half < 2u; half++) {
        int32_t i;
        cv_begin(240, 120, TI_BG);
        cv_oy = -(int32_t)half * 120;           /* (draw in screen coordinates; the canvas is this half) */
        cv_text_bg(120 - text_w(&FONT_L, "QUARTET") / 2, 16, &FONT_L, "QUARTET", RGB(246, 246, 250), TI_BG);
        for (i = 0; i < 4; i++) cv_rect(56 + i * 32, 52, 30, 4, rgbk(TRK_RGB[SEQ_ORDER[i]], 100));
        cv_text_bg(120 - text_w(&FONT_S, "4-TRACK GROOVEBOX") / 2, 64, &FONT_S, "4-TRACK GROOVEBOX", RGB(150, 150, 168), TI_BG);
        ti_px(3, 22, 54, 26, RGB(6, 6, 8));     /* the board, its outline, its rails, its feet */
        ti_px(4, 23, 52, 24, RGB(52, 52, 64));
        ti_px(4, 30, 52, 1, RGB(30, 30, 38));
        ti_px(4, 42, 52, 1, RGB(30, 30, 38));
        ti_px(5, 47, 4, 2, RGB(6, 6, 8)); ti_px(51, 47, 4, 2, RGB(6, 6, 8));
        ti_px(0, 31, 7, 1, RGB(120, 120, 132)); /* the cable in, a plug */
        ti_px(6, 30, 1, 3, RGB(170, 170, 180));
        for (i = 0; i < 4; i++) {               /* four pedals, LEAD PAD BASS DRUMS, patched together */
            ti_pedal(7 + i * 12, 26, TRK_RGB[SEQ_ORDER[i]], i != 1);
            ti_px(17 + i * 12, 31, i < 3 ? 2 : 7, 1, RGB(120, 120, 132));
        }
        for (i = 0; i < 60; i += 2) ti_px(i, 49, 1, 1, RGB(36, 36, 46));   /* the floor */
        cv_oy = 0;
        cv_blit(0, half * 120u);
    }
}

/* ---- MIDI in: from the main loop, and from app_idle while the screen draws (idle_ok) */
static uint32_t idle_ok, mclk_n, mclk_t0, mclk_bpm, mclk_run, mclk_beat_ms, sel_trk = T_LEAD;

static void play_live(uint32_t trk, uint32_t note, uint32_t on)   /* a live note on a track (MIDI note numbers) */
{
    if (trk < NSYN) { live_note(trk, note, on); return; }
    if (on) {                                   /* DRUMS: the General MIDI drum notes */
        uint32_t d = note == 36u || note == 35u ? DR_KICK : note == 38u || note == 40u ? DR_SNARE
                   : note == 42u || note == 44u ? DR_HAT : note == 46u ? DR_OPEN : note == 39u ? DR_CLAP
                   : note == 37u || note == 75u || note == 76u ? DR_PERC : NDR;
        if (d < NDR) hit_req[d]++;
    }
}

static void midi_in(void)
{
    uint8_t st, d1, d2;
    while (midi_get(&st, &d1, &d2) != 0) {      /* from USB or the TRS jack */
        uint32_t ty = st & 0xF0u;
        if (st == 0xF8u) {                      /* clock: 24 a quarter note (shown as a blink of PLAY) */
            if (mclk_n % 24u == 0u) {
                if (mclk_n) {
                    uint32_t ms = fm1_ms - mclk_t0;
                    mclk_bpm = ms ? (60000u + ms / 2u) / ms : 0;
                }
                mclk_t0 = fm1_ms;
                mclk_beat_ms = fm1_ms;
            }
            mclk_n++;
            continue;
        }
        if (st == 0xFAu) { mclk_run = 1; mclk_n = 0; continue; }   /* START */
        if (st == 0xFBu) { mclk_run = 1; continue; }               /* CONTINUE */
        if (st == 0xFCu) { mclk_run = 0; continue; }               /* STOP */
        if (set.ch && (st & 15u) != set.ch - 1u) continue;        /* not our channel */
        if (ty == 0x90u && d2) play_live(sel_trk, d1, 1);
        else if (ty == 0x80u || ty == 0x90u) play_live(sel_trk, d1, 0);
    }
}
static void app_idle(void)
{
    if (idle_ok) midi_in();
}

/* ---- editing (main loop) */
static void syn_tie(uint32_t t, uint32_t pt, uint32_t cur, uint32_t len)   /* the note before the cursor lasts up to it */
{
    uint32_t j;
    for (j = 1; j < len; j++) {
        uint32_t q = (cur + len - j) % len;
        uint8_t *bl = step_len(t, pt, q);
        if (!*step_note(t, pt, q)) continue;
        if (*bl == j && j < NSTEP) *bl = (uint8_t)(j + 1u);
        if (*bl > j) *step_note(t, pt, cur) = 0;   /* the step is under the note now */
        return;                                 /* (a note that ended before: a gap, nothing to tie) */
    }
}
static void syn_clear_step(uint32_t t, uint32_t pt, uint32_t s)
{
    uint8_t *nt = step_note(t, pt, s);
    uint32_t i;
    for (i = 0; i < STEP_NN(t); i++) nt[i] = 0; /* (note[0] first: the interrupt then sees a rest) */
    *step_len(t, pt, s) = 1;
}
static void pat_clear(uint32_t t, uint32_t pt)  /* a whole pattern of a track */
{
    uint32_t s, d;
    if (t < NSYN) for (s = 0; s < NSTEP; s++) syn_clear_step(t, pt, s);
    else for (d = 0; d < NDR; d++) for (s = 0; s < NSTEP / PAGE; s++) set.dpat[pt][d][s] = 0;
}
static void pat_copy(uint32_t t, uint32_t from, uint32_t to)
{
    uint32_t s, d;
    if (from == to) return;
    if (t == T_BASS) for (s = 0; s < NSTEP; s++) set.mono[to][s] = set.mono[from][s];
    else if (t < NSYN) for (s = 0; s < NSTEP; s++) set.chord[t == T_PAD][to][s] = set.chord[t == T_PAD][from][s];
    else for (d = 0; d < NDR; d++) for (s = 0; s < NSTEP / PAGE; s++) set.dpat[to][d][s] = set.dpat[from][d][s];
    set.plen[t][to] = set.plen[t][from];
}

static int knob(uint32_t enc, uint8_t *v, int32_t lo, int32_t hi)   /* a knob turns a setting; 1 = it changed */
{
    int32_t e = fm1_enc_take(enc), t;
    if (!e) return 0;
    t = (int32_t)*v + e;
    *v = (uint8_t)(t < lo ? lo : t > hi ? hi : t);
    return 1;
}
#define ARP_CLICKS 3                            /* the arp's knobs: clicks a step (a few, so a nudge does not jump) */
static int knob_pedal(uint32_t enc, uint8_t *v, int32_t *acc)   /* a pedal's 0..127 knob, in 20 steps of a click */
{
    int32_t e = fm1_enc_take(enc), steps, l;
    uint8_t nv;
    if (!e) return 0;
    *acc += e;
    steps = *acc / PEDAL_CLICKS;
    if (!steps) return 0;
    *acc -= steps * PEDAL_CLICKS;
    l = (int32_t)pedal_level(*v) + steps;
    l = l < 0 ? 0 : l > PEDAL_STEPS ? PEDAL_STEPS : l;
    nv = (uint8_t)(l * 127 / PEDAL_STEPS);      /* (0, 6, 12, 19 ... 120, 127) */
    if (nv == *v) return 0;
    *v = nv;
    return 1;
}
#define SWITCH_CLICKS 3                         /* KNOB1 as a pedal's switch: clicks to turn it on (clockwise) / off */
static int knob_switch(uint32_t enc, int32_t *acc)   /* 1: turned on, -1: turned off, 0: not yet */
{
    int32_t e = fm1_enc_take(enc);
    if (!e) return 0;
    if ((e > 0) != (*acc > 0)) *acc = 0;        /* (the other way: count afresh) */
    *acc += e;
    if (*acc >= SWITCH_CLICKS) { *acc = 0; return 1; }
    if (*acc <= -SWITCH_CLICKS) { *acc = 0; return -1; }
    return 0;
}
static int knob_slow(uint32_t enc, uint8_t *v, int32_t lo, int32_t hi, int32_t *acc)   /* ARP_CLICKS clicks a step */
{
    int32_t e = fm1_enc_take(enc), steps, t;
    if (!e) return 0;
    *acc += e;
    steps = *acc / ARP_CLICKS;                  /* (whole steps; the rest waits for more clicks the same way) */
    if (!steps) return 0;
    *acc -= steps * ARP_CLICKS;
    t = (int32_t)*v + steps;
    t = t < lo ? lo : t > hi ? hi : t;
    if ((uint8_t)t == *v) return 0;
    *v = (uint8_t)t;
    return 1;
}

static int settings_ok(void)
{
    uint32_t t, s, i, p;
    if ((int8_t)set.oct < -2 || (int8_t)set.oct > 2 || set.ch > 16u || set.bpm < BPM_MIN || set.bpm > BPM_MAX
        || set.mute > 15u)
        return 0;
    for (t = 0; t < NTRK; t++) {
        if (set.sel[t] >= NPAT) return 0;
        for (p = 0; p < NPAT; p++) if (set.plen[t][p] % PAGE || set.plen[t][p] < PAGE || set.plen[t][p] > NSTEP) return 0;
    }
    for (t = 0; t < NSYN; t++) {
        const snd_t *sp = &set.snd[t];
        if (sp->cut > 127u || sp->res > 127u || sp->fenv > 127u || sp->w1 >= NWAVE || sp->w2 >= NWAVE || sp->tune > 48u
            || sp->mix > 127u || sp->a > 127u || sp->d > 127u || sp->s > 127u || sp->r > 127u || sp->lrate > 127u
            || sp->ldepth > 127u || sp->ltgt >= NLTGT || sp->lwave >= NLWAVE || sp->preset >= NPRESET)
            return 0;
        for (p = 0; p < NPAT; p++)
            for (s = 0; s < NSTEP; s++) {
                const uint8_t *nt = step_note(t, p, s), ln = *step_len(t, p, s);
                if (ln < 1u || ln > NSTEP) return 0;
                for (i = 0; i < STEP_NN(t); i++) if (nt[i] > 127u) return 0;
            }
    }
    for (i = 0; i < NDR; i++)
        if (set.dsnd[i].pitch > 127u || set.dsnd[i].decay > 127u || set.dsnd[i].level > 127u) return 0;
    if (set.dkit >= NKIT || set.arp_on > 7u || set.swing > 127u) return 0;
    for (t = 0; t < NTRK; t++) if (set.vol[t] > 127u) return 0;
    for (t = 0; t < NSYN; t++) if (set.arp_rate[t] > 3u || set.arp_oct[t] > 3u || set.arp_dir[t] > 2u) return 0;
    for (t = 0; t < NTRK; t++) {
        const tfx_t *f = &set.fx[t];
        if (f->on >= 1u << NFX || f->rev_send > 127u || f->dly_send > 127u || f->cho_send > 127u || f->drive > 127u
            || f->tone > 127u || f->dlevel > 127u
            || f->prate > 127u || f->pdepth > 127u || f->pfb > 127u)
            return 0;
    }
    if (set.sseq_on > 1u || set.sseq_sel >= NCHAIN || set.sound_order != 1u) return 0;
    for (i = 0; i < NCHAIN; i++) if (set.clen[i] < 1u || set.clen[i] > NLINK) return 0;
    {
        const uint8_t *c = &set.chain[0][0][0];
        for (i = 0; i < sizeof set.chain; i++) if (c[i] > CH_REST) return 0;
    }
    {
        const gfx_t *g = &set.gfx;
        if (g->rtone > 127u || g->rtime > 127u || g->dtime >= NDLY || g->dfb > 127u || g->crate > 127u || g->cdepth > 127u)
            return 0;
    }
    return 1;
}

/* ---- the save slots (4). A load goes into a spare copy first and is checked whole (CRC, then every value) before
 * it replaces the current work: a broken or foreign slot never touches what you have. */
typedef char project_fits_in_a_slot[sizeof set <= SLOT_MAX && NPROJ <= NSLOT ? 1 : -1];
static uint8_t proj_buf[sizeof set] __attribute__((section(".pool")));   /* a slot read in here first */
static uint8_t proj_keep[sizeof set] __attribute__((section(".pool")));  /* the current work, while a load is checked */

static void bytes_copy(void *to, const void *from, uint32_t n)
{
    uint8_t *d = (uint8_t *)to;
    const uint8_t *s = (const uint8_t *)from;
    while (n--) *d++ = *s++;
}
static void proj_scan(void)                     /* which slots hold a project (at boot, a few ms) */
{
    uint32_t i;
    for (i = 0; i < NPROJ; i++)
        proj_bpm[i] = slot_load(i, proj_buf, sizeof set) == 0 || slot_load(i, proj_buf, SET_ORD) == 0
                      || slot_load(i, proj_buf, SET_SS1) == 0 || slot_load(i, proj_buf, SET_V10) == 0 ? proj_buf[2] : 0u;   /* (bpm: the 3rd byte of set) */
}
static int proj_save(uint32_t i)                /* 0 = saved */
{
    set.mute = (uint8_t)trk_mute;
    if (slot_save(i, &set, sizeof set)) return -1;
    proj_bpm[i] = set.bpm;
    return 0;
}
static int proj_load(uint32_t i)                /* 0 = loaded; -1 = empty or broken (the current work is untouched) */
{
    uint32_t n = sizeof set, c;
    if (slot_load(i, proj_buf, n)) {            /* (an older project is shorter: what it lacks starts as new) */
        n = SET_ORD;
        if (slot_load(i, proj_buf, n)) {
            n = SET_SS1;
            if (slot_load(i, proj_buf, n)) { n = SET_V10; if (slot_load(i, proj_buf, n)) return -1; }
        }
    }
    bytes_copy(proj_keep, &set, sizeof set);
    bytes_copy(&set, proj_buf, n);
    if (n <= SET_V10) chain_defaults();         /* (v1.0: no SUPER SEQ) */
    else if (n <= SET_SS1) for (c = 0; c < NCHAIN; c++) set.clen[c] = NLINK;   /* (no song lengths: all 8) */
    if (n < sizeof set) sound_reorder();        /* (from before QUARTET's order) */
    if (!settings_ok()) { bytes_copy(&set, proj_keep, sizeof set); return -1; }
    trk_mute = set.mute;
    clock_set_bpm(set.bpm);
    return 0;
}

/* the knobs that work by position (ALGORITHM, SELECT, PRESETS): where they stand in a turn of ALG_CLICKS clicks */
static uint32_t turn(uint32_t pos, int32_t e)   /* one knob position, e clicks on, round and round */
{
    return (uint32_t)(((int32_t)pos + e % (int32_t)ALG_CLICKS + (int32_t)ALG_CLICKS) % (int32_t)ALG_CLICKS);
}
static void knobs_sync(uint32_t *snd_pos, uint32_t *pat_pos)   /* SELECT and PRESETS stand where what is chosen is */
{
    uint32_t t;
    for (t = 0; t < NTRK; t++) {
        uint32_t c = t < NSYN ? set.snd[t].preset % NPRESET : set.dkit % NKIT;
        snd_pos[t] = c * ALG_CLICKS / NPRESET;   /* the first of its 2 clicks */
        pat_pos[t] = (set.sel[t] % NPAT) * ALG_CLICKS / NPAT; /* the start of its two clicks */
    }
}

/* ---- live recording: what you play goes into the pattern playing, on the nearest step ("on the beat") */
#define REC_LAG 441                             /* ~10 ms (in samples): the sound reaches you, and your key reaches us, this much
                                                   after the clock: a note is placed where you heard it (20 ms pulled
                                                   notes too early) */
static uint32_t rec_step(uint32_t t, uint32_t *ahead)   /* the step nearest now on track t; ahead: the next one (not yet played) */
{
    uint32_t len = pat_len(t, play_pat[t]), now = trk_pos[t], s = now;
    int32_t step_q8 = (int32_t)(fxc_len * TPS), at;   /* a step, and where we are in it, in 1/256 samples */
    if (fxc_tick / TPS != clk_step) s = (s + len - 1u) % len;   /* (the step changed since the block began: measure from the old one) */
    at = (int32_t)((fxc_tick % TPS) * fxc_len + fxc_pos) - REC_LAG * 256;
    if (at < 0) { at += step_q8; s = (s + len - 1u) % len; }   /* (that far back is the step before) */
    if (at >= step_q8 * 2 / 5) s = (s + 1u) % len;   /* past 40 % of the step: the next one (a press a little early
                                                   goes on the beat it was meant for, not the one before) */
    *ahead = s == (now + 1u) % len;             /* not played yet: the step will play it */
    return s;
}
static void rec_note(uint32_t t, uint32_t p, uint32_t s, uint32_t note, uint32_t ln)   /* add a note to a step (a chord: a free place) */
{
    uint8_t *nt = step_note(t, p, s), *l = step_len(t, p, s);
    uint32_t i, n = STEP_NN(t), empty = !nt[0];
    for (i = 0; i < n; i++) if (nt[i] == note) break;   /* there already: only the length */
    if (i == n) { for (i = 0; i < n; i++) if (!nt[i]) break; if (i == n) i = n - 1u; }   /* (full: the last place) */
    if (n == 1u || empty || ln > *l) *l = (uint8_t)(ln > NSTEP ? NSTEP : ln);   /* (the length first: the interrupt may read the step) */
    nt[i] = (uint8_t)note;
}

static void wipe_all(void)                      /* HOME, hold REC, then REC: every pattern empty, every sound, pedal, mixer
                                                   and SUPER SEQ song as new, pattern A everywhere (the MIDI channel and
                                                   the octave stay; the save slots are not touched) */
{
    uint32_t ch = set.ch, oct = set.oct, t, p;
    set_defaults();
    for (t = 0; t < NTRK; t++) for (p = 0; p < NPAT; p++) pat_clear(t, p);   /* (no starting groove either) */
    set.ch = (uint8_t)ch; set.oct = (uint8_t)oct;
}

static void app_main(void)
{
    uint32_t prev = 0, k, t_change = 0, dirty = 0, edits = 0, big_drawn = ~0u, status_drawn = ~0u, info_drawn = ~0u;
    uint32_t view_drawn = ~0u, cpu_drawn = ~0u, cpu_t = 0, ch_t = 0;
    uint32_t edit = 0, drum = DR_KICK, dpage = 0, rec_t0 = 0, rec_long = 1, chord_n = 0, chord_trk = T_PAD, home_used = 1;
    uint32_t spage = PG_MAIN;                   /* the page shown (the knobs edit it) */
    uint32_t sseq_cur = 0, song_pos = 0;        /* SUPER SEQ: the box KNOB2 is on; where PRESETS stands (the song, A-J) */
    uint32_t fxb_t0[NFX] = {0}, fxb_long = (1u << NFX) - 1u;   /* when each top button went down; bit e: no long press due */
    uint32_t proj_slot = 0, confirm = 0, confirm_t = 0, msg_t = 0, msg_ok = 0;   /* the save page: the slot; "sure?" (1 save, 2 load); a message */
    uint32_t proj_go = 0, proj_go_t = 0;        /* a save (1) / load (2) confirmed: done once the sound has stopped */
    uint32_t wipe_ask = 0, wipe_t = 0;          /* HOME: "CLEAR ALL?" up (REC again to do it), since when */
    uint32_t page_hold_t = 0, vpage = 0;        /* SEQ screens: when you last moved the page / cursor (it stays put 3 s); the page shown */
    const char *msg = 0;                        /* shown big for 1.5 s: SAVED 3, LOAD FAILED, ALL CLEARED ... */
    uint32_t pedal_mode = 0, head_drawn = ~0u, pedal_drawn = ~0u;   /* the pedal screen up?, what it shows */
    /* ALGORITHM: 2 clicks either way go to the next / the previous track, LEAD, PAD, BASS, DRUMS, round and round */
    static const uint8_t ALG_TRK[4] = {T_LEAD, T_PAD, T_BASS, T_DRUMS};
    int32_t alg_acc = 0;                        /* clicks gathered towards the next track */
    /* SELECT and PRESETS work the same way, one turn of ALG_CLICKS clicks per track: SELECT in tenths (the 10 sounds),
     * PRESETS in tenths (A-J, two clicks each). Where each knob stands, per track; set from what is chosen (knobs_sync) */
    uint32_t snd_pos[NTRK], pat_pos[NTRK];
    uint32_t arp_used = 1, arp_t0 = 0;          /* ARP held and used with PRESETS / SELECT: no arp action when let go; when it went down */
    int32_t arp_acc[3] = {0, 0, 0};             /* the arp knobs: clicks gathered towards the next step */
    int32_t seq_acc[2] = {0, 0};                /* edit mode: the length and page knobs, likewise */
    int32_t ped_acc[3] = {0, 0, 0};             /* the pedals' knobs, likewise (PEDAL_CLICKS) */
    int32_t sw_acc = 0;                         /* KNOB1 as the pedal / arpeggiator switch: clicks gathered */
    uint32_t preset_t = 0, aud_t = 0, aud_trk = NTRK;   /* a preset picked: when (its name shows), the test note playing */
    static const uint8_t AUD_NOTE[NSYN][3] = {{72, 0, 0}, {36, 0, 0}, {60, 63, 67}};   /* C5, C2, a C minor chord */
    uint32_t cur[NSYN] = {0, 0, 0};             /* each synth's step-recording cursor */
    uint8_t held_note[27] = {0}, held_trk[27] = {0};   /* what each held key plays, on which track (note 0: none) */
    uint8_t slot[27];                           /* a white key: its step on the page 0..15; a black key: 0..10 */
    uint8_t rec_at[27], rec_pp[27];             /* live recording: the step each held key's note goes on (0xFF: none), its pattern */
#if BOARD_SMK37
    static const uint8_t WHITE[16] = {0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26};   /* white key -> key */
    uint8_t pad_note[16] = {0}, pad_trk[16] = {0}, ext_note[FM1_NKEY] = {0}, ext_trk[FM1_NKEY] = {0};
    smk_pad_events = 1;                         /* the pads as events (smk_pad_take); the pitch wheel is unused for now */
#endif
#ifndef DINGHY_HOST
    uint32_t boot_t = fm1_ms;
    draw_title();                               /* the startup screen, while the work loads */
#endif
    set_defaults();
    if (song_load() != 0) set_defaults();       /* (an older save has other sizes: that fails, so defaults) */
    else {
        if (song_loaded < sizeof set) sound_reorder();   /* (from before the reorder) */
        if (!settings_ok()) set_defaults();
    }
    proj_scan();                                /* which project slots are full */
    knobs_sync(snd_pos, pat_pos);
    song_pos = (set.sseq_sel % NCHAIN) * ALG_CLICKS / NCHAIN;
    trk_mute = set.mute;
    clock_set_bpm(set.bpm);
    {
        uint32_t w = 0, bl = 0;
        for (k = 0; k < 27u; k++) { slot[k] = (uint8_t)(black(KEY0 + k) ? bl++ : w++); rec_at[k] = 0xFFu; }
    }
#ifndef DINGHY_HOST
    while (fm1_ms - boot_t < 2500u && !fm1_in.buttons && !fm1_in.notes)   /* the startup screen stays 2.5 s (a key or a
                                                   button skips it); USB, the watchdog and updates go on meanwhile */
        platform_poll();
#endif

    lcd_fill(0, 0, 240, 240, C_BLACK);

#define CHANGED() (dirty = 1, t_change = fm1_ms, edits++)
    for (;;) {
        uint32_t keys, down, up, rel, b, out_ch = set.ch ? set.ch - 1u : 0u, st, run, trk = sel_trk, pt, len, t;
        uint32_t syn_edit, drum_edit, play;
        int32_t oct = (int8_t)set.oct, e;
        uint32_t base = (uint32_t)((int32_t)KEY0 + 12 * oct);
        platform_poll();
        pt = (live_mode ? play_pat[trk] : set.sel[trk]) % NPAT;   /* the pattern edited: the one chosen (live: the one playing) */
        len = pat_len(trk, pt);
        for (t = 0; t < NSYN; t++) if (cur[t] >= pat_len(t, set.sel[t])) cur[t] = 0;   /* (a shorter pattern chosen) */
        if (dpage >= pat_len(T_DRUMS, set.sel[T_DRUMS]) / PAGE) dpage = 0;
        if (trk < NSYN && cur[trk] >= len) cur[trk] = 0;   /* (live: the pattern playing may be shorter) */
        if (trk == T_DRUMS && dpage >= len / PAGE) dpage = 0;
        syn_edit = edit && trk < NSYN;
        drum_edit = edit && trk == T_DRUMS;

        keys = fm1_in.notes;                    /* bit k: key k (0 = F3 .. 26 = G5) */
        down = keys & ~prev;
        up = prev & ~keys;
        prev = keys;
        for (k = 0; k < 27u; k++) {
            if (up >> k & 1u && held_note[k]) {  /* a key stops the note it started, on its track */
                if (rec_at[k] != 0xFFu && live_rec && clk_run) {   /* live recording: in it goes, as long as it was held */
                    uint32_t t2 = held_trk[k], a, ll = pat_len(t2, rec_pp[k]), ln = (rec_step(t2, &a) + ll - rec_at[k] % ll) % ll;
                    rec_note(t2, rec_pp[k], rec_at[k], held_note[k], ln ? ln : 1u);
                    CHANGED();
                }
                rec_at[k] = 0xFFu;
                play_live(held_trk[k], held_note[k], 0);
                midi_send(0x80u | out_ch, held_note[k], 0);
                held_note[k] = 0;
            }
            if (!(down >> k & 1u)) continue;
            if (drum_edit) {                    /* drums: the keys edit the page of the beat shown */
                if (black(KEY0 + k)) {
                    if (slot[k] < NDR) { drum = slot[k]; hit_req[drum]++; }   /* choose a drum, and hear it */
                } else if (dpage * PAGE + slot[k] < len) {
                    set.dpat[pt][drum][dpage] ^= (uint16_t)(1u << slot[k]);   /* a step on / off */
                    page_hold_t = fm1_ms;       /* (the page stays while you tap) */
                    if (!clk_run && dbit(pt, drum, dpage * PAGE + slot[k])) hit_req[drum]++;
                    CHANGED();
                }
            } else if (trk == T_DRUMS) {        /* drums, playing: the first 4 white keys hit them */
                if (!black(KEY0 + k) && slot[k] < NDR) {   /* (the knobs then set it) */
                    uint32_t ahead = 0;
                    drum = slot[k];
                    if (live_rec && clk_run) {  /* live recording: on the nearest step (one just ahead: heard from there) */
                        uint32_t s2 = rec_step(T_DRUMS, &ahead), lp = play_pat[T_DRUMS] % NPAT;
                        set.dpat[lp][drum][(s2 / PAGE) % (NSTEP / PAGE)] |= (uint16_t)(1u << (s2 % PAGE));
                        CHANGED();
                    }
                    if (!ahead) hit_req[drum]++;
                }
            } else {                            /* a synth: play it; editing, also write it */
                int32_t n = (int32_t)(base + k) + TRK[trk].transpose;
                n = n < 1 ? 1 : n > 127 ? 127 : n;
                held_note[k] = (uint8_t)n; held_trk[k] = (uint8_t)trk;
                play_live(trk, (uint32_t)n, 1);
                midi_send(0x90u | out_ch, (uint32_t)n, 100);
                rec_at[k] = 0xFFu;
                if (live_rec && clk_run) {      /* live recording: it starts on the nearest step (written when let go) */
                    uint32_t a;
                    rec_pp[k] = (uint8_t)(play_pat[trk] % NPAT);
                    rec_at[k] = (uint8_t)rec_step(trk, &a);
                }
                if (syn_edit) {
                    uint8_t *nt = step_note(trk, pt, cur[trk]), *ln = step_len(trk, pt, cur[trk]);
                    page_hold_t = fm1_ms;       /* (the cursor's page stays in view while you write) */
                    if (STEP_NN(trk) > 1u) {         /* a chord: the keys held together, the cursor moves when all are up */
                        if (chord_n == 0u) { syn_clear_step(trk, pt, cur[trk]); chord_trk = trk; }
                        if (chord_n < 4u) {
                            if (chord_n == 0u) { *ln = 1; nt[0] = (uint8_t)n; }
                            else nt[chord_n] = (uint8_t)n;
                            chord_n++;
                        }
                    } else {
                        *ln = 1;                /* (the length first: the interrupt may read the step meanwhile) */
                        nt[0] = (uint8_t)n;
                        cur[trk] = (cur[trk] + 1u) % len;
                    }
                    CHANGED();
                }
            }
        }
        if (chord_n && !keys) { cur[chord_trk] = (cur[chord_trk] + 1u) % pat_len(chord_trk, set.sel[chord_trk]); chord_n = 0; }
#if BOARD_SMK37
        while ((e = smk_extkey_take()) >= 0) {  /* the keys left and right of the FM-1's 27: key << 8 | velocity */
            uint32_t key = (uint32_t)e >> 8, vel = (uint32_t)e & 0x7Fu;
            if (key >= FM1_NKEY) continue;
            if (vel && trk < NSYN) {
                int32_t n = (int32_t)(base + key) - (int32_t)SMK_KEY_F3 + TRK[trk].transpose;
                ext_note[key] = (uint8_t)(n < 1 ? 1 : n > 127 ? 127 : n); ext_trk[key] = (uint8_t)trk;
                play_live(trk, ext_note[key], 1);
            } else if (!vel && ext_note[key]) { play_live(ext_trk[key], ext_note[key], 0); ext_note[key] = 0; }
        }
        while ((e = smk_pad_take()) >= 0) {     /* a pad: hardware pad << 8 | velocity (0 = let go) */
            uint32_t pad = ((uint32_t)e >> 8) & 15u, vel = (uint32_t)e & 0x7Fu;
            uint32_t w = pad < 8u ? pad + 8u : pad - 8u;   /* bottom row = white keys 1-8, top row 9-16 */
            if (vel && trk == T_DRUMS) { if (w < NDR) hit_req[w]++; }
            else if (vel && trk < NSYN) {
                int32_t n = (int32_t)(base + WHITE[w]) + TRK[trk].transpose;
                pad_note[pad] = (uint8_t)(n < 1 ? 1 : n > 127 ? 127 : n); pad_trk[pad] = (uint8_t)trk;
                play_live(trk, pad_note[pad], 1);
            } else if (!vel && pad_note[pad]) { play_live(pad_trk[pad], pad_note[pad], 0); pad_note[pad] = 0; }
        }
#endif

        b = fm1_input_edges(&rel);              /* buttons pressed since the last call (bit = button id) */
        if (spage == PG_MIX) {                  /* the mixer: SAVE ARP SEQ PLAY mute LEAD PAD BASS DRUMS (only here) */
            uint32_t mask = 0;
            for (k = 0; k < NTRK; k++) {
                mask |= 1u << MUTE_BTN[k];
                if (b >> MUTE_BTN[k] & 1u) { trk_mute ^= 1u << SEQ_ORDER[k]; CHANGED(); }
            }
            b &= ~mask; rel &= ~mask;           /* (their usual jobs wait until the mixer is closed) */
            arp_used = 1;
        }
        /* SAVE: the save page, and SAVE again back home. There KNOB1 picks the slot (1-4), REC saves to it, PLAY loads
         * it: each asks "sure?" first; the same button again within 3 s does it, any other button cancels */
        if (b & (1u << BTN_SAVE)) { spage = spage == PG_PROJ ? PG_MAIN : PG_PROJ; confirm = 0; b &= ~(1u << BTN_SAVE); }
        if (spage == PG_PROJ) {
            uint32_t mask = 1u << BTN_PLAY | 1u << BTN_REC, op = b >> BTN_REC & 1u ? 1u : b >> BTN_PLAY & 1u ? 2u : 0u;
            if (b & ~mask) confirm = 0;         /* another button: cancelled */
            if (op && !proj_go) {
                if (op == 2u && !proj_bpm[proj_slot]) { msg = "EMPTY"; msg_ok = 0; msg_t = fm1_ms ? fm1_ms : 1u; confirm = 0; }
                else if (confirm != op || fm1_ms - confirm_t > 3000u) { confirm = op; confirm_t = fm1_ms; }
                else {                          /* sure: stop, let it ring out, then do it (below) */
                    confirm = 0; proj_go = op; proj_go_t = fm1_ms; clk_run = 0;
                    msg = op == 1u ? "SAVING" : "LOADING"; msg_ok = 1; msg_t = fm1_ms ? fm1_ms : 1u;
                }
            }
            b &= ~mask; rel &= ~mask;           /* (PLAY and REC do only this here) */
        }
        if (proj_go && (quiet() || fm1_ms - proj_go_t > 1500u)) {   /* (writing the flash stalls the sound: done in silence) */
            static char done[16];
            char *p;
            int rc = proj_go == 2u ? proj_load(proj_slot) : proj_save(proj_slot);
            p = put(done, rc ? (proj_go == 2u ? "LOAD FAILED" : "SAVE FAILED") : (proj_go == 2u ? "LOADED " : "SAVED "));
            if (!rc) *p++ = (char)('1' + proj_slot);
            *p = 0;
            msg = done; msg_ok = rc == 0; msg_t = fm1_ms ? fm1_ms : 1u;
            if (!rc && proj_go == 2u) { CHANGED(); knobs_sync(snd_pos, pat_pos); song_pos = (set.sseq_sel % NCHAIN) * ALG_CLICKS / NCHAIN; }   /* (now the current work) */
            else if (!rc) edits++;
            proj_go = 0;
        }
        if (drum_edit) {                        /* drums editing: OCT- / OCT+ turn the page */
            if (b & (1u << BTN_OCT_DOWN) && dpage > 0u) { dpage--; page_hold_t = fm1_ms; }
            if (b & (1u << BTN_OCT_UP) && dpage + 1u < len / PAGE) { dpage++; page_hold_t = fm1_ms; }
        } else {
            if (b & (1u << BTN_OCT_DOWN) && oct > -2) { set.oct = (uint8_t)(oct - 1); CHANGED(); }
            if (b & (1u << BTN_OCT_UP) && oct < 2) { set.oct = (uint8_t)(oct + 1); CHANGED(); }
        }
        if (b & (1u << BTN_PLAY)) {             /* all tracks: start (from step 1) / stop */
            if (clk_run) clk_run = 0;
            else { clk_go++; clk_run = 1; }     /* (clk_go first: the interrupt restarts at step 1, then counts on) */
        }
        if (!clk_run) live_rec = 0;             /* stopped: the recording ends */
        /* SEQ, on a pedal, the arp, the mixer, the save page or SUPER SEQ: back to the sequencer screen (nothing else) */
        if (b & (1u << BTN_SEQ)) {              /* from home: the step recorder; then between it and live recording */
            if (spage >= PG_FX) spage = PG_MAIN;
            else if (live_mode) { live_mode = 0; live_rec = 0; edit = 1; }
            else if (edit) { edit = 0; live_mode = 1; }
            else edit = 1;
            chord_n = 0;
        }
        /* HOME (let go, not used with PRESETS): always home, the sequencer screen; from there, SUPER SEQ and back */
        if (b & (1u << BTN_HOME)) home_used = 0;
        if (rel & (1u << BTN_HOME)) {
            if (!home_used) {
                if (spage == PG_SSEQ) spage = PG_MAIN;
                else if (spage < PG_FX && !edit && !live_mode) spage = PG_SSEQ;
                else { spage = PG_MAIN; edit = 0; live_mode = 0; live_rec = 0; chord_n = 0; }
            }
            home_used = 1;
        }
        if ((e = fm1_enc_take(ENC_ALGORITHM)) != 0) {   /* the track: 2 clicks to the next (or the one before) */
            alg_acc += e;
            if (alg_acc >= 2 || alg_acc <= -2) {
                int32_t st = alg_acc / 2, i;
                alg_acc -= st * 2;
                for (i = 0; i < 4 && ALG_TRK[i] != sel_trk; i++) {}
                i = ((i % 4) + st % 4 + 4) % 4;
                sel_trk = ALG_TRK[i];
                chord_n = 0;
            }
        }
        if ((e = fm1_enc_take(ENC_PRESETS)) != 0) {
            if (fm1_in.buttons >> BTN_HOME & 1u) {           /* HOME held: the MIDI channel */
                int32_t v = (int32_t)set.ch + e;
                set.ch = (uint8_t)(v < 0 ? 0 : v > 16 ? 16 : v);
                home_used = 1; ch_t = fm1_ms;
            } else if (spage == PG_SSEQ) {      /* SUPER SEQ: the song A-J, two clicks each (ARP held: copy it along) */
                uint32_t to;
                song_pos = turn(song_pos, e);
                to = song_pos * NCHAIN / ALG_CLICKS;
                if (to != set.sseq_sel % NCHAIN) {
                    if (fm1_in.buttons >> BTN_ARP & 1u) { bytes_copy(set.chain[to], set.chain[set.sseq_sel % NCHAIN], sizeof set.chain[0]); arp_used = 1; }
                    set.sseq_sel = (uint8_t)to;
                }
            } else {                            /* the pattern A-J, two clicks each, round and round (ARP held: a copy) */
                uint32_t to;
                if (fm1_in.buttons >> BTN_ARP & 1u) arp_used = 1;   /* (ARP held: a copy, not the arp screen) */
                pat_pos[trk] = turn(pat_pos[trk], e);
                to = pat_pos[trk] * NPAT / ALG_CLICKS;
                if (to != pt) {
                    if (!edit && fm1_in.buttons >> BTN_ARP & 1u) { pat_copy(trk, pt, to); arp_used = 1; }   /* (take this one along;
                                                   not in edit mode, where ARP is the tie) */
                    set.sel[trk] = (uint8_t)to;
                }
            }
            CHANGED();
        }
        if ((e = fm1_enc_take(ENC_SELECT)) != 0) {
            if (spage == PG_SSEQ) {}            /* (SUPER SEQ: nothing; KNOB4 turns it on / off) */
            else if (syn_edit) {                /* editing a synth: the cursor, through all pages */
                cur[trk] = (uint32_t)(((int32_t)cur[trk] + e % (int32_t)len + (int32_t)len) % (int32_t)len);
                page_hold_t = fm1_ms;
            }
            else if (!edit && fm1_in.buttons >> BTN_ARP & 1u) {   /* ARP held: the detailed pages MAIN, OSC, ENV, LFO */
                arp_used = 1;
                int32_t v = (int32_t)(spage >= PG_FX ? PG_MAIN : spage) + (spage >= PG_FX ? 0 : e);
                spage = (uint32_t)(v < 0 ? 0 : v >= (int32_t)NPAGE ? (int32_t)NPAGE - 1 : v);
            } else if (!edit) {                 /* the sound: 2 clicks each, 10 a turn (DRUMS: the kit) */
                uint32_t c, now = trk < NSYN ? set.snd[trk].preset : set.dkit, i;
                if (spage >= PG_FX) spage = PG_MAIN;   /* (from a pedal: back to the sequencer) */
                snd_pos[trk] = turn(snd_pos[trk], e);
                c = snd_pos[trk] * NPRESET / ALG_CLICKS;
                if (c != now) {                 /* only on entering another sound's clicks: a small turn keeps your tweaks */
                    if (aud_trk < NSYN)         /* (a test note still playing: let it go first) */
                        for (i = 0; i < 3u; i++) if (AUD_NOTE[aud_trk][i]) live_note(aud_trk, AUD_NOTE[aud_trk][i], 0);
                    aud_trk = NTRK;
                    if (trk < NSYN) {
                        set.snd[trk] = PRESETS[trk][c];
                        if (!clk_run) {         /* stopped: hear it, a note (the pad: a chord) for 0.6 s */
                            for (i = 0; i < 3u; i++) if (AUD_NOTE[trk][i]) live_note(trk, AUD_NOTE[trk][i], 1);
                            aud_trk = trk; aud_t = fm1_ms;
                        }
                    } else {
                        set.dkit = (uint8_t)c;
                        for (i = 0; i < NDR; i++) set.dsnd[i] = KITS[c][i];
                        if (!clk_run) { hit_req[DR_KICK]++; hit_req[DR_HAT]++; }   /* stopped: hear the kit */
                    }
                    preset_t = fm1_ms ? fm1_ms : 1u;   /* (its name shows big for a moment) */
                    CHANGED();
                }
            }
        }
        /* the top buttons: a press opens that effect's page for the chosen track; held 0.5 s: the effect on / off */
        for (k = 0; k < NFX; k++) {
            if (b >> FX_BTN[k] & 1u) { spage = PG_FX + k; fxb_t0[k] = fm1_ms; fxb_long &= ~(1u << k); }
            if (fm1_in.buttons >> FX_BTN[k] & 1u && !(fxb_long >> k & 1u) && fm1_ms - fxb_t0[k] >= 500u) {
                set.fx[trk].on ^= (uint8_t)(1u << k);
                fxb_long |= 1u << k;
                CHANGED();
            }
            if (rel >> FX_BTN[k] & 1u) fxb_long |= 1u << k;   /* (let go: no long press until the next press) */
        }
        if (confirm && (fm1_ms - confirm_t > 3000u || spage != PG_PROJ)) confirm = 0;   /* "sure?" lapses */
        /* KNOB1-4: the page's four settings */
        if ((edit || live_mode) && spage < PG_FX) {   /* the step recorder and the live page: KNOB1 length, KNOB2 the
                                                   step, KNOB3 the page; KNOB4 swing (step recorder) */
            uint32_t sy = trk < NSYN;
            uint8_t li = (uint8_t)(len / PAGE - 1u), pg = (uint8_t)((sy ? cur[trk] : dpage * PAGE) / PAGE);
            int ch = 0;
            if (knob_slow(ENC_KNOB1, &li, 0, NSTEP / PAGE - 1u, &seq_acc[0])) {   /* 16 / 32 / 48 / 64, 3 clicks a step */
                set.plen[trk][pt] = (uint8_t)((li + 1u) * PAGE);
                len = pat_len(trk, pt);
                if (sy && cur[trk] >= len) cur[trk] = len - 1u;
                if (dpage >= len / PAGE) dpage = len / PAGE - 1u;
                ch = 1;
            }
            if ((e = fm1_enc_take(ENC_KNOB2)) != 0 && sy) {   /* the step cursor, through all pages */
                cur[trk] = (uint32_t)(((int32_t)cur[trk] + e % (int32_t)len + (int32_t)len) % (int32_t)len);
                page_hold_t = fm1_ms;
            }
            if (knob_slow(ENC_KNOB3, &pg, 0, (int32_t)(len / PAGE) - 1, &seq_acc[1])) {   /* the page of 16, 3 clicks a step */
                page_hold_t = fm1_ms;
                if (sy) cur[trk] = pg * PAGE + cur[trk] % PAGE;
                else dpage = pg;
            }
            if (edit) ch |= knob(ENC_KNOB4, &set.swing, 0, 127);   /* swing, for the whole groove */
            else fm1_enc_take(ENC_KNOB4);
            if (ch) CHANGED();
        } else if (IS_FX_PAGE(spage)) {         /* a pedal, for the chosen track: KNOB1 on / off, KNOB2-4 its three knobs */
            static const uint8_t ENC3[3] = {ENC_KNOB2, ENC_KNOB3, ENC_KNOB4};
            pknob_t pk[3];
            int ch = 0, sw = knob_switch(ENC_KNOB1, &sw_acc);
            uint8_t bit = (uint8_t)(1u << (spage - PG_FX));
            if (sw > 0 && !(set.fx[trk].on & bit)) { set.fx[trk].on |= bit; ch = 1; }   /* (its button lights) */
            if (sw < 0 && set.fx[trk].on & bit) { set.fx[trk].on &= (uint8_t)~bit; ch = 1; }
            pedal_knobs(trk, spage - PG_FX, pk);
            for (k = 0; k < 3u; k++)            /* (0..127 ones: 20 steps; DELAY's REPEAT: its 7 note values) */
                ch |= pk[k].max == 127u ? knob_pedal(ENC3[k], pk[k].v, &ped_acc[k]) : knob(ENC3[k], pk[k].v, 0, pk[k].max);
            if (ch) CHANGED();
        } else if (spage == PG_PROJ) {          /* the save page: KNOB1 the slot */
            uint8_t sl = (uint8_t)proj_slot;
            if (knob(ENC_KNOB1, &sl, 0, NPROJ - 1)) confirm = 0;   /* (a change cancels "sure?") */
            proj_slot = sl;
            fm1_enc_take(ENC_KNOB2); fm1_enc_take(ENC_KNOB3); fm1_enc_take(ENC_KNOB4);
        } else if (spage == PG_ARP) {           /* the arpeggiator of the chosen synth: KNOB1 on / off, KNOB2 rate,
                                                   KNOB3 octaves, KNOB4 direction; slow knobs: ARP_CLICKS clicks a step */
            int ch = 0;
            if (trk < NSYN) {
                int sw = knob_switch(ENC_KNOB1, &sw_acc);
                uint8_t bit = (uint8_t)(1u << trk);
                if (sw > 0 && !(set.arp_on & bit)) { set.arp_on |= bit; ch = 1; }   /* (ARP lights) */
                if (sw < 0 && set.arp_on & bit) { set.arp_on &= (uint8_t)~bit; ch = 1; }
                ch |= knob_slow(ENC_KNOB2, &set.arp_rate[trk], 0, 3, &arp_acc[0])
                    | knob_slow(ENC_KNOB3, &set.arp_oct[trk], 0, 3, &arp_acc[1])
                    | knob_slow(ENC_KNOB4, &set.arp_dir[trk], 0, 2, &arp_acc[2]);
            } else { fm1_enc_take(ENC_KNOB1); fm1_enc_take(ENC_KNOB2); fm1_enc_take(ENC_KNOB3); fm1_enc_take(ENC_KNOB4); }
            if (ch) CHANGED();
        } else if (spage == PG_MIX) {           /* the mixer: KNOB1-4 the volume of LEAD, PAD, BASS, DRUMS */
            static const uint8_t ENC4[4] = {ENC_KNOB1, ENC_KNOB2, ENC_KNOB3, ENC_KNOB4};
            int ch = 0;
            for (k = 0; k < 4u; k++) ch |= knob(ENC4[k], &set.vol[SEQ_ORDER[k]], 0, 127);
            if (ch) CHANGED();
        } else if (spage == PG_SSEQ) {          /* SUPER SEQ: KNOB1 the song's length, KNOB2 the box, KNOB3 its pattern, KNOB4 on / off */
            uint8_t cu = (uint8_t)sseq_cur, *cl = &set.clen[set.sseq_sel % NCHAIN];
            int ch = knob(ENC_KNOB1, cl, 1, NLINK);
            if (cu >= *cl) cu = (uint8_t)(*cl - 1u);
            knob(ENC_KNOB2, &cu, 0, *cl - 1);
            sseq_cur = cu;
            ch |= knob(ENC_KNOB3, &set.chain[set.sseq_sel % NCHAIN][trk][sseq_cur % NLINK], 0, CH_REST);   /* blank, A-J, X */
            ch |= knob(ENC_KNOB4, &set.sseq_on, 0, 1);   /* on / off */
            if (ch) CHANGED();
        } else if (trk == T_DRUMS || spage == PG_MAIN) {   /* sound pages: drums and MAIN have the tempo on KNOB1 */
            if (knob(ENC_KNOB1, &set.bpm, BPM_MIN, BPM_MAX)) { clock_set_bpm(set.bpm); CHANGED(); }
        }
        if (spage >= PG_FX) {                   /* (the pedals and the other pages: handled above) */
            /* (an effect page: done above) */
        } else if (trk == T_DRUMS) {            /* drums: the chosen drum's pitch, decay, level */
            dsnd_t *ds = &set.dsnd[drum];
            if (knob(ENC_KNOB2, &ds->pitch, 0, 127) | knob(ENC_KNOB3, &ds->decay, 0, 127) | knob(ENC_KNOB4, &ds->level, 0, 127))
                CHANGED();
        } else {
            snd_t *sp = &set.snd[trk];
            int ch = 0;
            switch (spage) {
            case PG_MAIN: ch = knob(ENC_KNOB2, &sp->cut, 0, 127) | knob(ENC_KNOB3, &sp->res, 0, 127) | knob(ENC_KNOB4, &sp->fenv, 0, 127); break;
            case PG_OSC: ch = knob(ENC_KNOB1, &sp->w1, 0, NWAVE - 1) | knob(ENC_KNOB2, &sp->w2, 0, NWAVE - 1)
                            | knob(ENC_KNOB3, &sp->tune, 0, 48) | knob(ENC_KNOB4, &sp->mix, 0, 127); break;
            case PG_ENV: ch = knob(ENC_KNOB1, &sp->a, 0, 127) | knob(ENC_KNOB2, &sp->d, 0, 127)
                            | knob(ENC_KNOB3, &sp->s, 0, 127) | knob(ENC_KNOB4, &sp->r, 0, 127); break;
            default: ch = knob(ENC_KNOB1, &sp->lrate, 0, 127) | knob(ENC_KNOB2, &sp->ldepth, 0, 127)
                            | knob(ENC_KNOB3, &sp->ltgt, 0, NLTGT - 1) | knob(ENC_KNOB4, &sp->lwave, 0, NLWAVE - 1); break;
            }
            if (ch) CHANGED();
        }
        /* REC: a tap = a rest (synth step recorder) / recording on, off (the live page: it starts the music too); held
         * 1 s = clear the pattern (live: the one playing). HOME: held 1 s asks "CLEAR ALL?", REC again clears everything */
        {
            uint32_t home = !edit && !live_mode && spage < PG_FX;
            if (wipe_ask && (!home || fm1_ms - wipe_t > 3000u || b & ~(1u << BTN_REC))) wipe_ask = 0;   /* (3 s, another button: no) */
            if (b & (1u << BTN_REC)) {
                if (wipe_ask) {                 /* sure: a fresh start */
                    wipe_ask = 0; rec_long = 1;
                    clk_run = 0; live_rec = 0;
                    wipe_all();
                    trk_mute = set.mute;
                    clock_set_bpm(set.bpm);
                    knobs_sync(snd_pos, pat_pos);
                    song_pos = 0; sseq_cur = 0; dpage = 0; drum = DR_KICK;
                    for (k = 0; k < NSYN; k++) cur[k] = 0;
                    msg = "ALL CLEARED"; msg_ok = 1; msg_t = fm1_ms ? fm1_ms : 1u;
                    CHANGED();
                } else { rec_t0 = fm1_ms; rec_long = 0; }
            }
            if (!rec_long && fm1_in.buttons >> BTN_REC & 1u && fm1_ms - rec_t0 >= 1000u) {
                rec_long = 1;
                if (home) { wipe_ask = 1; wipe_t = fm1_ms; }   /* HOME: ask first */
                else { pat_clear(trk, pt); CHANGED(); }
            }
        }
        if (rel & (1u << BTN_REC)) {
            if (!rec_long && syn_edit) { syn_clear_step(trk, pt, cur[trk]); cur[trk] = (cur[trk] + 1u) % len; CHANGED(); }
            else if (!rec_long && live_mode && spage < PG_FX) {
                if (live_rec) live_rec = 0;
                else { live_rec = 1; if (!clk_run) { clk_go++; clk_run = 1; } }
            }
            rec_long = 1;
        }
        if (b & (1u << BTN_ARP) && syn_edit) { syn_tie(trk, pt, cur[trk], len); cur[trk] = (cur[trk] + 1u) % len; CHANGED(); }
        /* ARP outside edit mode: a tap (not used with PRESETS or SELECT) opens the arpeggiator; GLO: the mixer */
        if (b & (1u << BTN_ARP)) { arp_used = edit != 0u; arp_t0 = fm1_ms; }
        if (rel & (1u << BTN_ARP)) {           /* let go: a tap opens the arpeggiator, a hold (0.5 s) turns it on / off */
            if (!arp_used && !edit) {
                if (fm1_ms - arp_t0 >= 500u) { if (trk < NSYN) { set.arp_on ^= (uint8_t)(1u << trk); CHANGED(); } }
                else spage = PG_ARP;
            }
            arp_used = 1;
        }
        if (b & (1u << BTN_GLO)) spage = spage == PG_MIX ? PG_MAIN : PG_MIX;   /* GLO: the mixer, and back home */

        if (aud_trk < NSYN && fm1_ms - aud_t >= 600u) {   /* the preset's test note ends */
            for (k = 0; k < 3u; k++) if (AUD_NOTE[aud_trk][k]) live_note(aud_trk, AUD_NOTE[aud_trk][k], 0);
            aud_trk = NTRK;
        }
        midi_in();
        if (mclk_bpm && fm1_ms - mclk_t0 > 2000u) mclk_bpm = 0, mclk_run = 0;   /* the MIDI clock has stopped coming */

        trk = sel_trk;
        pt = (live_mode ? play_pat[trk] : set.sel[trk]) % NPAT;
        len = pat_len(trk, pt);
        syn_edit = edit && trk < NSYN;
        drum_edit = edit && trk == T_DRUMS;
        oct = (int8_t)set.oct;
        run = clk_run;
        scr_home = !edit && !live_mode;
        play = run && play_pat[trk] == pt ? trk_pos[trk] : ~0u;   /* the step playing in the pattern edited, if it plays */
        /* the SEQ screens follow the page playing (patterns longer than 16), except for 3 s after you moved the
         * cursor or the page or wrote something (drums: the white keys follow too, they are the page shown) */
        {
            uint32_t follow = (edit || live_mode) && play != ~0u && len > PAGE && fm1_ms - page_hold_t >= 3000u;
            if (follow && trk == T_DRUMS) dpage = play / PAGE;
            vpage = trk == T_DRUMS ? dpage : follow ? play / PAGE : cur[trk] / PAGE;
        }
        for (k = 0; k < 27u; k++) {
            uint32_t on = keys >> k & 1u, dim = 0, page = vpage;
            uint32_t st2 = page * PAGE + slot[k];
            if (edit && black(KEY0 + k))        /* drums: the 6 drum keys, the chosen one lit; synths: as held */
                on = drum_edit ? slot[k] == drum : keys >> k & 1u;
            else if (edit && st2 >= len)        /* past the pattern's end: dark */
                on = 0;
            else if (drum_edit)                 /* the steps of the chosen drum; the playing step flips */
                on = dbit(pt, drum, st2) ^ (st2 == play);
            else if (syn_edit) {                /* synth: the cursor lit, the playing step flips, steps with a note glow */
                on = (st2 == cur[trk]) ^ (st2 == play);
                dim = !on && *step_note(trk, pt, st2);
            }
            fm1_led_key(KEY_LED(k), on);
            fm1_led_key_glow(KEY_LED(k), dim);
        }
        fm1_led_key(BTN_OCT_DOWN, drum_edit ? dpage > 0u : oct < 0);
        fm1_led_key(BTN_OCT_UP, drum_edit ? dpage + 1u < len / PAGE : oct > 0);
        fm1_led_key(BTN_SEQ, edit || (live_mode && (fm1_ms / 400u) % 2u));   /* step recorder: lit; live: blinking */
        fm1_led_key(BTN_HOME, spage == PG_SSEQ);   /* SUPER SEQ up */
        fm1_led_key(BTN_SAVE, spage == PG_PROJ);   /* the save page up */
        fm1_led_key(BTN_GLO, spage == PG_MIX);  /* the mixer open */
        fm1_led_key(BTN_ARP, trk < NSYN && set.arp_on >> trk & 1u);   /* the chosen track arpeggiates */
        for (k = 0; k < NFX; k++) {             /* top buttons: lit = that effect is on for the chosen track; its page open: a glow */
            uint32_t on = set.fx[trk].on >> k & 1u;
            fm1_led_key(FX_BTN[k], on);
            fm1_led_key_glow(FX_BTN[k], !on && spage == PG_FX + k);
        }
        if (spage == PG_PROJ && confirm)        /* the save page's "sure?": the button to press again blinks */
            fm1_led_key(BTN_PLAY, confirm == 2u && (fm1_ms / 150u) % 2u);
        else fm1_led_key(BTN_PLAY, run || mclk_run || (mclk_bpm && fm1_ms - mclk_beat_ms < 80u));   /* lit: running; a blink a beat */
        fm1_led_key(BTN_REC, ((spage == PG_PROJ && confirm == 1u) || wipe_ask ? (fm1_ms / 150u) % 2u : 0u)
                             || (live_mode && live_rec));   /* (live: lit while recording; "sure?": blinking) */
        if (spage == PG_MIX)                    /* the mixer: SAVE ARP SEQ PLAY lit = LEAD PAD BASS DRUMS playing (not muted) */
            for (k = 0; k < NTRK; k++) fm1_led_key(MUTE_BTN[k], !(trk_mute >> SEQ_ORDER[k] & 1u));
#if BOARD_SMK37
        for (k = 0; k < 16u; k++) {             /* a pad glows lilac while its key is held */
            uint32_t w = k < 8u ? k + 8u : k - 8u, on = keys >> WHITE[w] & 1u;
            padled_set(k, on ? 90 : 4, on ? 60 : 2, on ? 128 : 8);
        }
        padled_flush();                         /* (sends only what changed) */
#endif

        idle_ok = 1;                            /* the drawing below may read MIDI meanwhile (LCD_IDLE) */
        if (fm1_ms - cpu_t >= 500u || cpu_drawn == ~0u) {   /* the CPU meter, twice a second */
            uint32_t pct = 999;
#ifndef DINGHY_HOST
            pct = (cpu_peak * 100u + 69659u) / 139319u;   /* of the 5.8 ms a 256-frame block has (24 MHz ticks) */
            cpu_reset++;
            if (pct > 100u) pct = 100;
#endif
            cpu_t = fm1_ms;
            cpu_drawn = pct;                    /* (shown on the mixer's bottom line) */
        }
        /* each screen part is redrawn when what it shows changed. The middle shows one of: the sequencer (0), a pedal
         * (1), the arpeggiator (2), the mixer (3) */
        {
            uint32_t scr = IS_FX_PAGE(spage) ? 1u : spage == PG_ARP ? 2u : spage == PG_MIX ? 3u : spage == PG_SSEQ ? 4u : spage == PG_PROJ ? 5u : 0u;
            if (scr != pedal_mode) {
                pedal_mode = scr;
                lcd_fill(0, 0, 240, 240, C_BLACK);  /* (all of it: the screens differ in their parts) */
                view_drawn = big_drawn = info_drawn = head_drawn = pedal_drawn = status_drawn = ~0u;
            }
        }
        if (pedal_mode == 5u) {                 /* the save page: the slots, "sure?", a message */
            uint32_t show_msg = msg && fm1_ms - msg_t < 1500u;
            st = mix(mix(mix(mix(mix(proj_slot, confirm), show_msg), show_msg ? msg_t : 0u), proj_go), 5u);
            for (k = 0; k < NPROJ; k++) st = mix(st, proj_bpm[k]);
            if (st != pedal_drawn) { pedal_drawn = st; draw_save(proj_slot, confirm, show_msg ? msg : 0, msg_ok); }
        } else if (pedal_mode == 4u) {          /* SUPER SEQ: redrawn whole when anything on it changes */
            const uint8_t *cb = &set.chain[set.sseq_sel % NCHAIN][0][0];
            st = mix(mix(mix(mix(mix(mix(mix(mix(trk, sseq_cur), set.sseq_on), set.sseq_sel), run), trk_mute), chain_rest), 4u), set.clen[set.sseq_sel % NCHAIN]);
            for (k = 0; k < NTRK * NLINK; k++) st = mix(st, cb[k]);
            if (set.sseq_on && run) for (k = 0; k < NTRK; k++) st = mix(st, chain_at[k] | play_pat[k] << 4);
            if (st != pedal_drawn) { pedal_drawn = st; draw_sseq(trk, sseq_cur, run); }
        } else if (pedal_mode == 3u) {          /* the mixer: MIXER big, the four strips */
            const uint8_t *vb = set.vol;
            if (head_drawn) { head_drawn = 0; head_mixer(); }
            st = mix(trk_mute, 3u);
            for (k = 0; k < NTRK; k++) st = mix(st, vb[k]);
            if (st != pedal_drawn) { pedal_drawn = st; draw_mixer(); }
        } else if (pedal_mode == 2u) {          /* the arpeggiator: the track's name, the arp "pedal" */
            if ((st = mix(trk, trk_mute >> trk & 1u)) != head_drawn) { head_drawn = st; draw_pedal_head(trk); }
            st = mix(mix(mix(trk, 2u), set.arp_on), trk < NSYN ? set.arp_rate[trk] | set.arp_oct[trk] << 2 | set.arp_dir[trk] << 4 : 0u);
            if (st != pedal_drawn) { pedal_drawn = st; draw_arp(trk); }
        } else if (pedal_mode) {                /* a pedal: the track's name, the pedal (no sequence: HOME goes back) */
            const uint8_t *fb = (const uint8_t *)&set.fx[trk], *gb = (const uint8_t *)&set.gfx;
            if ((st = mix(trk, trk_mute >> trk & 1u)) != head_drawn) { head_drawn = st; draw_pedal_head(trk); }
            st = mix(trk, spage);
            for (k = 0; k < sizeof(tfx_t); k++) st = mix(st, fb[k]);
            for (k = 0; k < sizeof(gfx_t); k++) st = mix(st, gb[k]);
            if (st != pedal_drawn) { pedal_drawn = st; draw_pedal(trk, spage - PG_FX); }
        } else {
        st = mix(mix(mix(mix(edits, edit), trk), run), trk_mute);
        if (drum_edit) st = mix(mix(mix(st, drum), dpage), play);
        else if (syn_edit) st = mix(mix(mix(st, cur[trk]), play), vpage);
        else if (live_mode) st = mix(mix(mix(mix(mix(mix(st, 77u), play), pt | drum << 4), cur[trk < NSYN ? trk : 0]), dpage), vpage);
        else for (t = 0; t < NTRK; t++) st = mix(mix(st, trk_pos[t]), play_pat[t] | set.sel[t] << 4 | (chain_rest >> t & 1u) << 8 | set.sseq_on << 9);
        if (st != view_drawn) {
            view_drawn = st;
            if (drum_edit) draw_grid(pt, drum, dpage, play, len);
            else if (syn_edit) draw_roll(trk, pt, cur[trk], play, len, vpage);
            else if (live_mode) {               /* live recording: the pattern playing, as the step recorder shows it */
                if (trk == T_DRUMS) draw_grid(pt, drum, dpage, play, len);
                else draw_roll(trk, pt, cur[trk], play, len, vpage);
            } else draw_home(trk, run);
        }
        {
            uint32_t flash = preset_t && fm1_ms - preset_t < 1500u;
            uint32_t show_msg = msg && fm1_ms - msg_t < 1500u;
            st = mix(mix(mix(mix(mix(mix(trk, drum_edit ? 16u + drum : 0u), trk_mute >> trk & 1u), play_pat[trk]), set.sel[trk]), flash), set.sseq_on | live_mode << 1 | live_rec << 2 | edit << 3);
            if (flash) st = mix(st, trk < NSYN ? set.snd[trk].preset : 16u + set.dkit);
            st = mix(mix(mix(st, show_msg), show_msg ? msg_t : 0u), wipe_ask);
            if (st != big_drawn) {
                big_drawn = st;
                if (wipe_ask) head_text("CLEAR ALL?", RGB(255, 200, 60), RGB(255, 200, 60));
                else if (show_msg)              /* SAVED 3, LOAD FAILED, ALL CLEARED ... */
                    head_text(msg, msg_ok ? C_WHITE : C_MUTE, msg_ok ? C_PLAYG : C_MUTE);   /* (red: it did not happen) */
                else draw_big(trk, drum_edit, drum, flash);
            }
        }
        }                                       /* (end of the sequencer screen's parts) */
        {
            uint32_t show_ch = fm1_ms - ch_t < 1500u && ch_t;
            const uint8_t *sb = trk < NSYN ? (const uint8_t *)&set.snd[trk] : (const uint8_t *)&set.dsnd[drum];
            uint32_t nb = trk < NSYN ? sizeof(snd_t) : sizeof(dsnd_t);
            st = mix(mix(mix(mix(mix(mix(mix(mix(trk, (uint32_t)(oct + 2)), set.ch), show_ch), spage), drum), set.bpm), set.dkit), trk_mute >> trk & 1u);
            for (k = 0; k < nb; k++) st = mix(st, sb[k]);
            if (IS_FX_PAGE(spage)) {            /* an effect page: what it shows */
                const uint8_t *fb = (const uint8_t *)&set.fx[trk], *gb = (const uint8_t *)&set.gfx;
                for (k = 0; k < sizeof(tfx_t); k++) st = mix(st, fb[k]);
                for (k = 0; k < sizeof(gfx_t); k++) st = mix(st, gb[k]);
            }
            {
                uint32_t leg = wipe_ask ? 5u : spage < PG_FX ? (syn_edit ? 1u : drum_edit ? 2u : live_mode ? (trk < NSYN ? 3u : 4u) : 0u) : 0u;   /* SEQ screens: the legend; "CLEAR ALL?" */
                uint32_t pj = leg << 6;
                st = mix(mix(st, pj), spage == PG_MIX ? cpu_drawn : 0u);
                if (st != status_drawn && spage != PG_SSEQ) { status_drawn = st; draw_status(oct, trk, show_ch, spage, drum, pj, cpu_drawn); }
            }
        }
        if (!pedal_mode) {
            uint32_t mode = syn_edit ? 1u : drum_edit ? 2u : live_mode ? 3u : 0u, c = trk < NSYN ? cur[trk] : 0u;
            st = mix(mix(mix(mix(mix(mix(mix(mode, trk), set.bpm), run), (uint32_t)(oct + 2)), len), set.swing), trk_mute >> trk & 1u);
            if (mode == 0u && run) st = mix(st, 16u + (clk_step / 4u) % 4u);   /* (the beat light) */
            if (mode == 3u) st = mix(mix(mix(st, live_rec), c), dpage);   /* (live: REC, the cursor, the page) */
            if (mode == 1u) {
                const uint8_t *nt = step_note(trk, pt, c);
                uint32_t i;
                st = mix(mix(st, c), *step_len(trk, pt, c));
                for (i = 0; i < STEP_NN(trk); i++) st = mix(st, nt[i]);
            }
            if (mode == 2u) st = mix(st, dpage);
            if (mode) st = mix(st, vpage);
            if (st != info_drawn) { info_drawn = st; draw_info(trk, mode, c, vpage, oct); }
        }
        idle_ok = 0;

        if (dirty && quiet() && fm1_ms - t_change > 2000u) {   /* 2 s after the change, when silent (an erase stalls the sound) */
            set.mute = (uint8_t)trk_mute;
            song_save();
            dirty = 0;
        }
    }
}
