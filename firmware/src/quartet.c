/* SPDX-License-Identifier: GPL-3.0-only */
/* QUARTET: a 4-track groovebox for the M-VAVE FM-1, built on Dinghy's platform (how to play it: docs/USER_GUIDE.md; how
 * it was planned and built: docs/development/SPEC.md, docs/development/GROOVEBOX_PLAN.md). Four tracks, each with 10
 * patterns (A-J) of 16-64 steps, on one master clock; a 2-oscillator synth engine; tracks 1-3 synth slots, each LEAD, BASS
 * or PAD; 72 sounds and 18 drum kits in banks, and a USER bank; four pedal slots, an arpeggiator, a mixer; step and live recording; SUPER SEQ songs; 4 save slots.
 * Everything below this file (platform.c) is done: updates, the USB rescue, MIDI (USB, the TRS jack), USB audio,
 * keys, knobs, LEDs, the screen, flash storage.
 *
 * The modules (each its own file, included below):
 *   gb_clock.h  the master clock: counts samples in the audio interrupt, steps every 16th note, no drift;
 *               moves each track through its pattern and switches patterns when they come round
 *   gb_drums.h  track 4 DRUMS: KICK, SNARE, HAT, OPEN HAT, CLAP, PERC (synthesised; pitch, decay, level each)
 *   gb_synth.h  tracks 1-3, each a LEAD (4 voices), BASS (1 voice) or PAD (4 voices), 9 voices shared: 2 oscillators
 *               (sine, triangle, saw, square, noise, FM, pulse, sync), resonant filter, ADSR, LFO, glide; the sound
 *               library (banks ORIGINAL, CLASSIC, EXPERIMENTAL, USER)
 *   gb_fx.h     the pedals: distortion and phaser per track; reverb, delay, chorus shared (sends)
 *
 * Sound (outside edit mode): SELECT picks the chosen track's sound, a click each through every sound of the slot's
 * type, bank after bank (no test note; the library: gb_synth.h SND_LEAD / SND_BASS / SND_PAD, gb_drums.h KITS):
 *   ORIGINAL (ids 0-9, every older save's), CLASSIC (32-39), EXPERIMENTAL (64-69), USER (96-103, set.mu.ub)
 *   DRUMS  ORIGINAL (0-9), ELECTRONIC (10-13), EXPERIMENTAL (14-17), USER (96-99)
 * (its name shows big). Home: KNOB1 tempo (KNOB2-4 nothing: the sound is EDIT's). EDIT: the SOUND page, the sound in
 * depth (EDIT again: the signal chain, again: home; HOME: home; ALGORITHM another track):
 *   SELECT the page: INSTR a menu: type (LEAD/PAD/BASS), bank, sound (the bank's listed), USER slot (REC twice: save)
 *                    MAIN glide, cutoff, resonance, filter envelope   OSC wave 1, wave 2, osc 2 tuning, mix
 *                    ENV attack, decay, sustain, release              LFO rate, depth, target (pitch/filter/amp/PWM), wave
 *   DRUMS  KIT (bank, kit, USER slot; REC twice: save), then a page per drum: KNOB1-3 pitch, decay, level
 * The signal chain (EDIT twice): the sound, the four pedal slots in order (the chosen one open), OUT (the level).
 *   SELECT the slot, PRESETS another pedal (the whole library, category by category), KNOB1 on / off, KNOB2-4 its knobs
 * The rules on every page: ALGORITHM the track; PRESETS the pattern (or the page's own: pedal, song); SELECT moves
 * within the page, never off it; KNOB1 a page's on / off switch; a tap switches / opens, a hold (0.5 s) the other;
 * REC twice confirms (save, copy, clear all); a page's own button again moves on or closes it; HOME: home
 *
 * Pedals (gb_fx.h), for the chosen track: four slots (FX SEL ENV LFO), each holding any pedal of the library. A tap
 * on a top button switches its pedal on / off ("GLITCH ON" big on top; lit = on); held 0.5 s it shows the pedal (the
 * track's name above it, the pedal's category beside it, the ON lamp, three knobs; HOME goes back). There SELECT picks
 * the category (DRIVE, FILTER, MODULATION, SPACE, PITCH, OTHER), PRESETS the pedal in it (a click each; a pedal in
 * another of the track's slots is not offered, nor GLITCH beside GHOST: ped_free()); KNOB1 switches it on (a click
 * clockwise) / off (a click back), KNOB2-4 turn its knobs. (EDIT's slot is unused: EDIT is the sound editor.)
 * The space pedals (reverb, delay, tape echo, shimmer, reverse delay, spring reverb) are one each, shared: a track's
 * first knob is what it sends. STOP silences every pedal (gb_fx.h fx_flush).
 * A pedal that would overload the chip is refused: "CPU FULL" (ped_fits()). New: reverb, delay, distortion,
 * chorus in the four slots.
 *   GLO   a tap: the MIXER (again: the KEY page, again: home): four channel strips; KNOB1-4 the volume of LEAD, PAD,
 *         BASS, DRUMS (100 = as is); there (only), FX / SEL / ENV / LFO mute LEAD / PAD / BASS / DRUMS, lit = playing.
 *         The KEY page (Conductor's): SELECT the key (sharps / flats), KNOB1 LOCK (the keys play only its notes, one
 *         off it the note below), KNOB2 LIGHTS (its notes glow on the keys).
 *         Held: BEAT REPEAT, the slice playing goes round until let go (then the music is where it would have been);
 *         meanwhile the white keys 1-5 choose the slice: 1/2 bar, 1/4, 1/8 (to start), 1/16, 1/32
 *   ARP   (outside edit mode) as a pedal's button: a tap turns the chosen synth track's arpeggiator on / off ("ARP ON"),
 *         held 0.5 s it shows the ARPEGGIATOR;
 *         KNOB1 on / off (a click clockwise / back), KNOB2 the rate 1/4 .. 1/32, KNOB3 octaves 1-4, KNOB4 direction
 *         UP / DOWN / RANDOM (a click a step).
 *         The keys you hold play one at a time, locked to the beat: each note on the grid of its rate
 *
 * The save page (SAVE; SAVE again: back home): 4 slots, SAVE 1-4, each the whole groovebox (patterns, sounds,
 * pedals, mixer, SUPER SEQ). KNOB1 picks the slot. REC saves to it, PLAY loads it: each asks first ("SAVE 2?", the
 * button blinks); the same button again within 3 s does it, any other button or 3 s cancels. Playing stops first
 * (writing the flash would stutter the sound). A load is checked whole before it replaces your work ("LOAD FAILED"
 * leaves everything as it was). Your current work is also kept by itself (saved when stopped and quiet), so
 * switching off loses nothing.
 *
 * Playing:
 *   ALGORITHM  choose the track: a click right the next one, left the one before (LEAD, PAD, BASS, DRUMS, round)
 *   PRESETS    choose the track's pattern A-J, a click each. While playing it is queued ("A>C") and starts when the playing
 *              pattern comes round to its step 1; stopped, at once.
 *              Hold ARP and turn PRESETS: copy the pattern into the next letter(s)
 *              Hold HOME and turn PRESETS: the MIDI channel (OMNI, 1-16)
 *   keys       play the chosen track live, also while the patterns play (DRUMS: the first 6 white keys)
 *   HOME       always home (the sequencer screen). (Muting: on the mixer)
 *
 * SUPER SEQ (SEQ from the sequencer: SEQ twice from home; SEQ again or HOME: home): songs made of your patterns. A row a track (ALGORITHM chooses it), 8 boxes each; each box
 * plays its pattern once, then the next box, the 8 round and round. A blank box goes on with the pattern before it:
 * A...B.A. plays AAAABBAA. X is a rest (blanks after it rest too); blanks at the start rest (the track comes in
 * later). New: box 1 = A, the rest blank.
 *   KNOB1  SUPER SEQ on / off. On, it plays the songs (each track starts at box 1 when its pattern comes round,
 *          or on PLAY); off, the patterns chosen with PRESETS play as ever
 *   KNOB2 (or SELECT)  the box   KNOB3  its pattern: blank, A-J, X (rest)   KNOB4  the song's length, 1-8 boxes
 *   PRESETS  the song A-J (10 songs); ARP held + PRESETS: copy the song into the next letter
 *   (Elsewhere, under SUPER SEQ, PRESETS still picks the pattern you edit; SUPER shows on the top bar)
 *   PLAY       start / stop all tracks together (from step 1)
 *   OCT- OCT+  the octave of the keys
 *   REC        hold 1 s: "CLEAR ALL?"; REC again (within 3 s): every pattern empty, every
 *              sound, pedal, mixer and SUPER SEQ song as new, pattern A everywhere (the save slots stay). Another
 *              button: no
 *   MASTER     the volume (platform.c)
 *
 * Live recording (REC in the step recorder; SEQ blinks; SEQ: on to SUPER SEQ; HOME: home). The keys
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
 *   SEQ        from home: the step recorder; REC there: live recording (above); SEQ again: SUPER SEQ. HOME: home.
 *              Held 0.5 s (here or live): the COPY page (Conductor's): every track's patterns; ALGORITHM the track and
 *              PRESETS / SELECT the pattern to copy from (synths from synths, drums from drums); REC asks, REC again
 *              copies it into the pattern edited (and goes back); SEQ: back
 *   PRESETS    the pattern A-J, as outside edit mode
 *   KNOB1      the pattern's length in bars, 1-4 (16 / 32 / 48 / 64 steps)
 *   KNOB2      synths: the note length, 1/16 1/8 1/8. 1/4 1/4. 1/2 1/2. 1 (the cursor's note, and what is written next)
 *   KNOB3      the dynamic p mp mf f ACC (the cursor's note and what is written next; drums: new hits, or the hit
 *              whose white key is held)
 *   KNOB4      synths: the cursor's note (chord) up / down a semitone (heard, when stopped); drums: the page of 16
 *              (the info line shows BARS LEN DYN and the note; the bottom line the step and the length in words)
 *   LEAD BASS  step recording: the lit white key is the cursor (the 16 white keys show the cursor's page of 16)
 *   PAD          a key     writes that note on the cursor's step, as long as KNOB2 says; the cursor moves past it
 *                         (LEAD, PAD: hold several keys together for a chord, up to 4; the cursor moves when you let go)
 *                REC       live recording.  Hold REC 1 s: clear the pattern
 *                ARP       a tap: a tie, the note before the cursor lasts one step longer; held 0.5 s: a rest, the
 *                          cursor's step cleared; either way the cursor moves on
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
typedef struct { uint8_t note, len; } mono_t;   /* (older saves: BASS's steps; now the user bank's place, below) */
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
#define PEDAL_SLOTS 5u                          /* the pedal slots of a track (the top buttons; NFX in gb_fx.h) */
#define NSPACE 4u                               /* the shared pedals: reverb, delay, tape echo, shimmer (gb_fx.h) */

#define NUSND 8u                                /* user sounds per synth type (the USER bank) */
#define NUKIT 4u                                /* user drum kits */
typedef struct {                                /* the USER bank: sounds you saved (SOUND page, INSTR), by type */
    snd_t snd[NSYN][NUSND];                     /* (by type: LEAD, BASS, PAD; its preset field: 1 = in use) */
    uint8_t glide[NSYN][NUSND];
    struct { uint8_t used, base; dsnd_t d[NDR]; } kit[NUKIT];   /* a kit: the kit it was made from, its six drums */
} ubank_t;
static struct {
    uint8_t oct, ch, bpm, mute;                 /* mute: bit t = track t muted */
    uint8_t dkit;                               /* the drum kit: 808, CR-78, LM-1, LO-FI (SELECT on DRUMS) */
    uint8_t swing;                              /* 0..127: 50 % (straight) .. 66 % (for now always 0: no knob) */
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
    union {                                     /* (an older save: BASS's patterns here, moved to chord_b on loading;
                                                   since then: the USER bank) */
        mono_t mono[NPAT][NSTEP];
        ubank_t ub;
    } mu;
    chord_t chord[2][NPAT][NSTEP];              /* LEAD (0) and PAD (1): chords of up to 4 */
    /* SUPER SEQ (added after v1.0, so kept last: a v1.0 save is this struct without them, and still loads) */
    uint8_t sseq_on, sseq_sel;                  /* on (KNOB1 on its page); the song played (PRESETS on its page) */
    uint8_t chain[NCHAIN][NTRK][NLINK];         /* each song: per track 8 boxes, 0 = blank (the one before goes on), 1..10 = A..J, 11 = X (rest) */
    uint8_t clen[NCHAIN];                       /* each song's length: boxes 1..8 used (KNOB1 on its page; added later, so last) */
    uint8_t sound_order;                        /* (1: the sounds in QUARTET's order. Only its place matters: a save without
                                                   it is older, and its sound numbers are moved to the new order) */
    /* the pedal library (added later, so last: an older save lacks them and gets its five pedals as they were) */
    uint8_t ped[NTRK][PEDAL_SLOTS];             /* each slot's pedal (PD_DIST ..., gb_fx.h) */
    uint8_t pk[NTRK][PEDAL_SLOTS][3];           /* its three knobs (a shared pedal: [0] is what this track sends) */
    uint8_t spk[NSPACE][2];                     /* the shared pedals' other two knobs */
    uint8_t pad_ped;                            /* (the padding a save of the struct up to here had: its size, 8758) */
    /* each note's dynamic and the key (added after the pedal library, so last: older saves get f and C major) */
    uint8_t sdyn[NSYN][NPAT][NSTEP];            /* a synth step's dynamic: 0 f (as before), 1 mf, 2 mp, 3 p, 4 accent */
    uint8_t ddyn[NPAT][NDR][NSTEP / 2u];        /* a drum hit's (the same codes), two steps a byte: the even one low */
    int8_t key;                                 /* the key: sharps +1..+7 or flats -1..-7, 0 C major (the KEY page) */
    uint8_t keyopt;                             /* bit 0: scale lock (the keys play only the key's notes); bit 1: its
                                                   notes glow on the keys */
    uint8_t spk2[2][2];                         /* (later) REVERSE DELAY's and SPRING REVERB's shared knobs */
    /* (later still) the synth slots' instruments: slots 1-3 each LEAD, BASS or PAD (DRUMS stays slot 4) */
    uint8_t styp[NSYN];                         /* each synth slot's type: T_LEAD, T_BASS or T_PAD (as the slot it began as) */
    uint8_t glide[NSYN];                        /* each synth slot's glide (portamento) 0..127, 0 off */
    chord_t chord_b[NPAT][NSTEP];               /* slot 2's patterns (it began as BASS, one note a step: now chords too) */
} set;
typedef char ubank_fits[sizeof(ubank_t) <= sizeof(mono_t) * NPAT * NSTEP ? 1 : -1];
#define SET_V10 ((uint32_t)((uint8_t *)&set.sseq_on - (uint8_t *)&set))   /* the size of a v1.0 save */
#define SET_SS1 ((uint32_t)((uint8_t *)&set.clen - (uint8_t *)&set))      /* the size of a save from before song lengths */
#define SET_ORD (((uint32_t)((uint8_t *)&set.sound_order - (uint8_t *)&set) + 1u) & ~1u)   /* ... from before the reorder */
#define SET_PED ((uint32_t)((uint8_t *)&set.sdyn - (uint8_t *)&set))   /* ... from before the dynamics and the key */
#define SET_DYN ((uint32_t)((uint8_t *)&set.spk2 - (uint8_t *)&set))   /* ... from before the later shared pedals */
#define SET_SLT ((uint32_t)((uint8_t *)&set.styp - (uint8_t *)&set))   /* ... from before the slots' instruments */
#define TTYPE(t) ((t) < NSYN ? set.styp[t] % NSYN : (uint32_t)T_DRUMS)   /* what a slot plays: T_LEAD, T_BASS, T_PAD, T_DRUMS */
#define STEP_NN(t) (TTYPE(t) == T_BASS ? 1u : 4u)   /* notes a step of slot t plays: a BASS one, LEAD and PAD a chord of 4 */
static chord_t *step_at(uint32_t t, uint32_t p, uint32_t s)   /* each synth slot keeps chords (slot 2 in chord_b) */
{
    p %= NPAT; s %= NSTEP;
    return t == T_BASS ? &set.chord_b[p][s] : &set.chord[t == T_PAD][p][s];
}
static uint8_t *step_note(uint32_t t, uint32_t p, uint32_t s) { return step_at(t, p, s)->note; }   /* [0 .. STEP_NN(t) - 1] */
static uint8_t *step_len(uint32_t t, uint32_t p, uint32_t s) { return &step_at(t, p, s)->len; }
/* a step's dynamic: stored 0 f (so every older note is as loud as before), 1 mf, 2 mp, 3 p, 4 accent. On the knob and
 * the screen they run soft to loud: p, mp, mf, f, ACC (DYN_UI: knob place -> stored code; the same table back) */
static const uint8_t DYN_UI[5] = {3, 2, 1, 0, 4};
static const char *const DYN_NAME[5] = {"f", "mf", "mp", "p", "ACC"};   /* (by the stored code) */
static uint32_t sdyn_get(uint32_t t, uint32_t p, uint32_t s) { return set.sdyn[t % NSYN][p % NPAT][s % NSTEP] % 5u; }
static uint32_t ddyn_get(uint32_t p, uint32_t d, uint32_t s)
{
    uint32_t b = set.ddyn[p % NPAT][d % NDR][(s % NSTEP) / 2u];
    return (s & 1u ? b >> 4 : b & 15u) % 5u;
}
static void ddyn_put(uint32_t p, uint32_t d, uint32_t s, uint32_t v)
{
    uint8_t *b = &set.ddyn[p % NPAT][d % NDR][(s % NSTEP) / 2u];
    *b = (uint8_t)(s & 1u ? (*b & 15u) | (v % 5u) << 4 : (*b & 0xF0u) | (v % 5u));
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
    else if (song_load_n(SET_SLT) == 0) song_loaded = SET_SLT;
    else if (song_load_n(SET_DYN) == 0) song_loaded = SET_DYN;
    else if (song_load_n(SET_PED) == 0) song_loaded = SET_PED;
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


static void sound_reorder(void)                 /* a save from before QUARTET's order: its sounds and kit to their new places */
{
    static const uint8_t SYN_MAP[NSYN][NPRESET] = {{0, 7, 1, 2, 4, 5, 6, 9, 8, 3}, {6, 7, 3, 4, 0, 2, 1, 8, 9, 5}, {6, 0, 3, 1, 2, 4, 7, 8, 9, 5}};   /* LEAD, BASS, PAD: old number -> new */
    static const uint8_t KIT_MAP[10] = {6, 3, 4, 8, 0, 2, 7, 5, 1, 9};
    uint32_t t;
    for (t = 0; t < NSYN; t++) set.snd[t].preset = SYN_MAP[t][set.snd[t].preset % NPRESET];
    set.dkit = KIT_MAP[set.dkit % 10u];
    set.sound_order = 1;
}

static void chain_defaults(void)                /* SUPER SEQ off; every song: each track plays A */
{
    uint32_t c, t, i;
    set.sseq_on = 0; set.sseq_sel = 0;
    for (c = 0; c < NCHAIN; c++) for (t = 0; t < NTRK; t++) for (i = 0; i < NLINK; i++) set.chain[c][t][i] = i == 0u;
    for (c = 0; c < NCHAIN; c++) set.clen[c] = NLINK;
}

static void ped_from_legacy(void)               /* the five pedals every slot had before they could be chosen, with their
                                                   settings (an older save; and the defaults) */
{
    const gfx_t *g = &set.gfx;
    uint32_t t;
    for (t = 0; t < NTRK; t++) {
        const tfx_t *f = &set.fx[t];
        uint8_t *k;
        set.ped[t][E_REV] = PD_REV; k = set.pk[t][E_REV]; k[0] = f->rev_send; k[1] = 0; k[2] = 0;
        set.ped[t][E_DLY] = PD_DLY; k = set.pk[t][E_DLY]; k[0] = f->dly_send; k[1] = 0; k[2] = 0;
        set.ped[t][E_DST] = PD_DIST; k = set.pk[t][E_DST]; k[0] = f->dlevel; k[1] = f->tone; k[2] = f->drive;
        set.ped[t][E_CHO] = PD_CHO; k = set.pk[t][E_CHO]; k[0] = g->crate; k[1] = g->cdepth; k[2] = f->cho_send;
        set.ped[t][E_PHS] = PD_PHS; k = set.pk[t][E_PHS]; k[0] = f->prate; k[1] = f->pdepth; k[2] = f->pfb;
    }
    set.spk[SP_REV][0] = g->rtone; set.spk[SP_REV][1] = g->rtime;
    set.spk[SP_DLY][0] = g->dtime; set.spk[SP_DLY][1] = g->dfb;
    set.spk[SP_TAPE][0] = 2; set.spk[SP_TAPE][1] = 60;   /* (1/8, some repeats) */
    set.spk[SP_SHIM][0] = 90; set.spk[SP_SHIM][1] = 70;
}
static void slots_from_legacy(void)             /* a save from before the slots' instruments: slots 1-3 LEAD, BASS, PAD as
                                                   ever, no glide; BASS's one-note steps into chord_b; the USER bank empty */
{
    uint32_t p, s, t;
    for (p = 0; p < NPAT; p++)
        for (s = 0; s < NSTEP; s++) {
            chord_t *ch = &set.chord_b[p][s];
            ch->note[0] = set.mu.mono[p][s].note; ch->note[1] = ch->note[2] = ch->note[3] = 0;
            ch->len = set.mu.mono[p][s].len;
        }
    { uint8_t *z = (uint8_t *)&set.mu; for (s = 0; s < sizeof set.mu; s++) z[s] = 0; }
    for (t = 0; t < NSYN; t++) { set.styp[t] = (uint8_t)t; set.glide[t] = 0; }
}
static void spk2_defaults(void)                 /* REVERSE DELAY 1/8, some repeats; SPRING REVERB mid tone, mid time */
{
    set.spk2[0][0] = 2; set.spk2[0][1] = 50;
    set.spk2[1][0] = 70; set.spk2[1][1] = 70;
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
    for (i = 0; i < NSYN; i++) { set.styp[i] = (uint8_t)i; sound_load(i, 0); }   /* each slot its first instrument and sound */
    trk_req++;                                  /* (the voices dealt again for them) */
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
    ped_from_legacy();
    spk2_defaults();
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
static uint32_t ui_dyn = 0;                     /* the step recorder's DYN cell: the dynamic shown (a stored code) */
#define NNLEN 8u                                /* the step recorder's note lengths (KNOB2), in steps (16ths) */
static const uint8_t NLEN_ST[NNLEN] = {1, 2, 3, 4, 6, 8, 12, 16};
static const char *const NLEN_SHORT[NNLEN] = {"1/16", "1/8", "1/8.", "1/4", "1/4.", "1/2", "1/2.", "1"};
static const char *const NLEN_NAME[NNLEN] = {"16TH", "8TH", "DOTTED 8TH", "QUARTER", "DOTTED QUARTER", "HALF",
                                             "DOTTED HALF", "WHOLE"};
static uint8_t len_ui = 0;                      /* the length written next (an index of NLEN_ST) */
static uint32_t ui_len = 0;                     /* the LEN cell: the cursor's note's length, or what you write next (index) */
static uint32_t nlen_idx(uint32_t st)           /* a length in steps -> the nearest named one (at or under it) */
{
    uint32_t i = 0;
    while (i + 1u < NNLEN && NLEN_ST[i + 1u] <= st) i++;
    return i;
}
static const char *head_badge;                  /* a badge after the name (a pedal page: its category), or none */
#define KEY0 53u                                /* key 0 = F3 */
static int black(uint32_t note) { return (0x54Au >> (note % 12u)) & 1u; }
static uint32_t key_pc(void) { return (uint32_t)(((int32_t)set.key * 7 % 12 + 12) % 12); }   /* the key's home note (0 C) */
static uint32_t in_key(uint32_t n) { return 0xAB5u >> ((n + 12u - key_pc()) % 12u) & 1u; }   /* in its major scale */
static uint32_t key_lock(uint32_t n) { return in_key(n) || n < 1u ? n : n - 1u; }   /* scale lock: off the key, the note below */
static const char *const TYPE_NAME[NTRK] = {"LEAD", "BASS", "PAD", "DRUMS"};
static const char *const TYPE_SHORT[NTRK] = {"LD", "BS", "PD", "DR"};
static char *put(char *p, const char *a);
static uint32_t slot_nth(uint32_t t)            /* 0: the only one of its kind; else which of its kind (1, 2, 3) */
{
    uint32_t k, n = 0, me = 0, ty = TTYPE(t);
    for (k = 0; k < 4u; k++) {                  /* (in the screen's order: LEAD's slot, PAD's, BASS's) */
        uint32_t s = k < 3u ? (k == 0u ? 0u : k == 1u ? 2u : 1u) : 3u;
        if (TTYPE(s) == ty) { n++; if (s == t) me = n; }
    }
    return n > 1u ? me : 0u;
}
static const char *slot_name(uint32_t t)        /* "LEAD", or "LEAD 2" when there are two */
{
    static char nb[4][10];
    char *p = put(nb[t & 3u], TYPE_NAME[TTYPE(t)]);
    uint32_t k = slot_nth(t);
    if (k) { *p++ = ' '; *p++ = (char)('0' + k); }
    *p = 0;
    return nb[t & 3u];
}
static const char *slot_short(uint32_t t)       /* "LD", or "L1", "L2" ... */
{
    static char sb[4][3];
    uint32_t k = slot_nth(t);
    sb[t & 3u][0] = TYPE_SHORT[TTYPE(t)][0];
    sb[t & 3u][1] = k ? (char)('0' + k) : TYPE_SHORT[TTYPE(t)][1];
    sb[t & 3u][2] = 0;
    return sb[t & 3u];
}
static const char *kit_name_id(uint32_t id)     /* a kit's name (a USER kit: USER n) */
{
    static char un[8] = "USER 1";
    if (id < NKIT) return KIT_NAME[id];
    un[5] = (char)('1' + (id - KUID(0)) % 10u);
    return un;
}
static const char *kit_name(void) { return kit_name_id(set.dkit); }   /* the kit playing */
static const uint8_t SEQ_ORDER[NTRK] = {T_LEAD, T_PAD, T_BASS, T_DRUMS};   /* the order on the screen, the mixer, ALGORITHM */
/* on the mixer only: the buttons that mute LEAD, PAD, BASS, DRUMS (lit = playing) */
static const uint8_t MUTE_BTN[NTRK] = {BTN_FX, BTN_SEL, BTN_ENV, BTN_LFO};
static const char *const MUTE_BTN_NAME[NTRK] = {"FX", "SEL", "ENV", "LFO"};
static const char *const DR_NAME_ALL[NDR] = {"KICK", "SNARE", "HAT", "OPEN HAT", "CLAP", "PERC"};
static const char *const DR_NAME_LM1[NDR] = {"KICK", "SNARE", "HAT", "OPEN HAT", "TOM L", "TOM H"};   /* (the LM-1's samples) */
#define DR_NAME (kit_now() == KIT_LM1 ? DR_NAME_LM1 : DR_NAME_ALL)   /* each drum's name, with this kit */
enum { PG_MAIN, PG_OSC, PG_ENV, PG_LFO, NPAGE,  /* the sound pages (the SOUND page's tabs; home is MAIN) */
       PG_FX = NPAGE,                           /* then the effect pages PG_FX + E_REV .. (the top buttons) */
       PG_PROJ = PG_FX + NFX,                   /* the save page (SAVE) */
       PG_ARP, PG_MIX, PG_SSEQ,                 /* the arpeggiator (ARP), the mixer (GLO), SUPER SEQ (HOME) */
       PG_KEY,                                  /* the key (GLO from the mixer) */
       PG_SOUND,                                /* (then) the signal chain (EDIT twice), the copy page (SEQ held) */
       PG_CHAIN, PG_COPY };                              /* the sound editor (EDIT) */
#define IS_FX_PAGE(p) ((p) >= PG_FX && (p) < PG_PROJ)
/* the top buttons: the four pedal slots (FX SEL ENV LFO); EDIT, the fifth, is the sound editor */
static const uint8_t FX_BTN[NFX] = {BTN_FX, BTN_SEL, BTN_ENV, BTN_LFO, BTN_EDIT};   /* (GLO: the mixer) */

/* the pedal library: each pedal's name, category, colour (dark: black letters on it) and three knobs (KNOB2-4):
 * where each is kept (0-2: the slot's knob; 3, 4: the shared pedal's), how far it goes, where it starts, how shown */
enum { KV_LVL, KV_DLY, KV_R4, KV_R8, KV_GL, KV_OCT, KV_PAT, KV_SRC, KV_SEMI, KV_OCT5, KV_INT, KV_OCT2 };   /* ... -12..+12;
                                                   -2..+2 octaves; the harmony's interval; -1/-2 octaves */   /* 0..20; a note value; 1/4-1/16; 1/8-1/32; 1/32-1/8;
                                                   0/-1/-2; 1-8; KICK/1/4/1/8 */
typedef struct { const char *name; uint8_t src, max, def, kind; } pkdef_t;
typedef struct { const char *name; uint8_t cat, rgb[3], dark; pkdef_t k[3]; } pdef_t;
#define KL(n, s, d) {n, s, 127, d, KV_LVL}
static const pdef_t PDEF[NPD] = {
    {"DISTORTION", 0, {235, 120, 20}, 1, {KL("LEVEL", 0, 32), KL("TONE", 1, 80), KL("DIST", 2, 60)}},
    {"OVERDRIVE", 0, {40, 160, 80}, 1, {KL("LEVEL", 0, 64), KL("TONE", 1, 70), KL("DRIVE", 2, 60)}},
    {"FUZZ", 0, {170, 30, 40}, 0, {KL("LEVEL", 0, 64), KL("TONE", 1, 60), KL("FUZZ", 2, 90)}},
    {"OCTAVE FUZZ", 0, {230, 200, 40}, 1, {KL("LEVEL", 0, 64), KL("TONE", 1, 70), KL("FUZZ", 2, 80)}},
    {"GATED FUZZ", 0, {90, 90, 100}, 0, {KL("LEVEL", 0, 64), KL("GATE", 1, 40), KL("FUZZ", 2, 100)}},
    {"WAVEFOLDER", 0, {20, 140, 150}, 0, {KL("LEVEL", 0, 64), KL("TONE", 1, 90), KL("FOLD", 2, 60)}},
    {"FILTER SEQ", 1, {60, 70, 200}, 0, {{"RATE", 0, 2, 2, KV_R4}, KL("DEPTH", 1, 100), KL("RES", 2, 80)}},
    {"WAH", 1, {30, 30, 34}, 0, {KL("SENS", 0, 80), KL("RANGE", 1, 90), KL("RES", 2, 70)}},
    {"CHORUS", 2, {70, 180, 210}, 1, {KL("RATE", 0, 40), KL("DEPTH", 1, 60), KL("LEVEL", 2, 70)}},
    {"PHASER", 2, {120, 60, 170}, 0, {KL("RATE", 0, 40), KL("DEPTH", 1, 90), KL("FEEDBACK", 2, 60)}},
    {"FLANGER", 2, {200, 200, 210}, 1, {KL("RATE", 0, 30), KL("DEPTH", 1, 90), KL("FEEDBACK", 2, 80)}},
    {"TREMOLO", 2, {150, 40, 110}, 0, {KL("RATE", 0, 60), KL("DEPTH", 1, 90), KL("SHAPE", 2, 0)}},
    {"SLICER", 2, {240, 90, 140}, 1, {{"RATE", 0, 2, 1, KV_R8}, KL("DEPTH", 1, 127), {"PATTERN", 2, 7, 0, KV_PAT}}},
    {"VIBRATO", 2, {80, 120, 60}, 0, {KL("RATE", 0, 60), KL("DEPTH", 1, 50), KL("MIX", 2, 127)}},
    {"UNI", 2, {200, 140, 60}, 1, {KL("RATE", 0, 40), KL("DEPTH", 1, 90), KL("MIX", 2, 64)}},
    {"REVERB", 3, {30, 80, 160}, 0, {KL("LEVEL", 0, 60), KL("TONE", 3, 70), KL("TIME", 4, 70)}},
    {"DELAY", 3, {190, 190, 196}, 1, {{"REPEAT", 3, NDLY - 1u, 3, KV_DLY}, KL("ECHO", 0, 60), KL("INTENSITY", 4, 50)}},
    {"TAPE ECHO", 3, {120, 80, 50}, 0, {KL("ECHO", 0, 70), {"TIME", 3, NDLY - 1u, 2, KV_DLY}, KL("REPEATS", 4, 60)}},
    {"SHIMMER", 3, {170, 200, 240}, 1, {KL("LEVEL", 0, 70), KL("TIME", 3, 90), KL("SHIMMER", 4, 70)}},
    {"CASSETTE", 5, {60, 60, 70}, 0, {KL("WOW", 0, 60), KL("HISS", 1, 40), KL("TONE", 2, 60)}},
    {"VINYL", 5, {110, 30, 30}, 0, {KL("CRACKLE", 0, 60), KL("AGE", 1, 60), KL("LEVEL", 2, 64)}},
    {"GLITCH", 5, {50, 230, 120}, 1, {KL("CHANCE", 0, 50), {"LENGTH", 1, 2, 1, KV_GL}, KL("MIX", 2, 100)}},
    {"UNDERWATER", 5, {20, 90, 140}, 0, {KL("DEPTH", 0, 80), KL("WOBBLE", 1, 50), KL("LEVEL", 2, 64)}},
    {"SYNTH", 5, {130, 200, 40}, 1, {{"OCTAVE", 0, 2, 1, KV_OCT}, KL("TONE", 1, 60), KL("MIX", 2, 80)}},
    {"PUMP", 2, {235, 235, 120}, 1, {KL("DEPTH", 0, 100), KL("RELEASE", 1, 60), {"SOURCE", 2, 2, 0, KV_SRC}}},
    {"BITCRUSHER", 0, {90, 200, 220}, 1, {KL("BITS", 0, 80), KL("RATE", 1, 40), KL("MIX", 2, 127)}},
    {"REVERSE DELAY", 3, {100, 40, 140}, 0, {KL("ECHO", 0, 70), {"TIME", 3, NDLY - 1u, 2, KV_DLY}, KL("REPEATS", 4, 50)}},
    {"SPRING REVERB", 3, {170, 160, 140}, 1, {KL("LEVEL", 0, 70), KL("TONE", 3, 70), KL("TIME", 4, 70)}},
    {"SHIFTER", 4, {240, 110, 70}, 1, {{"SEMI", 0, 24, 12, KV_SEMI}, {"OCTAVE", 1, 4, 3, KV_OCT5}, KL("MIX", 2, 127)}},
    {"HARMONIZER", 4, {60, 150, 110}, 0, {{"INTERVAL", 0, 9, 8, KV_INT}, KL("LEVEL", 1, 90), KL("TONE", 2, 90)}},
    {"LO-FI", 5, {150, 130, 100}, 0, {KL("RATE", 0, 50), KL("NOISE", 1, 40), KL("DIRT", 2, 60)}},
    {"SLOW", 5, {120, 160, 210}, 1, {KL("RISE", 0, 20), KL("DEPTH", 1, 110), KL("SENS", 2, 80)}},
    {"GHOST", 5, {210, 210, 220}, 1, {KL("DELAY", 0, 60), KL("LEVEL", 1, 50), KL("DRIFT", 2, 50)}},
    {"MUSIC BOX", 5, {230, 170, 190}, 1, {KL("DECAY", 0, 70), KL("TONE", 1, 70), KL("MIX", 2, 100)}},
    {"BROKEN CLOCK", 5, {140, 110, 60}, 0, {KL("AMOUNT", 0, 70), KL("CHANCE", 1, 50), KL("HICCUP", 2, 20)}},
    {"SHADOW", 5, {50, 50, 70}, 0, {KL("LEVEL", 0, 60), KL("TONE", 1, 40), {"OCTAVE", 2, 1, 0, KV_OCT2}}},
};
#define NPCAT 6u
static const char *const PCAT_NAME[NPCAT] = {"DRIVE", "FILTER", "MODULATION", "SPACE", "PITCH", "OTHER"};
static uint32_t ped_free(uint32_t t, uint32_t e, uint32_t p)   /* can slot e of track t take pedal p? Not one already in
                                                   another of its slots, nor GLITCH beside GHOST (they share the track's line) */
{
    uint32_t e2;
    for (e2 = 0; e2 < NPED_SLOTS; e2++) {
        uint32_t q = set.ped[t][e2] % NPD;
        if (e2 == e) continue;
        if (q == p || (q == PD_GLITCH && p == PD_GHOST) || (q == PD_GHOST && p == PD_GLITCH)) return 0;
    }
    return 1;
}
static int32_t ped_first(uint32_t t, uint32_t e, uint32_t cat)   /* a category's first pedal free for the slot; -1: none */
{
    uint32_t p;
    for (p = 0; p < NPD; p++) if (PDEF[p].cat == cat && ped_free(t, e, p)) return (int32_t)p;
    return -1;
}
static uint32_t ped_step(uint32_t t, uint32_t e, uint32_t p, int32_t stp)   /* stp free pedals on in p's category, round */
{
    uint32_t cat = PDEF[p % NPD].cat, k;
    while (stp > 0) { for (k = 0; k < NPD; k++) { p = (p + 1u) % NPD; if (PDEF[p].cat == cat && ped_free(t, e, p)) break; } stp--; }
    while (stp < 0) { for (k = 0; k < NPD; k++) { p = (p + NPD - 1u) % NPD; if (PDEF[p].cat == cat && ped_free(t, e, p)) break; } stp++; }
    return p;
}

static uint32_t ped_any_step(uint32_t t, uint32_t e, uint32_t p, int32_t stp)   /* stp free pedals on through the whole
                                                   library, category after category, round (the chain's PRESETS) */
{
    uint8_t ord[NPD];
    uint32_t n = 0, ca, q, at = 0, k;
    for (ca = 0; ca < NPCAT; ca++) for (q = 0; q < NPD; q++) if (PDEF[q].cat == ca) ord[n++] = (uint8_t)q;
    for (k = 0; k < n; k++) if (ord[k] == p % NPD) at = k;
    while (stp != 0) {
        int32_t d = stp > 0 ? 1 : -1;
        for (k = 0; k < n; k++) { at = (uint32_t)(((int32_t)at + d + (int32_t)n) % (int32_t)n); if (ped_free(t, e, ord[at])) break; }
        stp -= d;
    }
    return ord[at];
}

/* each pedal's three knobs (KNOB2-4): where the value is kept, how far it goes */
typedef struct { const char *name; uint8_t *v; uint8_t max; } pknob_t;
static void pedal_knobs(uint32_t trk, uint32_t e, pknob_t k[3])
{
    uint32_t p = set.ped[trk][e] % NPD, i;
    for (i = 0; i < 3u; i++) {
        const pkdef_t *d = &PDEF[p].k[i];
        k[i].name = d->name; k[i].max = d->max;
        k[i].v = d->src < 3u ? &set.pk[trk][e][d->src] : &spk_of((uint32_t)sp_of(p))[d->src - 3u];
    }
}
/* what the pedals switched on cost: tenths of a microsecond per audio block, measured on a PC (fx_process timed,
 * each pedal in 20 slots; a shared pedal once, plus a little per send). The five original pedals on all four tracks
 * come to 156 (on the FM-1 ~40 % of the CPU, on top of the instruments); more than PED_BUDGET is refused */
static const uint8_t PD_COST[NPD] = {5, 8, 4, 5, 6, 4, 17, 16, 7, 18, 5, 2, 4, 5, 19, 1, 1, 1, 1, 9, 5, 3, 6, 4, 3,
                                     2, 1, 1, 9, 10, 3, 4, 5, 10, 1, 10};   /* (MUSIC BOX: with four notes ringing) */
static const uint8_t SP_COST[NSPACE_ALL] = {19, 9, 17, 18, 6, 15};   /* reverb, delay, tape echo, shimmer, reverse, spring */
#define PED_BUDGET 170u
static int ped_fits(void)
{
    uint32_t t, e, c = 0, sp = 0;
    for (t = 0; t < NTRK; t++)
        for (e = 0; e < NPED_SLOTS; e++)
            if (set.fx[t].on >> e & 1u) {
                uint32_t p = set.ped[t][e] % NPD;
                c += PD_COST[p];
                if (PD_SPACE(p)) sp |= 1u << sp_of(p);
            }
    for (e = 0; e < NSPACE_ALL; e++) if (sp >> e & 1u) c += SP_COST[e];
    return c <= PED_BUDGET;
}

static void ped_set(uint32_t trk, uint32_t e, uint32_t p)   /* a slot gets another pedal: its own knobs start fresh */
{
    uint32_t i;
    set.ped[trk][e] = (uint8_t)p;
    for (i = 0; i < 3u; i++) if (PDEF[p].k[i].src < 3u) set.pk[trk][e][PDEF[p].k[i].src] = PDEF[p].k[i].def;
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
    return rgbk(trk_mute >> t & 1u ? MUTE_RGB : TRK_RGB[TTYPE(t)], k);
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
    if (head_badge) {                           /* a pedal page: the pedal's category */
        int32_t bw = text_w(&FONT_S, head_badge) + 12;
        if (bx + bw <= lim) {
            cv_rrect(bx, 11, bw, 16, 3, C_KEYCAP);
            cv_text_bg(bx + 6, 11, &FONT_S, head_badge, C_TXT, C_KEYCAP);
            bx += bw + 6;
        }
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
        head_text(trk < NSYN ? sound_name(TTYPE(trk), set.snd[trk].preset) : kit_name(), C_WHITE, trk_c(trk, 100));
        return;
    }
    head_track(trk, drum_edit ? DR_NAME[drum] : slot_name(trk), 1);
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

static uint32_t kit_list(uint8_t *ids);
static uint32_t id_bank(uint32_t t, uint32_t id);
static uint32_t snd_pg;                         /* the sound editor's page: 0 INSTR (drums: KIT), then MAIN OSC ENV LFO (drums: a drum each) */
static uint32_t usr_slot, usr_ask, usr_t;       /* INSTR: the USER slot to save to; "sure?" (REC again within 3 s) */
static uint32_t chain_sl;                       /* the signal chain (EDIT twice): the pedal slot chosen, 0..3 */
static uint32_t cp_trk, cp_pat, cp_ask, cp_t, cp_live;   /* the copy page (SEQ held): the pattern to copy from; "sure?"
                                                   (REC again within 3 s); back to the live page (else the step recorder) */
static const char *const WAVE_NAME[NWAVE] = {"SIN", "TRI", "SAW", "SQR", "NOISE", "FM", "PULSE", "SYNC"};
static const char *const LTGT_NAME[NLTGT] = {"PITCH", "FILT", "AMP", "PWM"};

/* the bottom bar: the sound page and what KNOB1-4 set on it (drums: the chosen drum); HOME + PRESETS: octave, MIDI;
 * a dot in the track's colour when there is room */
static void draw_status(int32_t oct, uint32_t trk, uint32_t show_ch, uint32_t page, uint32_t drum, uint32_t proj,
                        uint32_t cpu)   /* proj: legend (1 synth edit, 2 drum edit, 3 / 4 live synth / drums) << 6; cpu: % (> 100: no meter, the PC) */
{
    char s[40], *p = s;
    const snd_t *sp = &set.snd[trk < NSYN ? trk : 0];
    if (show_ch) {
        p = put(p, "OCT "); *p++ = oct < 0 ? '-' : '+'; *p++ = (char)('0' + (oct < 0 ? -oct : oct));
        p = put(p, "  MIDI ");
        if (set.ch) { p = put(p, "CH"); p = put_num(p, set.ch, 1); } else p = put(p, "OMNI");
    } else if (proj >> 6) {                     /* the SEQ (edit) screen: what each knob does */
        static const char *const LEG[6] = {"", "", "K1:BARS  K3:DYN  K4:PAGE",
                                           "K1:BARS K2:STEP K3:PG REC:REC", "K1:BARS  K3:PAGE  REC:RECORD",
                                           "REC AGAIN: CLEAR EVERYTHING"};
        if ((proj >> 6) % 6u == 1u) {           /* synth: STEP 17/64  QUARTER (K1 BARS K2 LEN K3 DYN K4 NOTE on the info line) */
            uint32_t L = pat_len(trk, set.sel[trk] % NPAT);
            p = put(p, "STEP "); p = put_num(p, drum + 1u, 1); *p++ = '/'; p = put_num(p, L, 1);
            p = put(p, "  K2: "); p = put(p, NLEN_NAME[ui_len % NNLEN]);
        } else p = put(p, LEG[(proj >> 6) % 6u]);
    } else if (page == PG_KEY) {                /* the key page */
        p = put(p, "SELECT:KEY  K1:LOCK  K2:LIGHTS");
    } else if (page == PG_PROJ) {               /* the save page: how to work it */
        p = put(p, "K1:SLOT  REC:SAVE  PLAY:LOAD");
    } else if (page == PG_ARP) {                /* the arpeggiator: how to work it */
        p = put(p, trk < NSYN ? "TAP ARP OR K1: ON/OFF" : "HOME:BACK");
    } else if (page == PG_MIX) {                /* the mixer */
        p = put(p, "FX, SEL, ENV, LFO: MUTE TRACKS");
    } else if (page == PG_SOUND) {              /* the sound editor */
        if (snd_pg == 0u && usr_ask) { p = put(p, "REC AGAIN: SAVE TO USER "); *p++ = (char)('1' + usr_slot); }
        else if (snd_pg == 0u) p = put(p, "SELECT:PAGE  REC TWICE:SAVE");
        else p = put(p, "SELECT:PAGE  EDIT:CHAIN");
    } else if (page == PG_CHAIN) {              /* the signal chain */
        p = put(p, "SELECT:SLOT  PRESETS:PEDAL");
    } else if (page == PG_COPY) {               /* the copy page */
        p = put(p, cp_ask ? "REC AGAIN: COPY" : "REC TWICE:COPY  SEQ:BACK");
    } else if (IS_FX_PAGE(page)) {              /* a pedal: the values are on the pedal; here, how to work it */
        p = put(p, "SELECT:TYPE  PRESETS:PEDAL");   /* (KNOB1 or holding the button: on / off) */
    } else if (trk == T_DRUMS) {                /* home: the kit, its bank */
        p = put(p, kit_name()); p = put(p, "  "); p = put(p, KBANK_NAME[kit_bank(set.dkit) % NBANK]);
    } else {                                    /* home: the sound, its bank */
        p = put(p, sound_name(TTYPE(trk), sp->preset)); p = put(p, "  "); p = put(p, BANK_NAME[id_bank(trk, sp->preset) % NBANK]);
    }
    *p = 0;
    bottom_bar(s, page == PG_MIX ? C_TXT : trk_c(trk, 100));
}

/* the info line: little panels. Home: play / stop, the tempo, four beat lights, the octave. SEQ: one cell above each
 * knob: the step recorder BARS, LEN (the note's), DYN, the note; live BARS, STEP, PG, REC */
static uint32_t syn_covers(uint32_t t, uint32_t pt, uint32_t s, uint32_t len);
static void info_cell(int32_t x, const char *lab, const char *val, uint16_t lc, uint16_t vc)
{
    int32_t tw = (lab[0] ? text_w(&FONT_S, lab) + 4 : -4) + text_w(&FONT_S, val), tx = x + 30 - tw / 2;
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
    if (mode) {                                 /* the step recorder: BARS, LEN, DYN, NOTE (drums: BARS, DYN, PG); the
                                                   live page: BARS, STEP, PG, REC */
        uint32_t sy = mode == 1u || (mode == 3u && trk < NSYN), pg = page;   /* (page: the one shown) */
        *put_num(v, len / PAGE, 1) = 0;
        info_cell(0, "BARS", v, C_SUB, col);
        if (mode == 1u) {                       /* synth: KNOB2 the length, KNOB3 the dynamic, KNOB4 the note */
            const uint8_t *nt = step_note(trk, set.sel[trk] % NPAT, cur);
            info_cell(60, "LEN", NLEN_SHORT[ui_len % NNLEN], C_SUB, col);
            info_cell(120, "DYN", DYN_NAME[ui_dyn % 5u], C_SUB, col);
            if (nt[0]) { *put_note(v, nt[0]) = 0; info_cell(180, "", v, C_SUB, col); }
            else info_cell(180, "", syn_covers(trk, set.sel[trk] % NPAT, cur, len) ? "HELD" : "REST", C_DIMC, C_DIMC);
        } else if (mode == 2u) {                /* drums: KNOB3 the dynamic, KNOB4 the page */
            info_cell(60, "", "--", C_DIMC, C_DIMC);
            info_cell(120, "DYN", DYN_NAME[ui_dyn % 5u], C_SUB, col);
            p = put_num(v, pg + 1u, 1); *p++ = '/'; *put_num(p, len / PAGE, 1) = 0;
            info_cell(180, "PG", v, C_SUB, col);
        } else {
            if (sy) { *put_num(v, cur + 1u, 2) = 0; info_cell(60, "STEP", v, C_SUB, col); }
            else info_cell(60, "STEP", "--", C_DIMC, C_DIMC);   /* (drums live: KNOB2 has nothing to do) */
            p = put_num(v, pg + 1u, 1); *p++ = '/'; *put_num(p, len / PAGE, 1) = 0;
            info_cell(120, "PG", v, C_SUB, col);
            info_cell(180, "REC", live_rec ? "ON" : "OFF", live_rec ? C_MUTE : C_SUB, live_rec ? C_MUTE : C_DIMC);
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
        cv_text_bg(17 - text_w(&FONT_S, slot_short(t)) / 2, y + 1, &FONT_S, slot_short(t), me ? C_BLACK : col, blk);
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
    const char *const *DR_SHORT = kit_now() == KIT_LM1 ? DR_SHORT_LM1 : DR_SHORT_ALL;
    uint32_t d, s;
    uint16_t col = trk_c(T_DRUMS, 100), dim = trk_c(T_DRUMS, 22);
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
            {
                static const uint8_t PCT[5] = {100, 82, 64, 46, 100};   /* f mf mp p ACC */
                uint32_t dy = on ? ddyn_get(pt, d, st) : 0u;
                c = on ? trk_c(T_DRUMS, PCT[dy] * (me ? 100u : 62u) / 100u) : s % 4u == 0u ? C_EMPTY4 : C_EMPTY;
                if (st == play) c = on ? C_WHITE : C_HEADC;
                cv_rrect(28 + (int32_t)s * 13, y + 4, 11, 14, 2, c);
                if (dy == 4u) cv_rect(31 + (int32_t)s * 13, y + 6, 5, 2, st == play ? C_BLACK : C_WHITE);   /* (an accent) */
            }
        }
    }
    cv_blit(0, Y_VIEW);
}

/* a synth: a piano roll of the cursor's page, higher notes higher (2 px a semitone), a bar as long as the note; a little
 * keyboard on the left, a line at every C, the cursor's column in the track's colour */
static void draw_roll(uint32_t t, uint32_t pt, uint32_t cur, uint32_t play, uint32_t len, uint32_t page)   /* cur ~0: none */
{
    uint32_t s, i;
    int32_t lo = TTYPE(t) == T_BASS ? 24 : TTYPE(t) == T_LEAD ? 60 : 48, k;   /* the lowest note shown (by what it plays) */
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
            {
                static const uint8_t PCT[5] = {100, 82, 64, 46, 100};   /* f mf mp p ACC */
                uint32_t dy = sdyn_get(t, pt, st);
                cv_rrect(x, y, w, 6, 2, st == cur ? C_WHITE : dy ? trk_c(t, PCT[dy]) : col);
                if (dy == 4u) cv_rect(x + 1, y, w - 2 > 0 ? w - 2 : 1, 2, st == cur ? C_BLACK : C_WHITE);   /* (an accent) */
            }
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

/* ---- the pedal screen: the chosen track in the top bar (the pedal's category beside it), under it the pedal */
static void draw_pedal_head(uint32_t trk, int32_t e)   /* e < 0: no badge (the arpeggiator) */
{
    head_badge = e >= 0 ? PCAT_NAME[PDEF[set.ped[trk][e] % NPD].cat % NPCAT] : 0;
    head_track(trk, slot_name(trk), 0);
    head_badge = 0;
}

static void ped_vals(uint32_t trk, uint32_t e, pknob_t pk[3], char val[3][8])   /* the three knobs, their values as shown
                                                   (0..127 ones as 0..20; note values, octaves, intervals by name) */
{
    static const char *const R4[3] = {"1/4", "1/8", "1/16"}, *const R8[3] = {"1/8", "1/16", "1/32"};
    static const char *const GL[3] = {"1/32", "1/16", "1/8"}, *const OC[3] = {"0", "-1", "-2"};
    static const char *const SRC[3] = {"KICK", "1/4", "1/8"};
    static const char *const OC5[5] = {"-2", "-1", "0", "+1", "+2"}, *const OC2[2] = {"-1", "-2"};
    static const char *const INTV[10] = {"-OCT", "-5TH", "-4TH", "-3RD", "-m3", "+m3", "+3RD", "+4TH", "+5TH", "+OCT"};
    const pdef_t *d = &PDEF[set.ped[trk][e] % NPD];
    uint32_t k;
    pedal_knobs(trk, e, pk);
    for (k = 0; k < 3u; k++) {
        uint32_t v = *pk[k].v, m = pk[k].max ? pk[k].max : 1u;
        const char *s = 0;
        switch (d->k[k].kind) {
        case KV_DLY: s = DLY_NAME[v % NDLY]; break;
        case KV_R4: s = R4[v % 3u]; break;
        case KV_R8: s = R8[v % 3u]; break;
        case KV_GL: s = GL[v % 3u]; break;
        case KV_OCT: s = OC[v % 3u]; break;
        case KV_SRC: s = SRC[v % 3u]; break;
        case KV_OCT5: s = OC5[v % 5u]; break;
        case KV_OCT2: s = OC2[v % 2u]; break;
        case KV_INT: s = INTV[v % 10u]; break;
        case KV_SEMI: { char *pp = val[k]; int32_t sv = (int32_t)(v % 25u) - 12;
            if (sv > 0) *pp++ = '+'; else if (sv < 0) *pp++ = '-';
            *put_num(pp, (uint32_t)(sv < 0 ? -sv : sv), 1) = 0; break; }
        default: break;
        }
        if (s) *put(val[k], s) = 0;
        else if (d->k[k].kind == KV_SEMI) {}
        else *put_num(val[k], d->k[k].kind == KV_PAT ? v % (m + 1u) + 1u : pedal_level(v), 1) = 0;
    }
}
static void draw_pedal(uint32_t trk, uint32_t e)
{
    const pdef_t *d = &PDEF[set.ped[trk][e] % NPD];
    char val[3][8];
    kview_t kv[3];
    pknob_t pk[3];
    uint32_t k;
    ped_vals(trk, e, pk, val);
    for (k = 0; k < 3u; k++) kv[k] = (kview_t){pk[k].name, *pk[k].v, pk[k].max ? pk[k].max : 1u, val[k]};
    draw_stomp(d->rgb, d->dark, d->name, set.fx[trk].on >> e & 1u, kv, 3, 0);
}

/* ---- the signal chain (EDIT twice): the sound, the four pedal slots in order (the chosen one open with its three
 * knobs, KNOB2-4; KNOB1 its switch; PRESETS another pedal), then the track's level. SELECT the slot */
static void draw_chain(uint32_t trk, uint32_t sl)
{
    static const char *const SLN[NPED_SLOTS] = {"FX", "SEL", "ENV", "LFO"};
    const uint8_t *rgb = trk_mute >> trk & 1u ? MUTE_RGB : TRK_RGB[TTYPE(trk)];
    uint16_t col = rgbk(rgb, 100), ink = rgbk(rgb, 20);
    const char *sn = trk < NSYN ? sound_name(TTYPE(trk), set.snd[trk].preset) : kit_name();
    const char *bn = trk < NSYN ? BANK_NAME[id_bank(trk, set.snd[trk].preset) % NBANK] : KBANK_NAME[kit_bank(set.dkit) % NBANK];
    uint32_t half, e, k;
    char val[3][8];
    pknob_t pk[3];
    for (half = 0; half < 2u; half++) {
        int32_t y;
        cv_begin(240, H_PEDAL / 2, C_BLACK);
        cv_oy = -(int32_t)half * (H_PEDAL / 2);
        cv_rrect(4, 0, 232, 20, 4, col);        /* the sound, at the head of the chain */
        cv_text_bg(12, 2, &FONT_S, sn, C_BLACK, col);
        cv_text_bg(228 - text_w(&FONT_S, bn), 2, &FONT_S, bn, ink, col);
        y = 23;
        for (e = 0; e < NPED_SLOTS; e++) {
            const pdef_t *d = &PDEF[set.ped[trk][e] % NPD];
            const char *cn = PCAT_NAME[d->cat % NPCAT];
            uint32_t on = set.fx[trk].on >> e & 1u, me = e == sl;
            int32_t h = me ? 52 : 18;
            uint16_t bg = me ? C_PANEL2 : C_PANEL;
            cv_rect(119, y - 3, 2, 3, C_DIMC);  /* (the cable) */
            if (me) { cv_rrect(4, y, 232, h, 5, col); cv_rrect(6, y + 2, 228, h - 4, 4, bg); }
            else cv_rrect(4, y, 232, h, 4, bg);
            disc(15, y + 9, 3, on ? C_MUTE : C_DIMC);   /* the lamp: on */
            cv_text_bg(23, y + 1, &FONT_S, SLN[e], C_DIMC, bg);
            cv_text_bg(58, y + 1, &FONT_S, d->name, me ? col : on ? C_TXT : C_SUB, bg);
            if (58 + text_w(&FONT_S, d->name) + 10 < 229 - text_w(&FONT_S, cn))
                cv_text_bg(229 - text_w(&FONT_S, cn), y + 1, &FONT_S, cn, C_DIMC, bg);
            if (me) {                           /* its three knobs: name, value */
                ped_vals(trk, e, pk, val);
                for (k = 0; k < 3u; k++) {
                    int32_t cx = 44 + (int32_t)k * 76;
                    cv_text_bg(cx - text_w(&FONT_S, pk[k].name) / 2, y + 18, &FONT_S, pk[k].name, C_SUB, bg);
                    cv_text_bg(cx - text_w(&FONT_S, val[k]) / 2, y + 33, &FONT_S, val[k], on ? C_TXT : C_SUB, bg);
                }
            }
            y += h + 3;
        }
        cv_rect(119, y - 3, 2, 3, C_DIMC);
        cv_rrect(4, y, 232, 18, 4, C_PANEL);    /* OUT: the track's level (the mixer's) */
        cv_text_bg(12, y + 1, &FONT_S, "OUT", C_SUB, C_PANEL);
        cv_rrect(58, y + 7, 170, 5, 2, C_KEYCAP);
        cv_rrect(58, y + 7, (int32_t)((set.vol[trk] & 127u) * 170u / 127u) + 1, 5, 2, col);
        cv_oy = 0;
        cv_blit(0, Y_PEDAL + half * (H_PEDAL / 2));
    }
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
            cv_text_bg(cx - text_w(&FONT_S, slot_name(t)) / 2, 2, &FONT_S, slot_name(t), C_BLACK, col);
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
    info_cell(0, "SEQ", on ? "ON" : "OFF", C_SUB, on ? C_PLAYG : C_DIMC);
    v[0] = (char)('1' + cur % NLINK);
    info_cell(60, "BOX", v, C_SUB, col);
    v[0] = c[sel][cur % NLINK] == CH_REST ? 'X' : c[sel][cur % NLINK] ? (char)('A' + c[sel][cur % NLINK] - 1) : '-';
    info_cell(120, "PAT", v, C_SUB, col);
    v[0] = (char)('0' + cl);
    info_cell(180, "LEN", v, C_SUB, C_TXT);
    cv_blit(0, Y_INFO);
    cv_begin(240, H_VIEW, C_BLACK);             /* the rows */
    for (r = 0; r < NTRK; r++) {
        uint32_t t = SEQ_ORDER[r], me = t == sel, last = 0;
        int32_t y = (int32_t)r * 32;
        uint16_t tc = trk_c(t, 100), blk = me ? tc : trk_c(t, 24);
        cv_rrect(0, y + 1, 240, 30, 5, me ? C_PANEL2 : C_PANEL);
        cv_rrect(0, y + 1, 34, 30, 5, blk);
        cv_text_bg(17 - text_w(&FONT_S, slot_short(t)) / 2, y + 1, &FONT_S, slot_short(t), me ? C_BLACK : tc, blk);
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
    bottom_bar("SELECT:BOX  PRESETS:SONG", C_TXT);
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

/* ---- the KEY page (GLO from the mixer; Conductor's): the key, its relative minor, the lock and the lights; one
 * octave of keyboard from the key's home note, its notes lit and named, the notes held gold. SELECT the key (a sharp or flat a click), KNOB1 the lock, KNOB2 the lights */
static const char *const KMAJ[15] = {"C-FLAT", "G-FLAT", "D-FLAT", "A-FLAT", "E-FLAT", "B-FLAT", "F", "C", "G", "D", "A",
                                      "E", "B", "F-SHARP", "C-SHARP"};
static const char *const KMIN[15] = {"A-FLAT", "E-FLAT", "B-FLAT", "F", "C", "G", "D", "A", "E", "B", "F-SHARP", "C-SHARP",
                                      "G-SHARP", "D-SHARP", "A-SHARP"};
static void head_key(void)                      /* KEY, the stripe in the four track colours */
{
    uint32_t r;
    cv_begin(240, H_BIG, C_BLACK);
    cv_rrect(0, 0, 240, H_BIG, 6, C_PANEL);
    for (r = 0; r < NTRK; r++) cv_rrect(6, 5 + (int32_t)r * 7, 4, 6, 2, rgbk(TRK_RGB[SEQ_ORDER[r]], 100));
    cv_text_bg(18, 3, &FONT_L, "KEY", C_TXT, C_PANEL);
    cv_blit(0, Y_BIG);
}
static void sc_sharp(int32_t x, int32_t y, uint16_t c)   /* a sharp, about 7 x 15, its middle at y */
{
    cv_rect(x + 2, y - 7, 1, 14, c); cv_rect(x + 5, y - 8, 1, 14, c);
    cv_rect(x, y - 2, 8, 2, c); cv_rect(x, y + 3, 8, 2, c);
}
static void draw_key(uint32_t held)             /* held: the pitch classes played (bit 0 C) */
{
    static const char *const WN[12] = {"C", "", "D", "", "E", "F", "", "G", "", "A", "", "B"};
    static const char *const SHN[12] = {"", "C#", "", "D#", "", "", "F#", "", "G#", "", "A#", ""};
    static const char *const FLN[12] = {"", "Db", "", "Eb", "", "", "Gb", "", "Ab", "", "Bb", ""};
    int32_t key = set.key, nks = key < 0 ? -key : key, pc = (int32_t)key_pc(), k, top = 80;
    uint32_t half, lock = set.keyopt & 1u, lights = set.keyopt >> 1 & 1u;
    uint16_t gold = rgbk(TRK_RGB[T_LEAD], 100);
    char t[40], *p;
    for (half = 0; half < 2u; half++) {
        cv_begin(240, H_PEDAL / 2, C_BLACK);
        cv_oy = -(int32_t)half * (H_PEDAL / 2);
        cv_rrect(0, 0, 240, H_PEDAL, 8, C_PANEL);
        p = put(t, KMAJ[key + 7]); *put(p, " MAJOR") = 0;
        cv_text_bg(120 - text_w(&FONT_L, t) / 2, 0, &FONT_L, t, gold, C_PANEL);
        p = put(t, KMIN[key + 7]); p = put(p, " MINOR   ");
        if (!nks) p = put(p, "NO SHARPS OR FLATS");
        else { p = put_num(p, (uint32_t)nks, 1); p = put(p, key > 0 ? (nks > 1 ? " SHARPS" : " SHARP") : (nks > 1 ? " FLATS" : " FLAT")); }
        *p = 0;
        cv_text_bg(120 - text_w(&FONT_S, t) / 2, 32, &FONT_S, t, C_SUB, C_PANEL);
        {                                       /* LOCK and LIGHTS: lit when on */
            static const char *const LB[2] = {"LOCK", "LIGHTS"};
            for (k = 0; k < 2; k++) {
                uint32_t on = k ? lights : lock;
                int32_t bx = 34 + k * 92;
                p = put(t, LB[k]); *put(p, on ? " ON" : " OFF") = 0;
                cv_rrect(bx, 51, 80, 18, 4, on ? gold : C_KEYCAP);
                cv_text_bg(bx + 40 - text_w(&FONT_S, t) / 2, 52, &FONT_S, t, on ? C_BLACK : C_SUB, on ? gold : C_KEYCAP);
            }
        }
        {                                       /* the piano: the key's home note to the next (a black home: from the
                                                   white key below); its notes lit and named, the notes held gold */
            int32_t lo = black((uint32_t)pc) ? pc - 1 : pc, hi = black((uint32_t)pc) ? pc + 13 : pc + 12, nw = 0, kw, x, n, wx[16];
            for (n = lo; n <= hi; n++) if (!black((uint32_t)(n + 12) % 12u)) nw++;
            kw = 216 / nw;
            for (n = lo, k = 0; n <= hi; n++) {
                uint32_t cpc = (uint32_t)(n + 12) % 12u, on = in_key(cpc), hit = held >> cpc & 1u;
                if (black(cpc)) continue;
                x = 12 + (216 - nw * kw) / 2 + k * kw; wx[n - lo] = x; k++;
                cv_rect(x, top, kw - 2, 80, hit ? gold : on ? RGB(232, 232, 238) : RGB(96, 96, 106));
                if (on) cv_text_bg(x + kw / 2 - text_w(&FONT_S, WN[cpc]) / 2, top + 58, &FONT_S, WN[cpc], C_BLACK, hit ? gold : RGB(232, 232, 238));
            }
            for (n = lo + 1; n < hi; n++) {
                uint32_t cpc = (uint32_t)(n + 12) % 12u, on = in_key(cpc), hit = held >> cpc & 1u;
                const char *nm = key > 0 ? SHN[cpc] : FLN[cpc];
                if (!black(cpc)) continue;
                x = wx[n - 1 - lo] + kw - 12;
                cv_rect(x, top, 22, 48, hit ? gold : on ? RGB(70, 70, 84) : RGB(14, 14, 18));
                if (on) cv_text_bg(x + 11 - text_w(&FONT_S, nm) / 2, top + 26, &FONT_S, nm, hit ? C_BLACK : gold, hit ? gold : RGB(70, 70, 84));
            }
        }
        cv_oy = 0;
        cv_blit(0, Y_PEDAL + half * (H_PEDAL / 2));
    }
}

/* ---- the SOUND page (EDIT): the chosen instrument's sound in depth. Synths: five pages (SELECT) INSTR (type, bank,
 * sound, USER slot), MAIN (glide, cutoff, resonance, filter envelope), OSC (wave 1, wave 2, its tuning, the mix / FM
 * depth), ENV (attack, decay, sustain, release), LFO (rate, depth, target, wave). Drums: KIT (bank, kit, USER slot),
 * then the drums, each its pitch, decay, level (and the tempo). KNOB1-4 the four knobs; ALGORITHM another
 * instrument; EDIT or HOME back */
static void menu_row(int32_t y, const char *k, const char *lab, const char *val, uint16_t vc, int line)
{                                               /* a menu row: its knob, its name, its value on the right */
    cv_text_bg(8, y, &FONT_S, k, C_DIMC, C_PANEL);
    cv_text_bg(32, y, &FONT_S, lab, C_SUB, C_PANEL);
    cv_text_bg(232 - text_w(&FONT_S, val), y, &FONT_S, val, vc, C_PANEL);
    if (line) cv_rect(6, y + 18, 228, 1, C_KEYCAP);
}
/* INSTR (drums: KIT), the SOUND page's first: a menu, a row a knob. KNOB1 the type, KNOB2 the bank, KNOB3 the sound
 * (the bank's sounds listed, the one playing lit), KNOB4 the USER slot REC (twice) saves to */
static void draw_instr(uint32_t trk)
{
    static const char *const TAB[NPAGE + 1u] = {"INSTR", "MAIN", "OSC", "ENV", "LFO"};
    static const char *const DTAB[NDR + 1u] = {"KIT", "BD", "SD", "HH", "OH", "CP", "PC"};
    static const uint16_t YEL = RGB(255, 200, 60);
    const uint8_t *rgb = trk_mute >> trk & 1u ? MUTE_RGB : TRK_RGB[TTYPE(trk)];
    uint16_t col = rgbk(rgb, 100);
    uint8_t ids[40], sub[40];
    uint32_t now = trk < NSYN ? set.snd[trk].preset : set.dkit, bk = id_bank(trk, now), n, m = 0, at = 0, i, half, top;
    uint32_t ntab = trk < NSYN ? NPAGE + 1u : NDR + 1u;
    uint32_t used = trk < NSYN ? set.mu.ub.snd[TTYPE(trk)][usr_slot % NUSND].preset != 0u : set.mu.ub.kit[usr_slot % NUKIT].used != 0u;
    char ty[16], bkn[20], sv[8], us[20], *p;
    n = trk < NSYN ? sound_list(TTYPE(trk), ids) : kit_list(ids);
    for (i = 0; i < n; i++) if (id_bank(trk, ids[i]) == bk) { if (ids[i] == now) at = m; sub[m++] = ids[i]; }
    top = at ? at - 1u : 0u;                    /* the list: three of the bank's sounds, the one playing among them */
    if (m >= 3u && top + 3u > m) top = m - 3u;
    if (trk < NSYN) { p = put(ty, "< "); p = put(p, TYPE_NAME[TTYPE(trk)]); *put(p, " >") = 0; } else *put(ty, "DRUMS") = 0;
    p = put(bkn, "< "); p = put(p, (trk < NSYN ? BANK_NAME : KBANK_NAME)[bk % NBANK]); *put(p, " >") = 0;
    p = put_num(sv, at + 1u, 1); *p++ = '/'; *put_num(p, m, 1) = 0;
    p = put(us, "USER "); *p++ = (char)('1' + usr_slot); p = put(p, used ? "  USED" : "  EMPTY"); *p = 0;
    for (half = 0; half < 2u; half++) {
        cv_begin(240, H_PEDAL / 2, C_BLACK);
        cv_oy = -(int32_t)half * (H_PEDAL / 2);
        cv_rrect(0, 0, 240, H_PEDAL, 8, C_PANEL);
        for (i = 0; i < ntab; i++) {            /* the pages, INSTR lit */
            int32_t tw = 232 / (int32_t)ntab, tx = 4 + (int32_t)i * tw;
            const char *tn = trk < NSYN ? TAB[i] : DTAB[i];
            cv_rrect(tx + 1, 6, tw - 2, 22, 4, i == 0u ? col : C_KEYCAP);
            cv_text_bg(tx + tw / 2 - text_w(&FONT_S, tn) / 2, 9, &FONT_S, tn, i == 0u ? C_BLACK : C_SUB, i == 0u ? col : C_KEYCAP);
        }
        menu_row(34, "K1", "TYPE", ty, trk < NSYN ? C_TXT : C_SUB, 1);
        menu_row(54, "K2", "BANK", bkn, C_TXT, 1);
        menu_row(74, "K3", trk < NSYN ? "SOUND" : "KIT", sv, C_SUB, 0);
        for (i = 0; i < 3u && top + i < m; i++) {   /* the bank's sounds */
            uint32_t id = sub[top + i], me = top + i == at;
            int32_t y = 92 + (int32_t)i * 16;
            const char *nm = trk < NSYN ? sound_name(TTYPE(trk), id) : kit_name_id(id);
            if (me) cv_rrect(28, y, 204, 16, 3, col);
            cv_text_bg(36, y, &FONT_S, nm, me ? C_BLACK : C_SUB, me ? col : C_PANEL);
        }
        cv_rect(6, 142, 228, 1, C_KEYCAP);
        menu_row(145, "K4", usr_ask ? "SAVE?" : "SAVE TO", us, usr_ask ? YEL : used ? C_TXT : C_SUB, 0);
        cv_oy = 0;
        cv_blit(0, Y_PEDAL + half * (H_PEDAL / 2));
    }
}
static void draw_sound(uint32_t trk, uint32_t pg, uint32_t drum)
{
    static const char *const TAB[NPAGE + 1u] = {"INSTR", "MAIN", "OSC", "ENV", "LFO"};
    static const char *const DTAB[NDR + 1u] = {"KIT", "BD", "SD", "HH", "OH", "CP", "PC"};
    const char *const *WAVE = WAVE_NAME, *const *LTGT = LTGT_NAME;
    static const char *const LWAVE[NLWAVE] = {"TRI", "SQR", "SAW", "RND"};
    const uint8_t *rgb = trk_mute >> trk & 1u ? MUTE_RGB : TRK_RGB[TTYPE(trk)];
    uint16_t col = rgbk(rgb, 100), ink = C_TXT;
    const char *nm[4], *vs[4];
    uint32_t v[4], mx[4], half, i, ntab = trk < NSYN ? NPAGE + 1u : NDR + 1u, sel = pg % ntab;
    char vb[4][8];
    if (sel == 0u) { draw_instr(trk); return; }   /* (INSTR / KIT: a menu) */
    if (trk < NSYN) {
        const snd_t *sp = &set.snd[trk];
        switch (sel - 1u) {
        case PG_MAIN: nm[0] = "GLIDE"; v[0] = set.glide[trk]; mx[0] = 127;
                      nm[1] = "CUTOFF"; v[1] = sp->cut; nm[2] = "RESO"; v[2] = sp->res; nm[3] = "FILT ENV"; v[3] = sp->fenv;
                      mx[1] = mx[2] = mx[3] = 127; break;
        case PG_OSC: nm[0] = "WAVE 1"; v[0] = sp->w1 % NWAVE; mx[0] = NWAVE - 1u; nm[1] = "WAVE 2"; v[1] = sp->w2 % NWAVE; mx[1] = NWAVE - 1u;
                     nm[2] = "TUNE 2"; v[2] = sp->tune; mx[2] = 48; nm[3] = "MIX"; v[3] = sp->mix; mx[3] = 127; break;
        case PG_ENV: nm[0] = "ATTACK"; v[0] = sp->a; nm[1] = "DECAY"; v[1] = sp->d; nm[2] = "SUSTAIN"; v[2] = sp->s; nm[3] = "RELEASE"; v[3] = sp->r;
                     mx[0] = mx[1] = mx[2] = mx[3] = 127; break;
        default: nm[0] = "RATE"; v[0] = sp->lrate; mx[0] = 127; nm[1] = "DEPTH"; v[1] = sp->ldepth; mx[1] = 127;
                 nm[2] = "TARGET"; v[2] = sp->ltgt % NLTGT; mx[2] = NLTGT - 1u; nm[3] = "WAVE"; v[3] = sp->lwave % NLWAVE; mx[3] = NLWAVE - 1u;
        }
    } else {
        const dsnd_t *ds = &set.dsnd[(sel - 1u) % NDR];
        (void)drum;
        nm[0] = "PITCH"; v[0] = ds->pitch; nm[1] = "DECAY"; v[1] = ds->decay; nm[2] = "LEVEL"; v[2] = ds->level;
        nm[3] = ""; v[3] = 0; mx[0] = mx[1] = mx[2] = 127; mx[3] = 0;
    }
    for (i = 0; i < 4u; i++) {                  /* the values as words or numbers */
        vs[i] = vb[i];
        if (!nm[i][0]) vb[i][0] = 0;
        else if (trk < NSYN && sel - 1u == PG_OSC && i < 2u) vs[i] = WAVE[v[i] % NWAVE];
        else if (trk < NSYN && sel - 1u == PG_OSC && i == 2u) {
            int32_t st = (int32_t)v[2] - 24; char *p = vb[2];
            *p++ = st < 0 ? '-' : '+'; *put_num(p, (uint32_t)(st < 0 ? -st : st), 1) = 0;
        } else if (trk < NSYN && sel - 1u == PG_LFO && i == 2u) vs[i] = LTGT[v[2] % NLTGT];
        else if (trk < NSYN && sel - 1u == PG_LFO && i == 3u) vs[i] = LWAVE[v[3] % NLWAVE];
        else *put_num(vb[i], v[i], 1) = 0;
    }
    for (half = 0; half < 2u; half++) {
        cv_begin(240, H_PEDAL / 2, C_BLACK);
        cv_oy = -(int32_t)half * (H_PEDAL / 2);
        cv_rrect(0, 0, 240, H_PEDAL, 8, C_PANEL);
        for (i = 0; i < ntab; i++) {            /* the pages (drums: the drums), the chosen one lit */
            int32_t tw = 232 / (int32_t)ntab, tx = 4 + (int32_t)i * tw;   /* (7 drum tabs: 33 px each) */
            const char *tn = trk < NSYN ? TAB[i] : DTAB[i];
            cv_rrect(tx + 1, 6, tw - 2, 22, 4, i == sel ? col : C_KEYCAP);
            cv_text_bg(tx + tw / 2 - text_w(&FONT_S, tn) / 2, 9, &FONT_S, tn, i == sel ? C_BLACK : C_SUB, i == sel ? col : C_KEYCAP);
        }
        {
            const char *sn = trk < NSYN ? sound_name(TTYPE(trk), set.snd[trk].preset) : kit_name();
            cv_text_bg(120 - text_w(&FONT_S, sn) / 2, 36, &FONT_S, sn, col, C_PANEL);
        }
        for (i = 0; i < 4u; i++) {              /* the four knobs, KNOB1-4 (drums: three) */
            int32_t cx = 31 + (int32_t)i * 59;
            if (!nm[i][0]) continue;
            draw_knob(cx, 84, v[i], mx[i] ? mx[i] : 1u, col, rgbk(rgb, 35));
            cv_text_bg(cx - text_w(&FONT_S, nm[i]) / 2, 106, &FONT_S, nm[i], ink, C_PANEL);
            lcd_value(cx, 126, 54, vs[i], RGB(240, 240, 246));
        }
        cv_oy = 0;
        cv_blit(0, Y_PEDAL + half * (H_PEDAL / 2));
    }
}

/* ---- MIDI in: from the main loop, and from app_idle while the screen draws (idle_ok) */
static uint32_t idle_ok, mclk_n, mclk_t0, mclk_bpm, mclk_run, mclk_beat_ms, sel_trk = T_LEAD;

static void play_live(uint32_t trk, uint32_t note, uint32_t on);
static uint32_t prev_note, prev_trk, prev_t;    /* a note heard for a moment (the step recorder's KNOB4, stopped) */
static void prev_play(uint32_t trk, uint32_t note)
{
    if (prev_note) play_live(prev_trk, prev_note, 0);
    play_live(trk, note, 1); prev_note = note; prev_trk = trk; prev_t = fm1_ms ? fm1_ms : 1u;
}
static void prev_tick(void) { if (prev_note && fm1_ms - prev_t > 250u) { play_live(prev_trk, prev_note, 0); prev_note = 0; } }
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
static void syn_clear_step(uint32_t t, uint32_t pt, uint32_t s);
static uint32_t syn_room(uint32_t t, uint32_t pt, uint32_t s, uint32_t len)   /* a note written at s, the length chosen
                                                   (not past the pattern's end): the notes under it cleared */
{
    uint32_t L = NLEN_ST[len_ui % NNLEN], j;
    if (s + L > len) L = len - s;
    for (j = 1; j < L; j++) syn_clear_step(t, pt, s + j);
    return L;
}
static void syn_clear_step(uint32_t t, uint32_t pt, uint32_t s)
{
    uint8_t *nt = step_note(t, pt, s);
    uint32_t i;
    for (i = 0; i < STEP_NN(t); i++) nt[i] = 0; /* (note[0] first: the interrupt then sees a rest) */
    *step_len(t, pt, s) = 1;
    set.sdyn[t % NSYN][pt % NPAT][s % NSTEP] = 0;
}
static void pat_clear(uint32_t t, uint32_t pt)  /* a whole pattern of a track */
{
    uint32_t s, d;
    if (t < NSYN) for (s = 0; s < NSTEP; s++) syn_clear_step(t, pt, s);
    else for (d = 0; d < NDR; d++) {
        for (s = 0; s < NSTEP / PAGE; s++) set.dpat[pt][d][s] = 0;
        for (s = 0; s < NSTEP / 2u; s++) set.ddyn[pt][d][s] = 0;
    }
}
static uint32_t pat_used(uint32_t t, uint32_t p)   /* a pattern with anything in it? */
{
    uint32_t s, d;
    if (t < NSYN) { for (s = 0; s < NSTEP; s++) if (*step_note(t, p, s)) return 1; return 0; }
    for (d = 0; d < NDR; d++) for (s = 0; s < NSTEP / PAGE; s++) if (set.dpat[p % NPAT][d][s]) return 1;
    return 0;
}
static void pat_copy(uint32_t t, uint32_t from, uint32_t to);
static void pat_copy_from(uint32_t ts, uint32_t ps, uint32_t td, uint32_t pd)   /* the copy page: track ts's pattern ps into
                                                   td's pd (a synth into a synth: its chords, lengths, dynamics) */
{
    uint32_t s;
    if (ts == td) { pat_copy(td, ps, pd); return; }
    if (ts >= NSYN || td >= NSYN) return;
    for (s = 0; s < NSTEP; s++) { *step_at(td, pd, s) = *step_at(ts, ps, s); set.sdyn[td][pd][s] = set.sdyn[ts][ps][s]; }
    set.plen[td][pd] = set.plen[ts][ps];
}
/* ---- the copy page (SEQ held in the step recorder or the live page; Conductor's): every track's patterns A-J, the one
 * to copy from lit, the one being edited (where the copy goes) framed white; one with notes tinted, an empty one dark;
 * the rows it cannot come from (drums for a synth, synths for the drums) faint. ALGORITHM the track, PRESETS or
 * SELECT the pattern; REC asks, REC again copies; SEQ back */
static void draw_copy(uint32_t dt, uint32_t dp, uint32_t ts, uint32_t ps, uint32_t ask)
{
    static const uint16_t YEL = RGB(255, 200, 60);
    uint32_t half, r, q;
    char v[2] = {0, 0}, s[32], *p;
    p = put(s, slot_short(ts)); *p++ = ' '; *p++ = (char)('A' + ps % NPAT); p = put(p, "  INTO  ");
    p = put(p, slot_short(dt)); *p++ = ' '; *p++ = (char)('A' + dp % NPAT); *p = 0;
    for (half = 0; half < 2u; half++) {
        cv_begin(240, H_PEDAL / 2, C_BLACK);
        cv_oy = -(int32_t)half * (H_PEDAL / 2);
        for (q = 0; q < NPAT; q++) { v[0] = (char)('A' + q); cv_text_bg(48 + (int32_t)q * 20 - text_w(&FONT_S, v) / 2, 0, &FONT_S, v, C_DIMC, C_BLACK); }
        for (r = 0; r < NTRK; r++) {
            uint32_t t = SEQ_ORDER[r], other = (t < NSYN) != (dt < NSYN), me = t == ts;
            int32_t y = 20 + (int32_t)r * 30;
            uint16_t tc = trk_c(t, 100), blk = other ? C_PANEL : me ? tc : trk_c(t, 24);
            cv_rrect(0, y, 34, 26, 4, blk);
            cv_text_bg(17 - text_w(&FONT_S, slot_short(t)) / 2, y + 5, &FONT_S, slot_short(t), other ? C_DIMC : me ? C_BLACK : tc, blk);
            for (q = 0; q < NPAT; q++) {
                int32_t x = 38 + (int32_t)q * 20;
                uint32_t u = !other && pat_used(t, q), src = me && q == ps, dst = t == dt && q == dp;
                uint16_t bg = src ? tc : other ? C_BLACK : u ? trk_c(t, 30) : C_EMPTY;
                uint16_t in = src ? C_BLACK : other ? C_PANEL2 : u ? trk_c(t, 90) : C_DIMC;
                if (dst) cv_rrect(x - 1, y - 1, 20, 28, 4, C_WHITE);
                cv_rrect(x + 1, y + 1, 16, 24, 3, bg);
                v[0] = (char)('A' + q);
                cv_text_bg(x + 9 - text_w(&FONT_S, v) / 2, y + 5, &FONT_S, v, in, bg);
            }
        }
        cv_text_bg(120 - text_w(&FONT_S, s) / 2, 143, &FONT_S, s, ask ? YEL : C_TXT, C_BLACK);
        cv_oy = 0;
        cv_blit(0, Y_PEDAL + half * (H_PEDAL / 2));
    }
}
static void pat_copy(uint32_t t, uint32_t from, uint32_t to)
{
    uint32_t s, d;
    if (from == to) return;
    if (t < NSYN) for (s = 0; s < NSTEP; s++) *step_at(t, to, s) = *step_at(t, from, s);
    else for (d = 0; d < NDR; d++) {
        for (s = 0; s < NSTEP / PAGE; s++) set.dpat[to][d][s] = set.dpat[from][d][s];
        for (s = 0; s < NSTEP / 2u; s++) set.ddyn[to][d][s] = set.ddyn[from][d][s];
    }
    if (t < NSYN) for (s = 0; s < NSTEP; s++) set.sdyn[t][to][s] = set.sdyn[t][from][s];
    set.plen[t][to] = set.plen[t][from];
}

/* the library, step by step: synth slots through every sound of their type, the drums through every kit (bank: only
 * within the bank the sound is in, for the SOUND page's SOUND knob) */
static uint32_t kit_list(uint8_t *ids)
{
    uint32_t i, n = 0;
    for (i = 0; i < NKIT; i++) ids[n++] = (uint8_t)i;
    for (i = 0; i < NUKIT; i++) if (set.mu.ub.kit[i].used) ids[n++] = (uint8_t)KUID(i);
    return n;
}
static void kit_load(uint32_t id)               /* the drums take kit id: its six drums' settings */
{
    uint32_t i;
    if (id >= KUID(0) && id < KUID(NUKIT) && set.mu.ub.kit[id - KUID(0)].used)
        for (i = 0; i < NDR; i++) set.dsnd[i] = set.mu.ub.kit[id - KUID(0)].d[i];
    else { id %= NKIT; for (i = 0; i < NDR; i++) set.dsnd[i] = KITS[id][i]; }
    set.dkit = (uint8_t)id;
}
static uint32_t id_bank(uint32_t t, uint32_t id) { return t < NSYN ? (id >= UID(0) ? BK_USER : id >> 5) : kit_bank(id); }
static void snd_step(uint32_t t, int32_t e, int bank_only)   /* e sounds on (bank_only: staying in its bank) */
{
    uint8_t ids[40], sub[40];
    uint32_t n = t < NSYN ? sound_list(TTYPE(t), ids) : kit_list(ids), i, k = 0, m = 0, now = t < NSYN ? set.snd[t].preset : set.dkit;
    for (i = 0; i < n; i++) if (!bank_only || id_bank(t, ids[i]) == id_bank(t, now)) sub[m++] = ids[i];
    if (!m) return;
    for (i = 0; i < m; i++) if (sub[i] == now) k = i;
    k = (uint32_t)(((int32_t)k + e % (int32_t)m + (int32_t)m) % (int32_t)m);
    if (t < NSYN) sound_load(t, sub[k]); else kit_load(sub[k]);
}
static int snd_bank(uint32_t t, int32_t e)      /* another bank: its first sound (an empty USER bank is passed by) */
{
    uint8_t ids[40];
    uint32_t n = t < NSYN ? sound_list(TTYPE(t), ids) : kit_list(ids), b = id_bank(t, t < NSYN ? set.snd[t].preset : set.dkit), tries, i;
    for (tries = 0; tries < NBANK; tries++) {
        b = (uint32_t)(((int32_t)b + (e > 0 ? 1 : -1) + (int32_t)NBANK) % (int32_t)NBANK);
        for (i = 0; i < n; i++) if (id_bank(t, ids[i]) == b) { if (t < NSYN) sound_load(t, ids[i]); else kit_load(ids[i]); return 1; }
    }
    return 0;
}

static int knob(uint32_t enc, uint8_t *v, int32_t lo, int32_t hi)   /* a knob turns a setting; 1 = it changed */
{
    int32_t e = fm1_enc_take(enc), t;
    if (!e) return 0;
    t = (int32_t)*v + e;
    *v = (uint8_t)(t < lo ? lo : t > hi ? hi : t);
    return 1;
}
#define ARP_CLICKS 1                            /* the slower knobs (length, page, the arp's): clicks a step (now one) */
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
#define SWITCH_CLICKS 1                         /* KNOB1 as a pedal's switch: a click clockwise on, anticlockwise off */
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
            || sp->ldepth > 127u || sp->ltgt >= NLTGT || sp->lwave >= NLWAVE || set.styp[t] >= NSYN
            || !sound_ok(set.styp[t], sp->preset) || set.glide[t] > 127u)
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
    if (!(set.dkit < NKIT || (set.dkit >= KUID(0) && set.dkit < KUID(NUKIT) && set.mu.ub.kit[set.dkit - KUID(0)].used))
        || set.arp_on > 7u || set.swing > 127u) return 0;
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
    for (t = 0; t < NTRK; t++)
        for (i = 0; i < NFX; i++) {
            if (set.ped[t][i] >= NPD) return 0;
            for (s = 0; s < 3u; s++) if (set.pk[t][i][s] > 127u) return 0;
        }
    for (i = 0; i < NSPACE; i++) if (set.spk[i][0] > 127u || set.spk[i][1] > 127u) return 0;
    if (set.spk[SP_DLY][0] >= NDLY || set.spk[SP_TAPE][0] >= NDLY) return 0;
    {
        const uint8_t *d = &set.sdyn[0][0][0];
        for (i = 0; i < sizeof set.sdyn; i++) if (d[i] > 4u) return 0;
        d = &set.ddyn[0][0][0];
        for (i = 0; i < sizeof set.ddyn; i++) if ((d[i] & 15u) > 4u || d[i] >> 4 > 4u) return 0;
    }
    if (set.key < -7 || set.key > 7 || set.keyopt > 3u) return 0;
    if (set.spk2[0][0] >= NDLY || set.spk2[0][1] > 127u || set.spk2[1][0] > 127u || set.spk2[1][1] > 127u) return 0;
    return 1;
}

/* ---- the save slots (4). A load goes into a spare copy first and is checked whole (CRC, then every value) before
 * it replaces the current work: a broken or foreign slot never touches what you have. */
typedef char project_fits_in_a_slot[sizeof set <= SLOT_MAX && NPROJ <= NSLOT ? 1 : -1];
static uint8_t proj_buf[sizeof set] __attribute__((section(".pool")));   /* a slot read in here first */
static void set_swap(void)                      /* the current work <-> proj_buf (a load is checked in place; no copy kept) */
{
    uint8_t *a = (uint8_t *)&set, *b = proj_buf, x;
    uint32_t k;
    for (k = 0; k < sizeof set; k++) { x = a[k]; a[k] = b[k]; b[k] = x; }
}

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
        proj_bpm[i] = slot_load(i, proj_buf, sizeof set) == 0 || slot_load(i, proj_buf, SET_SLT) == 0
                      || slot_load(i, proj_buf, SET_DYN) == 0
                      || slot_load(i, proj_buf, SET_PED) == 0
                      || slot_load(i, proj_buf, SET_ORD) == 0
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
    if (slot_load(i, proj_buf, n) && slot_load(i, proj_buf, n = SET_SLT) && slot_load(i, proj_buf, n = SET_DYN)
        && slot_load(i, proj_buf, n = SET_PED)) {   /* (an older project is shorter: what it
                                                   lacks starts as new) */
        n = SET_ORD;
        if (slot_load(i, proj_buf, n)) {
            n = SET_SS1;
            if (slot_load(i, proj_buf, n)) { n = SET_V10; if (slot_load(i, proj_buf, n)) return -1; }
        }
    }
    set_swap();                                 /* (the work now in proj_buf; what the project lacks: as it was) */
    bytes_copy((uint8_t *)&set + n, proj_buf + n, sizeof set - n);
    if (n <= SET_V10) chain_defaults();         /* (v1.0: no SUPER SEQ) */
    else if (n <= SET_SS1) for (c = 0; c < NCHAIN; c++) set.clen[c] = NLINK;   /* (no song lengths: all 8) */
    if (n < SET_ORD) sound_reorder();           /* (from before QUARTET's order) */
    if (n < SET_PED) ped_from_legacy();         /* (from before the pedal library: its five pedals, as they were) */
    if (n < SET_DYN) {                          /* (from before the dynamics: every note f, C major) */
        uint8_t *z = (uint8_t *)&set + SET_PED;
        uint32_t k;
        for (k = SET_PED; k < SET_DYN; k++) *z++ = 0;
    }
    if (n < SET_SLT) spk2_defaults();           /* (from before the later shared pedals) */
    if (n < sizeof set) slots_from_legacy();    /* (from before the slots' instruments) */
    if (!settings_ok()) { set_swap(); return -1; }   /* (broken: the work back, untouched) */
    trk_req++;                                  /* (the slots' instruments: the voices dealt again) */
    set.swing = 0;                              /* (swing: off for now) */
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
        uint32_t c = t < NSYN ? set.snd[t].preset % NPRESET : set.dkit % 10u;
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
    bytes_copy(proj_buf, &set.mu.ub, sizeof(ubank_t));   /* (the USER bank stays: your saved sounds) */
    set_defaults();
    bytes_copy(&set.mu.ub, proj_buf, sizeof(ubank_t));
    for (t = 0; t < NTRK; t++) for (p = 0; p < NPAT; p++) pat_clear(t, p);   /* (no starting groove either) */
    set.ch = (uint8_t)ch; set.oct = (uint8_t)oct;
}

static void app_main(void)
{
    uint32_t prev = 0, k, t_change = 0, dirty = 0, edits = 0, big_drawn = ~0u, status_drawn = ~0u, info_drawn = ~0u;
    uint32_t view_drawn = ~0u, cpu_drawn = ~0u, cpu_t = 0, ch_t = 0;
    uint32_t edit = 0, drum = DR_KICK, dpage = 0, rec_t0 = 0, rec_long = 1, chord_n = 0, chord_trk = T_PAD, home_used = 1;
    uint32_t spage = PG_MAIN;                   /* the page shown (the knobs edit it) */
    uint32_t sseq_cur = 0;                      /* SUPER SEQ: the box KNOB2 is on */
    uint32_t fxb_t0[NFX] = {0}, fxb_long = (1u << NFX) - 1u;   /* when each top button went down; bit e: no long press due */
    uint32_t proj_slot = 0, confirm = 0, confirm_t = 0, msg_t = 0, msg_ok = 0;   /* the save page: the slot; "sure?" (1 save, 2 load); a message */
    uint32_t proj_go = 0, proj_go_t = 0;        /* a save (1) / load (2) confirmed: done once the sound has stopped */
    uint32_t wipe_ask = 0, wipe_t = 0;          /* HOME: "CLEAR ALL?" up (REC again to do it), since when */
    uint32_t page_hold_t = 0, vpage = 0;        /* SEQ screens: when you last moved the page / cursor (it stays put 3 s); the page shown */
    uint8_t dyn_ui = 3;                         /* the step recorder's dynamic (KNOB2), by the knob: p mp mf f ACC (f) */
    uint32_t dheld = 0xFFu;                     /* drums editing: the white key held (KNOB2 sets that hit's dynamic) */
    uint32_t glo_t0 = 0, glo_dn = 0, glo_used = 0, rep_sel = 2;   /* GLO: when it went down; held; used (no page);
                                                   the beat repeat's length (white keys 1-5 while held: 1/2 .. 1/32; 1/8) */
    static const uint8_t REP_STEPS[5] = {8, 4, 2, 1, REP_HALF};
    static const char *const REP_MSG[5] = {"REPEAT 1/2", "REPEAT 1/4", "REPEAT 1/8", "REPEAT 1/16", "REPEAT 1/32"};
    const char *msg = 0;                        /* shown big for 1.5 s: SAVED 3, LOAD FAILED, ALL CLEARED ... */
    uint32_t pedal_mode = 0, head_drawn = ~0u, pedal_drawn = ~0u;   /* the pedal screen up?, what it shows */
    /* ALGORITHM: 2 clicks either way go to the next / the previous track, LEAD, PAD, BASS, DRUMS, round and round */
    static const uint8_t ALG_TRK[4] = {T_LEAD, T_PAD, T_BASS, T_DRUMS};
    /* SELECT and PRESETS work the same way, one turn of ALG_CLICKS clicks per track: SELECT in tenths (the 10 sounds),
     * PRESETS in tenths (A-J, two clicks each). Where each knob stands, per track; set from what is chosen (knobs_sync) */
    uint32_t snd_pos[NTRK], pat_pos[NTRK];
    uint32_t arp_used = 1, arp_t0 = 0;          /* ARP held and used with PRESETS / SELECT: no arp action when let go; when it went down */
    int32_t arp_acc[3] = {0, 0, 0};             /* the arp knobs: clicks gathered towards the next step */
    int32_t seq_acc[2] = {0, 0};                /* edit mode: the length and page knobs, likewise */
    int32_t ped_acc[3] = {0, 0, 0};             /* the pedals' knobs, likewise (PEDAL_CLICKS) */
    int32_t sw_acc = 0;                         /* KNOB1 as the pedal / arpeggiator switch: clicks gathered */
    uint32_t preset_t = 0;                      /* a preset picked: when (its name shows) */
    uint32_t seq_dn = 0, seq_t0 = 0;            /* SEQ down in the step recorder / live page: a tap or a hold */
    uint32_t earp_t0 = 0, earp_used = 1;        /* the step recorder: ARP down when; used (a hold: the rest) */
    static char pmsg[24];                       /* "GLITCH ON" ... */
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
        if (song_loaded < SET_ORD) sound_reorder();   /* (from before the reorder) */
        if (song_loaded < SET_PED) ped_from_legacy();   /* (from before the pedal library) */
        if (song_loaded < SET_SLT) spk2_defaults();   /* (from before the later shared pedals) */
        if (song_loaded < sizeof set) slots_from_legacy();   /* (from before the slots' instruments) */
        if (!settings_ok()) set_defaults();
    }
    set.swing = 0;                              /* (swing: off for now, no knob for it) */
    trk_req++;                                  /* (the slots' instruments: the voices dealt for them) */
    proj_scan();                                /* which project slots are full */
    knobs_sync(snd_pos, pat_pos);
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
#define CPU_FULL() (msg = "CPU FULL", msg_ok = 0, msg_t = fm1_ms ? fm1_ms : 1u)   /* a pedal refused: too much at once */
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
            if (up >> k & 1u && dheld == k) dheld = 0xFFu;
            if (!(down >> k & 1u)) continue;
            if (fm1_in.buttons >> BTN_GLO & 1u) {   /* GLO held: the white keys 1-5 choose the beat repeat (no notes) */
                if (!black(KEY0 + k) && slot[k] < 5u) rep_sel = slot[k];
                glo_used = 1;
                continue;
            }
            if (drum_edit) {                    /* drums: the keys edit the page of the beat shown */
                if (black(KEY0 + k)) {
                    if (slot[k] < NDR) { drum = slot[k]; hit_req[drum]++; }   /* choose a drum, and hear it */
                } else if (dpage * PAGE + slot[k] < len) {
                    set.dpat[pt][drum][dpage] ^= (uint16_t)(1u << slot[k]);   /* a step on / off */
                    if (dbit(pt, drum, dpage * PAGE + slot[k])) ddyn_put(pt, drum, dpage * PAGE + slot[k], DYN_UI[dyn_ui % 5u]);   /* (at the dynamic set) */
                    dheld = k;                  /* (held: KNOB2 sets its dynamic) */
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
                if (set.keyopt & 1u) n = (int32_t)key_lock((uint32_t)n);   /* (scale lock: the key's notes only) */
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
                        if (chord_n == 0u) { syn_clear_step(trk, pt, cur[trk]); chord_trk = trk; set.sdyn[trk][pt][cur[trk]] = DYN_UI[dyn_ui % 5u]; }
                        if (chord_n < 4u) {
                            if (chord_n == 0u) { *ln = (uint8_t)syn_room(trk, pt, cur[trk], len); nt[0] = (uint8_t)n; }
                            else nt[chord_n] = (uint8_t)n;
                            chord_n++;
                        }
                    } else {
                        uint32_t L = syn_room(trk, pt, cur[trk], len);
                        *ln = (uint8_t)L;       /* (the length first: the interrupt may read the step meanwhile) */
                        set.sdyn[trk][pt][cur[trk]] = DYN_UI[dyn_ui % 5u];
                        nt[0] = (uint8_t)n;
                        cur[trk] = (cur[trk] + L) % len;   /* (on past the note) */
                    }
                    CHANGED();
                }
            }
        }
        if (chord_n && !keys) {                 /* the chord's keys all up: the cursor on past it */
            uint32_t L2 = pat_len(chord_trk, set.sel[chord_trk]);
            cur[chord_trk] = (cur[chord_trk] + *step_len(chord_trk, set.sel[chord_trk] % NPAT, cur[chord_trk])) % L2;
            chord_n = 0;
        }
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

        prev_tick();                            /* (a note heard for a moment: off again) */
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
            if (!rc && proj_go == 2u) { CHANGED(); knobs_sync(snd_pos, pat_pos); }   /* (now the current work) */
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
        /* SEQ: the sequencer (the step recorder; REC there: live recording); SEQ again: SUPER SEQ; again: home */
        if (b & (1u << BTN_SEQ)) {
            if (spage == PG_COPY) { spage = PG_MAIN; if (cp_live) live_mode = 1; else edit = 1; cp_ask = 0; }   /* back */
            else if (edit || live_mode) { seq_dn = 1; seq_t0 = fm1_ms; }   /* (a tap or a hold: below) */
            else if (spage == PG_SSEQ) spage = PG_MAIN;
            else { spage = PG_MAIN; edit = 1; }
            chord_n = 0;
        }
        if (seq_dn && (edit || live_mode) && fm1_in.buttons >> BTN_SEQ & 1u && fm1_ms - seq_t0 >= 500u) {   /* held: the
                                                   copy page, into the pattern edited */
            seq_dn = 0; cp_live = live_mode; cp_trk = trk; cp_pat = set.sel[trk] % NPAT; cp_ask = 0;
            edit = 0; live_mode = 0; live_rec = 0; spage = PG_COPY;
        }
        if (rel & (1u << BTN_SEQ) && seq_dn) {  /* a tap: SUPER SEQ */
            seq_dn = 0;
            if (edit || live_mode) { edit = 0; live_mode = 0; live_rec = 0; spage = PG_SSEQ; }
        }
        /* HOME (let go, not used with PRESETS): always home, the sequencer screen; from there, SUPER SEQ and back */
        if (b & (1u << BTN_HOME)) home_used = 0;
        if (rel & (1u << BTN_HOME)) {
            if (!home_used) { spage = PG_MAIN; edit = 0; live_mode = 0; live_rec = 0; chord_n = 0; }   /* (always home) */
            home_used = 1;
        }
        if ((e = fm1_enc_take(ENC_ALGORITHM)) != 0 && spage == PG_COPY) {   /* the copy page: the track to copy from
                                                   (one of the same kind) */
            int32_t i, d = e > 0 ? 1 : -1, n2 = e > 0 ? e : -e, tr;
            for (i = 0; i < 4 && ALG_TRK[i] != cp_trk; i++) {}
            while (n2--) for (tr = 0; tr < 4; tr++) { i = ((i % 4) + d + 4) % 4; if ((ALG_TRK[i] < NSYN) == (trk < NSYN)) break; }
            cp_trk = ALG_TRK[i % 4]; cp_ask = 0;
        } else if (e != 0) {                    /* the track: a click to the next (or the one before) */
            int32_t i;
            for (i = 0; i < 4 && ALG_TRK[i] != sel_trk; i++) {}
            i = ((i % 4) + e % 4 + 4) % 4;
            sel_trk = ALG_TRK[i];
            chord_n = 0;
        }
        if ((e = fm1_enc_take(ENC_PRESETS)) != 0) {
            if (fm1_in.buttons >> BTN_HOME & 1u) {           /* HOME held: the MIDI channel */
                int32_t v = (int32_t)set.ch + e;
                set.ch = (uint8_t)(v < 0 ? 0 : v > 16 ? 16 : v);
                home_used = 1; ch_t = fm1_ms;
            } else if (spage == PG_COPY) {      /* the copy page: the pattern to copy from */
                cp_pat = (uint32_t)(((int32_t)cp_pat + e % (int32_t)NPAT + (int32_t)NPAT) % (int32_t)NPAT); cp_ask = 0;
            } else if (spage == PG_CHAIN) {     /* the chain: the chosen slot's pedal, through the whole library */
                uint32_t sl = chain_sl % NPED_SLOTS;
                ped_set(trk, sl, ped_any_step(trk, sl, set.ped[trk][sl] % NPD, e));
                if (set.fx[trk].on >> sl & 1u && !ped_fits()) { set.fx[trk].on &= (uint8_t)~(1u << sl); CPU_FULL(); }
            } else if (IS_FX_PAGE(spage)) {     /* a pedal page: the pedal within its category (a click each) */
                uint32_t sl = spage - PG_FX, p = set.ped[trk][sl] % NPD;
                ped_set(trk, sl, ped_step(trk, sl, p, e));
                if (set.fx[trk].on >> sl & 1u && !ped_fits()) { set.fx[trk].on &= (uint8_t)~(1u << sl); CPU_FULL(); }
            } else if (spage == PG_SSEQ) {      /* SUPER SEQ: the song A-J, a click each (ARP held: copy it along) */
                uint32_t to = (uint32_t)(((int32_t)(set.sseq_sel % NCHAIN) + e % (int32_t)NCHAIN + (int32_t)NCHAIN) % (int32_t)NCHAIN);
                if (to != set.sseq_sel % NCHAIN) {
                    if (fm1_in.buttons >> BTN_ARP & 1u) { bytes_copy(set.chain[to], set.chain[set.sseq_sel % NCHAIN], sizeof set.chain[0]); arp_used = 1; }
                    set.sseq_sel = (uint8_t)to;
                }
            } else {                            /* the pattern A-J, a click each, round and round (ARP held: a copy) */
                uint32_t to = (uint32_t)(((int32_t)pt + e % (int32_t)NPAT + (int32_t)NPAT) % (int32_t)NPAT);
                if (fm1_in.buttons >> BTN_ARP & 1u) arp_used = 1;   /* (ARP held: a copy, not the arp screen) */
                if (to != pt) {
                    if (!edit && fm1_in.buttons >> BTN_ARP & 1u) { pat_copy(trk, pt, to); arp_used = 1; }   /* (take this one along;
                                                   not in edit mode, where ARP is the tie) */
                    set.sel[trk] = (uint8_t)to;
                }
            }
            CHANGED();
        }
        if ((e = fm1_enc_take(ENC_SELECT)) != 0) {   /* SELECT: a move within the page (it never leaves it) */
            if (spage == PG_CHAIN) { int32_t v = (int32_t)chain_sl + e; chain_sl = (uint32_t)(v < 0 ? 0 : v > 3 ? 3 : v); }
            else if (spage == PG_COPY) { cp_pat = (uint32_t)(((int32_t)cp_pat + e % (int32_t)NPAT + (int32_t)NPAT) % (int32_t)NPAT); cp_ask = 0; }
            else if (spage == PG_PROJ) { int32_t v = (int32_t)proj_slot + e; proj_slot = (uint32_t)(v < 0 ? 0 : v >= (int32_t)NPROJ ? (int32_t)NPROJ - 1 : v); confirm = 0; }
            else if (spage == PG_SSEQ) {        /* SUPER SEQ: the box */
                int32_t v = (int32_t)sseq_cur + e, cl2 = (int32_t)set.clen[set.sseq_sel % NCHAIN];
                sseq_cur = (uint32_t)(v < 0 ? 0 : v >= cl2 ? cl2 - 1 : v);
            }
            else if (spage == PG_MIX || spage == PG_ARP) {}   /* (nothing to move) */
            else if (IS_FX_PAGE(spage)) {            /* a pedal page: the category (a click each); its first free pedal */
                {
                    uint32_t sl = spage - PG_FX, ca = PDEF[set.ped[trk][sl] % NPD].cat % NPCAT, tries;
                    int32_t stp = e, dir = stp > 0 ? 1 : -1, pf = -1;
                    while (stp) {                       /* one category a step (one with nothing free is passed by) */
                        for (tries = 0, pf = -1; tries < NPCAT && pf < 0; tries++) {
                            ca = (uint32_t)(((int32_t)ca + dir + (int32_t)NPCAT) % (int32_t)NPCAT);
                            pf = ped_first(trk, sl, ca);
                        }
                        stp -= dir;
                    }
                    if (pf >= 0) ped_set(trk, sl, (uint32_t)pf);
                    if (set.fx[trk].on >> sl & 1u && !ped_fits()) { set.fx[trk].on &= (uint8_t)~(1u << sl); CPU_FULL(); }
                    CHANGED();
                }
            }
            else if (spage == PG_KEY) {         /* the key: a sharp or flat a click */
                int32_t v = (int32_t)set.key + e;
                set.key = (int8_t)(v < -7 ? -7 : v > 7 ? 7 : v);
                CHANGED();
            }
            else if (spage == PG_SOUND) {       /* the sound editor: the page: INSTR, then MAIN OSC ENV LFO (drums: KIT, then
                                                   the six drums) */
                uint32_t np = trk < NSYN ? NPAGE + 1u : NDR + 1u;
                snd_pg = (uint32_t)(((int32_t)(snd_pg % np) + e % (int32_t)np + (int32_t)np) % (int32_t)np);
                if (trk == T_DRUMS && snd_pg) drum = snd_pg - 1u;
                usr_ask = 0;
            }
            else if (syn_edit) {                /* editing a synth: the cursor, through all pages */
                cur[trk] = (uint32_t)(((int32_t)cur[trk] + e % (int32_t)len + (int32_t)len) % (int32_t)len);
                page_hold_t = fm1_ms;
            }
            else if (!edit) {                   /* the sound: a click each through the whole library, bank after bank
                                                   (then USER), round and round (DRUMS: the kits); no test note */
                if (spage >= PG_FX) spage = PG_MAIN;   /* (from a pedal: back to the sequencer) */
                snd_step(trk, e, 0);
                preset_t = fm1_ms ? fm1_ms : 1u;   /* (its name shows big for a moment) */
                CHANGED();
            }
        }
        /* the top buttons FX SEL ENV LFO: a tap switches that slot's pedal on / off for the chosen track ("GLITCH ON");
         * held 0.5 s: its page, to choose and set it. (On the mixer they mute the tracks: handled above.) EDIT: the sound
         * editor, and back */
        for (k = 0; k < NPED_SLOTS; k++) {
            if (b >> FX_BTN[k] & 1u) { fxb_t0[k] = fm1_ms; fxb_long &= ~(1u << k); }
            if (spage != PG_MIX && fm1_in.buttons >> FX_BTN[k] & 1u && !(fxb_long >> k & 1u) && fm1_ms - fxb_t0[k] >= 500u) {
                spage = PG_FX + k; edit = 0; live_mode = 0; live_rec = 0;   /* held: the pedal's page */
                fxb_long |= 1u << k;
            }
            if (rel >> FX_BTN[k] & 1u) {
                if (!(fxb_long >> k & 1u)) {    /* a tap: on / off */
                    char *pp;
                    set.fx[trk].on ^= (uint8_t)(1u << k);
                    if (set.fx[trk].on >> k & 1u && !ped_fits()) { set.fx[trk].on &= (uint8_t)~(1u << k); CPU_FULL(); }
                    else {
                        pp = put(pmsg, PDEF[set.ped[trk][k] % NPD].name);
                        *put(pp, set.fx[trk].on >> k & 1u ? " ON" : " OFF") = 0;
                        msg = pmsg; msg_ok = 1; msg_t = fm1_ms ? fm1_ms : 1u;
                    }
                    CHANGED();
                }
                fxb_long |= 1u << k;            /* (let go: no long press until the next press) */
            }
        }
        if (b & (1u << BTN_EDIT)) {             /* EDIT: the sound editor; again: the signal chain; again: home */
            if (spage == PG_SOUND) { spage = PG_CHAIN; chain_sl = 0; }
            else if (spage == PG_CHAIN) spage = PG_MAIN;
            else { spage = PG_SOUND; snd_pg = 0; usr_ask = 0; edit = 0; live_mode = 0; live_rec = 0; chord_n = 0; }
        }
        if (confirm && (fm1_ms - confirm_t > 3000u || spage != PG_PROJ)) confirm = 0;   /* "sure?" lapses */
        if (cp_ask && (fm1_ms - cp_t > 3000u || spage != PG_COPY || (b & ~(1u << BTN_REC)))) cp_ask = 0;   /* (the copy's too) */
        /* KNOB1-4: the page's four settings */
        if ((edit || live_mode) && spage < PG_FX) {   /* the step recorder and the live page: KNOB1 length, KNOB2 the
                                                   step, KNOB3 the page (step recorder: KNOB2 the length, KNOB3 the dynamic, KNOB4 the note) */
            uint32_t sy = trk < NSYN;
            uint8_t li = (uint8_t)(len / PAGE - 1u), pg = (uint8_t)((sy ? cur[trk] : dpage * PAGE) / PAGE);
            int ch = 0;
            if (knob_slow(ENC_KNOB1, &li, 0, NSTEP / PAGE - 1u, &seq_acc[0])) {   /* 16 / 32 / 48 / 64, a click a step */
                set.plen[trk][pt] = (uint8_t)((li + 1u) * PAGE);
                len = pat_len(trk, pt);
                if (sy && cur[trk] >= len) cur[trk] = len - 1u;
                if (dpage >= len / PAGE) dpage = len / PAGE - 1u;
                ch = 1;
            }
            if (edit && sy) {                   /* the step recorder: KNOB2 the length (the note at the cursor, and what
                                                   you write next), named as music does */
                uint8_t *ln = step_len(trk, pt, cur[trk]);
                if (*step_note(trk, pt, cur[trk])) len_ui = (uint8_t)nlen_idx(*ln);   /* (from the note's) */
                if (knob(ENC_KNOB2, &len_ui, 0, NNLEN - 1u)) {
                    if (*step_note(trk, pt, cur[trk])) {
                        uint32_t L = NLEN_ST[len_ui], room = len - cur[trk];
                        *ln = (uint8_t)(L < room ? L : room);
                    }
                    ch = 1;
                }
            } else if (edit) fm1_enc_take(ENC_KNOB2);
            if (edit) {                         /* the step recorder: KNOB3 the dynamic, p mp mf f ACC: the note at the
                                                   cursor (drums: the hit whose key is held), and what you write next */
                if (sy && *step_note(trk, pt, cur[trk])) dyn_ui = DYN_UI[sdyn_get(trk, pt, cur[trk])];   /* (from the note's) */
                if (knob(ENC_KNOB3, &dyn_ui, 0, 4)) {
                    if (sy && *step_note(trk, pt, cur[trk])) { set.sdyn[trk][pt][cur[trk]] = DYN_UI[dyn_ui]; ch = 1; }
                    else if (!sy && dheld < 27u && dpage * PAGE + slot[dheld] < len) {
                        uint32_t s2 = dpage * PAGE + slot[dheld];
                        set.dpat[pt][drum][dpage] |= (uint16_t)(1u << slot[dheld]);
                        ddyn_put(pt, drum, s2, DYN_UI[dyn_ui]);
                        if (!clk_run) hit_req[drum]++;
                        ch = 1;
                    }
                }
            } else if ((e = fm1_enc_take(ENC_KNOB2)) != 0 && sy) {   /* live: the step cursor, through all pages */
                cur[trk] = (uint32_t)(((int32_t)cur[trk] + e % (int32_t)len + (int32_t)len) % (int32_t)len);
                page_hold_t = fm1_ms;
            }
            if (edit && sy) {                   /* KNOB4: the note (the chord) at the cursor up / down a semitone */
                if ((e = fm1_enc_take(ENC_KNOB4)) != 0) {
                    uint8_t *nt = step_note(trk, pt, cur[trk]);
                    uint32_t i, ok = nt[0] != 0;
                    for (i = 0; i < STEP_NN(trk); i++) if (nt[i] && ((int32_t)nt[i] + e < 1 || (int32_t)nt[i] + e > 127)) ok = 0;
                    if (ok) {
                        for (i = 0; i < STEP_NN(trk); i++) if (nt[i]) nt[i] = (uint8_t)((int32_t)nt[i] + e);
                        if (!clk_run) prev_play(trk, nt[0]);   /* (heard, when stopped) */
                        ch = 1;
                    }
                    page_hold_t = fm1_ms;
                }
            } else if (knob_slow(edit ? ENC_KNOB4 : ENC_KNOB3, &pg, 0, (int32_t)(len / PAGE) - 1, &seq_acc[1])) {   /* the page of
                                                   16, a click a step (drums: KNOB4; live: KNOB3) */
                page_hold_t = fm1_ms;
                if (sy) cur[trk] = pg * PAGE + cur[trk] % PAGE;
                else dpage = pg;
            }
            if (!edit) fm1_enc_take(ENC_KNOB4);
            if (ch) CHANGED();
        } else if (IS_FX_PAGE(spage) || spage == PG_CHAIN) {   /* a pedal (a pedal page, or the chain's chosen slot):
                                                   KNOB1 on / off, KNOB2-4 its three knobs */
            static const uint8_t ENC3[3] = {ENC_KNOB2, ENC_KNOB3, ENC_KNOB4};
            uint32_t sl = spage == PG_CHAIN ? chain_sl % NPED_SLOTS : spage - PG_FX;
            pknob_t pk[3];
            int ch = 0, sw = knob_switch(ENC_KNOB1, &sw_acc);
            uint8_t bit = (uint8_t)(1u << sl);
            if (sw > 0 && !(set.fx[trk].on & bit)) {   /* (its button lights) */
                set.fx[trk].on |= bit; ch = 1;
                if (!ped_fits()) { set.fx[trk].on &= (uint8_t)~bit; CPU_FULL(); }
            }
            if (sw < 0 && set.fx[trk].on & bit) { set.fx[trk].on &= (uint8_t)~bit; ch = 1; }
            pedal_knobs(trk, sl, pk);
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
        } else if (spage == PG_SSEQ) {          /* SUPER SEQ: KNOB1 on / off, KNOB2 the box, KNOB3 its pattern, KNOB4 the length */
            uint8_t cu = (uint8_t)sseq_cur, *cl = &set.clen[set.sseq_sel % NCHAIN];
            int ch = 0, sw = knob_switch(ENC_KNOB1, &sw_acc);   /* KNOB1: on / off, as every switch */
            if (sw > 0 && !set.sseq_on) { set.sseq_on = 1; ch = 1; }
            if (sw < 0 && set.sseq_on) { set.sseq_on = 0; ch = 1; }
            ch |= knob(ENC_KNOB4, cl, 1, NLINK);   /* KNOB4: the song's length */
            if (cu >= *cl) cu = (uint8_t)(*cl - 1u);
            knob(ENC_KNOB2, &cu, 0, *cl - 1);
            sseq_cur = cu;
            ch |= knob(ENC_KNOB3, &set.chain[set.sseq_sel % NCHAIN][trk][sseq_cur % NLINK], 0, CH_REST);   /* blank, A-J, X */
            if (ch) CHANGED();
        } else if (spage == PG_SOUND && snd_pg == 0u) {   /* INSTR: KNOB1 the type (synth slots), KNOB2 the bank, KNOB3 the
                                                   sound in it, KNOB4 the USER slot REC saves to */
            int ch = 0;
            if ((e = fm1_enc_take(ENC_KNOB1)) != 0 && trk < NSYN) {   /* LEAD, PAD, BASS (as the screen's order) */
                static const uint8_t TORD[3] = {T_LEAD, T_PAD, T_BASS};
                uint32_t k2 = 0;
                while (k2 < 3u && TORD[k2] != set.styp[trk]) k2++;
                k2 = (uint32_t)(((int32_t)k2 + e % 3 + 3) % 3);
                set.styp[trk] = TORD[k2]; trk_req++;
                sound_load(trk, 0);             /* (the type's first sound; the patterns stay) */
                ch = 1;
            }
            if ((e = fm1_enc_take(ENC_KNOB2)) != 0) ch |= snd_bank(trk, e);
            if ((e = fm1_enc_take(ENC_KNOB3)) != 0) { snd_step(trk, e, 1); ch = 1; }
            { uint8_t us = (uint8_t)usr_slot; knob(ENC_KNOB4, &us, 0, (int32_t)(trk < NSYN ? NUSND : NUKIT) - 1); usr_slot = us; }
            if (ch) { CHANGED(); usr_ask = 0; }
        } else if (spage == PG_SOUND) {         /* the sound editor: the page's four knobs */
            int ch = 0;
            if (trk < NSYN && snd_pg - 1u == PG_MAIN) ch |= knob(ENC_KNOB1, &set.glide[trk], 0, 127);   /* (MAIN: KNOB1 the glide) */
            if (trk == T_DRUMS) {
                dsnd_t *ds = &set.dsnd[drum];
                ch |= knob(ENC_KNOB1, &ds->pitch, 0, 127) | knob(ENC_KNOB2, &ds->decay, 0, 127) | knob(ENC_KNOB3, &ds->level, 0, 127);
                fm1_enc_take(ENC_KNOB4);
            } else {
                snd_t *sp = &set.snd[trk];
                switch (snd_pg - 1u) {
                case PG_MAIN: ch |= knob(ENC_KNOB2, &sp->cut, 0, 127) | knob(ENC_KNOB3, &sp->res, 0, 127) | knob(ENC_KNOB4, &sp->fenv, 0, 127); break;
                case PG_OSC: ch |= knob(ENC_KNOB1, &sp->w1, 0, NWAVE - 1) | knob(ENC_KNOB2, &sp->w2, 0, NWAVE - 1)
                                 | knob(ENC_KNOB3, &sp->tune, 0, 48) | knob(ENC_KNOB4, &sp->mix, 0, 127); break;
                case PG_ENV: ch |= knob(ENC_KNOB1, &sp->a, 0, 127) | knob(ENC_KNOB2, &sp->d, 0, 127)
                                 | knob(ENC_KNOB3, &sp->s, 0, 127) | knob(ENC_KNOB4, &sp->r, 0, 127); break;
                default: ch |= knob(ENC_KNOB1, &sp->lrate, 0, 127) | knob(ENC_KNOB2, &sp->ldepth, 0, 127)
                                | knob(ENC_KNOB3, &sp->ltgt, 0, NLTGT - 1) | knob(ENC_KNOB4, &sp->lwave, 0, NLWAVE - 1); break;
                }
            }
            if (ch) CHANGED();
        } else if (spage == PG_KEY) {           /* the key: KNOB1 the lock, KNOB2 the lights */
            uint8_t lk = set.keyopt & 1u, li = set.keyopt >> 1 & 1u;
            if (knob(ENC_KNOB1, &lk, 0, 1) | knob(ENC_KNOB2, &li, 0, 1)) { set.keyopt = (uint8_t)(lk | li << 1); CHANGED(); }
            fm1_enc_take(ENC_KNOB3); fm1_enc_take(ENC_KNOB4);
        } else if (spage == PG_COPY) {          /* the copy page: no knobs */
            fm1_enc_take(ENC_KNOB1); fm1_enc_take(ENC_KNOB2); fm1_enc_take(ENC_KNOB3); fm1_enc_take(ENC_KNOB4);
        } else if (spage == PG_MAIN) {          /* home: KNOB1 the tempo (the sound itself: EDIT) */
            if (knob(ENC_KNOB1, &set.bpm, BPM_MIN, BPM_MAX)) { clock_set_bpm(set.bpm); CHANGED(); }
            fm1_enc_take(ENC_KNOB2); fm1_enc_take(ENC_KNOB3); fm1_enc_take(ENC_KNOB4);
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
                    sseq_cur = 0; dpage = 0; drum = DR_KICK;
                    for (k = 0; k < NSYN; k++) cur[k] = 0;
                    msg = "ALL CLEARED"; msg_ok = 1; msg_t = fm1_ms ? fm1_ms : 1u;
                    CHANGED();
                } else { rec_t0 = fm1_ms; rec_long = 0; }
            }
            if (!rec_long && fm1_in.buttons >> BTN_REC & 1u && fm1_ms - rec_t0 >= 1000u) {
                rec_long = 1;
                if (home) { wipe_ask = 1; wipe_t = fm1_ms; }   /* HOME: ask first */
                else if (edit || live_mode) { pat_clear(trk, pt); CHANGED(); }   /* (only where the pattern shows) */
            }
        }
        if (rel & (1u << BTN_REC)) {
            if (!rec_long && edit && spage < PG_FX) { edit = 0; live_mode = 1; chord_n = 0; }   /* the step recorder: live */
            else if (!rec_long && spage == PG_COPY) {   /* the copy page: REC asks, REC again copies */
                uint32_t dp = set.sel[trk] % NPAT;
                if (cp_trk == trk && cp_pat == dp) { msg = "SAME PATTERN"; msg_ok = 0; msg_t = fm1_ms ? fm1_ms : 1u; cp_ask = 0; }
                else if ((cp_trk < NSYN) != (trk < NSYN)) { msg = "NOT THE SAME KIND"; msg_ok = 0; msg_t = fm1_ms ? fm1_ms : 1u; cp_ask = 0; }
                else if (!cp_ask || fm1_ms - cp_t > 3000u) { cp_ask = 1; cp_t = fm1_ms; }
                else {
                    pat_copy_from(cp_trk, cp_pat, trk, dp);
                    if (trk < NSYN && cur[trk] >= pat_len(trk, dp)) cur[trk] = 0;
                    cp_ask = 0; spage = PG_MAIN;
                    if (cp_live) live_mode = 1; else edit = 1;
                    msg = "COPIED"; msg_ok = 1; msg_t = fm1_ms ? fm1_ms : 1u;
                    CHANGED();
                }
            }
            else if (!rec_long && spage == PG_SOUND && snd_pg == 0u) {   /* INSTR: save the sound to the USER slot (REC twice) */
                if (!usr_ask || fm1_ms - usr_t > 3000u) { usr_ask = 1; usr_t = fm1_ms; }
                else {
                    static char sm[16];
                    char *pp;
                    usr_ask = 0;
                    if (trk < NSYN) {
                        uint32_t ty = TTYPE(trk), u = usr_slot % NUSND;
                        set.mu.ub.snd[ty][u] = set.snd[trk]; set.mu.ub.snd[ty][u].preset = 1;   /* (1: in use) */
                        set.mu.ub.glide[ty][u] = set.glide[trk];
                        set.snd[trk].preset = (uint8_t)UID(u);   /* (the slot now plays the saved sound) */
                    } else {
                        uint32_t u = usr_slot % NUKIT, d2;
                        set.mu.ub.kit[u].used = 1; set.mu.ub.kit[u].base = (uint8_t)kit_now();
                        for (d2 = 0; d2 < NDR; d2++) set.mu.ub.kit[u].d[d2] = set.dsnd[d2];
                        set.dkit = (uint8_t)KUID(u);
                    }
                    pp = put(sm, "SAVED USER "); *pp++ = (char)('1' + usr_slot); *pp = 0;
                    msg = sm; msg_ok = 1; msg_t = fm1_ms ? fm1_ms : 1u;
                    CHANGED();
                }
            }
            else if (!rec_long && live_mode && spage < PG_FX) {
                if (live_rec) live_rec = 0;
                else { live_rec = 1; if (!clk_run) { clk_go++; clk_run = 1; } }
            }
            rec_long = 1;
        }
        /* the step recorder: ARP a tap = tie (the note before goes on into this step), held 0.5 s = a rest (the step
         * cleared); either way the cursor moves on */
        if (b & (1u << BTN_ARP) && syn_edit) { earp_t0 = fm1_ms; earp_used = 0; }
        if (syn_edit && !earp_used && fm1_in.buttons >> BTN_ARP & 1u && fm1_ms - earp_t0 >= 500u) {
            syn_clear_step(trk, pt, cur[trk]); cur[trk] = (cur[trk] + 1u) % len; earp_used = 1; CHANGED();
        }
        if (rel & (1u << BTN_ARP) && syn_edit && !earp_used) { syn_tie(trk, pt, cur[trk], len); cur[trk] = (cur[trk] + 1u) % len; CHANGED(); }
        if (rel & (1u << BTN_ARP)) earp_used = 1;
        /* ARP outside edit mode: a tap (not used with PRESETS or SELECT) opens the arpeggiator; GLO: the mixer */
        if (b & (1u << BTN_ARP)) { arp_used = edit != 0u; arp_t0 = fm1_ms; }
        if (rel & (1u << BTN_ARP)) {           /* let go: as a pedal's button, a tap turns it on / off, a hold (0.5 s)
                                                   opens its page */
            if (!arp_used && !edit) {
                if (fm1_ms - arp_t0 >= 500u) spage = PG_ARP;
                else if (trk < NSYN) {
                    set.arp_on ^= (uint8_t)(1u << trk);
                    msg = set.arp_on >> trk & 1u ? "ARP ON" : "ARP OFF"; msg_ok = 1; msg_t = fm1_ms ? fm1_ms : 1u;
                    CHANGED();
                } else { msg = "ARP: SYNTHS ONLY"; msg_ok = 0; msg_t = fm1_ms ? fm1_ms : 1u; }
            }
            arp_used = 1;
        }
        /* GLO: a tap (let go within 0.25 s) opens the mixer, then the KEY page, then home; held: BEAT REPEAT, the slice
         * playing goes round until let go (the white keys meanwhile choose its length) */
        if (b & (1u << BTN_GLO)) { glo_t0 = fm1_ms; glo_dn = 1; glo_used = 0; }
        if (glo_dn && fm1_in.buttons >> BTN_GLO & 1u && (glo_used || fm1_ms - glo_t0 >= 250u)) {
            glo_used = 1;
            rep_len = REP_STEPS[rep_sel % 5u];
            msg = REP_MSG[rep_sel % 5u]; msg_ok = 1; msg_t = fm1_ms ? fm1_ms : 1u;
        }
        if (rel & (1u << BTN_GLO)) {
            if (glo_dn && !glo_used) spage = spage == PG_MIX ? PG_KEY : spage == PG_KEY ? PG_MAIN : PG_MIX;
            if (glo_used) msg = 0;
            glo_dn = 0; glo_used = 0; rep_len = 0;
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
            } else if (set.keyopt >> 1 & 1u && trk < NSYN && !edit)   /* LIGHTS: the key's notes glow */
                dim = !on && in_key(base + k);
            fm1_led_key(KEY_LED(k), on);
            fm1_led_key_glow(KEY_LED(k), dim);
        }
        fm1_led_key(BTN_OCT_DOWN, drum_edit ? dpage > 0u : oct < 0);
        fm1_led_key(BTN_OCT_UP, drum_edit ? dpage + 1u < len / PAGE : oct > 0);
        fm1_led_key(BTN_SEQ, edit || spage == PG_SSEQ || spage == PG_COPY || (live_mode && (fm1_ms / 400u) % 2u));   /* step recorder, SUPER SEQ:
                                                   lit; live: blinking */
        fm1_led_key(BTN_HOME, 0);
        fm1_led_key(BTN_EDIT, spage == PG_SOUND || spage == PG_CHAIN);   /* the sound editor up */
        fm1_led_key(BTN_SAVE, spage == PG_PROJ);   /* the save page up */
        fm1_led_key(BTN_GLO, ((spage == PG_MIX || spage == PG_KEY) && !rep_len) || (rep_len && (fm1_ms / 120u) % 2u));   /* the mixer
                                                   or the key open; blinking: a beat repeat */
        fm1_led_key(BTN_ARP, trk < NSYN && set.arp_on >> trk & 1u);   /* the chosen track arpeggiates */
        for (k = 0; k < NPED_SLOTS; k++) {      /* top buttons: lit = that effect is on for the chosen track; its page open: a glow */
            uint32_t on = set.fx[trk].on >> k & 1u;
            fm1_led_key(FX_BTN[k], on);
            fm1_led_key_glow(FX_BTN[k], !on && spage == PG_FX + k);
        }
        if (spage == PG_PROJ && confirm)        /* the save page's "sure?": the button to press again blinks */
            fm1_led_key(BTN_PLAY, confirm == 2u && (fm1_ms / 150u) % 2u);
        else fm1_led_key(BTN_PLAY, run || mclk_run || (mclk_bpm && fm1_ms - mclk_beat_ms < 80u));   /* lit: running; a blink a beat */
        fm1_led_key(BTN_REC, ((spage == PG_PROJ && confirm == 1u) || wipe_ask || cp_ask || usr_ask ? (fm1_ms / 150u) % 2u : 0u)
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
            uint32_t scr = IS_FX_PAGE(spage) ? 1u : spage == PG_ARP ? 2u : spage == PG_MIX ? 3u : spage == PG_SSEQ ? 4u : spage == PG_PROJ ? 5u
                         : spage == PG_KEY ? 7u : spage == PG_SOUND ? 8u : spage == PG_CHAIN ? 9u : spage == PG_COPY ? 10u : 0u;
            if (scr != pedal_mode) {
                pedal_mode = scr;
                lcd_fill(0, 0, 240, 240, C_BLACK);  /* (all of it: the screens differ in their parts) */
                view_drawn = big_drawn = info_drawn = head_drawn = pedal_drawn = status_drawn = ~0u;
            }
        }
        if (pedal_mode == 9u || pedal_mode == 10u) {   /* the signal chain (CHAIN), the copy page (COPY) */
            uint32_t show_msg = msg && fm1_ms - msg_t < 1500u;
            uint32_t ty3 = trk < NSYN ? set.styp[trk] * 7u + set.styp[(trk + 1u) % NSYN] * 3u + set.styp[(trk + 2u) % NSYN] : 0u;
            if ((st = mix(mix(mix(mix(mix(trk, trk_mute >> trk & 1u), pedal_mode), show_msg ? msg_t : 0u), ty3), set.sel[trk])) != head_drawn) {
                head_drawn = st;
                if (show_msg) head_text(msg, msg_ok ? C_WHITE : C_MUTE, msg_ok ? C_PLAYG : C_MUTE);   /* CPU FULL ... */
                else { head_badge = pedal_mode == 9u ? "CHAIN" : "COPY"; head_track(trk, slot_name(trk), pedal_mode == 10u); head_badge = 0; }
            }
            if (pedal_mode == 9u) {
                const uint8_t *kb = &set.pk[trk][0][0], *sb = &set.spk[0][0];
                st = mix(mix(mix(mix(mix(trk, chain_sl), set.fx[trk].on), set.vol[trk]), trk < NSYN ? set.snd[trk].preset : set.dkit), 91u);
                st = mix(st, ty3);
                for (k = 0; k < NPED_SLOTS; k++) st = mix(st, set.ped[trk][k]);
                for (k = 0; k < NPED_SLOTS * 3u; k++) st = mix(st, kb[k]);
                for (k = 0; k < NSPACE * 2u; k++) st = mix(st, sb[k]);
                for (k = 0; k < 4u; k++) st = mix(st, (&set.spk2[0][0])[k]);
                if (st != pedal_drawn) { pedal_drawn = st; draw_chain(trk, chain_sl); }
            } else {
                st = mix(mix(mix(mix(mix(mix(mix(trk, set.sel[trk]), cp_trk), cp_pat), cp_ask), edits), ty3), 101u);
                if (st != pedal_drawn) { pedal_drawn = st; draw_copy(trk, set.sel[trk] % NPAT, cp_trk, cp_pat, cp_ask); }
            }
        } else if (pedal_mode == 8u) {                 /* the sound editor: the track (SOUND), its page */
            const uint8_t *sb = trk < NSYN ? (const uint8_t *)&set.snd[trk] : (const uint8_t *)&set.dsnd[drum];
            uint32_t nb = trk < NSYN ? sizeof(snd_t) : sizeof(dsnd_t);
            if ((st = mix(mix(mix(trk, trk_mute >> trk & 1u), 8u), trk < NSYN ? set.styp[trk] * 7u + set.styp[(trk + 1u) % NSYN] * 3u + set.styp[(trk + 2u) % NSYN] : 0u)) != head_drawn) {
                head_drawn = st; head_badge = "SOUND"; head_track(trk, slot_name(trk), 0); head_badge = 0;
            }
            st = mix(mix(mix(mix(mix(mix(trk, snd_pg), drum), set.bpm), set.dkit), trk_mute >> trk & 1u), 81u);
            st = mix(mix(mix(mix(st, usr_slot), usr_ask), trk < NSYN ? set.styp[trk] : 9u), trk < NSYN ? set.glide[trk] : 0u);
            for (k = 0; k < nb; k++) st = mix(st, sb[k]);
            if (st != pedal_drawn) { pedal_drawn = st; draw_sound(trk, snd_pg, drum); }
        } else if (pedal_mode == 7u) {          /* the key: KEY, the page (the notes held light gold) */
            uint32_t pcs = 0;
            for (k = 0; k < 27u; k++)
                if (keys >> k & 1u) {
                    uint32_t n = base + k;
                    pcs |= 1u << ((set.keyopt & 1u ? key_lock(n) : n) % 12u);
                }
            if (head_drawn) { head_drawn = 0; head_key(); }
            st = mix(mix(mix((uint32_t)(int32_t)set.key, set.keyopt), pcs), 7u);
            if (st != pedal_drawn) { pedal_drawn = st; draw_key(pcs); }
        } else if (pedal_mode == 5u) {          /* the save page: the slots, "sure?", a message */
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
            if ((st = mix(trk, trk_mute >> trk & 1u)) != head_drawn) { head_drawn = st; draw_pedal_head(trk, -1); }
            st = mix(mix(mix(trk, 2u), set.arp_on), trk < NSYN ? set.arp_rate[trk] | set.arp_oct[trk] << 2 | set.arp_dir[trk] << 4 : 0u);
            if (st != pedal_drawn) { pedal_drawn = st; draw_arp(trk); }
        } else if (pedal_mode) {                /* a pedal: the track's name, the pedal (no sequence: HOME goes back) */
            const uint8_t *kb = set.pk[trk][spage - PG_FX], *sb = &set.spk[0][0];
            uint32_t pd = set.ped[trk][spage - PG_FX] % NPD;
            uint32_t show_msg = msg && fm1_ms - msg_t < 1500u;
            if ((st = mix(mix(mix(mix(trk, trk_mute >> trk & 1u), PDEF[pd].cat), spage), show_msg ? msg_t : 0u)) != head_drawn) {
                head_drawn = st;
                if (show_msg) head_text(msg, msg_ok ? C_WHITE : C_MUTE, msg_ok ? C_PLAYG : C_MUTE);   /* CPU FULL ... */
                else draw_pedal_head(trk, (int32_t)(spage - PG_FX));
            }
            st = mix(mix(mix(trk, spage), pd), set.fx[trk].on);
            for (k = 0; k < 3u; k++) st = mix(st, kb[k]);
            for (k = 0; k < NSPACE * 2u; k++) st = mix(st, sb[k]);
            for (k = 0; k < 4u; k++) st = mix(st, (&set.spk2[0][0])[k]);
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
                st = mix(mix(mix(mix(mix(st, pj), snd_pg), usr_ask), usr_slot), chain_sl | cp_ask << 4);
                if (leg == 1u) st = mix(mix(st, cur[trk]), ui_len);   /* (synth step recorder: the cursor, its length) */
                if (st != status_drawn && spage != PG_SSEQ) { status_drawn = st; draw_status(oct, trk, show_ch, spage, leg == 1u ? cur[trk] : drum, pj, cpu_drawn); }
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
            ui_dyn = trk < NSYN && *step_note(trk, pt, c) ? sdyn_get(trk, pt, c)   /* (the DYN cell: the cursor's note, or
                                                   the drum key held, or what you write next) */
                   : trk == T_DRUMS && dheld < 27u && dbit(pt, drum, dpage * PAGE + slot[dheld]) ? ddyn_get(pt, drum, dpage * PAGE + slot[dheld])
                   : DYN_UI[dyn_ui % 5u];
            st = mix(st, ui_dyn);
            ui_len = trk < NSYN && *step_note(trk, pt, c) ? nlen_idx(*step_len(trk, pt, c)) : len_ui;   /* (the LEN cell) */
            st = mix(mix(st, ui_len), set.sel[trk]);
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
