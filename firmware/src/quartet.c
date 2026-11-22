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
 *               library (offered in four groups a type, then USER; gb_synth.h SND_GRP)
 *   gb_fx.h     the pedals: distortion and phaser per track; reverb, delay, chorus shared (sends)
 *
 * Sound (outside edit mode): ALGORITHM picks the chosen track's sound, a click each through every sound of the slot's
 * type, bank after bank (no test note; the library: gb_synth.h SND_LEAD / SND_BASS / SND_PAD, gb_drums.h KITS):
 *   the library's ids: 0-9, 32-39, 64-69 (never renumbered: every save keeps its sound), USER 96-103 (set.mu.ub);
 *   offered in four groups a type (SND_GRP: LEAD keys, band, synth, odd; PAD strings, keys, synth, atmos; BASS
 *   acoustic, analog, digital, wild), the others loadable still. DRUMS: kits 0-17 (KIT_GRP: groove, machines,
 *   electro, odd), USER 96-99
 * (its name shows big). Home: KNOB1 tempo, KNOB2 the chosen track's mix level, KNOB3 its brightness (a synth's cutoff; drums: the kit's decay), KNOB4 the pedals' fade in / out (0-4 bars); the octave, when shifted, a badge in the top bar. EDIT: the SOUND page, the sound in
 * depth (EDIT again: the signal chain, again: home; HOME: home; SELECT another track):
 *   ALGORITHM the page: INSTR a menu: type (LEAD/PAD/BASS), bank, sound (the bank's listed), USER slot (REC twice: save)
 *                    MAIN glide, cutoff, resonance, filter envelope   OSC wave 1, wave 2, osc 2 tuning, mix
 *                    ENV attack, decay, sustain, release              LFO rate, depth, target (pitch/filter/amp/PWM), wave
 *   DRUMS  KIT (bank, kit, USER slot; REC twice: save), then a page per drum: KNOB1-3 pitch, decay, level
 * The signal chain (EDIT twice): the sound, the four pedal slots in order (the chosen one open), OUT (the level).
 *   ALGORITHM the slot, PRESETS another pedal (the whole library, category by category), KNOB1 on / off, KNOB2-4 its knobs
 * The rules on every page: SELECT the track; PRESETS the pattern (or the page's own: pedal, song); ALGORITHM moves
 * within the page, never off it; KNOB1 a page's on / off switch; a tap switches / opens, a hold (0.5 s) the other;
 * REC twice confirms (save, copy, clear all); a page's own button again moves on or closes it; HOME: home
 *
 * Pedals (gb_fx.h), for the chosen track: four slots (FX SEL ENV LFO), each holding any pedal of the library. A tap
 * on a top button switches its pedal on / off ("GLITCH ON" big on top; lit = on); held 0.5 s it shows the pedal (the
 * track's name above it, the pedal's category beside it, the ON lamp, three knobs; HOME goes back). There PRESETS picks
 * the type (DRIVE, FILTER, MODULATION, SPACE, PITCH, TEXTURE, BEHAVIOUR: PORD's order), ALGORITHM the pedal in it (a click each; a pedal in
 * another of the track's slots is not offered, nor GLITCH beside LOOSE: ped_free()); KNOB1 switches it on (a click
 * clockwise) / off (a click back), KNOB2-4 turn its knobs. (EDIT's slot is unused: EDIT is the sound editor.)
 * The space pedals (reverb, delay, tape echo, shimmer, reverse delay, spring reverb) are one each, shared: a track's
 * first knob is what it sends. STOP silences every pedal (gb_fx.h fx_flush).
 * A pedal that would overload the chip is refused: "CPU FULL" (ped_fits()). New: reverb, delay, distortion,
 * chorus in the four slots.
 *   GLO   a tap: the MIXER (again: the SONG page, again: home): four channel strips; KNOB1-4 the volume of LEAD, PAD,
 *         BASS, DRUMS (100 = as is); there (only), FX / SEL / ENV / LFO mute LEAD / PAD / BASS / DRUMS, lit = playing.
 *         The SONG page (the KEY page as was, Conductor's): ALGORITHM the key (sharps / flats), KNOB1 LOCK (the keys play
 *         only its notes, one off it the note below), KNOB2 LIGHTS (its notes glow on the keys), KNOB3 the swing 50-66 %.
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
 *   SELECT  choose the track: a click right the next one, left the one before (LEAD, PAD, BASS, DRUMS, round)
 *   PRESETS    choose the track's pattern A-J, a click each. While playing it is queued ("A>C") and starts when the playing
 *              pattern comes round to its step 1; stopped, at once.
 *              Hold ARP and turn PRESETS: copy the pattern into the next letter(s)
 *              Hold HOME and turn PRESETS: the theme, in a menu; turn ALGORITHM: the MIDI channel (OMNI, 1-16)
 *   keys       play the chosen track live, also while the patterns play (DRUMS: the first 6 white keys)
 *   HOME       always home (the sequencer screen). (Muting: on the mixer)
 *
 * SUPER SEQ (SEQ from the sequencer: SEQ twice from home; SEQ again: back to the step recorder; HOME: home): songs made of your patterns. A row a track (SELECT chooses it), 8 boxes each; each box
 * plays its pattern once, then the next box, the 8 round and round. A blank box goes on with the pattern before it:
 * A...B.A. plays AAAABBAA. X is a rest (blanks after it rest too); blanks at the start rest (the track comes in
 * later). New: box 1 = A, the rest blank.
 *   KNOB1  SUPER SEQ on / off. On, it plays the songs (each track starts at box 1 when its pattern comes round,
 *          or on PLAY); off, the patterns chosen with PRESETS play as ever
 *   KNOB2 (or ALGORITHM)  the box   KNOB3  its pattern: blank, A-J, X (rest)   KNOB4  the song's length, 1-8 boxes
 *   PRESETS  the song A-J (10 songs); ARP held + PRESETS: copy the song into the next letter
 *   (Elsewhere, under SUPER SEQ, PRESETS still picks the pattern you edit; SUPER shows on the top bar)
 *   PLAY       start / stop all tracks together (from step 1)
 *   OCT- OCT+  a tap: the octave of the keys; held 0.5 s: the pattern being edited down / up an octave (synths)
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
 *   SEQ        from home: the step recorder; REC there: live recording (above); SEQ again: SUPER SEQ, and again back. HOME: home.
 *              Held 0.5 s (here or live): the COPY page (Conductor's): every track's patterns; SELECT the track and
 *              PRESETS / ALGORITHM the pattern to copy from (synths from synths, drums from drums); REC asks, REC again
 *              copies it into the pattern edited (and goes back); SEQ: back
 *   PRESETS    the pattern A-J, as outside edit mode
 *   KNOB1      the pattern's length in bars, 1-4 (16 / 32 / 48 / 64 steps)
 *   KNOB2      synths: the note length, 1/16 1/8 1/8. 1/4 1/4. 1/2 1/2. 1 (the cursor's note, and what is written next)
 *   KNOB3      EXP, the expression: NORMAL SOFT LOUD SHORT PUNCH SMOOTH PLUCKED (DYN_NAME; the cursor's note and what is
 *              written next; drums: new hits, or the hit
 *              whose white key is held)
 *   KNOB4      synths: the cursor's note (chord) up / down a semitone (heard, when stopped); drums: the page of 16
 *              (the info line shows BARS LEN DYN and the note; the bottom line the step and the length in words)
 *   LEAD BASS  step recording: the lit white key is the cursor (the 16 white keys show the cursor's page of 16)
 *   PAD          a key     writes that note on the cursor's step, as long as KNOB2 says; the cursor moves past it
 *                         (LEAD, PAD: hold several keys together for a chord, up to 4; the cursor moves when you let go)
 *                REC       live recording.  Hold REC 1 s: clear the pattern
 *                ARP       a tap: a tie, the note before the cursor lasts one step longer; held 0.5 s: a rest, the
 *                          cursor's step cleared; either way the cursor moves on
 *                ALGORITHM    move the cursor (through all the pattern's pages)
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
#include "serif_font.h"                        /* FONT_T: the pedal names, the battery warning */

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
#define ALG_CLICKS 20u                          /* clicks in one full turn of ALGORITHM and PRESETS (usual for such knobs; if
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
    uint8_t theme;                              /* (later) the screen's theme (HOME held, ALGORITHM): with the instrument,
                                                   in the room the older saves' BASS patterns left, so no save changes size */
    uint8_t pfade;                              /* (later still) the pedals' fade in / out: 0 at once, 1-4 bars (home, KNOB4) */
    uint8_t recopt;                             /* recording (REC held): bit 0 no count-in, bit 1 a click while recording */
} ubank_t;
typedef char ubank_fits[sizeof(ubank_t) <= sizeof(mono_t) * 10u * 64u ? 1 : -1];
static struct {
    uint8_t oct, ch, bpm, mute;                 /* mute: bit t = track t muted */
    uint8_t dkit;                               /* the drum kit: 808, CR-78, LM-1, LO-FI (ALGORITHM on DRUMS) */
    uint8_t swing;                              /* 0..127: 50 % (straight) .. 66 % (home's KNOB2; gb_clock.h) */
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
    uint8_t sdyn[NSYN][NPAT][NSTEP];            /* a synth step's dynamic (and articulation): DYN_NAME below */
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
/* a step's EXPRESSION (KNOB3, EXP: how it is played), stored as a code: 0 NORMAL (every older note: as loud as
 * before), 1-3 SOFT (older saves' mf, mp, p, each as soft as it was; a new SOFT is 2), 4 PUNCH (louder, brighter: an
 * accent), 5 LOUD, 6 SHORT (staccato), 7 SMOOTH (legato: on into the next note, which glides on without a new attack),
 * 8 PLUCKED (pizzicato: it dies away by itself). On drums: SHORT and PLUCKED choke the hit, SMOOTH lets it ring. The
 * knob's order: DYN_UI (place -> code), DYN_POS (code -> place) */
#define NDYN 9u
#define NDYN_UI 7u
static const uint8_t DYN_UI[NDYN_UI] = {0, 2, 5, 6, 4, 7, 8};   /* NORMAL SOFT LOUD SHORT PUNCH SMOOTH PLUCKED */
static const uint8_t DYN_POS[NDYN] = {0, 1, 1, 1, 4, 2, 3, 5, 6};
static const char *const DYN_NAME[NDYN] = {"NORMAL", "SOFT", "SOFT", "SOFT", "PUNCH", "LOUD", "SHORT", "SMOOTH", "PLUCKED"};
static const char *const DYN_SHORT[NDYN] = {"NORMAL", "SOFT", "SOFT", "SOFT", "PUNCH", "LOUD", "SHORT", "SMOOTH", "PLUCK"};   /* (the info line) */
#define DYN_LONG DYN_NAME
static uint32_t sdyn_get(uint32_t t, uint32_t p, uint32_t s)
{
    uint32_t v = set.sdyn[t % NSYN][p % NPAT][s % NSTEP];
    return v < NDYN ? v : 0u;
}
static uint32_t ddyn_get(uint32_t p, uint32_t d, uint32_t s)
{
    uint32_t b = set.ddyn[p % NPAT][d % NDR][(s % NSTEP) / 2u], v = s & 1u ? b >> 4 : b & 15u;
    return v < NDYN ? v : 0u;
}
static void ddyn_put(uint32_t p, uint32_t d, uint32_t s, uint32_t v)
{
    uint8_t *b = &set.ddyn[p % NPAT][d % NDR][(s % NSTEP) / 2u];
    v = v < NDYN ? v : 0u;
    *b = (uint8_t)(s & 1u ? (*b & 15u) | v << 4 : (*b & 0xF0u) | v);
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
    for (i = 0; i < NSYN; i++) { set.styp[i] = (uint8_t)i; sound_load(i, i == T_BASS ? SND_REESE : 0u); }   /* PIANO, REESE, STRINGS */
    trk_req++;                                  /* (the voices dealt again for them) */
    set.dkit = KIT_808;                         /* the drums: the 808 kit */
    for (i = 0; i < NDR; i++) set.dsnd[i] = KITS[KIT_808][i];
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
/* ---- the themes (HOME held, ALGORITHM): every colour of the screens but the pedals' own, by role. A theme changes
 * colours only, so every screen works the same in each. DARK is QUARTET's own; PAPER light, the chosen track washed
 * and bordered; NEON deep purple and hot colours, washed and bordered; LCD an old pocket LCD in four olive shades */
enum { TC_BG, TC_MUTE, TC_PLAYHD, TC_PANEL, TC_PANEL2, TC_TXT, TC_SUB, TC_DIMC, TC_BOTTXT, TC_LCD, TC_KEYCAP, TC_PLAYG,
       TC_EMPTY, TC_EMPTY4, TC_HEADC, TC_ON, TC_HI,
       TC_ROW, TC_ROWTXT, TC_LCDTXT,             /* a list's rows and their text; a little value screen's text */
       TC_GRID, TC_GRID4, TC_GRIDL, TC_PKB, TC_PKW,   /* the piano roll: its steps, every 4th, the octave lines, its keys */
       TC_KWON, TC_KWOFF, TC_KBON, TC_KBOFF, TC_KWTXT,   /* the KEY page's keys: white and black, in the key or not */
       NTC };
typedef struct {
    const char *name;
    uint8_t c[NTC][3];                          /* by role (TC_...): ON is text on a track's colour, HI where white was */
    uint8_t trk[NTRK][3];                       /* LEAD, BASS, PAD, DRUMS */
    uint8_t style;                              /* home: 0 the chosen track filled; 1 washed and bordered; 2 filled, the
                                                   others bordered */
    uint8_t mono;                               /* the pedals too: 1 in the LCD's shades; 2 as neon on the dark */
} theme_t;
static const theme_t THEMES[] = {               /* the first is the default */
    {"PAPER", {{240, 238, 233}, {214, 48, 38}, {26, 26, 30}, {250, 249, 246}, {228, 226, 220}, {26, 26, 30}, {110, 110, 118},
               {178, 176, 170}, {96, 96, 104}, {228, 226, 220}, {218, 216, 210}, {28, 160, 58}, {224, 222, 216}, {210, 208, 202},
               {150, 150, 156}, {255, 255, 255}, {26, 26, 30}, {230, 228, 222}, {40, 40, 46}, {26, 26, 30}, {247, 246, 242}, {236, 234, 228}, {208, 206, 200}, {40, 40, 46}, {255, 255, 255}, {255, 255, 255}, {196, 194, 188}, {60, 60, 66}, {124, 122, 116}, {26, 26, 30}},
     {{214, 120, 0}, {0, 136, 200}, {128, 76, 226}, {26, 150, 54}}, 1},
    {"DARK", {{12, 12, 16}, {238, 56, 46}, {236, 236, 244}, {22, 22, 28}, {36, 36, 44}, {232, 232, 240}, {160, 160, 172},
              {70, 70, 84}, {190, 190, 204}, {10, 10, 13}, {52, 52, 64}, {92, 232, 112}, {32, 32, 42}, {50, 50, 64},
              {110, 118, 140}, {12, 12, 16}, {255, 255, 255}, {28, 28, 34}, {200, 200, 212}, {240, 240, 246}, {17, 21, 33}, {26, 32, 48}, {44, 52, 74}, {30, 34, 46}, {201, 206, 218}, {232, 232, 238}, {96, 96, 106}, {70, 70, 84}, {14, 14, 18}, {12, 12, 16}},
     {{255, 184, 36}, {36, 198, 240}, {178, 124, 255}, {92, 226, 104}}, 0},
    {"NEON", {{22, 8, 40}, {255, 60, 60}, {255, 255, 255}, {36, 16, 62}, {54, 26, 90}, {250, 240, 255}, {180, 150, 210},
              {90, 60, 130}, {210, 190, 235}, {16, 6, 30}, {64, 32, 104}, {176, 255, 40}, {48, 22, 78}, {66, 32, 104},
              {150, 120, 190}, {22, 8, 40}, {255, 255, 255}, {40, 18, 68}, {220, 200, 240}, {250, 240, 255}, {30, 12, 52}, {40, 18, 68}, {80, 40, 120}, {30, 12, 52}, {220, 200, 240}, {250, 240, 255}, {110, 80, 150}, {90, 60, 130}, {24, 10, 44}, {22, 8, 40}},
     {{255, 70, 180}, {176, 255, 40}, {0, 230, 255}, {255, 150, 30}}, 1, 2},
    {"LCD", {{156, 170, 92}, {120, 30, 20}, {24, 32, 12}, {144, 158, 84}, {128, 142, 72}, {30, 40, 16}, {68, 82, 38},
             {116, 130, 64}, {46, 58, 24}, {136, 150, 78}, {122, 136, 68}, {30, 40, 16}, {136, 150, 78}, {122, 136, 68},
             {80, 94, 46}, {172, 186, 106}, {24, 32, 12}, {138, 152, 80}, {30, 40, 16}, {24, 32, 12}, {150, 164, 88}, {140, 154, 82}, {110, 124, 62}, {30, 40, 16}, {176, 190, 110}, {178, 192, 112}, {128, 142, 72}, {56, 68, 28}, {104, 118, 56}, {24, 32, 12}},
     {{18, 26, 6}, {56, 70, 26}, {36, 48, 14}, {72, 86, 34}}, 1, 1},
};
#define NTHEME (sizeof THEMES / sizeof THEMES[0])
static const theme_t *thm = &THEMES[0];
static uint16_t thc[NTC];
static uint32_t thm_light;                      /* the background is light: shades go towards it, not towards black */
static void theme_set(uint32_t i)
{
    uint32_t k;
    thm = &THEMES[i < NTHEME ? i : 0u];
    for (k = 0; k < NTC; k++) thc[k] = RGB(thm->c[k][0], thm->c[k][1], thm->c[k][2]);
    thm_light = (uint32_t)thm->c[TC_BG][0] + thm->c[TC_BG][1] + thm->c[TC_BG][2] > 384u;
}
#define TRK_RGB (thm->trk)                      /* the track colours (by type: T_LEAD, T_BASS, T_PAD, T_DRUMS) */
static const uint8_t MUTE_RGB[3] = {238, 56, 46};   /* a muted track: red (every theme) */
#define C_BG thc[TC_BG]                         /* the background */
#define C_MUTE thc[TC_MUTE]
#define C_PLAYHD thc[TC_PLAYHD]                 /* the playhead */
#define C_PANEL thc[TC_PANEL]                   /* the cards */
#define C_PANEL2 thc[TC_PANEL2]                 /* the chosen card */
#define C_TXT thc[TC_TXT]                       /* values */
#define C_SUB thc[TC_SUB]                       /* labels */
#define C_DIMC thc[TC_DIMC]                     /* off, unused */
#define C_BOTTXT thc[TC_BOTTXT]                 /* the bottom bar's text */
#define C_LCD thc[TC_LCD]                       /* the little value screens */
#define C_KEYCAP thc[TC_KEYCAP]
#define C_PLAYG thc[TC_PLAYG]                   /* the play sign */
#define C_EMPTY thc[TC_EMPTY]                   /* an empty step; on a beat: */
#define C_EMPTY4 thc[TC_EMPTY4]
#define C_HEADC thc[TC_HEADC]                   /* the step playing, when empty */
#define C_ON thc[TC_ON]                         /* text on a track's colour */
#define C_ROW thc[TC_ROW]
#define C_ROWTXT thc[TC_ROWTXT]
#define C_LCDTXT thc[TC_LCDTXT]
#define C_GRID thc[TC_GRID]
#define C_GRID4 thc[TC_GRID4]
#define C_GRIDL thc[TC_GRIDL]
#define C_PKB thc[TC_PKB]
#define C_PKW thc[TC_PKW]
#define C_KWON thc[TC_KWON]
#define C_KWOFF thc[TC_KWOFF]
#define C_KBON thc[TC_KBON]
#define C_KBOFF thc[TC_KBOFF]
#define C_KWTXT thc[TC_KWTXT]
#undef C_WHITE
#define C_WHITE thc[TC_HI]                      /* the accent: what is being touched / where we are */
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
static const uint8_t SEQ_ORDER[NTRK] = {T_LEAD, T_PAD, T_BASS, T_DRUMS};   /* the order on the screen, the mixer, SELECT */
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
       PG_CHAIN, PG_COPY,                       /* the sound editor (EDIT) */
       PG_REC };                                /* the RECORD page (REC held): the count-in, the click; clear */
#define IS_FX_PAGE(p) ((p) >= PG_FX && (p) < PG_PROJ)
/* the top buttons: the four pedal slots (FX SEL ENV LFO); EDIT, the fifth, is the sound editor */
static const uint8_t FX_BTN[NFX] = {BTN_FX, BTN_SEL, BTN_ENV, BTN_LFO, BTN_EDIT};   /* (GLO: the mixer) */

/* the pedal library: each pedal's name, category, colour (dark: black letters on it) and three knobs (KNOB2-4):
 * where each is kept (0-2: the slot's knob; 3, 4: the shared pedal's), how far it goes, where it starts, how shown */
enum { KV_LVL, KV_DLY, KV_R4, KV_R8, KV_GL, KV_OCT, KV_PAT, KV_SRC, KV_SEMI, KV_OCT5, KV_INT, KV_OCT2,
       KV_DJ, KV_TWL, KV_REP, KV_HOLD };    /* (DJ FILTER: LP .. OPEN .. HP; TAPE WORM's length, repeats; WRONG STATION's hold) */   /* ... -12..+12;
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
    {"GLITCH", 6, {50, 230, 120}, 1, {KL("CHANCE", 0, 50), {"LENGTH", 1, 2, 1, KV_GL}, KL("MIX", 2, 100)}},
    {"UNDERWATER", 5, {20, 90, 140}, 0, {KL("DEPTH", 0, 80), KL("WOBBLE", 1, 50), KL("LEVEL", 2, 64)}},
    {"SYNTH", 4, {130, 200, 40}, 1, {{"OCTAVE", 0, 2, 1, KV_OCT}, KL("TONE", 1, 60), KL("MIX", 2, 80)}},
    {"PUMP", 2, {235, 235, 120}, 1, {KL("DEPTH", 0, 100), KL("RELEASE", 1, 60), {"SOURCE", 2, 2, 0, KV_SRC}}},
    {"BITCRUSHER", 0, {90, 200, 220}, 1, {KL("BITS", 0, 80), KL("RATE", 1, 40), KL("MIX", 2, 127)}},
    {"REVERSE DELAY", 3, {100, 40, 140}, 0, {KL("ECHO", 0, 70), {"TIME", 3, NDLY - 1u, 2, KV_DLY}, KL("REPEATS", 4, 50)}},
    {"SPRING REVERB", 3, {170, 160, 140}, 1, {KL("LEVEL", 0, 70), KL("TONE", 3, 70), KL("TIME", 4, 70)}},
    {"SHIFTER", 4, {240, 110, 70}, 1, {{"SEMI", 0, 24, 12, KV_SEMI}, {"OCTAVE", 1, 4, 3, KV_OCT5}, KL("MIX", 2, 127)}},
    {"HARMONIZER", 4, {60, 150, 110}, 0, {{"INTERVAL", 0, 9, 8, KV_INT}, KL("LEVEL", 1, 90), KL("TONE", 2, 90)}},
    {"LO-FI", 5, {150, 130, 100}, 0, {KL("RATE", 0, 50), KL("NOISE", 1, 40), KL("DIRT", 2, 60)}},
    {"SLOW", 5, {120, 160, 210}, 1, {KL("RISE", 0, 20), KL("DEPTH", 1, 110), KL("SENS", 2, 80)}},
    {"GHOST", 6, {210, 210, 220}, 1, {KL("DELAY", 0, 60), KL("LEVEL", 1, 50), KL("DRIFT", 2, 50)}},
    {"MUSIC BOX", 4, {230, 170, 190}, 1, {KL("DECAY", 0, 70), KL("TONE", 1, 70), KL("MIX", 2, 100)}},
    {"BROKEN CLOCK", 6, {140, 110, 60}, 0, {KL("AMOUNT", 0, 70), KL("CHANCE", 1, 50), KL("HICCUP", 2, 20)}},
    {"SHADOW", 4, {50, 50, 70}, 0, {KL("LEVEL", 0, 60), KL("TONE", 1, 40), {"OCTAVE", 2, 1, 0, KV_OCT2}}},
    {"LOOSE", 6, {200, 170, 110}, 1, {KL("LATE", 0, 50), KL("DOUBLE", 1, 50), KL("DRIFT", 2, 40)}},   /* GHOST + BROKEN CLOCK */
    {"OLD MEDIA", 5, {120, 70, 45}, 0, {KL("WEAR", 0, 60), KL("NOISE", 1, 45), KL("TONE", 2, 60)}},   /* CASSETTE + VINYL */
    {"DJ FILTER", 1, {40, 200, 230}, 1, {{"FILTER", 0, 127, 34, KV_DJ}, KL("RES", 1, 50), KL("LEVEL", 2, 64)}},
    {"ACID", 1, {196, 232, 40}, 1, {KL("CUTOFF", 0, 30), KL("RES", 1, 90), KL("DECAY", 2, 40)}},
    {"RING MOD", 4, {190, 196, 204}, 1, {KL("FREQ", 0, 70), KL("MIX", 1, 90), KL("WOBBLE", 2, 20)}},
    {"WRONG STATION", 5, {232, 214, 170}, 1, {KL("CHANCE", 0, 60), KL("DRIFT", 1, 30), {"HOLD", 2, 3, 1, KV_HOLD}}},
    {"MECH FAILURE", 6, {110, 118, 126}, 0, {KL("WEAR", 0, 70), KL("STICK", 1, 60), KL("RECOVERY", 2, 30)}},
    {"MEMORY LOSS", 6, {206, 200, 222}, 1, {KL("SPEED", 0, 60), KL("FLOOR", 1, 30), KL("RECALL", 2, 40)}},
    {"TAPE WORM", 6, {214, 178, 120}, 1, {KL("CHANCE", 0, 60), {"LENGTH", 1, 3, 1, KV_TWL}, {"REPEATS", 2, 3, 1, KV_REP}}},
    {"COMPRESSOR", 0, {60, 90, 170}, 0, {KL("SQUASH", 0, 70), KL("ATTACK", 1, 40), KL("LEVEL", 2, 64)}},
    {"ROTARY", 2, {150, 90, 40}, 0, {KL("SPEED", 0, 20), KL("DEPTH", 1, 90), KL("MIX", 2, 110)}},
    {"BLOOM", 6, {210, 80, 150}, 0, {KL("TIME", 0, 50), KL("DEPTH", 1, 80), KL("TONE", 2, 60)}},
};
/* pedals no longer offered (an older save that has one keeps it, as it was): SHADOW (SHIFTER / HARMONIZER do it), GATED
 * FUZZ, SLOW (the sound's attack does it), UNI; GHOST and BROKEN CLOCK (now LOOSE), CASSETTE and VINYL (now OLD MEDIA) */
static int ped_shown(uint32_t p)
{
    return p != PD_SHADOW && p != PD_GATEF && p != PD_SLOW && p != PD_UNI && p != PD_GHOST && p != PD_BROKEN
        && p != PD_CASS && p != PD_VINYL;
}
#define NPCAT 7u
static const char *const PCAT_NAME[NPCAT] = {"DRIVE", "FILTER", "MODULATION", "SPACE", "PITCH", "TEXTURE", "BEHAVIOUR"};
/* the order the pedals are offered in, category by category (their numbers stay what saves know them by) */
static const uint8_t PORD[] = {
    PD_OD, PD_DIST, PD_FUZZ, PD_OCTF, PD_FOLD, PD_CRUSH, PD_COMP,                  /* DRIVE */
    PD_WAH, PD_DJF, PD_ACID, PD_FSEQ,                                             /* FILTER */
    PD_CHO, PD_FLG, PD_PHS, PD_TREM, PD_VIB, PD_SLICE, PD_PUMP, PD_ROTARY,        /* MODULATION */
    PD_REV, PD_SPRING, PD_SHIM, PD_DLY, PD_TAPE, PD_REVD,                         /* SPACE */
    PD_SHIFT, PD_HARM, PD_SYNTH, PD_MBOX, PD_RING,                                /* PITCH */
    PD_OLDM, PD_LOFI, PD_WATER, PD_WRONG,                                         /* TEXTURE */
    PD_GLITCH, PD_LOOSE, PD_MECH, PD_MEML, PD_WORM, PD_BLOOM};                    /* BEHAVIOUR */
#define NPORD (sizeof PORD / sizeof PORD[0])
static uint32_t pord_at(uint32_t p)             /* p's place in PORD; one not offered: its category's first */
{
    uint32_t i;
    for (i = 0; i < NPORD; i++) if (PORD[i] == p) return i;
    for (i = 0; i < NPORD; i++) if (PDEF[PORD[i]].cat == PDEF[p % NPD].cat) return i;
    return 0;
}
static uint32_t ped_free(uint32_t t, uint32_t e, uint32_t p)   /* can slot e of track t take pedal p? Not one already in
                                                   another of its slots, nor GLITCH beside GHOST (they share the track's line) */
{
    uint32_t e2;
    if (!ped_shown(p)) return 0;                /* (one no longer offered) */
    for (e2 = 0; e2 < NPED_SLOTS; e2++) {
        uint32_t q = set.ped[t][e2] % NPD;
        uint32_t ql = q == PD_GHOST || q == PD_LOOSE, pl = p == PD_GHOST || p == PD_LOOSE;   /* (they use the track's line) */
        if (e2 == e) continue;
        if (q == p || (q == PD_GLITCH && pl) || (ql && p == PD_GLITCH)) return 0;
    }
    return 1;
}
static int32_t ped_first(uint32_t t, uint32_t e, uint32_t cat)   /* a category's first pedal free for the slot; -1: none */
{
    uint32_t p;
    uint32_t i;
    for (i = 0; i < NPORD; i++) { p = PORD[i]; if (PDEF[p].cat == cat && ped_free(t, e, p)) return (int32_t)p; }
    return -1;
}
static uint32_t ped_step(uint32_t t, uint32_t e, uint32_t p, int32_t stp)   /* stp free pedals on in p's category, round */
{
    uint32_t cat = PDEF[p % NPD].cat, k, at = pord_at(p);   /* (in PORD's order) */
    while (stp > 0) { for (k = 0; k < NPORD; k++) { at = (at + 1u) % NPORD; if (PDEF[PORD[at]].cat == cat && ped_free(t, e, PORD[at])) break; } p = PORD[at]; stp--; }
    while (stp < 0) { for (k = 0; k < NPORD; k++) { at = (at + NPORD - 1u) % NPORD; if (PDEF[PORD[at]].cat == cat && ped_free(t, e, PORD[at])) break; } p = PORD[at]; stp++; }
    return p;
}

static uint32_t ped_any_step(uint32_t t, uint32_t e, uint32_t p, int32_t stp)   /* stp free pedals on through the whole
                                                   library, category after category, round (the chain's PRESETS) */
{
    const uint8_t *ord = PORD;                  /* (category after category, in their order) */
    uint32_t n = NPORD, at = pord_at(p % NPD), k;
    while (stp != 0) {
        int32_t d = stp > 0 ? 1 : -1;
        for (k = 0; k < n; k++) { at = (uint32_t)(((int32_t)at + d + (int32_t)n) % (int32_t)n); if (ped_free(t, e, ord[at])) break; }
        stp -= d;
    }
    return ord[at];
}

static void ped_set(uint32_t trk, uint32_t e, uint32_t p);
static int ped_fits(void);
static int ped_cat_step(uint32_t trk, uint32_t sl, int32_t e)   /* a pedal page's PRESETS: another type (category), its
                                                   first free pedal (a type with nothing free is passed by); 1: it
                                                   was on and would overload the chip, so it is off now */
{
    uint32_t ca = PDEF[set.ped[trk][sl] % NPD].cat % NPCAT, tries;
    int32_t stp = e, dir = stp > 0 ? 1 : -1, pf = -1;
    while (stp) {
        for (tries = 0, pf = -1; tries < NPCAT && pf < 0; tries++) {
            ca = (uint32_t)(((int32_t)ca + dir + (int32_t)NPCAT) % (int32_t)NPCAT);
            pf = ped_first(trk, sl, ca);
        }
        stp -= dir;
    }
    if (pf >= 0) ped_set(trk, sl, (uint32_t)pf);
    if (set.fx[trk].on >> sl & 1u && !ped_fits()) { set.fx[trk].on &= (uint8_t)~(1u << sl); return 1; }
    return 0;
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
                                     2, 1, 1, 9, 10, 3, 4, 5, 29, 1, 10, 6, 11,    /* (LOOSE, OLD MEDIA) */
                                     11, 11, 9, 2, 1, 1, 1, 4, 20, 10};   /* (COMPRESSOR, ROTARY, BLOOM: measured as the rest) */   /* (DJ FILTER, ACID, RING MOD measured as the rest; the note pedals: next to nothing) */   /* (MUSIC BOX: measured with notes and drums playing on every track) */
static const uint8_t SP_COST[NSPACE_ALL] = {19, 9, 17, 18, 6, 15};   /* reverb, delay, tape echo, shimmer, reverse, spring */
#define PED_BUDGET 170u
static int ped_fits(void)
{
    uint32_t t, e, c = 0, sp = 0;
    for (t = 0; t < NTRK; t++)
        for (e = 0; e < NPED_SLOTS; e++)
            if (set.fx[t].on >> e & 1u || pfg[t][e]) {   /* (on, or still fading out) */
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
static uint16_t rgbm(const uint8_t *c, uint32_t k);
static uint16_t rgbk(const uint8_t *c, uint32_t k)   /* a colour at k % brightness (a light theme: k % from its background) */
{
    if (thm_light) return rgbm(c, k);
    return RGB(c[0] * k / 100u, c[1] * k / 100u, c[2] * k / 100u);
}
static uint16_t rgbl(const uint8_t *c, uint32_t k)   /* a colour k % of the way to white (the theme's HI: on a light one, ink) */
{
    const uint8_t *h = thm->c[TC_HI];
    return RGB(c[0] + ((int32_t)h[0] - c[0]) * (int32_t)k / 100, c[1] + ((int32_t)h[1] - c[1]) * (int32_t)k / 100,
               c[2] + ((int32_t)h[2] - c[2]) * (int32_t)k / 100);
}
static uint16_t trk_c(uint32_t t, uint32_t k)   /* a track's colour (red when muted) at k % */
{
    return rgbk(trk_mute >> t & 1u ? MUTE_RGB : TRK_RGB[TTYPE(t)], k);
}
static uint16_t rgbm(const uint8_t *c, uint32_t k)   /* a colour k % of the way from the background to it */
{
    const uint8_t *b = thm->c[TC_BG];
    return RGB(b[0] + ((int32_t)c[0] - b[0]) * (int32_t)k / 100, b[1] + ((int32_t)c[1] - b[1]) * (int32_t)k / 100,
               b[2] + ((int32_t)c[2] - b[2]) * (int32_t)k / 100);
}
static const uint8_t *trk_rgb(uint32_t t) { return trk_mute >> t & 1u ? MUTE_RGB : TRK_RGB[TTYPE(t)]; }
static void glow_card(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, const uint8_t *c, uint16_t fill)
{                                               /* the chosen card: a faint halo in its colour, a bright edge, the face */
    cv_rrect(x - 2, y - 2, w + 4, h + 4, r + 2, rgbm(c, 10));
    cv_rrect(x - 1, y - 1, w + 2, h + 2, r + 1, rgbm(c, 28));
    cv_rrect(x, y, w, h, r, rgbk(c, 100));
    cv_rrect(x + 1, y + 1, w - 2, h - 2, r > 0 ? r - 1 : 0, fill);
}
static void logo4(int32_t x, int32_t y)          /* QUARTET's mark: four slanted stripes in the track colours */
{
    static const uint8_t ORD[4] = {T_LEAD, T_PAD, T_BASS, T_DRUMS};
    uint32_t i;
    int32_t r;
    for (i = 0; i < 4u; i++)
        for (r = 0; r < 14; r++) cv_rect(x + (int32_t)i * 6 + (13 - r) / 3, y + r, 3, 1, rgbk(TRK_RGB[ORD[i]], 100));
}
static const char *text_fit(const char *s, int32_t maxw)   /* s cut to fit maxw pixels (FONT_S) */
{
    static char b[4][24];
    static uint32_t n;
    char *o = b[n++ & 3u];
    uint32_t i = 0;
    while (s[i] && i < 23u) { o[i] = s[i]; i++; }
    o[i] = 0;
    while (i > 1u && text_w(&FONT_S, o) > maxw) o[--i] = 0;
    return o;
}
static void disc(int32_t cx, int32_t cy, int32_t r, uint16_t c);
static void disc_clip(int32_t cx, int32_t cy, int32_t r, int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint16_t c)
{                                               /* a filled circle, only what falls inside x0..x1, y0..y1 */
    int32_t dy;
    for (dy = -r; dy <= r; dy++) {
        int32_t yy = cy + dy, h, a, b;
        if (yy < y0 || yy >= y1) continue;
        h = isqrt_small(r * r - dy * dy);
        a = cx - h < x0 ? x0 : cx - h;
        b = cx + h >= x1 ? x1 - 1 : cx + h;
        if (b >= a) cv_rect(a, yy, b - a + 1, 1, c);
    }
}
static void thick(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint16_t c)   /* a line two pixels wide */
{
    cv_line(x0, y0, x1, y1, c); cv_line(x0 + 1, y0, x1 + 1, y1, c); cv_line(x0, y0 + 1, x1, y1 + 1, c);
}
static void glyph4(uint32_t kind, int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c, uint16_t bg)
{                                               /* a type's sign: 0 a sawtooth, 1 an arch, 2 a square wave, 3 dots */
    int32_t a = w * 3 / 10, b = w * 65 / 100;
    switch (kind & 3u) {
    case 0: thick(x, y + h, x + a, y, c); thick(x + a, y, x + a, y + h, c); thick(x + a, y + h, x + b, y, c);
            thick(x + b, y, x + b, y + h, c); thick(x + b, y + h, x + w, y, c); break;
    case 1: disc_clip(x + w / 2, y + h, w / 2, x - 1, y - 1, x + w + 2, y + h, c);
            disc_clip(x + w / 2, y + h, w / 2 - 2, x - 1, y - 1, x + w + 2, y + h, bg); break;
    case 2: a = w / 3; b = w * 2 / 3;
            thick(x, y + h, x, y, c); thick(x, y, x + a, y, c); thick(x + a, y, x + a, y + h, c); thick(x + a, y + h, x + b, y + h, c);
            thick(x + b, y + h, x + b, y, c); thick(x + b, y, x + w, y, c); thick(x + w, y, x + w, y + h, c); break;
    default: disc(x + w / 5, y + h / 3, w / 6, c); disc(x + w * 3 / 4, y + h / 4, w / 8, c);
             disc(x + w / 2, y + h * 3 / 4, w / 5, c); disc(x + w * 9 / 10, y + h * 4 / 5, w / 10, c); break;
    }
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
    cv_begin(240, H_BIG, C_BG);
    cv_rrect(0, 0, 240, H_BIG, 6, C_PANEL);
    cv_rrect(6, 6, 4, 26, 2, tab);
}
static void head_text(const char *s, uint16_t c, uint16_t tab)   /* a sound's name, a message: centred */
{
    head_begin(tab);
    cv_text_bg(124 - text_w(&FONT_L, s) / 2, 3, &FONT_L, s, c, C_PANEL);
    cv_blit(0, Y_BIG);
}
/* ---- the battery: a low-battery warning (as in Conductor). The battery reaches the ADC (ch3) through a divider whose
 * values no document gives: BATT_LOW is about 10 % left for a lithium cell (~3.6 V) through an assumed 1:2 divider, an
 * ESTIMATE. The reading does not follow USB power, so the warning hides while a computer is connected */
#define BATT_LOW 558
#define BATT_OK 570                             /* (the warning clears above this) */
static int32_t batt_avg = -1;                   /* the raw reading x16, smoothed over ~4 s; -1 = none yet */
static uint32_t batt_low;
static int32_t batt_read(void)
{
#ifdef DINGHY_HOST
#ifdef HOST_BATT
    return HOST_BATT;                           /* (a host build can pretend: -DHOST_BATT=500) */
#else
    return -1;
#endif
#else
    return fm1_adc_read(FM1_ADC_BATT);
#endif
}
static uint32_t usb_powered(void)              /* plugged in to a computer: the host is sending USB frames */
{
#ifdef DINGHY_HOST
#ifdef HOST_USB
    return 1u;
#else
    return 0u;
#endif
#else
    return usb.up && usb.config && !usb.suspended;
#endif
}
static void batt_poll(void)                     /* main loop, 4 times a second */
{
    static uint32_t t;
    int32_t v;
    if (fm1_ms - t < 250u) return;
    t = fm1_ms;
    if ((v = batt_read()) < 0) return;
    if (batt_avg < 0) batt_avg = v * 16;
    batt_avg += (v * 16 - batt_avg) / 16;
    if (!batt_low && batt_avg < BATT_LOW * 16) batt_low = 1;
    else if (batt_low && batt_avg > BATT_OK * 16) batt_low = 0;
}
static void head_batt(void)                     /* the top bar: an empty battery, LOW BATTERY */
{
    head_begin(C_MUTE);
    cv_rect(16, 12, 20, 14, C_MUTE); cv_rect(18, 14, 16, 10, C_PANEL);   /* (the battery: its case, empty...) */
    cv_rect(36, 16, 3, 6, C_MUTE);              /* (...its nub, and a last sliver of charge) */
    cv_rect(20, 16, 3, 6, C_MUTE);
    cv_text_bg(52, 8, &FONT_T, "LOW BATTERY", C_MUTE, C_PANEL);
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
        cv_rrect(nx + 6, 11, 42, 16, 8, C_MUTE);
        cv_text_bg(nx + 6 + (42 - text_w(&FONT_S, "MUTE")) / 2, 11, &FONT_S, "MUTE", C_ON, C_MUTE);
        bx += 48;
    }
    if (chips && live_mode && bx + 42 <= lim) {   /* the live recording page: REC (red) while it records */
        cv_rrect(bx, 11, 42, 16, 8, live_rec ? C_MUTE : C_KEYCAP);
        cv_text_bg(bx + (42 - text_w(&FONT_S, live_rec ? "REC" : "LIVE")) / 2, 11, &FONT_S, live_rec ? "REC" : "LIVE", live_rec ? C_ON : C_TXT, live_rec ? C_MUTE : C_KEYCAP);
        bx += 48;
    }
    if (head_badge) {                           /* a pedal page: the pedal's category */
        int32_t bw = text_w(&FONT_S, head_badge) + 12;
        if (bx + bw <= lim) {
            cv_rrect(bx, 11, bw, 16, 8, C_KEYCAP);
            cv_text_bg(bx + 6, 11, &FONT_S, head_badge, C_TXT, C_KEYCAP);
            bx += bw + 6;
        }
    }
    if (chips && scr_home && (int8_t)set.oct) {   /* the keys shifted an octave or two: OCT+1 (home only) */
        char ob[6] = "OCT+0";
        int32_t o = (int8_t)set.oct, w;
        ob[3] = o < 0 ? '-' : '+'; ob[4] = (char)('0' + (o < 0 ? -o : o));
        w = text_w(&FONT_S, ob) + 10;
        if (bx + w <= lim) { cv_rrect(bx, 11, w, 16, 8, C_KEYCAP); cv_text_bg(bx + 5, 11, &FONT_S, ob, C_TXT, C_KEYCAP); bx += w + 6; }
    }
    if (chips && set.sseq_on) {                 /* SUPER SEQ plays the patterns: SUPER SEQ ON (or SUPER, if short of room) */
        const char *bt = bx + 106 <= lim ? "SUPER SEQ ON" : bx + 50 <= lim ? "SUPER" : 0;
        if (bt) {
            cv_rrect(bx, 11, text_w(&FONT_S, bt) + 10, 16, 8, C_TXT);
            cv_text_bg(bx + 5, 11, &FONT_S, bt, C_BG, C_TXT);
        }
    }
    if (chips && !blank) {
        l[0] = (char)('A' + (set.sseq_on ? set.sel[trk] : play_pat[trk]) % NPAT);   /* (SUPER SEQ: the one you edit) */
        cv_rrect(x, 4, 32, 30, 8, col);
        cv_text_bg(x + 8, 3, &FONT_L, l, C_ON, col);
        if (q) {
            l[0] = (char)('A' + set.sel[trk] % NPAT);
            cv_text_bg(195, 11, &FONT_S, ">", C_SUB, C_PANEL);
            cv_rrect(204, 4, 32, 30, 8, col);
            cv_rrect(206, 6, 28, 26, 7, C_PANEL);
            cv_text_bg(212, 3, &FONT_L, l, col, C_PANEL);
        }
    }
    cv_blit(0, Y_BIG);
}
static void head_mixer(void)                    /* MIXER, the stripe in the four track colours */
{
    cv_begin(240, H_BIG, C_BG);
    cv_rrect(0, 0, 240, H_BIG, 6, C_PANEL);
    logo4(6, 12);
    cv_text_bg(36, 3, &FONT_L, "MIXER", C_TXT, C_PANEL);
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

static const uint8_t *ped_rgb(const pdef_t *d)  /* a pedal's colour for the screen (a black pedal: light grey) */
{
    static const uint8_t LIGHT[3] = {200, 206, 220};
    return d->rgb[0] + d->rgb[1] + d->rgb[2] < 150u ? LIGHT : d->rgb;
}
static void bottom_pedals(uint32_t trk);       /* home's bottom line (with the pedal pages) */
static void bottom_bar(const char *s, uint16_t dot)   /* the bottom bar: the text centred, a dot when there is room */
{
    int32_t w = text_w(&FONT_S, s), x = 120 - w / 2;
    cv_begin(240, H_BOT, C_BG);
    cv_rrect(0, 0, 240, H_BOT, 6, C_PANEL);
    if (w <= 208) { x += 5; disc(x - 10, 12, 3, dot); }
    cv_text_bg(x, 4, &FONT_S, s, C_BOTTXT, C_PANEL);
    cv_blit(0, Y_BOT);
}

static uint32_t kit_list(uint8_t *ids, uint32_t cur);
static uint32_t id_bank(uint32_t t, uint32_t id);
static uint32_t snd_pg;                         /* the sound editor's page: 0 INSTR (drums: KIT), then MAIN OSC ENV LFO (drums: a drum each) */
static uint32_t usr_slot, usr_ask, usr_t;       /* the USER slot to save to (KNOB4 on INSTR); "sure?" (REC again within 3 s, any page) */
static uint32_t chain_sl;                       /* the signal chain (EDIT twice): the pedal slot chosen, 0..3 */
static uint32_t cp_trk, cp_pat, cp_ask, cp_t, cp_live;   /* the copy page (SEQ held): the pattern to copy from; "sure?"
                                                   (REC again within 3 s); back to the live page (else the step recorder) */
static const char *const WAVE_NAME[NWAVE] = {"SIN", "TRI", "SAW", "SQR", "NOISE", "FM", "PULSE", "SYNC"};
static const char *const LTGT_NAME[NLTGT] = {"PITCH", "FILT", "AMP", "PWM"};

/* the bottom bar: the sound page and what KNOB1-4 set on it (drums: the chosen drum); HOME + ALGORITHM: octave, MIDI;
 * a dot in the track's colour when there is room */
static void draw_status(int32_t oct, uint32_t trk, uint32_t show_ch, uint32_t page, uint32_t drum, uint32_t proj,
                        uint32_t cpu)   /* proj: legend (1 synth edit, 2 drum edit, 3 / 4 live synth / drums) << 6; cpu: % (> 100: no meter, the PC) */
{
    char s[40], *p = s;
    if (show_ch) {
        p = put(p, "OCT "); *p++ = oct < 0 ? '-' : '+'; *p++ = (char)('0' + (oct < 0 ? -oct : oct));
        p = put(p, "  MIDI ");
        if (set.ch) { p = put(p, "CH"); p = put_num(p, set.ch, 1); } else p = put(p, "OMNI");
    } else if (proj >> 6) {                     /* the SEQ (edit) screen: what each knob does */
        static const char *const LEG[6] = {"", "", "K1:BARS  K3:EXP  K4:PAGE",
                                           "K1:BARS K2:STEP K3:PG REC:REC", "K1:BARS  K3:PAGE  REC:RECORD",
                                           "REC AGAIN: CLEAR EVERYTHING"};
        if ((proj >> 6) % 6u == 1u) {           /* synth: STEP 17/64  QUARTER (K1 BARS K2 LEN K3 DYN K4 NOTE on the info line) */
            uint32_t L = pat_len(trk, set.sel[trk] % NPAT);
            p = put_num(p, drum + 1u, 1); *p++ = '/'; p = put_num(p, L, 1);   /* 7/16  QUARTER  NORMAL */
            *p++ = ' '; *p++ = ' '; p = put(p, NLEN_NAME[ui_len % NNLEN]);
            *p++ = ' '; p = put(p, DYN_LONG[ui_dyn % NDYN]);
        } else p = put(p, LEG[(proj >> 6) % 6u]);
    } else if (page == PG_REC) {                /* the RECORD page */
        p = put(p, "ALG:CHOOSE  REC REC:CLEAR");
    } else if (page == PG_KEY) {                /* the key page */
        p = put(p, "ALG:KEY K1:LOCK K2:LIGHT K3:SW");
    } else if (page == PG_PROJ) {               /* the save page: how to work it */
        p = put(p, "K1:SLOT  REC:SAVE  PLAY:LOAD");
    } else if (page == PG_ARP) {                /* the arpeggiator: how to work it */
        p = put(p, trk < NSYN ? "TAP ARP OR K1: ON/OFF" : "HOME:BACK");
    } else if (page == PG_MIX) {                /* the mixer */
        p = put(p, "FX, SEL, ENV, LFO: MUTE TRACKS");
    } else if (page == PG_SOUND) {              /* the sound editor */
        if (usr_ask) { p = put(p, "REC AGAIN: SAVE TO USER "); *p++ = (char)('1' + usr_slot); }
        else { p = put(p, "ALG:PAGE  REC TWICE:SAVE U"); *p++ = (char)('1' + usr_slot); }
    } else if (page == PG_CHAIN) {              /* the signal chain: what KNOB2-4 turn on the chosen pedal */
        pknob_t pk[3];
        uint32_t k, n;
        pedal_knobs(trk, chain_sl % NPED_SLOTS, pk);
        for (k = 0; k < 3u; k++) {
            if (k) *p++ = ' ';
            *p++ = 'K'; *p++ = (char)('2' + k); *p++ = ':';
            for (n = 0; pk[k].name[n] && n < 6u; n++) *p++ = pk[k].name[n];
        }
    } else if (page == PG_COPY) {               /* the copy page */
        p = put(p, cp_ask ? "REC AGAIN: COPY" : "REC TWICE:COPY  SEQ:BACK");
    } else if (IS_FX_PAGE(page)) {              /* a pedal: the values are on the pedal; here, how to work it */
        p = put(p, "PRESETS:TYPE  ALG:PEDAL");   /* (KNOB1 or holding the button: on / off) */
    } else { bottom_pedals(trk); return; }     /* home: the track's pedals (its sound shows on its card) */
    *p = 0;
    bottom_bar(s, page == PG_MIX ? C_TXT : trk_c(trk, 100));
}

/* the info line: little panels. Home: play / stop, the tempo, four beat lights, the octave. SEQ: one cell above each
 * knob: the step recorder BARS, LEN (the note's), DYN, the note; live BARS, STEP, PG, REC */
static uint32_t syn_covers(uint32_t t, uint32_t pt, uint32_t s, uint32_t len);
#define SWING_PCT(v) (50u + (((uint32_t)(v) & 127u) * 16u + 63u) / 127u)   /* set.swing -> 50 .. 66 % (nearest) */
static void info_cell(int32_t x, const char *lab, const char *val, uint16_t lc, uint16_t vc)
{
    int32_t tw = (lab[0] ? text_w(&FONT_S, lab) + 4 : 0) + text_w(&FONT_S, val), tx = x + 30 - tw / 2;
    cv_rrect(x + 1, 1, 58, 20, 10, C_PANEL);
    if (lab[0]) tx = cv_text_bg(tx, 3, &FONT_S, lab, lc, C_PANEL) + 4;   /* (no label: the value alone, centred) */
    cv_text_bg(tx, 3, &FONT_S, val, vc, C_PANEL);
}
static uint16_t mix565(uint16_t a, uint16_t b, int32_t k);
static void draw_info(uint32_t trk, uint32_t mode, uint32_t cur, uint32_t page, int32_t oct)   /* mode 0 home, 1 synth edit, 2 drum edit, 3 live; page: shown */
{
    char v[8], *p;
    uint32_t len = pat_len(trk, (mode == 3u ? play_pat[trk] : set.sel[trk]) % NPAT), i;   /* (live: the pattern playing) */
    uint16_t col = trk_c(trk, 100);
    cv_begin(240, H_INFO, C_BG);
    if (mode) {                                 /* the step recorder: BARS, LEN, DYN, NOTE (drums: BARS, DYN, PG); the
                                                   live page: BARS, STEP, PG, REC */
        uint32_t sy = mode == 1u || (mode == 3u && trk < NSYN), pg = page;   /* (page: the one shown) */
        *put_num(v, len / PAGE, 1) = 0;
        info_cell(0, "BARS", v, C_SUB, col);
        if (mode == 1u) {                       /* synth: KNOB2 the length, KNOB3 the dynamic, KNOB4 the note */
            const uint8_t *nt = step_note(trk, set.sel[trk] % NPAT, cur);
            info_cell(60, "LEN", NLEN_SHORT[ui_len % NNLEN], C_SUB, col);
            info_cell(120, "", DYN_SHORT[ui_dyn % NDYN], C_SUB, col);
            if (nt[0]) { *put_note(v, nt[0]) = 0; info_cell(180, "", v, C_SUB, col); }
            else info_cell(180, "", syn_covers(trk, set.sel[trk] % NPAT, cur, len) ? "HELD" : "REST", C_DIMC, C_DIMC);
        } else if (mode == 2u) {                /* drums: KNOB3 the dynamic, KNOB4 the page */
            info_cell(60, "", "--", C_DIMC, C_DIMC);
            info_cell(120, "", DYN_SHORT[ui_dyn % NDYN], C_SUB, col);
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
        int32_t bx;                             /* play, then the four knobs: the tempo, the chosen track's mix level and
                                                   brightness (bars in its colour), the pedals' fade */
        (void)oct; (void)i;
        cv_rrect(0, 1, 20, 20, 10, C_PANEL);
        if (clk_run) for (i = 0; i < 9u; i++) cv_rect(6 + (int32_t)i, 6 + (int32_t)i / 2, 1, 11 - ((int32_t)i / 2) * 2, C_PLAYG);
        else cv_rect(5, 6, 10, 10, C_SUB);
        cv_rrect(22, 1, 52, 20, 10, C_PANEL);   /* KNOB1: the tempo */
        *put_num(v, set.bpm, 1) = 0;
        bx = 48 - (text_w(&FONT_S, v) + 3 + text_w(&FONT_S, "BPM")) / 2;
        cv_text_bg(cv_text_bg(bx, 3, &FONT_S, v, C_TXT, C_PANEL) + 3, 3, &FONT_S, "BPM", C_SUB, C_PANEL);
        uint32_t kdec = 0;                      /* (drums: the kit's decay, its average) */
        for (i = 0; i < NDR; i++) kdec += set.dsnd[i].decay;
        kdec /= NDR;
        {                                       /* KNOB2 the mix (a speaker), KNOB3 the brightness (a sun; drums: the
                                                   decay, a hit dying away): a sign and a bar */
            for (i = 0; i < 2u; i++) {
                int32_t x = 76 + (int32_t)i * 62, bx0 = x + 25, bw = 29, f, j;   /* (both bars the same: x + 25 .. + 54) */
                uint32_t vv = i ? (trk < NSYN ? set.snd[trk].cut : kdec) : set.vol[trk];
                uint16_t ic = C_SUB;
                cv_rrect(x, 1, 60, 20, 10, C_PANEL);
                if (!i) { cv_rect(x + 9, 9, 3, 5, ic); for (j = 0; j < 5; j++) cv_rect(x + 12 + j, 11 - j, 1, 2 * j + 1, ic); }
                else if (trk >= NSYN) {         /* (a hit: straight up, then dying away) */
                    cv_line(x + 9, 17, x + 10, 4, ic); cv_line(x + 10, 17, x + 11, 4, ic);
                    cv_line(x + 11, 4, x + 14, 10, ic); cv_line(x + 14, 10, x + 18, 14, ic); cv_line(x + 18, 14, x + 23, 16, ic);
                    cv_line(x + 11, 5, x + 14, 11, ic); cv_line(x + 14, 11, x + 18, 15, ic); cv_line(x + 18, 15, x + 23, 17, ic);
                } else {
                    disc(x + 15, 11, 3, ic);   /* (the sun: in from the cell's round end, its rays whole) */
                    cv_rect(x + 15, 4, 1, 2, ic); cv_rect(x + 15, 17, 1, 2, ic); cv_rect(x + 8, 11, 2, 1, ic); cv_rect(x + 21, 11, 2, 1, ic);
                    cv_rect(x + 10, 6, 1, 1, ic); cv_rect(x + 20, 6, 1, 1, ic); cv_rect(x + 10, 16, 1, 1, ic); cv_rect(x + 20, 16, 1, 1, ic);
                }
                cv_rrect(bx0, 7, bw, 8, 4, C_KEYCAP);
                f = (int32_t)vv * bw / 127;
                if (f > 2) cv_rrect(bx0, 7, f, 8, 4, col);
            }
        }
        cv_rrect(200, 1, 40, 20, 10, C_PANEL);  /* KNOB4: the fade (a fade ramp) and its bars (- at once) */
        {
            uint32_t fb = set.mu.ub.pfade <= 4u ? set.mu.ub.pfade : 0u;
            uint16_t fc = fb ? C_SUB : C_DIMC;  /* (a fade ramp: low, a slope up, high) */
            thick(203, 15, 207, 15, fc); thick(207, 15, 214, 6, fc); thick(214, 6, 218, 6, fc);
            v[0] = fb ? (char)('0' + fb) : '-'; v[1] = 0;
            cv_text_bg(224, 3, &FONT_S, v, fb ? C_TXT : C_DIMC, C_PANEL);
        }
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

/* the home view: a pill a track, LEAD PAD BASS DRUMS (as on the screen) in its colour: its sign, its name, its sound,
 * its pattern. The chosen track's pill is bright and opens out with the 16 steps of the page it is on (the step playing
 * white); the others are slim, with a little strip of their steps. A muted track turns red */
static uint32_t slot_nth(uint32_t t);
#define Y_HOME 62                               /* home's pills: from under the info line... */
#define H_HOME 154                              /* ...to the bottom bar (two halves of 77), black space above the pedals */
static void draw_home(uint32_t sel, uint32_t run)
{
    uint32_t r, s, half;
    for (half = 0; half < 2u; half++) {
        int32_t y = 0;
        cv_begin(240, H_HOME / 2, C_BG);
        cv_oy = -(int32_t)half * (H_HOME / 2);
        for (r = 0; r < NTRK; r++) {
            uint32_t t = SEQ_ORDER[r], me = t == sel;
            uint32_t pt = play_pat[t] % NPAT, len = pat_len(t, pt), pos = trk_pos[t], page = run ? pos / PAGE : 0u;
            const uint8_t *rgb = trk_rgb(t);
            uint32_t sty = thm->style, wash = sty == 1u || (sty == 2u && !me);   /* (washed / bordered rows: the theme's) */
            uint16_t col = rgbk(rgb, 100), bg = sty == 1u ? rgbm(rgb, me ? 24 : 11) : sty == 2u && !me ? C_BG : me ? col : rgbm(rgb, 34);
            uint16_t ink = wash ? col : me ? C_ON : col, edge = sty == 1u ? (me ? col : rgbm(rgb, 50)) : col;
            uint16_t sub = wash ? (me ? C_TXT : rgbl(rgb, 35)) : me ? rgbm(rgb, 22) : rgbl(rgb, 35);
            int32_t h = me ? 62 : 25, pw, nx, lim;
            char lt[4], *q;
            q = put_pat(lt, t); *q = 0;
            if (wash) { int32_t bw = me || sty == 2u ? 2 : 1; cv_rrect(4, y, 232, h, 10, edge); cv_rrect(4 + bw, y + bw, 232 - 2 * bw, h - 2 * bw, 10 - bw, bg); }
            else cv_rrect(4, y, 232, h, 10, bg);
            glyph4(TTYPE(t), 11, y + 7, 18, 12, ink, bg);
            nx = cv_text_bg(36, y + 5, &FONT_S, slot_name(t), ink, bg);
            pw = text_w(&FONT_S, lt) + 12;
            if (pw < 26) pw = 26;
            lim = me ? 231 - pw - 6 : 145;
            cv_text_bg(nx + 8, y + 5, &FONT_S, text_fit(t < NSYN ? sound_name(TTYPE(t), set.snd[t].preset) : kit_name(), lim - nx - 8),
                       sub, bg);
            {
                uint16_t pf = wash ? (me ? col : rgbm(rgb, sty == 2u ? 40 : 30)) : me ? C_BG : rgbm(rgb, 18), pt = wash && me ? C_ON : col;
                cv_rrect(231 - pw, y + 4, pw, 18, 7, pf);
                cv_text_bg(231 - pw / 2 - text_w(&FONT_S, lt) / 2, y + 5, &FONT_S, lt, pt, pf);
            }
            if (me && !wash) cv_rrect(10, y + 28, 220, 29, 7, rgbm(rgb, 24));   /* (the chosen: its steps, big) */
            for (s = 0; s < PAGE; s++) {
                uint32_t st = page * PAGE + s, on = 0;
                uint16_t sc;
                if (st >= len) continue;
                if (t == T_DRUMS) { uint32_t d; for (d = 0; d < NDR; d++) on |= dbit(pt, d, st); on *= 2u; }
                else on = syn_covers(t, pt, st, len);
                sc = on == 2u ? col : on == 1u ? rgbm(rgb, wash ? 45u : 60u) : wash && me ? C_EMPTY : rgbm(rgb, me ? 42u : 55u);
                if (run && st == pos) sc = wash ? C_PLAYHD : C_WHITE;
                if (me) cv_rrect(14 + (int32_t)(s * 27u / 2u), y + 33, 11, 19, 3, sc);
                else cv_rect(148 + (int32_t)s * 3, y + 9, 2, 8, on || (run && st == pos) ? sc : rgbm(rgb, 58));
            }
            y += h + 4;
        }
        cv_oy = 0;
        cv_blit(0, Y_HOME + half * (H_HOME / 2));
    }
}

/* the drums: a row a drum (its short name in a tile, the chosen drum lit), a column a step of the page shown (= a white key) */
static void draw_grid(uint32_t pt, uint32_t drum, uint32_t page, uint32_t play, uint32_t len)   /* play: the step playing, or none */
{
    static const char *const DR_SHORT_ALL[NDR] = {"BD", "SD", "HH", "OH", "CP", "PC"};
    static const char *const DR_SHORT_LM1[NDR] = {"BD", "SD", "HH", "OH", "TL", "TH"};   /* (the LM-1: its two toms) */
    const char *const *DR_SHORT = kit_now() == KIT_LM1 ? DR_SHORT_LM1 : DR_SHORT_ALL;
    uint32_t d, s;
    uint16_t col = trk_c(T_DRUMS, 100), dim = trk_c(T_DRUMS, 22);
    cv_begin(240, H_VIEW, C_BG);
    for (d = 0; d < NDR; d++) {
        int32_t y = (int32_t)d * 21;
        uint32_t me = d == drum;
        cv_rrect(0, y + 1, 240, 20, 4, me ? C_PANEL2 : C_PANEL);
        cv_rrect(0, y + 1, 24, 20, 4, me ? col : dim);
        cv_text_bg(4, y + 3, &FONT_S, DR_SHORT[d], me ? C_ON : col, me ? col : dim);
        for (s = 0; s < PAGE; s++) {
            uint32_t st = page * PAGE + s, on;
            uint16_t c;
            if (st >= len) continue;
            on = dbit(pt, d, st);
            {
                static const uint8_t PCT[NDYN] = {100, 82, 64, 46, 100, 100, 70, 100, 70};   /* (soft, short: dimmer) */
                uint32_t dy = on ? ddyn_get(pt, d, st) : 0u;
                c = on ? trk_c(T_DRUMS, PCT[dy] * (me ? 100u : 62u) / 100u) : s % 4u == 0u ? C_EMPTY4 : C_EMPTY;
                if (st == play) c = on ? C_WHITE : C_HEADC;
                cv_rrect(28 + (int32_t)s * 13, y + 4, 11, 14, 2, c);
                if (dy == 4u || dy == 5u) cv_rect(31 + (int32_t)s * 13, y + 6, 5, 2, st == play ? C_BG : C_WHITE);   /* (an accent) */
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
    cv_begin(240, H_VIEW, C_BG);
    for (k = 0; k < 62; k++) {
        uint32_t bk = black((uint32_t)(lo + k));
        cv_rect(0, H_VIEW - 5 - 2 * k, bk ? 7 : 9, 2, bk ? C_PKB : C_PKW);
    }
    for (s = 0; s < PAGE; s++) {
        uint32_t st = page * PAGE + s;
        uint16_t c;
        if (st >= len) continue;
        c = st == cur ? tint : s % 4u == 0u ? C_GRID4 : C_GRID;
        cv_rect(12 + (int32_t)s * 14, 0, 13, H_VIEW, c);
    }
    for (k = 0; k < 62; k++)
        if ((lo + k) % 12 == 0) cv_rect(10, H_VIEW - 5 - 2 * k, 228, 1, C_GRIDL);
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
                static const uint8_t PCT[NDYN] = {100, 82, 64, 46, 100, 100, 100, 100, 100};   /* (the SOFTs dimmer) */
                uint32_t dy = sdyn_get(t, pt, st);
                if (dy == 6u && w > 6) w = 6;   /* (staccato: short) */
                cv_rrect(x - 1, y - 1, w + 2, 8, 3, rgbm(trk_rgb(t), 28));   /* (a soft glow) */
                cv_rrect(x, y, w, 6, 3, st == cur ? C_WHITE : dy ? trk_c(t, PCT[dy]) : col);
                if (dy == 4u || dy == 5u) cv_rect(x + 1, y, w - 2 > 0 ? w - 2 : 1, 2, st == cur ? C_BG : C_WHITE);   /* (an accent, loud) */
                if (dy == 7u) cv_rect(x + w - 1, y + 2, 3, 2, st == cur ? C_WHITE : col);   /* (legato: tied on) */
                if (dy == 8u) cv_rect(x + 1, y + 2, 2, 2, st == cur ? C_BG : C_BG);   /* (pizzicato: a dot) */
            }
        }
    }
    if (play != ~0u && play / PAGE == page)     /* the playhead */
        cv_rect(12 + (int32_t)(play % PAGE) * 14 + 5, 0, 2, H_VIEW, C_PLAYHD);
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
    disc(cx, cy, 16, C_BG);
    disc(cx, cy, 14, RGB(56, 64, 88));
    disc(cx, cy, 12, RGB(34, 41, 60));
    deg = -135 + (int32_t)(270u * v / mx);
    knob_xy(cx, cy, deg, 4, &x0, &y0);
    knob_xy(cx, cy, deg, 12, &x1, &y1);
    cv_line(x0, y0, x1, y1, C_WHITE);           /* the pointer, two pixels wide */
    cv_line(x0 + 1, y0, x1 + 1, y1, C_WHITE);
}
static void mini_knob(int32_t cx, int32_t cy, uint32_t v, uint32_t mx, uint16_t ring, uint16_t dim)   /* a small knob */
{
    int32_t i, x0, y0, x1, y1, deg;
    if (!mx) mx = 1;
    for (i = 0; i <= 12; i++) {                 /* its ring: 13 dots, lit up to the value */
        knob_xy(cx, cy, -135 + i * 45 / 2, 13, &x0, &y0);
        cv_rect(x0 - 1, y0 - 1, 2, 2, (uint32_t)i * mx <= v * 12u && v ? ring : dim);
    }
    disc(cx, cy, 9, C_KEYCAP);
    disc(cx, cy, 8, RGB(34, 41, 60));
    deg = -135 + (int32_t)(270u * (v > mx ? mx : v) / mx);
    knob_xy(cx, cy, deg, 2, &x0, &y0);
    knob_xy(cx, cy, deg, 7, &x1, &y1);
    cv_line(x0, y0, x1, y1, C_TXT);
    cv_line(x0 + 1, y0, x1 + 1, y1, C_TXT);
}
static void lcd_value(int32_t cx, int32_t y, int32_t w, const char *s, uint16_t c)   /* a value on a little dark screen */
{
    cv_rrect(cx - w / 2, y, w, 18, 4, C_LCD);
    cv_text_bg(cx - text_w(&FONT_S, s) / 2, y + 1, &FONT_S, s, c, C_LCD);
}

/* ---- a pedal page (each pedal, the arpeggiator): drawn like a screen-printed boutique pedal. Each has its own art
 * across the whole case (a storm, a lighthouse, a melting clock...), its name lettered (Noto Serif Bold, or the plain
 * face for the digital ones), three knobs (KNOB2-4: knurled coloured caps or black skirted ones) with their names and
 * values, an LED that glows when it is on (dark, with OFF beside it, when off) and often a footswitch.
 * Everything is lines, polygons and circles, in screen coordinates: the case is 6..234 x 44..208, drawn in two halves */
typedef struct { const char *name; uint32_t v, mx; const char *val; } kview_t;

static int32_t ab_x0, ab_y0, ab_x1, ab_y1;      /* the art's clip box: the case */
static void ap(int32_t x, int32_t y, uint16_t c) { if (x >= ab_x0 && x < ab_x1 && y >= ab_y0 && y < ab_y1) cv_pset(x, y, c); }
static void arow(int32_t x0, int32_t x1, int32_t y, uint16_t c)   /* a row x0..x1, clipped to the case */
{
    if (y < ab_y0 || y >= ab_y1) return;
    if (x0 < ab_x0) x0 = ab_x0;
    if (x1 >= ab_x1) x1 = ab_x1 - 1;
    if (x1 >= x0) cv_rect(x0, y, x1 - x0 + 1, 1, c);
}
static int32_t row_vis(int32_t y) { return y + cv_oy >= 0 && y + cv_oy < (int32_t)cv_h; }
static void arect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c)   /* a rectangle, clipped to the case */
{
    int32_t j;
    for (j = y; j < y + h; j++) if (row_vis(j)) arow(x, x + w - 1, j, c);
}
static void arrect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint16_t c)   /* round corners, clipped */
{
    int32_t i;
    if (2 * r > h) r = h / 2;
    if (2 * r > w) r = w / 2;
    for (i = 0; i < h; i++) {
        int32_t d = i < r ? r - 1 - i : i >= h - r ? i - (h - r) : -1, ins = d < 0 ? 0 : r - isqrt_small(r * r - d * d);
        if (row_vis(y + i)) arow(x + ins, x + w - 1 - ins, y + i, c);
    }
}
static uint16_t cv_get(int32_t x, int32_t y)
{
    x += cv_ox;
    y += cv_oy;
    if ((uint32_t)x < cv_w && (uint32_t)y < cv_h) return swap16(cv_px[(uint32_t)y * cv_w + (uint32_t)x]);
    return C_BG;
}
static uint16_t mix565(uint16_t a, uint16_t b, int32_t k)   /* a, k % of the way to b */
{
    int32_t r = a >> 11, g = (a >> 5) & 63, bl = a & 31;
    r += ((int32_t)(b >> 11) - r) * k / 100;
    g += ((int32_t)((b >> 5) & 63) - g) * k / 100;
    bl += ((int32_t)(b & 31) - bl) * k / 100;
    return (uint16_t)((r << 11) | (g << 5) | bl);
}
#define K_BLACK RGB(0, 0, 0)
#define K_WHITE RGB(255, 255, 255)
static int32_t acs(int32_t deg, int32_t r)   /* r cos(deg), r sin(deg): screen degrees, clockwise from east */
{
    uint32_t ph = (uint32_t)((deg % 360 + 360) % 360) * 11930465u;
    return r * sine(ph + 0x40000000u) / 32767;
}
static int32_t asn(int32_t deg, int32_t r)
{
    uint32_t ph = (uint32_t)((deg % 360 + 360) % 360) * 11930465u;
    return r * sine(ph) / 32767;
}
static void aplot(int32_t x, int32_t y, uint16_t c, int32_t w)
{
    ap(x, y, c);
    if (w >= 2) { ap(x + 1, y, c); ap(x, y + 1, c); }
    if (w >= 3) { ap(x - 1, y, c); ap(x, y - 1, c); ap(x + 1, y + 1, c); }
}
static void aline(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint16_t c, int32_t w)   /* a line w pixels wide */
{
    int32_t dx = x1 > x0 ? x1 - x0 : x0 - x1, sx = x0 < x1 ? 1 : -1;
    int32_t dy = y1 > y0 ? y0 - y1 : y1 - y0, sy = y0 < y1 ? 1 : -1;
    int32_t err = dx + dy, guard = 2000;
    while (guard--) {
        int32_t e2 = 2 * err;
        aplot(x0, y0, c, w);
        if (x0 == x1 && y0 == y1) break;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}
static void aoutline(const int16_t *p, uint32_t n, uint16_t c, int32_t w)   /* a closed outline */
{
    uint32_t i;
    for (i = 0; i < n; i++) aline(p[2 * i], p[2 * i + 1], p[2 * ((i + 1) % n)], p[2 * ((i + 1) % n) + 1], c, w);
}
static void afill(const int16_t *p, uint32_t n, uint16_t c, int32_t gap, int32_t dx8)
{                                               /* a filled polygon, or hatched: lines gap px apart, leaning dx8 / 8 px a row */
    int32_t y, y0 = 9999, y1 = -9999, xs[16];
    uint32_t i, j, m;
    for (i = 0; i < n; i++) { if (p[2 * i + 1] < y0) y0 = p[2 * i + 1]; if (p[2 * i + 1] > y1) y1 = p[2 * i + 1]; }
    if (y0 < ab_y0) y0 = ab_y0;
    if (y1 > ab_y1 - 1) y1 = ab_y1 - 1;
    if (y0 < -cv_oy) y0 = -cv_oy;
    if (y1 > (int32_t)cv_h - cv_oy - 1) y1 = (int32_t)cv_h - cv_oy - 1;
    for (y = y0; y <= y1; y++) {
        m = 0;
        for (i = 0; i < n; i++) {
            int32_t xa = p[2 * i], ya = p[2 * i + 1], xb = p[2 * ((i + 1) % n)], yb = p[2 * ((i + 1) % n) + 1];
            if (((ya <= y && yb > y) || (yb <= y && ya > y)) && m < 16u) xs[m++] = xa + (y - ya) * (xb - xa) / (yb - ya);
        }
        for (i = 1; i < m; i++)
            for (j = i; j > 0 && xs[j - 1] > xs[j]; j--) { int32_t t = xs[j]; xs[j] = xs[j - 1]; xs[j - 1] = t; }
        for (i = 0; i + 1 < m; i += 2) {
            int32_t x, g8 = gap * 8;
            if (!gap) { arow(xs[i], xs[i + 1], y, c); continue; }
            for (x = xs[i]; x <= xs[i + 1]; x++) { int32_t t = (x * 8 - y * dx8) % g8; if (t < 0) t += g8; if (t < 8) ap(x, y, c); }
        }
    }
}
#define NPT(a) (sizeof(a) / sizeof(a[0]) / 2u)
static void aell(int32_t cx, int32_t cy, int32_t rx, int32_t ry, uint16_t c)   /* a filled ellipse (rx = ry: a disc) */
{
    int32_t dy;
    for (dy = -ry; dy <= ry; dy++) if (row_vis(cy + dy)) { int32_t h = rx * isqrt_small(ry * ry - dy * dy) / (ry ? ry : 1); arow(cx - h, cx + h, cy + dy, c); }
}
static void adisc(int32_t cx, int32_t cy, int32_t r, uint16_t c) { aell(cx, cy, r, r, c); }
static void aring(int32_t cx, int32_t cy, int32_t r, int32_t w, uint16_t c)   /* a circle outline, w px thick */
{
    int32_t dy, ri = r - w;
    for (dy = -r; dy <= r; dy++) {
        int32_t ho;
        if (!row_vis(cy + dy)) continue;
        ho = isqrt_small(r * r - dy * dy);
        if (ri > 0 && dy * dy < ri * ri) { int32_t hi = isqrt_small(ri * ri - dy * dy); arow(cx - ho, cx - hi - 1, cy + dy, c); arow(cx + hi + 1, cx + ho, cy + dy, c); }
        else arow(cx - ho, cx + ho, cy + dy, c);
    }
}
static void aarc(int32_t cx, int32_t cy, int32_t rx, int32_t ry, int32_t d0, int32_t d1, uint16_t c, int32_t w)
{                                               /* an arc from d0 to d1 degrees */
    int32_t d, x = cx + acs(d0, rx), y = cy + asn(d0, ry);
    for (d = d0 + 6; ; d += 6) {
        int32_t dd = d > d1 ? d1 : d, nx = cx + acs(dd, rx), ny = cy + asn(dd, ry);
        aline(x, y, nx, ny, c, w);
        x = nx; y = ny;
        if (dd == d1) break;
    }
}
static void astars(uint32_t seed, int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t n, uint16_t c)
{                                               /* a scatter of dots, little crosses and diamonds */
    uint32_t s = seed, i, k;
    for (i = 0; i < n; i++) {
        int32_t x, y;
        s = s * 1664525u + 1013904223u; x = x0 + (int32_t)((s >> 16) % (uint32_t)(x1 - x0));
        s = s * 1664525u + 1013904223u; y = y0 + (int32_t)((s >> 16) % (uint32_t)(y1 - y0));
        s = s * 1664525u + 1013904223u; k = (s >> 16) % 20u;
        if (k < 11u) { ap(x, y, c); if (k < 3u) ap(x + 1, y, c); }
        else if (k < 16u) { aline(x - 2, y, x + 2, y, c, 1); aline(x, y - 2, x, y + 2, c, 1); }
        else { ap(x, y - 2, c); ap(x + 1, y - 1, c); ap(x + 2, y, c); ap(x + 1, y + 1, c); ap(x, y + 2, c); ap(x - 1, y + 1, c); ap(x - 2, y, c); ap(x - 1, y - 1, c); }
    }
}
static uint32_t rnd_n(uint32_t *s, uint32_t n) { *s = *s * 1664525u + 1013904223u; return (*s >> 16) % n; }
static int32_t atext(int32_t x, int32_t y, const felucca_font_t *f, const char *s, uint16_t c, int32_t sp)
{                                               /* text blended onto whatever is under it, sp more px between letters, inside the art's box */
    for (; *s; s++) {
        uint32_t gi = glyph(f, (uint8_t)*s), gx, gy, w = f->bw[gi], bpr = (w + 1u) / 2u;
        const uint8_t *gd = f->data + f->off[gi];
        for (gy = 0; gy < f->h; gy++) {
            int32_t yy = y + (int32_t)gy;
            if (!row_vis(yy)) continue;
            for (gx = 0; gx < w; gx++) {
                uint32_t v = gd[gy * bpr + gx / 2u];
                int32_t xx = x - f->pad + (int32_t)gx;
                v = (gx & 1u) ? (v & 15u) : (v >> 4);
                if (v && xx >= ab_x0 && xx < ab_x1 && yy >= ab_y0 && yy < ab_y1)   /* (kept inside the art's box) */
                    cv_pset(xx, yy, v == 15u ? c : mix565(cv_get(xx, yy), c, (int32_t)v * 100 / 15));
            }
        }
        x += f->adv[gi] + sp;
    }
    return x - sp;
}
static int32_t atext_halo(int32_t x, int32_t y, const felucca_font_t *f, const char *s, uint16_t c, uint16_t halo, int32_t sp)
{                                               /* text with a thin outline in the case's colour, to read over the art */
    atext(x - 1, y, f, s, halo, sp); atext(x + 1, y, f, s, halo, sp); atext(x, y - 1, f, s, halo, sp); atext(x, y + 1, f, s, halo, sp);
    return atext(x, y, f, s, c, sp);
}
static int32_t atext_w(const felucca_font_t *f, const char *s, int32_t sp)
{
    int32_t n = 0;
    const char *p;
    for (p = s; *p; p++) n++;
    return text_w(f, s) + (n ? (n - 1) * sp : 0);
}
static void knob_knurl(int32_t cx, int32_t cy, uint32_t pm, uint16_t cap, uint16_t ptr, int32_t r)
{                                               /* a coloured knurled cap, a white line for its pointer; pm 0..1000 */
    int32_t i, deg = 135 + (int32_t)(pm * 270u / 1000u);
    adisc(cx + 1, cy + 2, r + 1, mix565(cap, K_BLACK, 70));
    adisc(cx, cy, r + 1, mix565(cap, K_BLACK, 25));
    for (i = 0; i < 360; i += 15) ap(cx + acs(i, r + 1), cy + asn(i, r + 1), mix565(cap, K_BLACK, 55));
    adisc(cx, cy, r - 2, cap);
    adisc(cx - 3, cy - 4, r > 9 ? r - 8 : 1, mix565(cap, K_WHITE, 18));
    aline(cx + acs(deg, r / 4), cy + asn(deg, r / 4), cx + acs(deg, r - 3), cy + asn(deg, r - 3), ptr, 2);
}
static void knob_skirt(int32_t cx, int32_t cy, uint32_t pm, uint16_t cap, uint16_t ptr, int32_t r)
{                                               /* a black skirted knob with a pointer */
    int32_t i, deg = 135 + (int32_t)(pm * 270u / 1000u);
    adisc(cx + 1, cy + 2, r + 1, K_BLACK);
    adisc(cx, cy, r, cap);
    for (i = 0; i < 16; i++) aline(cx + acs(i * 45 / 2, r - 4), cy + asn(i * 45 / 2, r - 4), cx + acs(i * 45 / 2, r - 1), cy + asn(i * 45 / 2, r - 1), mix565(cap, K_WHITE, 12), 1);
    adisc(cx, cy, r - 5, mix565(cap, K_WHITE, 6));
    aline(cx + acs(deg, 2), cy + asn(deg, 2), cx + acs(deg, r - 1), cy + asn(deg, r - 1), ptr, 2);
}
static void foot_hex(int32_t cx, int32_t cy)   /* the chrome footswitch in its hex nut */
{
    int16_t h[12], s[12];
    uint32_t i;
    for (i = 0; i < 6u; i++) {
        h[2 * i] = (int16_t)(cx + acs(30 + 60 * (int32_t)i, 13)); h[2 * i + 1] = (int16_t)(cy + asn(30 + 60 * (int32_t)i, 13));
        s[2 * i] = (int16_t)(h[2 * i] + 1); s[2 * i + 1] = (int16_t)(h[2 * i + 1] + 2);
    }
    afill(s, 6, K_BLACK, 0, 0);
    afill(h, 6, RGB(150, 150, 158), 0, 0);
    adisc(cx, cy, 10, RGB(96, 96, 104));
    adisc(cx, cy, 8, RGB(196, 196, 204));
    adisc(cx - 2, cy - 2, 3, RGB(240, 240, 246));
}
static void led_lamp(int32_t cx, int32_t cy, uint32_t on, uint16_t col)   /* the LED: glowing, or dark */
{
    int32_t k;
    if (!on) { adisc(cx, cy, 3, mix565(col, K_BLACK, 70)); return; }
    for (k = 5; k >= 1; k--) aring(cx, cy, 3 + k, 1, mix565(col, K_BLACK, 55 + 8 * k));
    adisc(cx, cy, 3, col);
    ap(cx - 1, cy - 1, RGB(255, 255, 240));
}
static void mask_corners(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r)   /* round the case's corners again */
{
    int32_t i;
    for (i = 0; i < r; i++) {
        int32_t d = r - 1 - i, ins = r - isqrt_small(r * r - d * d);
        cv_rect(x, y + i, ins, 1, C_BG); cv_rect(x + w - ins, y + i, ins, 1, C_BG);
        cv_rect(x, y + h - 1 - i, ins, 1, C_BG); cv_rect(x + w - ins, y + h - 1 - i, ins, 1, C_BG);
    }
}

/* the looks: the name as lettered, colours (case, name, knob names, values, caps, pointers, LED), the art, flags, where
 * the name (baseline), the knob row, the LED and the footswitch (fx 0: none) sit, and the name's letter spacing */
enum { LK_DIST, LK_OD, LK_FUZZ, LK_OCTF, LK_FOLD, LK_CRUSH, LK_FSEQ, LK_WAH, LK_CHO, LK_PHS, LK_FLG, LK_TREM, LK_SLICE,
       LK_VIB, LK_PUMP, LK_REV, LK_DLY, LK_TAPE, LK_SHIM, LK_REVD, LK_SPRING, LK_SHIFT, LK_HARM, LK_OLDM, LK_GLITCH,
       LK_WATER, LK_SYNTH, LK_LOFI, LK_LOOSE, LK_MBOX, LK_DJF, LK_ACID, LK_RING, LK_WRONG, LK_MECH, LK_MEML,
       LK_WORM, LK_COMP, LK_ROT, LK_BLOOM, LK_ARP, NLOOK };
enum { L_SANS = 1, L_LOWER = 2, L_PCT = 4, L_SKIRT = 8, L_SIDES = 16, L_CENTRE = 32 };
typedef struct {
    const char *title;
    uint16_t body, tc, lab, val, cap, ptr, led;
    uint8_t fl, tx, ty, ky, lx, ly, fx, fy;
    int8_t sp;
} plook_t;
#define W_ RGB(250, 250, 250)
static const plook_t PLOOK[NLOOK] = {
    {"distortion", RGB(196, 40, 44), RGB(246, 222, 180), RGB(246, 222, 180), RGB(255, 240, 220), RGB(24, 24, 26), W_, RGB(255, 60, 50), L_SKIRT | L_LOWER, 18, 200, 66, 216, 196, 190, 170, 0},
    {"overdrive", RGB(70, 150, 84), RGB(240, 230, 190), RGB(24, 60, 32), RGB(16, 40, 20), RGB(24, 24, 26), W_, RGB(255, 70, 50), L_SKIRT | L_LOWER, 18, 200, 66, 216, 196, 196, 168, 0},
    {"fuzz", RGB(132, 146, 92), RGB(238, 232, 206), RGB(34, 40, 26), RGB(20, 24, 16), RGB(24, 24, 26), W_, RGB(180, 255, 120), L_SKIRT | L_LOWER, 28, 200, 66, 196, 176, 120, 186, 0},
    {"OCTAVE FUZZ", RGB(120, 24, 44), RGB(250, 200, 190), RGB(250, 200, 190), RGB(255, 236, 228), RGB(244, 228, 200), RGB(120, 24, 44), RGB(255, 60, 50), 0, 18, 62, 84, 216, 56, 120, 190, 0},
    {"WAVEFOLDER", RGB(20, 36, 56), RGB(250, 140, 40), RGB(250, 200, 150), RGB(255, 240, 220), RGB(250, 150, 50), W_, RGB(255, 160, 60), L_SANS | L_PCT, 18, 60, 84, 216, 56, 120, 182, 1},
    {"BITCRUSHER", RGB(14, 8, 20), RGB(240, 80, 190), RGB(240, 80, 190), RGB(255, 220, 245), RGB(230, 70, 180), W_, RGB(255, 90, 200), L_SANS | L_PCT, 18, 60, 84, 216, 56, 214, 150, 1},
    {"FILTER SEQ", RGB(12, 30, 34), RGB(80, 230, 190), RGB(80, 230, 190), RGB(220, 255, 245), RGB(60, 200, 170), W_, RGB(100, 255, 200), L_SANS | L_PCT, 18, 60, 84, 216, 56, 0, 0, 1},
    {"WAH", RGB(34, 34, 38), RGB(250, 210, 60), RGB(250, 210, 60), RGB(255, 245, 210), RGB(30, 30, 32), RGB(250, 210, 60), RGB(255, 60, 50), L_SKIRT, 18, 62, 84, 216, 56, 0, 0, 3},
    {"chorus", RGB(190, 168, 236), RGB(64, 40, 120), RGB(64, 40, 120), RGB(90, 60, 160), RGB(250, 248, 255), RGB(64, 40, 120), RGB(190, 120, 255), 0, 18, 62, 84, 216, 56, 80, 186, 0},
    {"phaser", RGB(248, 110, 30), RGB(30, 14, 6), RGB(30, 14, 6), RGB(60, 30, 10), RGB(24, 24, 26), W_, RGB(255, 240, 120), L_SKIRT | L_LOWER, 18, 62, 84, 216, 56, 0, 0, 0},
    {"FLANGER", RGB(196, 206, 218), RGB(30, 46, 80), RGB(30, 46, 80), RGB(20, 30, 60), RGB(40, 60, 110), W_, RGB(255, 60, 50), 0, 18, 62, 84, 216, 56, 120, 190, 1},
    {"TREMOLO", RGB(246, 220, 160), RGB(50, 30, 20), RGB(50, 30, 20), RGB(120, 40, 30), RGB(220, 60, 50), W_, RGB(255, 60, 50), 0, 18, 62, 84, 216, 56, 190, 186, 1},
    {"SLICER", RGB(20, 20, 20), RGB(250, 214, 40), RGB(250, 214, 40), RGB(255, 250, 220), RGB(250, 210, 40), RGB(20, 20, 20), RGB(255, 220, 60), L_SANS | L_PCT, 18, 60, 84, 216, 56, 0, 0, 1},
    {"vibrato", RGB(200, 222, 244), RGB(30, 70, 140), RGB(30, 70, 140), RGB(20, 50, 110), RGB(60, 130, 220), W_, RGB(80, 150, 255), L_LOWER, 18, 62, 84, 216, 56, 0, 0, 0},
    {"PUMP", RGB(18, 34, 20), RGB(170, 236, 70), RGB(170, 236, 70), RGB(230, 255, 200), RGB(150, 220, 60), RGB(18, 34, 20), RGB(170, 255, 80), L_SANS | L_PCT, 18, 60, 84, 216, 56, 0, 0, 1},
    {"REVERB", RGB(22, 52, 92), RGB(150, 214, 246), RGB(150, 214, 246), RGB(230, 244, 255), RGB(40, 140, 220), W_, RGB(255, 236, 150), 0, 18, 64, 88, 216, 58, 196, 190, 1},
    {"DELAY", RGB(18, 70, 80), RGB(110, 220, 210), RGB(110, 220, 210), RGB(226, 250, 246), RGB(240, 228, 196), RGB(18, 70, 80), RGB(120, 255, 230), L_PCT, 18, 62, 82, 216, 56, 120, 186, 2},
    {"TAPE ECHO", RGB(226, 180, 76), RGB(70, 40, 20), RGB(70, 40, 20), RGB(40, 20, 8), RGB(60, 34, 18), RGB(250, 230, 190), RGB(255, 90, 40), L_SKIRT | L_CENTRE, 120, 62, 82, 120, 160, 0, 0, 1},
    {"SHIM|MER", RGB(10, 10, 12), RGB(80, 196, 240), RGB(80, 196, 240), RGB(220, 240, 255), RGB(52, 150, 236), W_, RGB(255, 60, 50), L_SIDES | L_PCT, 18, 68, 0, 120, 60, 200, 180, 0},
    {"REVERSE DELAY", RGB(52, 24, 64), RGB(240, 120, 200), RGB(240, 120, 200), RGB(255, 220, 245), RGB(230, 110, 190), W_, RGB(255, 120, 220), L_SANS | L_PCT, 18, 60, 84, 216, 56, 0, 0, 1},
    {"SPRING REVERB", RGB(44, 58, 66), RGB(200, 220, 226), RGB(200, 220, 226), RGB(240, 250, 252), RGB(180, 190, 196), RGB(44, 58, 66), RGB(255, 60, 50), 0, 18, 62, 84, 216, 56, 120, 196, 0},
    {"SHIFTER", RGB(16, 28, 60), RGB(80, 210, 240), RGB(80, 210, 240), RGB(220, 250, 255), RGB(60, 190, 230), W_, RGB(100, 230, 255), L_SANS | L_PCT, 18, 60, 84, 216, 56, 0, 0, 1},
    {"harmonizer", RGB(20, 52, 40), RGB(226, 244, 230), RGB(226, 244, 230), RGB(200, 240, 210), RGB(236, 244, 238), RGB(20, 52, 40), RGB(120, 255, 160), L_LOWER | L_PCT, 18, 62, 84, 216, 56, 0, 0, 0},
    {"OLD MEDIA", RGB(110, 66, 40), RGB(246, 226, 186), RGB(246, 226, 186), RGB(255, 244, 220), RGB(50, 28, 16), RGB(246, 226, 186), RGB(255, 60, 50), L_SKIRT, 18, 62, 84, 216, 56, 40, 182, 1},
    {"", RGB(246, 236, 226), 0, RGB(40, 52, 80), RGB(200, 80, 140), RGB(240, 168, 214), W_, RGB(255, 80, 120), L_PCT, 0, 0, 86, 212, 150, 212, 186, 0},   /* GLITCH: its name is part of the art */
    {"underwater", RGB(14, 40, 84), RGB(150, 220, 250), RGB(150, 220, 250), RGB(220, 244, 255), RGB(60, 190, 210), W_, RGB(120, 230, 255), 0, 18, 62, 84, 216, 56, 0, 0, 0},
    {"SYNTH", RGB(24, 10, 30), RGB(255, 70, 90), RGB(255, 70, 90), RGB(255, 220, 225), RGB(230, 60, 80), W_, RGB(255, 90, 100), L_PCT, 18, 62, 84, 216, 56, 0, 0, 3},
    {"lo-fi", RGB(214, 200, 170), RGB(60, 46, 34), RGB(60, 46, 34), RGB(90, 70, 50), RGB(24, 24, 26), W_, RGB(255, 120, 40), L_SKIRT | L_LOWER, 18, 62, 84, 216, 56, 0, 0, 0},
    {"loose", RGB(226, 190, 120), RGB(80, 50, 26), RGB(80, 50, 26), RGB(120, 80, 40), RGB(110, 70, 36), RGB(250, 236, 200), RGB(255, 110, 40), L_LOWER, 18, 62, 84, 216, 56, 0, 0, 0},
    {"music box", RGB(250, 196, 214), RGB(140, 50, 90), RGB(140, 50, 90), RGB(180, 80, 120), RGB(255, 250, 252), RGB(140, 50, 90), RGB(255, 90, 160), L_LOWER, 18, 62, 84, 216, 56, 0, 0, 0},
    {"DJ FILTER", RGB(20, 22, 28), RGB(80, 220, 240), RGB(80, 220, 240), RGB(220, 250, 255), RGB(60, 200, 230), W_, RGB(80, 230, 255), L_SANS, 18, 60, 84, 216, 56, 0, 0, 1},
    {"ACID", RGB(196, 232, 40), RGB(20, 24, 10), RGB(20, 24, 10), RGB(40, 60, 10), RGB(24, 24, 26), RGB(196, 232, 40), RGB(255, 60, 50), L_SKIRT, 18, 62, 84, 216, 56, 0, 0, 4},
    {"RING MOD", RGB(34, 38, 46), RGB(200, 210, 220), RGB(200, 210, 220), RGB(240, 244, 250), RGB(190, 196, 204), RGB(34, 38, 46), RGB(120, 200, 255), 0, 18, 62, 84, 216, 56, 0, 0, 1},
    {"wrong station", RGB(232, 214, 170), RGB(70, 40, 20), RGB(70, 40, 20), RGB(130, 60, 30), RGB(90, 56, 30), RGB(244, 230, 196), RGB(255, 140, 40), L_LOWER, 18, 62, 84, 216, 56, 0, 0, 0},
    {"MECH FAILURE", RGB(110, 118, 126), RGB(250, 170, 60), RGB(240, 240, 244), RGB(255, 210, 150), RGB(24, 24, 26), RGB(250, 170, 60), RGB(255, 90, 40), L_SKIRT, 18, 62, 84, 216, 56, 0, 0, 0},
    {"memory loss", RGB(206, 200, 222), RGB(70, 60, 110), RGB(70, 60, 110), RGB(110, 100, 150), RGB(240, 238, 248), RGB(70, 60, 110), RGB(170, 140, 255), L_LOWER, 18, 62, 84, 216, 56, 0, 0, 0},
    {"tape worm", RGB(214, 178, 120), RGB(60, 36, 18), RGB(60, 36, 18), RGB(110, 60, 30), RGB(70, 44, 24), RGB(230, 200, 150), RGB(255, 110, 40), L_LOWER, 18, 62, 84, 216, 56, 0, 0, 0},
    {"COMPRESSOR", RGB(40, 66, 140), RGB(240, 242, 250), RGB(200, 214, 244), RGB(255, 255, 255), RGB(24, 24, 26), W_, RGB(255, 60, 50), L_SKIRT, 18, 62, 84, 216, 56, 0, 0, 0},
    {"rotary", RGB(122, 72, 36), RGB(250, 230, 190), RGB(250, 230, 190), RGB(255, 244, 220), RGB(24, 24, 26), W_, RGB(255, 100, 40), L_SKIRT | L_LOWER, 18, 62, 84, 216, 56, 0, 0, 0},
    {"bloom", RGB(30, 56, 40), RGB(250, 170, 205), RGB(250, 170, 205), RGB(255, 232, 242), RGB(236, 112, 166), W_, RGB(255, 120, 180), L_LOWER, 18, 62, 84, 216, 56, 0, 0, 0},
    {"ARPEGGIATOR", RGB(14, 40, 46), RGB(90, 220, 220), RGB(90, 220, 220), RGB(220, 255, 255), RGB(70, 200, 200), W_, RGB(100, 255, 240), L_SANS, 18, 60, 84, 216, 56, 0, 0, 1},
};
static const uint8_t PD_LOOK[NPD] = {           /* by PD_ number; those no longer offered borrow a look (and keep their name) */
    LK_DIST, LK_OD, LK_FUZZ, LK_OCTF, LK_FUZZ, LK_FOLD, LK_FSEQ, LK_WAH, LK_CHO, LK_PHS, LK_FLG, LK_TREM, LK_SLICE, LK_VIB,
    LK_VIB, LK_REV, LK_DLY, LK_TAPE, LK_SHIM, LK_OLDM, LK_OLDM, LK_GLITCH, LK_WATER, LK_SYNTH, LK_PUMP, LK_CRUSH, LK_REVD,
    LK_SPRING, LK_SHIFT, LK_HARM, LK_LOFI, LK_TREM, LK_LOOSE, LK_MBOX, LK_LOOSE, LK_REV, LK_LOOSE, LK_OLDM,
    LK_DJF, LK_ACID, LK_RING, LK_WRONG, LK_MECH, LK_MEML, LK_WORM, LK_COMP, LK_ROT, LK_BLOOM};

static void pedal_art(uint32_t lk, uint16_t body)   /* the art, under the knobs and the name */
{
    uint32_t s, i;
    int32_t k;
    switch (lk) {
    case LK_DIST: {
        static const int16_t CL[6][3] = {{60, 150, 28}, {96, 136, 30}, {140, 142, 30}, {180, 152, 26}, {40, 172, 20}, {210, 170, 22}};
        static const int16_t LOW[] = {6, 150, 234, 150, 234, 210, 6, 210};
        static const int16_t BOLT[] = {126, 120, 102, 166, 122, 166, 108, 206, 148, 152, 126, 152, 142, 120};
        for (i = 0; i < 6u; i++) adisc(CL[i][0], CL[i][1], CL[i][2], RGB(110, 16, 22));
        afill(LOW, NPT(LOW), RGB(110, 16, 22), 3, -5);
        afill(BOLT, NPT(BOLT), RGB(246, 222, 180), 0, 0);
        aoutline(BOLT, NPT(BOLT), RGB(60, 10, 14), 1);
        break;
    }
    case LK_OD: {
        static const int16_t HILL[] = {6, 210, 6, 180, 50, 164, 110, 186, 170, 160, 234, 178, 234, 210};
        adisc(120, 190, 46, RGB(240, 230, 190));
        for (k = 0; k < 13; k++) aline(120 + acs(180 + k * 15, 52), 190 + asn(180 + k * 15, 52), 120 + acs(180 + k * 15, 74), 190 + asn(180 + k * 15, 74), RGB(240, 230, 190), 2);
        afill(HILL, NPT(HILL), RGB(24, 60, 32), 0, 0);
        afill(HILL, NPT(HILL), body, 3, 4);
        break;
    }
    case LK_FUZZ: {
        static const int16_t MT[] = {6, 210, 76, 150, 100, 156, 120, 140, 140, 156, 164, 150, 234, 210};
        static const int16_t LIT[] = {76, 150, 100, 156, 120, 140, 140, 156, 164, 150, 234, 210, 150, 210};
        afill(MT, NPT(MT), RGB(34, 40, 26), 0, 0);
        afill(LIT, NPT(LIT), RGB(238, 232, 206), 3, 3);
        for (k = 0; k < 9; k++) aline(120 + acs(-90 + (k - 4) * 16, 8), 136 + asn(-90 + (k - 4) * 16, 6), 120 + acs(-90 + (k - 4) * 16, 22), 136 + asn(-90 + (k - 4) * 16, 14), RGB(238, 232, 206), 2);
        s = 5;
        for (i = 0; i < 18u; i++) { int32_t x = 60 + (int32_t)rnd_n(&s, 120), y = 112 + (int32_t)rnd_n(&s, 14); adisc(x, y, 1, RGB(34, 40, 26)); }
        break;
    }
    case LK_OCTF: {
        int32_t y;
        uint16_t pk = RGB(250, 200, 190);
        adisc(78, 162, 30, pk);
        for (y = 150; y < 192; y++) { int32_t h = isqrt_small(900 - (y - 162) * (y - 162)), x; for (x = 78 - h; x <= 78 + h; x++) if (!(x & 3)) ap(x, y, RGB(70, 10, 24)); }
        aring(78, 162, 30, 2, pk);
        aline(36, 174, 120, 150, pk, 2);
        aring(172, 150, 15, 2, pk); adisc(172, 150, 7, pk);
        aline(148, 158, 196, 142, pk, 1);
        astars(21, 10, 120, 230, 206, 26, pk);
        break;
    }
    case LK_FOLD: {
        static const int16_t XS[8] = {6, 40, 74, 108, 142, 176, 210, 234};
        for (i = 0; i < 7u; i++) {
            int32_t up = !(i & 1u), y0 = up ? 136 : 196, y1 = up ? 196 : 136;
            int16_t p[8] = {XS[i], (int16_t)y0, XS[i + 1], (int16_t)y1, XS[i + 1], 210, XS[i], 210};
            afill(p, 4, up ? RGB(250, 140, 40) : RGB(180, 80, 20), 0, 0);
        }
        for (i = 0; i < 7u; i++) aline(XS[i], i & 1u ? 196 : 136, XS[i + 1], i & 1u ? 136 : 196, RGB(255, 220, 170), 1);
        break;
    }
    case LK_CRUSH: {
        int32_t gx, gy;
        for (gy = 0; gy < 7; gy++) {
            if (gy == 2 || gy == 4 || gy == 6) continue;
            for (gx = -8; gx <= 8; gx++) if (gx * gx + gy * gy <= 56) arect(120 + gx * 6, 170 - gy * 6, 5, 5, mix565(RGB(240, 80, 190), RGB(255, 200, 60), gy * 100 / 7));
        }
        for (gy = 0; gy < 6; gy++)
            for (gx = 0; gx < 38; gx++) if (!((gx + gy) & 1)) arect(6 + gx * 6, 176 + gy * 6, 5, 5, mix565(RGB(240, 80, 190), body, 55 + gy * 7));
        break;
    }
    case LK_FSEQ: {
        static const uint8_t H[14] = {30, 14, 22, 44, 10, 26, 18, 38, 12, 30, 20, 34, 16, 24};
        uint16_t mi = RGB(80, 230, 190);
        for (i = 0; i < 14u; i++) {
            int32_t x = 10 + (int32_t)i * 16, top = 196 - H[i], wy;
            arect(x, top, 13, 208 - top, mix565(mi, body, i == 3u ? 0 : 55));
            for (wy = top + 6; wy < 206; wy += 6) { ap(x + 4, wy, mix565(mi, body, 10)); ap(x + 9, wy, mix565(mi, body, 30)); }
        }
        astars(31, 10, 130, 230, 150, 18, mi);
        break;
    }
    case LK_WAH: {
        uint16_t ye = RGB(250, 210, 60);
        for (k = 0; k < 10; k++) {
            int32_t th = -166 + k * 17;
            int16_t p[8] = {120, 196, (int16_t)(120 + acs(th - 10, 30)), (int16_t)(196 + asn(th - 10, 30)), (int16_t)(120 + acs(th, 54)), (int16_t)(196 + asn(th, 54)),
                            (int16_t)(120 + acs(th + 10, 30)), (int16_t)(196 + asn(th + 10, 30))};
            aoutline(p, 4, ye, 1);
        }
        adisc(120, 196, 10, ye);
        for (k = 0; k < 3; k++) aring(120, 196, 64 + k * 9, 1, mix565(ye, body, 40 + k * 20));
        break;
    }
    case LK_CHO:
        adisc(192, 158, 20, mix565(body, RGB(250, 246, 255), 60));
        adisc(200, 152, 18, body);
        for (k = 0; k < 12; k++) {
            int32_t x0 = 6, y0 = 150 + k * 5 + asn(k * 29, 6 * (100 - k * 4) / 100);
            for (i = 2; i <= 100u; i += 2) {
                int32_t x1 = 6 + (int32_t)i * 228 / 100, y1 = 150 + k * 5 + asn((int32_t)i * 5 + k * 29, 6 * (100 - k * 4) / 100);
                aline(x0, y0, x1, y1, mix565(RGB(64, 40, 120), body, k * 5), 1);
                x0 = x1; y0 = y1;
            }
        }
        break;
    case LK_PHS:
        for (k = 0; k < 8; k++) { aring(100, 196, 4 + k * 7, 2, RGB(30, 14, 6)); aring(144, 190, 4 + k * 7, 2, RGB(30, 14, 6)); }
        break;
    case LK_FLG: {
        static const int16_t PL1[] = {150, 120, 196, 136, 160, 150}, PL2[] = {150, 120, 160, 150, 166, 140};
        uint16_t nb = RGB(30, 46, 80);
        for (k = 0; k < 9; k++) aline(10, 150 + k * 6, 150 - k * 6, 120 + k * 6, mix565(nb, body, 20 + k * 7), 1);
        afill(PL1, NPT(PL1), nb, 0, 0);
        afill(PL2, NPT(PL2), mix565(nb, body, 40), 0, 0);
        break;
    }
    case LK_TREM: {
        static const int16_t B1[] = {120, 146, 240, 120, 240, 172}, B2[] = {120, 146, 0, 124, 0, 168};
        static const int16_t TW[] = {110, 210, 114, 150, 126, 150, 130, 210}, ROOM[] = {108, 150, 132, 150, 130, 138, 110, 138};
        uint16_t ik = RGB(50, 30, 20), rr = RGB(210, 50, 40);
        afill(B1, NPT(B1), RGB(252, 240, 200), 0, 0);
        afill(B2, NPT(B2), RGB(252, 240, 200), 0, 0);
        afill(TW, NPT(TW), ik, 0, 0);
        for (k = 164; k <= 200; k += 18) { int16_t p[8] = {113, (int16_t)k, 127, (int16_t)k, 128, (int16_t)(k + 7), 112, (int16_t)(k + 7)}; afill(p, 4, rr, 0, 0); }
        afill(ROOM, NPT(ROOM), rr, 0, 0);
        adisc(120, 138, 5, RGB(255, 240, 150));
        for (k = 0; k < 5; k++) aline(6 + k * 46, 206, 30 + k * 46, 206, ik, 1);
        break;
    }
    case LK_SLICE: {
        int32_t y;
        for (i = 0; i < 12u; i++) {
            int32_t x = 54 + (int32_t)i * 11, cy = 182 + (int32_t)(i & 1u) * 6 - 3;
            for (y = cy - 44; y <= cy + 44; y++) {
                int32_t h = isqrt_small(44 * 44 - (y - cy) * (y - cy)), a = 120 - h > x ? 120 - h : x, b = 120 + h < x + 8 ? 120 + h : x + 8;
                if (b >= a && row_vis(y)) arow(a, b, y, RGB(250, 214, 40));
            }
        }
        break;
    }
    case LK_VIB: {
        uint16_t db = RGB(30, 70, 140);
        aline(120, 210, 120, 184, db, 3); aline(110, 184, 110, 140, db, 3); aline(130, 184, 130, 140, db, 3); aline(110, 184, 130, 184, db, 3);
        for (k = 0; k < 4; k++) {
            aarc(100, 160, 14 + k * 10, 14 + k * 10, 150, 210, mix565(db, body, k * 20), 2);
            aarc(140, 160, 14 + k * 10, 14 + k * 10, -30, 30, mix565(db, body, k * 20), 2);
        }
        break;
    }
    case LK_PUMP: {
        static const int16_t ECG[] = {6, 190, 60, 190, 68, 182, 76, 198, 84, 160, 92, 204, 100, 190, 140, 190, 148, 182, 156, 198, 164, 160, 172, 204, 180, 190, 234, 190};
        uint16_t li = RGB(170, 236, 70);
        int32_t x0 = 0, y0 = 0;
        for (k = 0; k <= 72; k++) {
            int32_t d = k * 5, sn = asn(d, 1000), x, y;
            x = 120 + 22 * (sn * sn / 1000 * sn / 1000) / 1000;
            y = 168 - 20 * (13 * acs(d, 1000) - 5 * acs(2 * d, 1000) - 2 * acs(3 * d, 1000) - acs(4 * d, 1000)) / 16000;
            if (k) aline(x0, y0, x, y, li, 2);
            x0 = x; y0 = y;
        }
        for (i = 0; i + 1 < NPT(ECG); i++) aline(ECG[2 * i], ECG[2 * i + 1], ECG[2 * i + 2], ECG[2 * i + 3], mix565(li, body, 35), 1);
        break;
    }
    case LK_REV: {
        static const int16_t BEAM[] = {78, 150, 234, 108, 234, 196};
        uint16_t lb = RGB(70, 170, 232), pale = RGB(150, 214, 246);
        afill(BEAM, NPT(BEAM), mix565(body, pale, 55), 0, 0);
        for (k = 0; k < 9; k++) {           /* the swirl: spirals, wider and paler outwards */
            int32_t r0 = 2 + k * 16 / 5, r1 = 9 + k * 13 / 2, a, x0 = 0, y0 = 0;
            for (a = 0; a <= 576; a += 12) {
                int32_t r = r0 + (r1 - r0) * a / 576, x = 34 + acs(a + k * 40, r), y = 148 + asn(a + k * 40, r) * 85 / 100;
                if (a) aline(x0, y0, x, y, mix565(lb, body, k * 6), 1);
                x0 = x; y0 = y;
            }
        }
        for (k = 0; k < 16; k++) {          /* the hair-like flow beside it */
            int32_t x0 = 6, y0 = 120 + k * 6 + asn(k * 57, 6), j;
            for (j = 1; j < 30; j++) {
                int32_t x = 6 + j * 92 / 29, y = 120 + k * 6 + asn(j * 172 / 29 + k * 23, 18) * j / 29 + j * 30 / 29;
                aline(x0, y0, x, y, mix565(lb, body, 15 + (k % 3) * 15), 1);
                x0 = x; y0 = y;
            }
        }
        astars(4, 100, 52, 230, 205, 40, pale);
        break;
    }
    case LK_DLY: {
        static const int16_t DROP[] = {120, 128, 126, 142, 120, 148, 114, 142};
        uint16_t aq = RGB(110, 220, 210);
        for (k = 0; k < 7; k++) aring(120, 168, 8 + k * 12, k & 1 ? 1 : 2, mix565(aq, body, k * 10));
        afill(DROP, NPT(DROP), aq, 0, 0);
        break;
    }
    case LK_TAPE: {
        static const int16_t PATH[] = {70, 190, 120, 202, 170, 190};
        uint16_t br = RGB(70, 40, 20);
        for (k = 70; k <= 170; k += 100) {
            int32_t j;
            for (j = 0; j < 4; j++) aring(k, 160, 12 + j * 4, 1, mix565(br, body, 55));
            aring(k, 160, 30, 2, br);
            aring(k, 160, 7, 1, br);
            for (j = 0; j < 3; j++) aline(k + acs(j * 120 + 23, 7), 160 + asn(j * 120 + 23, 7), k + acs(j * 120 + 23, 26), 160 + asn(j * 120 + 23, 26), br, 1);
        }
        aline(PATH[0], PATH[1], PATH[2], PATH[3], br, 1); aline(PATH[2], PATH[3], PATH[4], PATH[5], br, 1);
        aring(120, 202, 4, 1, br);
        break;
    }
    case LK_SHIM: {
        uint16_t cy = RGB(80, 196, 240);
        s = 7;
        for (i = 0; i < 70u; i++) {         /* the comet's tail: fine lines falling from its head */
            int32_t sx = 116 + (int32_t)rnd_n(&s, 9), th = 62 + (int32_t)rnd_n(&s, 57), L = 60 + (int32_t)rnd_n(&s, 71), bend = (int32_t)rnd_n(&s, 21) - 10;
            uint16_t c = mix565(cy, body, (int32_t)rnd_n(&s, 50));
            int32_t x0 = sx, y0 = 96, j;
            for (j = 1; j <= 10; j++) {
                int32_t x = sx + acs(th, L * j / 10) + bend * j * j / 25, y = 96 + asn(th, L * j / 10);
                aline(x0, y0, x, y, c, 1);
                x0 = x; y0 = y;
            }
        }
        aring(120, 96, 9, 1, cy); adisc(120, 96, 4, cy);
        for (k = 0; k < 14; k++) ap(120 + acs(k * 360 / 14, 13), 96 + asn(k * 360 / 14, 13), cy);
        astars(9, 160, 120, 228, 205, 26, cy);
        break;
    }
    case LK_REVD: {
        static const int16_t HEAD[] = {84, 146, 92, 140, 90, 152};
        uint16_t pi = RGB(240, 120, 200);
        aring(120, 170, 32, 2, pi);
        for (k = 0; k < 12; k++) aline(120 + acs(k * 30, 27), 170 + asn(k * 30, 27), 120 + acs(k * 30, 31), 170 + asn(k * 30, 31), pi, 1);
        aline(120, 170, 120, 148, pi, 2); aline(120, 170, 104, 178, pi, 2);
        aarc(120, 170, 44, 44, 200, 320, pi, 1);
        afill(HEAD, NPT(HEAD), pi, 0, 0);
        break;
    }
    case LK_SPRING: {
        uint16_t sc = RGB(200, 220, 226);
        int32_t x0 = 14, y0 = 172;
        for (k = 1; k < 240; k++) {
            int32_t x = 14 + k * 9 / 10, y = 172 + asn(k * 2865 / 100, 18);
            aline(x0, y0, x, y, acs(k * 2865 / 100, 10) > 0 ? sc : mix565(sc, body, 60), 1);
            x0 = x; y0 = y;
        }
        adisc(14, 172, 4, sc); adisc(229, 172, 4, sc);
        break;
    }
    case LK_SHIFT: {
        uint16_t cn = RGB(80, 210, 240);
        for (k = 0; k < 7; k++) {
            int32_t x = 30 + k * 26, y = 200 - k * 10;
            arect(x, y, 26, 10, mix565(cn, body, 10 + k * 8));
            arect(x, y + 10, 26, 208 - y - 10, mix565(cn, body, 70));
        }
        aline(40, 186, 40, 170, cn, 1); aline(40, 170, 34, 176, cn, 1); aline(40, 170, 46, 176, cn, 1);
        break;
    }
    case LK_HARM: {
        static const int16_t BIRD[3][2] = {{70, 1}, {120, 3}, {170, 2}};
        uint16_t hw = RGB(226, 244, 230);
        for (k = 0; k < 5; k++) aline(6, 154 + k * 9, 234, 142 + k * 9, mix565(hw, body, 45), 1);
        for (i = 0; i < 3u; i++) {
            int32_t bx = BIRD[i][0], by = 148 + BIRD[i][1] * 9 - (bx - 6) * 12 / 228;
            int16_t beak[6] = {(int16_t)(bx + 9), (int16_t)(by - 5), (int16_t)(bx + 13), (int16_t)(by - 4), (int16_t)(bx + 9), (int16_t)(by - 3)};
            int16_t tail[6] = {(int16_t)(bx - 6), (int16_t)by, (int16_t)(bx - 13), (int16_t)(by + 4), (int16_t)(bx - 5), (int16_t)(by + 3)};
            adisc(bx, by, 6, hw); adisc(bx + 6, by - 5, 3, hw);
            afill(beak, 3, RGB(250, 190, 60), 0, 0);
            afill(tail, 3, hw, 0, 0);
        }
        break;
    }
    case LK_OLDM: {
        static const int16_t BASE[] = {70, 196, 176, 196, 186, 206, 60, 206}, HORN[] = {150, 166, 194, 134, 216, 154};
        uint16_t ce = RGB(246, 226, 186), dk = RGB(50, 28, 16);
        afill(BASE, NPT(BASE), dk, 0, 0);
        aell(110, 192, 31, 7, ce); aell(110, 192, 30, 6, dk);
        aline(140, 192, 150, 166, ce, 2);
        afill(HORN, NPT(HORN), ce, 0, 0);
        aoutline(HORN, NPT(HORN), dk, 1);
        for (k = 0; k < 3; k++) aline(150, 166, 194 + k * 11, 134 + k * 10, dk, 1);
        break;
    }
    case LK_GLITCH: {
        static const uint16_t PAL[6] = {RGB(244, 92, 80), RGB(166, 196, 60), RGB(98, 196, 200), RGB(240, 120, 170), RGB(250, 196, 60), RGB(40, 60, 100)};
        static const char WORD[] = "GLITCH";
        int32_t gx = 26;
        s = 11;
        for (i = 0; i < 46u; i++) {         /* the confetti, kept off the knobs, the name and the footswitch */
            int32_t x = 10 + (int32_t)rnd_n(&s, 220), y = 48 + (int32_t)rnd_n(&s, 158), r = 3 + (int32_t)rnd_n(&s, 2);
            uint32_t sh = rnd_n(&s, 6);
            uint16_t c = PAL[rnd_n(&s, 6)];
            if ((y > 66 && y < 130) || (y > 136 && x > 190) || (y > 140 && y < 176 && x < 180)) continue;
            if (sh == 0u) { int16_t t[6] = {(int16_t)x, (int16_t)(y - r), (int16_t)(x + r), (int16_t)(y + r), (int16_t)(x - r), (int16_t)(y + r)}; afill(t, 3, c, 0, 0); }
            else if (sh == 1u) { int16_t q[8] = {(int16_t)(x - r), (int16_t)(y - r), (int16_t)(x + r), (int16_t)(y - r), (int16_t)(x + r), (int16_t)(y + r), (int16_t)(x - r), (int16_t)(y + r)}; aoutline(q, 4, c, 1); }
            else if (sh == 2u) aring(x, y, r, 1, c);
            else if (sh == 3u) { aline(x - r, y, x + r, y, c, 2); aline(x, y - r, x, y + r, c, 2); }
            else if (sh == 4u) { aline(x - r, y + r, x - r / 3, y - r, c, 1); aline(x - r / 3, y - r, x + r / 3, y + r, c, 1); aline(x + r / 3, y + r, x + r, y - r, c, 1); }
            else adisc(x, y, 2, c);
        }
        for (i = 0; WORD[i]; i++) {         /* the name: big outlined letters, each its own colour */
            char ch[2] = {WORD[i], 0};
            uint16_t c = PAL[i == 5u ? 0 : i];
            int32_t dx, dy;
            for (dy = -1; dy <= 1; dy++)
                for (dx = -1; dx <= 1; dx++) if (dx || dy) atext(gx + dx, 141 + dy, &FONT_L, ch, c, 0);
            atext(gx, 141, &FONT_L, ch, body, 0);
            gx += text_w(&FONT_L, ch) + 1;
        }
        break;
    }
    case LK_WATER: {
        static const int16_t KX[5] = {22, 40, 196, 214, 226}, FISH[] = {96, 168, 124, 156, 140, 168, 124, 180}, TAIL[] = {96, 168, 86, 160, 86, 176};
        uint16_t bu = RGB(150, 220, 250);
        for (i = 0; i < 5u; i++) {
            int32_t x0 = KX[i] + asn((int32_t)i * 57, 6), y0 = 210, j;
            for (j = 1; j < 18; j++) { int32_t x = KX[i] + asn(j * 29 + (int32_t)i * 57, 6), y = 210 - j * 5; aline(x0, y0, x, y, RGB(40, 120, 120), 2); x0 = x; y0 = y; }
        }
        s = 3;
        for (i = 0; i < 22u; i++) { int32_t x = 60 + (int32_t)rnd_n(&s, 120), y = 130 + (int32_t)rnd_n(&s, 76); aring(x, y, 2 + (int32_t)rnd_n(&s, 3), 1, bu); }
        aoutline(FISH, NPT(FISH), bu, 1);
        aoutline(TAIL, NPT(TAIL), bu, 1);
        ap(130, 166, bu); ap(131, 166, bu);
        break;
    }
    case LK_SYNTH: {
        uint16_t sr = RGB(255, 70, 90), sy = RGB(255, 190, 80);
        for (k = 0; k < 15; k++) {
            int32_t yy = 139 + k * 2, h = isqrt_small(900 - (yy - 168) * (yy - 168));
            if (k % 3 != 2) { arow(120 - h, 120 + h, yy, mix565(sy, sr, k * 100 / 13)); arow(120 - h, 120 + h, yy + 1, mix565(sy, sr, k * 100 / 13)); }
        }
        for (k = -6; k <= 6; k++) aline(120 + k * 6, 168, 120 + k * 34, 210, mix565(sr, body, 30), 1);
        for (k = 0; k < 5; k++) arow(6, 234, 172 + k * k * 12 / 5, mix565(sr, body, 30));
        break;
    }
    case LK_LOFI: {
        uint16_t rb = RGB(60, 46, 34);
        arrect(60, 150, 120, 54, 8, rb);
        arrect(68, 158, 60, 38, 4, mix565(rb, body, 30));
        s = 5;
        for (i = 0; i < 160u; i++) { int32_t x = 70 + (int32_t)rnd_n(&s, 56), y = 160 + (int32_t)rnd_n(&s, 34); ap(x, y, rnd_n(&s, 10) < 6u ? body : rb); }
        adisc(154, 166, 7, body); adisc(154, 188, 7, body);
        aline(80, 150, 66, 128, rb, 2); aline(160, 150, 176, 124, rb, 2);
        break;
    }
    case LK_LOOSE: {
        static const int16_t DRIP[] = {170, 196, 176, 196, 174, 208, 172, 208};
        uint16_t lb = RGB(80, 50, 26), cr = RGB(250, 236, 200);
        int16_t p[80];
        aline(30, 190, 210, 190, lb, 2); aline(200, 190, 200, 210, lb, 2);
        for (k = 0; k < 40; k++) {          /* the melting clock: an oval, its right side sliding off the table */
            int32_t x = 80 + acs(k * 9, 60), sn = asn(k * 9, 28), y = 160 + (sn < 0 ? sn : sn / 2);
            if (x > 130) y += (x - 130) * 9 / 10;
            p[2 * k] = (int16_t)x; p[2 * k + 1] = (int16_t)y;
        }
        afill(p, 40, cr, 0, 0);
        aoutline(p, 40, lb, 1);
        aline(84, 166, 84, 146, lb, 2); aline(84, 166, 100, 170, lb, 2);
        afill(DRIP, NPT(DRIP), cr, 0, 0);
        aoutline(DRIP, NPT(DRIP), lb, 1);
        break;
    }
    case LK_MBOX: {
        static const int16_t NOTE[4][2] = {{40, 150}, {60, 136}, {76, 158}, {210, 140}}, BOX[] = {98, 168, 120, 168, 120, 184, 98, 184};
        uint16_t md = RGB(140, 50, 90);
        astars(8, 10, 120, 230, 206, 20, K_WHITE);
        aring(150, 176, 14, 2, md); aring(178, 176, 14, 2, md);
        aline(118, 176, 136, 176, md, 3);
        aoutline(BOX, NPT(BOX), md, 2);
        for (i = 0; i < 4u; i++) {
            int32_t x = NOTE[i][0], y = NOTE[i][1];
            adisc(x, y, 4, md); aline(x + 4, y, x + 4, y - 14, md, 1); aline(x + 4, y - 14, x + 9, y - 10, md, 1);
        }
        break;
    }
    case LK_DJF: {                              /* DJ FILTER: a record on a turntable, its tonearm */
        uint16_t pl = RGB(46, 48, 56), gr = RGB(66, 68, 80), cy = RGB(80, 220, 240);
        adisc(120, 176, 44, RGB(10, 10, 14));
        adisc(120, 176, 40, pl);
        for (k = 12; k <= 36; k += 4) aring(120, 176, k, 1, gr);
        adisc(120, 176, 10, cy); adisc(120, 176, 2, RGB(10, 10, 14));
        adisc(204, 132, 7, RGB(150, 150, 160)); adisc(204, 132, 3, RGB(60, 60, 66));
        aline(204, 132, 196, 172, RGB(190, 190, 200), 3); aline(196, 172, 170, 182, RGB(190, 190, 200), 3);
        arrect(164, 178, 10, 7, 2, RGB(220, 220, 228));
        for (k = 0; k < 6; k++) arect(16, 140 + k * 10, 14, 4, k == 2 ? cy : gr);   /* (the fader's scale) */
        break;
    }
    case LK_ACID: {                             /* ACID: the filter's curve with its sharp peak, dripping */
        static const int16_t D1[] = {146, 160, 151, 170, 146, 175, 141, 170}, D2[] = {160, 176, 164, 184, 160, 188, 156, 184};
        uint16_t ink = RGB(20, 24, 10);
        int32_t x0 = 10, y0 = 194;
        for (k = 12; k <= 230; k += 4) {
            int32_t d = k - 150, y = 194 - 3200 / (d * d / 16 + 64) + (d > 0 ? d * d / 50 : 0);
            if (y > 206) y = 206;
            aline(x0, y0, k, y, ink, 3);
            x0 = k; y0 = y;
        }
        afill(D1, NPT(D1), ink, 0, 0); afill(D2, NPT(D2), ink, 0, 0);
        aring(100, 196, 5, 1, ink); aring(210, 200, 4, 1, ink); aring(30, 196, 3, 1, ink);
        break;
    }
    case LK_RING: {                             /* RING MOD: a ringed planet, its waves */
        uint16_t sv = RGB(200, 210, 220);
        for (k = 0; k < 4; k++) aring(120, 168, 50 + k * 12, 1, mix565(sv, body, 70 + k * 7));
        adisc(120, 168, 22, mix565(sv, body, 30));
        afill((const int16_t[]){98, 158, 142, 158, 142, 178, 98, 178}, 4, body, 4, 2);
        aring(120, 168, 22, 1, sv);
        aarc(120, 168, 60, 13, 0, 360, sv, 2);
        astars(17, 10, 128, 230, 206, 22, sv);
        break;
    }
    case LK_WRONG: {                            /* WRONG STATION: a radio's tuning dial, the needle off between stations */
        uint16_t br = RGB(70, 40, 20), cr = RGB(244, 230, 196), rd = RGB(220, 50, 40);
        arrect(14, 136, 212, 56, 8, br);
        arrect(20, 142, 200, 44, 6, cr);
        for (k = 0; k <= 48; k++) aline(24 + k * 4, 176, 24 + k * 4, k % 4 ? 172 : 166, br, 1);
        for (k = 0; k < 6; k++) adisc(36 + k * 34, 156, 2, br);   /* (the stations) */
        aline(131, 146, 128, 184, rd, 2);
        s = 9;
        for (i = 0; i < 40u; i++) ap(24 + (int32_t)rnd_n(&s, 192), 146 + (int32_t)rnd_n(&s, 14), mix565(br, cr, 50));
        adisc(56, 200, 6, br); adisc(184, 200, 6, br);
        break;
    }
    case LK_MECH: {                             /* MECH FAILURE: gears, one cracked, sparks where they meet */
        uint16_t gm = RGB(60, 64, 70), am = RGB(250, 170, 60), gl = RGB(160, 166, 174);
        int32_t g, cx[2] = {86, 150}, cy[2] = {172, 158}, r[2] = {28, 20};
        for (g = 0; g < 2; g++) {
            for (k = 0; k < 360; k += 30) aline(cx[g] + acs(k, r[g] - 2), cy[g] + asn(k, r[g] - 2), cx[g] + acs(k, r[g] + 5), cy[g] + asn(k, r[g] + 5), gm, 3);
            adisc(cx[g], cy[g], r[g], gm); adisc(cx[g], cy[g], r[g] - 8, gl); adisc(cx[g], cy[g], 4, gm);
        }
        aline(80, 150, 92, 168, am, 1); aline(92, 168, 84, 180, am, 1); aline(84, 180, 96, 196, am, 1);   /* (the crack) */
        for (k = 0; k < 5; k++) aline(118, 166, 118 + acs(-60 + k * 30, 14), 166 + asn(-60 + k * 30, 14), RGB(255, 230, 120), 1);
        for (k = 0; k < 6; k++) aline(186 + (k & 1) * 12, 140 + k * 10, 186 + ((k + 1) & 1) * 12, 150 + k * 10, gl, 2);   /* (a spring) */
        break;
    }
    case LK_MEML: {                             /* MEMORY LOSS: a tune fading out, the last notes only outlines */
        uint16_t ink = RGB(70, 60, 110);
        for (k = 0; k < 5; k++) aline(10, 150 + k * 9, 230, 150 + k * 9, mix565(ink, body, 70), 1);
        for (k = 0; k < 8; k++) {
            int32_t x = 24 + k * 26, y = 186 - (k * 37 % 5) * 9;
            uint16_t c = mix565(ink, body, k * 12);
            if (k < 5) { adisc(x, y, 5, c); aline(x + 5, y, x + 5, y - 22, c, 2); }
            else { aring(x, y, 5, 1, c); }
        }
        break;
    }
    case LK_WORM: {                             /* TAPE WORM: a worm made of tape, coiling */
        uint16_t tp = RGB(80, 46, 22), seg = RGB(120, 76, 40);
        int32_t x0 = 30, y0 = 180, hx = 0, hy = 0;
        for (k = 0; k <= 36; k++) {
            int32_t x = 30 + k * 5, y = 176 + asn(k * 30, 16);
            aline(x0, y0, x, y, tp, 3); aline(x0, y0 + 4, x, y + 4, tp, 3);
            if (k % 3 == 0) aring(x, y + 2, 5, 1, seg);
            x0 = x; y0 = y; hx = x; hy = y;
        }
        adisc(hx + 6, hy + 2, 9, tp); adisc(hx + 9, hy - 1, 2, RGB(250, 240, 220));
        aring(26, 196, 8, 2, tp); aring(44, 202, 6, 2, tp);   /* (the reels it came off) */
        break;
    }
    case LK_COMP: {                             /* COMPRESSOR: a big wave squeezed through a clamp, out small and even */
        uint16_t sv = RGB(200, 214, 244), dk = RGB(20, 34, 80);
        int32_t px = 10, py = 180;
        for (k = 0; k <= 220; k += 2) {
            int32_t amp = k < 96 ? 26 : k < 118 ? 26 - (k - 96) : 6, y = 180 + asn(k * 9, amp);
            aline(px, py, 10 + k, y, sv, 2);
            px = 10 + k; py = y;
        }
        arrect(98, 142, 22, 18, 3, dk); arrect(98, 200, 22, 18, 3, dk);   /* (the clamp's jaws, its screw below) */
        aline(109, 218, 109, 226, dk, 3); aline(96, 226, 122, 226, dk, 4);
        break;
    }
    case LK_ROT: {                              /* ROTARY: the cabinet's louvres, the horn going round, its swirl */
        uint16_t cr = RGB(250, 230, 190), dk = RGB(70, 38, 16);
        arrect(40, 128, 160, 88, 10, dk);
        for (k = 0; k < 7; k++) arrect(48, 136 + k * 11, 144, 5, 2, mix565(cr, dk, 55));
        adisc(120, 160, 7, cr);
        aline(84, 160, 156, 160, cr, 4);         /* (the horn: two bells on a rotor) */
        afill((const int16_t[]){84, 160, 70, 150, 70, 170}, 3, cr, 0, 0);
        afill((const int16_t[]){156, 160, 170, 150, 170, 170}, 3, cr, 0, 0);
        aarc(120, 160, 62, 8, 200, 330, cr, 2); aarc(120, 160, 62, 8, 20, 150, cr, 2);
        aarc(120, 160, 74, 14, 210, 300, mix565(cr, body, 40), 1); aarc(120, 160, 74, 14, 30, 120, mix565(cr, body, 40), 1);
        break;
    }
    case LK_BLOOM: {                            /* BLOOM: buds opening along a stem, the last in full flower */
        uint16_t pk = RGB(250, 170, 205), st2 = RGB(110, 170, 90), cy = RGB(255, 220, 120);
        aline(20, 206, 220, 206, mix565(st2, body, 40), 2);
        for (k = 0; k < 4; k++) {
            int32_t x = 40 + k * 50, h = 30 + k * 6, r = 3 + k * 3, y = 206 - h, j;
            aline(x, 206, x, y, st2, 2);
            if (k == 0) { adisc(x, y, 4, mix565(pk, body, 30)); continue; }
            for (j = 0; j < 6; j++) adisc(x + acs(j * 60 + k * 15, r), y + asn(j * 60 + k * 15, r), r, mix565(pk, body, 30 - k * 10));
            adisc(x, y, r / 2 + 2, cy);
        }
        astars(7, 10, 128, 230, 150, 10, pk);
        break;
    }
    case LK_ARP: {
        uint16_t aa = RGB(90, 220, 220);
        adisc(206, 160, 12, mix565(aa, K_WHITE, 50));
        adisc(212, 156, 11, body);
        for (k = 0; k < 9; k++) { int32_t x = 24 + k * 20, y = 200 - k * 8 - (k % 3) * 4; arrect(x, y, 14, 5, 2, mix565(aa, body, k == 4 ? 0 : 50)); }
        astars(12, 10, 126, 190, 160, 16, aa);
        break;
    }
    default: break;
    }
}

static void mono_rect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t body, uint16_t led);
#define MONO_SH ((const uint16_t[5]){RGB(24, 32, 12), RGB(56, 70, 26), RGB(104, 118, 56), RGB(156, 170, 92), RGB(186, 200, 120)})
static uint32_t mono_lvl(uint16_t c)            /* which of the five LCD shades a colour becomes, by its brightness */
{
    int32_t r = (c >> 11) << 3, g = ((c >> 5) & 63) << 2, b = (c & 31) << 3, l = (r * 77 + g * 150 + b * 29) >> 8;
    return l < 45 ? 0u : l < 90 ? 1u : l < 135 ? 2u : l < 185 ? 3u : 4u;
}
static uint16_t neon_name;                      /* neon: the pedal's name, its colour near white */
static void mono_text(int32_t x, int32_t y, const felucca_font_t *f, const char *t, int32_t sp)
{                                               /* LCD: text again, darkest on what is light under it, lightest on dark */
    int32_t w = atext_w(f, t, sp), i, j, n = 0, sum = 0;
    for (j = y; j < y + f->h; j++)
        if (row_vis(j))
            for (i = x; i < x + w; i++) { sum += (int32_t)mono_lvl(cv_get(i, j)); n++; }
    if (thm->mono == 2u) { atext_halo(x, y, f, t, neon_name, RGB(14, 6, 28), sp); return; }   /* neon: outlined, lit */
    if (n) atext(x, y, f, t, MONO_SH[sum * 2 >= n * 4 ? 0 : 4], sp);   /* the average shade: 2 and up is light */
}
static void ped_name(const plook_t *L, const felucca_font_t *tf, int32_t base, const char *ttl, uint16_t col, uint32_t mono)
{                                               /* a pedal's name; "A|B" is split either side of the LED */
    const char *bar = ttl;
    while (*bar && *bar != '|') bar++;
    if (*bar) {
        char left[16];
        uint32_t n = 0;
        while (ttl + n < bar && n < 15u) { left[n] = ttl[n]; n++; }
        left[n] = 0;
        if (mono) {
            mono_text(L->tx, L->ty - base, tf, left, L->sp);
            mono_text(240 - L->tx - atext_w(tf, bar + 1, L->sp), L->ty - base, tf, bar + 1, L->sp);
        } else {
            atext(L->tx, L->ty - base, tf, left, col, L->sp);
            atext(240 - L->tx - atext_w(tf, bar + 1, L->sp), L->ty - base, tf, bar + 1, col, L->sp);
        }
    } else {
        int32_t x = L->fl & L_CENTRE ? L->tx - atext_w(tf, ttl, L->sp) / 2 : L->tx;
        if (mono) mono_text(x, L->ty - base, tf, ttl, L->sp);
        else atext(x, L->ty - base, tf, ttl, col, L->sp);
    }
}
static void draw_pedal_page(uint32_t lk, const char *name, uint32_t on, const kview_t *kv, uint32_t nk, const char *msg)
{
    const plook_t *L = &PLOOK[lk % NLOOK];
    const felucca_font_t *tf = L->fl & L_SANS ? &FONT_S : &FONT_T;
    int32_t base = L->fl & L_SANS ? 12 : 17;   /* the name font's baseline, from its top */
    const char *ttl = name ? name : L->title;
    uint32_t half, i;
    char pv[3][8], nm[3][12];
    for (i = 0; i < nk; i++) {                  /* the values (% where the pedal is digital) and the knobs' names */
        const char *src = kv[i].name;
        uint32_t n = 0;
        if (L->fl & L_PCT && kv[i].mx == 127u) { char *p = put_num(pv[i], (kv[i].v * 100u + 63u) / 127u, 1); *p++ = '%'; *p = 0; }
        else { const char *v = kv[i].val; while (v[n] && n < 7u) { pv[i][n] = v[n]; n++; } pv[i][n] = 0; }
        for (n = 0; src[n] && n < 11u; n++) nm[i][n] = L->fl & L_LOWER && src[n] >= 'A' && src[n] <= 'Z' ? (char)(src[n] + 32) : src[n];
        nm[i][n] = 0;
    }
    for (half = 0; half < 2u; half++) {
        cv_begin(240, H_PEDAL / 2, C_BG);
        cv_oy = -Y_PEDAL - (int32_t)half * (H_PEDAL / 2);   /* drawn in screen coordinates */
        cv_rect(6, Y_PEDAL, 228, H_PEDAL, L->body);
        ab_x0 = 6; ab_y0 = Y_PEDAL; ab_x1 = 234; ab_y1 = Y_PEDAL + H_PEDAL;
        pedal_art(lk, L->body);
        mask_corners(6, Y_PEDAL, 228, H_PEDAL, 12);
        if (*ttl) ped_name(L, tf, base, ttl, L->tc, 0);
        if (msg) atext_halo(120 - text_w(&FONT_S, msg) / 2, L->ky - 8, &FONT_S, msg, L->lab, L->body, 0);
        for (i = 0; i < nk; i++) {              /* the knobs, their names and values */
            static const int16_t SIDE[3][2] = {{40, 88}, {200, 88}, {40, 154}};
            int32_t cx = L->fl & L_SIDES ? SIDE[i][0] : 46 + 74 * (int32_t)i, cy = L->fl & L_SIDES ? SIDE[i][1] : L->ky, r = L->fl & L_SIDES ? 13 : 14;
            uint32_t pm = (kv[i].v > kv[i].mx ? kv[i].mx : kv[i].v) * 1000u / (kv[i].mx ? kv[i].mx : 1u);
            const char *n = text_fit(nm[i], 72);
            if (L->fl & L_SKIRT) knob_skirt(cx, cy, pm, L->cap, L->ptr, r);
            else knob_knurl(cx, cy, pm, L->cap, L->ptr, r);
            atext_halo(cx - text_w(&FONT_S, n) / 2, cy + 17, &FONT_S, n, L->lab, L->body, 0);
            atext_halo(cx - text_w(&FONT_S, pv[i]) / 2, cy + 32, &FONT_S, pv[i], L->val, L->body, 0);
        }
        led_lamp(L->lx, L->ly, on, L->led);
        if (!on) {                              /* OFF beside the dark LED, on a little dark tag */
            int32_t w = text_w(&FONT_S, "OFF"), tx = L->lx - 9 - w;
            cv_rrect(tx - 3, L->ly - 9, w + 6, 17, 5, mix565(L->body, K_BLACK, 45));
            cv_text_bg(tx, L->ly - 8, &FONT_S, "OFF", C_SUB, mix565(L->body, K_BLACK, 45));
        }
        if (L->fx && !msg) foot_hex(L->fx, L->fy);
        if (thm->mono) {                        /* LCD or neon: all of it in the theme's look; on LCD the name stands out */
            mono_rect(6, Y_PEDAL, 228, H_PEDAL, L->body, L->led);
            mask_corners(6, Y_PEDAL, 228, H_PEDAL, 12);
            if (*ttl) ped_name(L, tf, base, ttl, 0, 1);
        }
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
    static const char *const TWL[4] = {"1/8", "1/4", "1/2", "BAR"}, *const REP[4] = {"1x", "2x", "3x", "4x"};
    static const char *const HLD[4] = {"1/4", "1/2", "1 BAR", "2 BARS"};
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
        case KV_TWL: s = TWL[v & 3u]; break;
        case KV_REP: s = REP[v & 3u]; break;
        case KV_HOLD: s = HLD[v & 3u]; break;
        case KV_DJ: { char *pp = val[k]; int32_t dv = (int32_t)v - 64;   /* LP 10 .. OPEN .. HP 10 */
            if (dv >= -2 && dv <= 2) { *put(pp, "OPEN") = 0; break; }
            pp = put(pp, dv < 0 ? "LP " : "HP "); *put_num(pp, (uint32_t)((dv < 0 ? -dv : dv) * 10 / 62), 1) = 0; break; }
        case KV_SEMI: { char *pp = val[k]; int32_t sv = (int32_t)(v % 25u) - 12;
            if (sv > 0) *pp++ = '+'; else if (sv < 0) *pp++ = '-';
            *put_num(pp, (uint32_t)(sv < 0 ? -sv : sv), 1) = 0; break; }
        default: break;
        }
        if (s) *put(val[k], s) = 0;
        else if (d->k[k].kind == KV_SEMI || d->k[k].kind == KV_DJ) {}
        else *put_num(val[k], d->k[k].kind == KV_PAT ? v % (m + 1u) + 1u : pedal_level(v), 1) = 0;
    }
}
static void draw_pedal(uint32_t trk, uint32_t e)
{
    uint32_t pd = set.ped[trk][e] % NPD, k;
    const pdef_t *d = &PDEF[pd];
    char val[3][8];
    kview_t kv[3];
    pknob_t pk[3];
    ped_vals(trk, e, pk, val);
    for (k = 0; k < 3u; k++) kv[k] = (kview_t){pk[k].name, *pk[k].v, pk[k].max ? pk[k].max : 1u, val[k]};
    draw_pedal_page(PD_LOOK[pd], ped_shown(pd) ? 0 : d->name, set.fx[trk].on >> e & 1u, kv, 3, 0);
}

/* where each look's art is most itself: the middle of the window shown on home's chips and the signal chain's cards */
static const uint8_t PL_FOCUS[NLOOK][2] = {
    {125, 165}, {120, 175}, {120, 160}, {100, 160}, {120, 170}, {120, 175}, {110, 185}, {120, 180}, {180, 160}, {122, 180},
    {140, 140}, {120, 160}, {120, 180}, {120, 165}, {120, 175}, {62, 150}, {120, 160}, {120, 165}, {120, 130}, {120, 165},
    {120, 172}, {110, 180}, {120, 165}, {160, 165}, {100, 158}, {120, 170}, {120, 170}, {120, 108}, {100, 170}, {140, 170},
    {120, 176}, {130, 160}, {120, 168}, {120, 164}, {118, 168}, {120, 172}, {110, 180}, {120, 175}};
static void art_window(uint32_t lk, int32_t dx, int32_t dy, int32_t w, int32_t h, int32_t r)
{                                               /* a window of a pedal's art (round its focus) at dx, dy, w x h */
    int32_t ox = cv_ox, oy = cv_oy, wx = PL_FOCUS[lk % NLOOK][0] - w / 2, wy = PL_FOCUS[lk % NLOOK][1] - h / 2;
    if (lk == LK_GLITCH && h < 40) wy += 34;    /* (a thin slice of GLITCH: its confetti, not its big name) */
    if (wx > 234 - w) wx = 234 - w;
    if (wx < 6) wx = 6;
    if (wy > Y_PEDAL + H_PEDAL - h) wy = Y_PEDAL + H_PEDAL - h;
    if (wy < Y_PEDAL) wy = Y_PEDAL;
    cv_rect(dx, dy, w, h, PLOOK[lk % NLOOK].body);
    cv_ox = ox + dx - wx; cv_oy = oy + dy - wy;
    ab_x0 = wx; ab_y0 = wy; ab_x1 = wx + w; ab_y1 = wy + h;
    pedal_art(lk % NLOOK, PLOOK[lk % NLOOK].body);
    cv_ox = ox; cv_oy = oy;
    ab_x0 = dx; ab_y0 = dy; ab_x1 = dx + w; ab_y1 = dy + h;   /* (what is drawn on it next stays on it) */
    if (r) mask_corners(dx, dy, w, h, r);
}
static void rgb3(uint16_t c, int32_t *q) { q[0] = (c >> 11) << 3; q[1] = ((c >> 5) & 63) << 2; q[2] = (c & 31) << 3; }
static int32_t hue3(uint16_t c, int32_t *q)    /* a colour at full strength, its hue alone; 0 if it is near grey */
{
    int32_t k, mx, mn;
    rgb3(c, q);
    mx = q[0] > q[1] ? q[0] : q[1]; if (q[2] > mx) mx = q[2];
    mn = q[0] < q[1] ? q[0] : q[1]; if (q[2] < mn) mn = q[2];
    if (mx - mn < 48) return 0;
    for (k = 0; k < 3; k++) q[k] = (q[k] - mn) * 255 / (mx - mn);
    return 1;
}
static void mono_rect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t body, uint16_t led)
{                                               /* a theme with mono: what is there, in its shades (LCD) or as neon */
    int32_t i, j, k, acc[3], q[3], lb;
    if (!thm->mono) return;
    if (!hue3(body, acc) && !hue3(led, acc)) { acc[0] = 255; acc[1] = 70; acc[2] = 180; }   /* the pedal's own colour */
    neon_name = RGB(acc[0] + (255 - acc[0]) * 3 / 5, acc[1] + (255 - acc[1]) * 3 / 5, acc[2] + (255 - acc[2]) * 3 / 5);
    rgb3(body, q); lb = (q[0] * 77 + q[1] * 150 + q[2] * 29) >> 8;   /* the body goes dark; what stands out from it glows */
    for (j = y; j < y + h; j++) {
        if (!row_vis(j)) continue;
        for (i = x; i < x + w; i++) {
            uint16_t c = cv_get(i, j);
            int32_t l, t, o[3];
            if (thm->mono == 1u) { cv_pset(i, j, MONO_SH[mono_lvl(c)]); continue; }
            rgb3(c, q); l = (q[0] * 77 + q[1] * 150 + q[2] * 29) >> 8;
            l = (l > lb ? l - lb : lb - l) * 255 / 150; if (l > 255) l = 255;
            if (!hue3(c, q)) for (k = 0; k < 3; k++) q[k] = acc[k];
            t = (q[0] * 77 + q[1] * 150 + q[2] * 29) >> 8;   /* a dark hue (blue, purple): lifted so it still glows */
            if (t < 140) for (k = 0; k < 3; k++) q[k] += (255 - q[k]) * (140 - t) / 260;
            t = l < 70 ? l * 40 / 70 : 40 + (l - 70) * 216 / 185;   /* the dark stays dark, a little tinted; the rest glows */
            o[0] = 14 + (q[0] - 14) * t / 256; o[1] = 6 + (q[1] - 6) * t / 256; o[2] = 28 + (q[2] - 28) * t / 256;
            if (l > 215) for (k = 0; k < 3; k++) o[k] += (255 - o[k]) * (l - 215) / 64;   /* the brightest: white hot */
            cv_pset(i, j, RGB(o[0], o[1], o[2]));
        }
    }
}
static void dim_rect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t k)   /* darken what is there, k % to the background */
{
    int32_t i, j;
    for (j = y; j < y + h; j++) if (row_vis(j)) for (i = x; i < x + w; i++) cv_pset(i, j, mix565(cv_get(i, j), C_BG, k));
}
static void rr_line(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint16_t c)   /* a round-cornered outline */
{
    int32_t sx0 = ab_x0, sy0 = ab_y0, sx1 = ab_x1, sy1 = ab_y1;
    ab_x0 = 0; ab_y0 = 0; ab_x1 = 240; ab_y1 = 400;
    cv_rect(x + r, y, w - 2 * r, 1, c); cv_rect(x + r, y + h - 1, w - 2 * r, 1, c);
    cv_rect(x, y + r, 1, h - 2 * r, c); cv_rect(x + w - 1, y + r, 1, h - 2 * r, c);
    aarc(x + r, y + r, r, r, 180, 270, c, 1); aarc(x + w - 1 - r, y + r, r, r, 270, 360, c, 1);
    aarc(x + r, y + h - 1 - r, r, r, 90, 180, c, 1); aarc(x + w - 1 - r, y + h - 1 - r, r, r, 0, 90, c, 1);
    ab_x0 = sx0; ab_y0 = sy0; ab_x1 = sx1; ab_y1 = sy1;
}
static const char *ped_title(uint32_t pd)       /* a pedal's name as lettered on it (its plain name when the look has none) */
{
    const char *t = PLOOK[PD_LOOK[pd]].title, *p;
    if (!ped_shown(pd) || !*t) return PDEF[pd].name;
    for (p = t; *p; p++) if (*p == '|') return PDEF[pd].name;
    return t;
}

/* home's bottom line: the chosen track's four pedals as quiet chips: an LED in the pedal's colour, lit when it is on
 * (dark when off), and its name, in ink when on and grey when off */
static void bottom_pedals(uint32_t trk)
{
    uint32_t e;
    cv_begin(240, H_BOT, C_BG);
    cv_oy = 0;
    for (e = 0; e < NPED_SLOTS; e++) {
        uint32_t pd = set.ped[trk][e] % NPD, on = set.fx[trk].on >> e & 1u, n = 0;
        const char *nm = PDEF[pd].name;
        const uint8_t *pc = ped_rgb(&PDEF[pd]);
        int32_t x = 1 + (int32_t)e * 60, w = 58;
        char sh[14];
        while (nm[n] && n < 13u) { sh[n] = nm[n]; n++; }
        sh[n] = 0;
        while (n > 1u && atext_w(&FONT_S, sh, -1) > w - 7) sh[--n] = 0;   /* (as much of the name as fits beside the LED) */
        cv_rrect(x, 2, w, 20, 9, C_KEYCAP);     /* the chip: an edge, its face */
        cv_rrect(x + 1, 3, w - 2, 18, 8, C_PANEL);
        if (on) disc(x + 5, 12, 3, rgbm(pc, 45));   /* the LED: a glow round it when on */
        disc(x + 5, 12, 2, on ? rgbk(pc, 100) : C_DIMC);
        ab_x0 = x; ab_y0 = 0; ab_x1 = x + w; ab_y1 = H_BOT;
        atext(x + 32 - atext_w(&FONT_S, sh, -1) / 2, 4, &FONT_S, sh, on ? C_TXT : C_SUB, -1);
    }
    cv_blit(0, Y_BOT);
}

/* ---- the signal chain (EDIT twice): the track's four pedals as cards, in order (FX SEL ENV LFO, left to right, top
 * to bottom): the power lamp, the name, three small knobs with their values. The chosen one glows; one switched off
 * is grey. ALGORITHM the slot, KNOB1 on / off, KNOB2-4 its knobs (named on the bottom line), PRESETS another pedal */
static void draw_chain(uint32_t trk, uint32_t sl)
{
    uint32_t half, e, k, pass;
    char val[3][8];
    pknob_t pk[3];
    for (half = 0; half < 2u; half++) {
        cv_begin(240, H_PEDAL / 2, C_BG);
        cv_oy = -(int32_t)half * (H_PEDAL / 2);
        for (pass = 0; pass < 2u; pass++)
            for (e = 0; e < NPED_SLOTS; e++) {
                uint32_t pd = set.ped[trk][e] % NPD, lk = PD_LOOK[pd], on = set.fx[trk].on >> e & 1u, me = e == sl;
                const plook_t *L = &PLOOK[lk];
                const char *nm = ped_title(pd);
                const felucca_font_t *f = &FONT_T;
                int32_t x = 4 + (int32_t)(e % 2u) * 118, y = 2 + (int32_t)(e / 2u) * 81, w = 114, h = 77;
                uint16_t halo = mix565(L->body, K_BLACK, 35);
                if (me != pass) continue;
                art_window(lk, x, y, w, h, 9);  /* the pedal itself: its art, its name, its knobs, its LED */
                if (atext_w(f, nm, 0) > w - 26) { f = &FONT_S; nm = text_fit(nm, w - 26); }
                atext_halo(x + 6, f == &FONT_T ? y + 1 : y + 4, f, nm, L->tc ? L->tc : L->lab, halo, 0);
                ped_vals(trk, e, pk, val);
                for (k = 0; k < 3u; k++) {
                    int32_t cx = x + 19 + (int32_t)k * 38;
                    uint32_t mx = pk[k].max ? pk[k].max : 1u, v = *pk[k].v > mx ? mx : *pk[k].v;
                    if (L->fl & L_SKIRT) knob_skirt(cx, y + 38, v * 1000u / mx, L->cap, L->ptr, 9);
                    else knob_knurl(cx, y + 38, v * 1000u / mx, L->cap, L->ptr, 9);
                    atext_halo(cx - text_w(&FONT_S, val[k]) / 2, y + 52, &FONT_S, val[k], L->val, halo, 0);
                }
                if (!on) dim_rect(x, y, w, h, 55);
                led_lamp(x + w - 11, y + 11, on, L->led);
                if (thm->mono) {
                    mono_rect(x, y, w, h, L->body, L->led);
                    mono_text(x + 6, f == &FONT_T ? y + 1 : y + 4, f, nm, 0);
                }
                mask_corners(x, y, w, h, 9);
                if (me) { rr_line(x - 1, y - 1, w + 2, h + 2, 10, C_WHITE); rr_line(x - 2, y - 2, w + 4, h + 4, 11, C_WHITE); }
            }
        cv_oy = 0;
        cv_blit(0, Y_PEDAL + half * (H_PEDAL / 2));
    }
}

/* the arpeggiator: its own page, notes climbing to the moon; RATE, OCTAVES, DIR */
static void draw_arp(uint32_t trk)
{
    if (trk < NSYN) {
        static const char *const DIR_NAME[3] = {"UP", "DOWN", "RANDOM"};
        static const char *const OCT_NAME[4] = {"1", "2", "3", "4"};
        uint32_t rate = set.arp_rate[trk] & 3u, oct = set.arp_oct[trk] & 3u, dir = set.arp_dir[trk] % 3u;
        kview_t kv[3] = {{"RATE", rate, 3, ARP_NAME[rate]}, {"OCTAVES", oct, 3, OCT_NAME[oct]}, {"DIR", dir, 2, DIR_NAME[dir]}};
        draw_pedal_page(LK_ARP, 0, set.arp_on >> trk & 1u, kv, 3, 0);
    } else
        draw_pedal_page(LK_ARP, 0, 0, 0, 0, "FOR LEAD, PAD, BASS");
}

/* the mixer: four channel strips, LEAD PAD BASS DRUMS (KNOB1-4): a header in the track's colour, a knob, its value, a
 * level meter (amber near the top), the button that mutes it with its light (on = playing); muted red */
static void draw_mixer(void)
{
    uint32_t half, r, i;
    for (half = 0; half < 2u; half++) {
        cv_begin(240, H_PEDAL / 2, C_BG);
        cv_oy = -(int32_t)half * (H_PEDAL / 2);
        for (r = 0; r < NTRK; r++) {
            uint32_t t = SEQ_ORDER[r], v = set.vol[t], muted = trk_mute >> t & 1u, n = (v * 10u + 63u) / 127u;
            int32_t x = (int32_t)r * 60, cx = x + 30;
            uint16_t col = trk_c(t, 100);
            char val[8];
            *put_num(val, v, 1) = 0;
            int32_t fh = (int32_t)(v * 100u / 127u);
            const char *tn = text_w(&FONT_S, slot_name(t)) <= 50 ? slot_name(t) : slot_short(t);
            (void)n; (void)i;
            cv_rrect(x + 2, 0, 56, H_PEDAL, 8, C_PANEL);
            cv_rrect(x + 4, 4, 52, 18, 9, col); /* the name, in a pill */
            cv_text_bg(cx - text_w(&FONT_S, tn) / 2, 5, &FONT_S, tn, C_ON, col);
            ab_x0 = x + 2; ab_y0 = 0; ab_x1 = x + 58; ab_y1 = H_PEDAL;
            knob_knurl(cx, 50, v * 1000u / 127u, muted ? mix565(col, C_PANEL, 60) : col, C_WHITE, 16);   /* its knob */
            cv_text_bg(cx - text_w(&FONT_S, muted ? "MUTE" : val) / 2, 72, &FONT_S, muted ? "MUTE" : val, muted ? C_MUTE : C_TXT, C_PANEL);
            for (i = 0; i < 10u; i++) {         /* the level, in LEDs: amber then red at the top */
                uint16_t lc = i >= 9u ? RGB(255, 90, 60) : i >= 7u ? RGB(255, 196, 60) : col;
                cv_rrect(cx - 8, 133 - (int32_t)i * 5, 16, 3, 1, i < n && !muted ? lc : mix565(lc, C_PANEL, 82));
            }
            led_lamp(x + 15, 154, !muted, muted ? C_MUTE : col);   /* playing: lit; muted: dark, the button's name red */
            cv_text_bg(x + 25, 146, &FONT_S, MUTE_BTN_NAME[r], muted ? C_MUTE : C_TXT, C_PANEL);
            (void)fh;
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
    cv_begin(240, H_BIG, C_BG);              /* the top bar: SUPER SEQ, the four colours, the song's tile */
    cv_rrect(0, 0, 240, H_BIG, 6, C_PANEL);
    logo4(6, 12);
    cv_text_bg(36, 3, &FONT_L, "SUPER SEQ", on ? C_TXT : C_SUB, C_PANEL);
    v[0] = (char)('A' + set.sseq_sel % NCHAIN);
    cv_rrect(202, 4, 32, 30, 8, C_TXT);
    cv_text_bg(210, 3, &FONT_L, v, C_BG, C_TXT);
    cv_blit(0, Y_BIG);
    if (cl < 1u || cl > NLINK) cl = NLINK;
    cv_begin(240, H_INFO, C_BG);             /* a cell above each knob: the length, the box, its pattern, on / off */
    info_cell(0, "SEQ", on ? "ON" : "OFF", C_SUB, on ? C_PLAYG : C_DIMC);
    v[0] = (char)('1' + cur % NLINK);
    info_cell(60, "BOX", v, C_SUB, col);
    v[0] = c[sel][cur % NLINK] == CH_REST ? 'X' : c[sel][cur % NLINK] ? (char)('A' + c[sel][cur % NLINK] - 1) : '-';
    info_cell(120, "PAT", v, C_SUB, col);
    v[0] = (char)('0' + cl);
    info_cell(180, "LEN", v, C_SUB, C_TXT);
    cv_blit(0, Y_INFO);
    cv_begin(240, H_VIEW, C_BG);             /* the rows */
    for (r = 0; r < NTRK; r++) {
        uint32_t t = SEQ_ORDER[r], me = t == sel, last = 0;
        int32_t y = (int32_t)r * 32;
        uint16_t tc = trk_c(t, 100), blk = me ? tc : trk_c(t, 24);
        cv_rrect(0, y + 1, 240, 30, 5, me ? C_PANEL2 : C_PANEL);
        cv_rrect(0, y + 1, 34, 30, 5, blk);
        cv_text_bg(17 - text_w(&FONT_S, slot_short(t)) / 2, y + 1, &FONT_S, slot_short(t), me ? C_ON : tc, blk);
        if (live) {                             /* the pattern it plays now ("-": resting) */
            v[0] = chain_rest >> t & 1u ? '-' : (char)('A' + play_pat[t] % NPAT);
            cv_text_bg(13, y + 15, &FONT_S, v, me ? C_ON : C_TXT, blk);
        }
        for (i = 0; i < NLINK; i++) {
            int32_t x = 38 + (int32_t)i * 25;
            uint32_t bv = c[t][i];
            uint16_t bg, ink;
            if (i >= cl) { cv_rrect(x, y + 5, 23, 22, 3, C_BG); continue; }   /* past the song's length: blacked out */
            if (bv) last = bv <= NPAT ? bv : 0u; /* (an X: what follows rests) */
            if (bv && bv <= NPAT) { bg = me ? tc : trk_c(t, 62); ink = C_BG; v[0] = (char)('A' + bv - 1); }
            else if (bv == CH_REST) { bg = C_EMPTY4; ink = me ? C_TXT : C_SUB; v[0] = 'X'; }   /* a rest */
            else if (last) { bg = trk_c(t, me ? 26 : 18); ink = trk_c(t, me ? 70 : 50); v[0] = (char)('A' + last - 1); }
            else { bg = C_EMPTY; ink = C_DIMC; v[0] = '-'; }
            if (me && i == cur) cv_rrect(x - 2, y + 3, 27, 26, 6, C_WHITE);
            cv_rrect(x, y + 5, 23, 22, 5, bg);
            cv_text_bg(x + 8, y + 8, &FONT_S, v, ink, bg);
            if (live && chain_at[t] % NLINK == i) cv_rrect(x + 3, y + 27, 17, 3, 1, C_PLAYHD);
        }
    }
    cv_blit(0, Y_VIEW);
    bottom_bar("ALG:BOX  PRESETS:SONG", C_TXT);
}

/* ---- the save page (SAVE): four slots, SAVE 1-4, each with its tempo (or EMPTY), the chosen one framed white; when
 * a save or load waits for "sure?", a box asks for the button again. 164 px under the top bar, in two halves */
/* the RECORD page (REC held): the two clears, the chosen one framed (ALGORITHM); REC asks, REC again does it */
static void draw_rec(uint32_t trk, uint32_t cur, uint32_t ask, const char *msg, uint32_t msg_ok)
{
    static const uint16_t YEL = RGB(255, 200, 60);
    uint32_t half, i;
    uint16_t red = RGB(214, 48, 38);
    char s[24], *p;
    if (msg) head_text(msg, msg_ok ? C_WHITE : C_MUTE, msg_ok ? C_PLAYG : C_MUTE);
    else {
        head_begin(red);
        disc(26, 19, 7, red);
        cv_text_bg(40, 3, &FONT_L, "RECORD", C_TXT, C_PANEL);
        cv_blit(0, Y_BIG);
    }
    for (half = 0; half < 2u; half++) {
        cv_begin(240, H_PEDAL / 2, C_BG);
        cv_oy = -(int32_t)half * (H_PEDAL / 2);
        for (i = 0; i < 2u; i++) {              /* the clears: a card each, the chosen one framed */
            int32_t y = 14 + (int32_t)i * 66;
            uint32_t me = i == cur;
            uint16_t card = me ? C_PANEL2 : C_PANEL;
            if (me) { cv_rrect(0, y, 240, 56, 8, C_WHITE); cv_rrect(2, y + 2, 236, 52, 7, card); }
            else cv_rrect(0, y, 240, 56, 8, card);
            if (i == 0u) { p = put(s, slot_name(trk)); p = put(p, " "); *p++ = (char)('A' + set.sel[trk] % NPAT); *p = 0; }
            else *put(s, "EVERYTHING") = 0;
            cv_text_bg(14, y + 5, &FONT_S, i ? "CLEAR ALL" : "CLEAR THE PATTERN", me ? C_SUB : C_DIMC, card);
            cv_text_bg(14, y + 20, &FONT_L, s, me ? C_TXT : C_SUB, card);
            if (me) cv_text_bg(228 - text_w(&FONT_S, "REC REC"), y + 5, &FONT_S, "REC REC", red, card);
        }
        if (ask) {                              /* "sure?" */
            const char *q = cur ? "CLEAR EVERYTHING?" : "CLEAR THE PATTERN?";
            cv_rrect(14, 34, 212, 96, 10, YEL);
            cv_rrect(17, 37, 206, 90, 8, C_PANEL);
            cv_text_bg(120 - text_w(&FONT_S, q) / 2, 52, &FONT_S, q, YEL, C_PANEL);
            cv_text_bg(120 - text_w(&FONT_S, "REC AGAIN TO DO IT") / 2, 78, &FONT_S, "REC AGAIN TO DO IT", C_TXT, C_PANEL);
            cv_text_bg(120 - text_w(&FONT_S, cur ? "SAVED PROJECTS STAY" : "THE OTHERS STAY") / 2, 100, &FONT_S, cur ? "SAVED PROJECTS STAY" : "THE OTHERS STAY", C_SUB, C_PANEL);
        }
        cv_oy = 0;
        cv_blit(0, Y_PEDAL + half * (H_PEDAL / 2));
    }
}
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
        cv_text_bg(210, 3, &FONT_L, s, C_BG, C_TXT);
        cv_blit(0, Y_BIG);
    }
    for (half = 0; half < 2u; half++) {
        cv_begin(240, H_PEDAL / 2, C_BG);
        cv_oy = -(int32_t)half * (H_PEDAL / 2);
        for (i = 0; i < NPROJ; i++) {
            int32_t y = (int32_t)i * 41;
            uint32_t me = i == slot, full = proj_bpm[i] != 0u;
            uint16_t card = me ? C_PANEL2 : C_PANEL;
            if (me) { cv_rrect(0, y + 1, 240, 38, 6, C_WHITE); cv_rrect(2, y + 3, 236, 34, 5, card); }
            else cv_rrect(0, y + 1, 240, 38, 6, card);
            cv_rrect(8, y + 6, 28, 28, 5, me ? C_TXT : C_KEYCAP);
            s[0] = (char)('1' + i); s[1] = 0;
            cv_text_bg(14, y + 4, &FONT_L, s, me ? C_BG : C_TXT, me ? C_TXT : C_KEYCAP);
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

/* ---- the startup screen (the tour's look, always Paper): the name; four pedals in the four colours, their lamps
 * lighting one after another over the 2.5 s start; under them the signal, clean, then shaped by each pedal that is on
 * (squared, doubled, echoed, shimmering), travelling left to right. ms: the time since switching on */
static int32_t ti_sin(int32_t ph)               /* a sine from a phase (256 a turn): -128..128, as two parabolas */
{
    int32_t x = ph & 255, h = x & 127, v = (h * (128 - h)) >> 5;
    return x < 128 ? v : -v;
}
static void draw_title(uint32_t ms)
{
    static const uint8_t TI_TRK[4][3] = {{200, 110, 0}, {122, 70, 218}, {0, 128, 190}, {24, 140, 50}};   /* LEAD PAD BASS DRUMS */
    const uint16_t paper = RGB(240, 238, 233), ink = RGB(26, 26, 30), sub = RGB(92, 92, 100), rule = RGB(214, 212, 206);
    const char *ver = APP_VER;
    char vs[16];
    uint32_t half, on = 0;
    int32_t i, x, y0 = 0;
    for (i = 0; i < 4; i++) if (ms > (uint32_t)i * 625u + 312u) on |= 1u << i;   /* (a lamp every 625 ms) */
    if (ver[0] == '0' && ver[1] == '.' && ver[2] == '0' && !ver[3]) { const char *t = "TEST BUILD"; for (i = 0; t[i]; i++) vs[i] = t[i]; vs[i] = 0; }
    else { vs[0] = 'V'; for (i = 0; ver[i] && i < 14; i++) vs[i + 1] = ver[i]; vs[i + 1] = 0; }
    for (half = 0; half < 2u; half++) {
        cv_begin(240, 120, paper);
        cv_oy = -(int32_t)half * 120;           /* (drawn in screen coordinates; the canvas is this half) */
        cv_text_bg(120 - text_w(&FONT_L, "QUARTET") / 2, 18, &FONT_L, "QUARTET", ink, paper);
        cv_text_bg(120 - text_w(&FONT_S, "A PEDAL-DRIVEN GROOVEBOX") / 2, 54, &FONT_S, "A PEDAL-DRIVEN GROOVEBOX", sub, paper);
        for (i = 0; i < 4; i++) {               /* the pedals: an outline; lit: the colour's edge, a wash of it, the lamp red */
            int32_t px = 16 + i * 54, lit = (int32_t)(on >> i & 1u);
            uint16_t c = RGB(TI_TRK[i][0], TI_TRK[i][1], TI_TRK[i][2]), fill = lit ? mix565(c, paper, 86) : paper, edge = lit ? c : ink;
            cv_rrect(px, 88, 46, 68, 8, edge);
            cv_rrect(px + (lit ? 2 : 1), 88 + (lit ? 2 : 1), 46 - (lit ? 4 : 2), 68 - (lit ? 4 : 2), lit ? 6 : 7, fill);
            disc(px + 14, 110, 6, ink); disc(px + 14, 110, 5, fill);   /* two knobs, the footswitch */
            disc(px + 32, 110, 6, ink); disc(px + 32, 110, 5, fill);
            disc(px + 23, 138, 8, ink); disc(px + 23, 138, 7, fill);
            disc(px + 38, 96, 3, lit ? RGB(226, 60, 40) : RGB(178, 176, 170));
        }
        for (x = 8; x < 232; x++) {             /* the signal: shaped by every lit pedal to its left */
            int32_t ph = x * 14 - (int32_t)(ms / 6u), v = ti_sin(ph), y, k, n = 0;
            for (k = 0; k < 4; k++) if (on >> k & 1u && x > 16 + k * 54 + 46) n++;
            if (n >= 1) { v = v * 22 / 10; v = v > 77 ? 77 : v < -77 ? -77 : v; v = v * 128 / 77; }   /* squared off */
            if (n >= 2) v = v * 7 / 10 + ti_sin(ph * 107 / 100 + 61) * 3 / 10;   /* doubled */
            if (n >= 3) v += ti_sin(ph - 90) * 3 / 10;                           /* an echo */
            if (n >= 4) v += ti_sin(x * 49 + (int32_t)(ms / 3u)) * 15 / 100;     /* shimmer */
            y = 184 - v * 6 / 128;
            if (x == 8) y0 = y;
            cv_rect(x, y < y0 ? y : y0, 1, (y < y0 ? y0 - y : y - y0) + 1, ink);
            y0 = y;
        }
        cv_rect(14, 214, 212, 1, rule);         /* the foot: who it's for, the version */
        cv_text_bg(16, 219, &FONT_S, "FOR THE M-VAVE FM-1", sub, paper);
        cv_text_bg(224 - text_w(&FONT_S, vs), 219, &FONT_S, vs, RGB(200, 110, 0), paper);
        cv_oy = 0;
        cv_blit(0, half * 120u);
    }
}
static void draw_theme_menu(uint32_t cur)        /* HOME + PRESETS: the themes, each with its colours, the one in use
                                                   outlined; over the middle of the screen */
{
    uint32_t i, k;
    int32_t h = 30 + (int32_t)NTHEME * 26;
    cv_begin(160, (uint32_t)h, C_BG);
    cv_rrect(0, 0, 160, h, 10, C_KEYCAP);
    cv_rrect(1, 1, 158, h - 2, 9, C_PANEL);
    cv_text_bg(80 - text_w(&FONT_S, "THEME") / 2, 6, &FONT_S, "THEME", C_SUB, C_PANEL);
    for (i = 0; i < NTHEME; i++) {
        const theme_t *t = &THEMES[i];
        int32_t y = 26 + (int32_t)i * 26;
        uint16_t bg = RGB(t->c[TC_BG][0], t->c[TC_BG][1], t->c[TC_BG][2]);
        if (i == cur) { cv_rrect(6, y, 148, 23, 7, C_PANEL2); rr_line(6, y, 148, 23, 7, C_TXT); }
        cv_text_bg(16, y + 4, &FONT_S, t->name, i == cur ? C_TXT : C_SUB, i == cur ? C_PANEL2 : C_PANEL);
        cv_rrect(92, y + 4, 54, 15, 5, RGB(t->c[TC_KEYCAP][0], t->c[TC_KEYCAP][1], t->c[TC_KEYCAP][2]));
        cv_rrect(93, y + 5, 52, 13, 4, bg);     /* a little of the theme: its background, its four track colours */
        for (k = 0; k < 4u; k++) disc(102 + (int32_t)k * 11, y + 11, 4, RGB(t->trk[k][0], t->trk[k][1], t->trk[k][2]));
    }
    cv_blit(40, (uint32_t)(120 - h / 2));
}

/* ---- the SONG page (the KEY page as was; GLO from the mixer; Conductor's): the key; LOCK, LIGHTS and its relative minor on
 * a line; the swing; one octave of keyboard from the key's home note, its notes lit and named, the notes held gold.
 * ALGORITHM the key (a sharp or flat a click), KNOB1 the lock, KNOB2 the lights, KNOB3 the swing */
static const char *const KMAJ[15] = {"C-FLAT", "G-FLAT", "D-FLAT", "A-FLAT", "E-FLAT", "B-FLAT", "F", "C", "G", "D", "A",
                                      "E", "B", "F-SHARP", "C-SHARP"};
static const char *const KMIN[15] = {"A-FLAT", "E-FLAT", "B-FLAT", "F", "C", "G", "D", "A", "E", "B", "F-SHARP", "C-SHARP",
                                      "G-SHARP", "D-SHARP", "A-SHARP"};
static void head_key(void)                      /* SONG, the stripe in the four track colours */
{
    uint32_t r;
    cv_begin(240, H_BIG, C_BG);
    cv_rrect(0, 0, 240, H_BIG, 6, C_PANEL);
    for (r = 0; r < NTRK; r++) cv_rrect(6, 5 + (int32_t)r * 7, 4, 6, 2, rgbk(TRK_RGB[SEQ_ORDER[r]], 100));
    cv_text_bg(18, 3, &FONT_L, "SONG", C_TXT, C_PANEL);
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
        cv_begin(240, H_PEDAL / 2, C_BG);
        cv_oy = -(int32_t)half * (H_PEDAL / 2);
        cv_rrect(0, 0, 240, H_PEDAL, 8, C_PANEL);
        p = put(t, KMAJ[key + 7]); *put(p, " MAJOR") = 0;
        cv_text_bg(120 - text_w(&FONT_L, t) / 2, 0, &FONT_L, t, gold, C_PANEL);
        (void)nks;
        {                                       /* one line: LOCK (KNOB1) and LIGHTS (KNOB2), lit when on; the other scale */
            static const char *const LB[2] = {"LOCK", "LIGHTS"};
            int32_t bx = 8;
            for (k = 0; k < 2; k++) {
                uint32_t on = k ? lights : lock;
                int32_t w = text_w(&FONT_S, LB[k]) + 16;
                cv_rrect(bx, 32, w, 18, 9, on ? gold : C_KEYCAP);
                cv_text_bg(bx + 8, 33, &FONT_S, LB[k], on ? C_ON : C_SUB, on ? gold : C_KEYCAP);
                bx += w + 6;
            }
            p = put(t, KMIN[key + 7]); *put(p, " MINOR") = 0;
            cv_text_bg(232 - text_w(&FONT_S, t), 33, &FONT_S, t, C_SUB, C_PANEL);
        }
        {                                       /* the swing (KNOB3): a bar from straight (50 %) to a heavy shuffle (66 %) */
            uint32_t sw = SWING_PCT(set.swing), w = (sw - 50u) * 120u / 16u;
            cv_text_bg(8, 55, &FONT_S, "SWING", C_SUB, C_PANEL);
            cv_rrect(66, 59, 124, 10, 5, C_KEYCAP);
            if (w) cv_rrect(68, 61, (int32_t)w, 6, 3, gold);
            p = put_num(t, sw, 2); *p++ = '%'; *p = 0;
            cv_text_bg(232 - text_w(&FONT_S, t), 55, &FONT_S, t, sw > 50u ? C_TXT : C_SUB, C_PANEL);
        }
        {                                       /* the piano: the key's home note to the next (a black home: from the
                                                   white key below); its notes lit and named, the notes held gold */
            int32_t lo = black((uint32_t)pc) ? pc - 1 : pc, hi = black((uint32_t)pc) ? pc + 13 : pc + 12, nw = 0, kw, x, n, wx[16];
            for (n = lo; n <= hi; n++) if (!black((uint32_t)(n + 12) % 12u)) nw++;
            kw = 216 / nw;
            if (thm_light) cv_rect(12 + (216 - nw * kw) / 2 - 2, top - 2, nw * kw + 2, 84, C_SUB);   /* (light: an outline) */
            for (n = lo, k = 0; n <= hi; n++) {
                uint32_t cpc = (uint32_t)(n + 12) % 12u, on = in_key(cpc), hit = held >> cpc & 1u;
                if (black(cpc)) continue;
                x = 12 + (216 - nw * kw) / 2 + k * kw; wx[n - lo] = x; k++;
                cv_rect(x, top, kw - 2, 80, hit ? gold : on ? C_KWON : C_KWOFF);
                if (on) cv_text_bg(x + kw / 2 - text_w(&FONT_S, WN[cpc]) / 2, top + 58, &FONT_S, WN[cpc], hit ? C_ON : C_KWTXT, hit ? gold : C_KWON);
            }
            for (n = lo + 1; n < hi; n++) {
                uint32_t cpc = (uint32_t)(n + 12) % 12u, on = in_key(cpc), hit = held >> cpc & 1u;
                const char *nm = key > 0 ? SHN[cpc] : FLN[cpc];
                if (!black(cpc)) continue;
                x = wx[n - 1 - lo] + kw - 12;
                cv_rect(x, top, 22, 48, hit ? gold : on ? C_KBON : C_KBOFF);
                if (on) cv_text_bg(x + 11 - text_w(&FONT_S, nm) / 2, top + 26, &FONT_S, nm, hit ? C_ON : gold, hit ? gold : C_KBON);
            }
        }
        cv_oy = 0;
        cv_blit(0, Y_PEDAL + half * (H_PEDAL / 2));
    }
}

/* ---- the SOUND page (EDIT): the chosen instrument's sound in depth. Synths: five pages (ALGORITHM) INSTR (type, bank,
 * sound, USER slot), MAIN (glide, cutoff, resonance, filter envelope), OSC (wave 1, wave 2, its tuning, the mix / FM
 * depth), ENV (attack, decay, sustain, release), LFO (rate, depth, target, wave). Drums: KIT (bank, kit, USER slot),
 * then the drums, each its pitch, decay, level (and the tempo). KNOB1-4 the four knobs; SELECT another
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
/* the sound browser's picture of the type, screen-printed in two inks like a pedal, its name lettered on it:
 * lead: mountains under a sun, bass: stacked blocks, pad: rings rising, drums: confetti dots */
static void art_print(uint32_t kind, int32_t x, int32_t y, int32_t w, int32_t h, const uint8_t *rgb)
{
    static const char *const NM[4] = {"lead", "bass", "pad", "drums"};
    uint16_t base = rgbk(rgb, 77), dk = rgbk(rgb, 27), ink = rgbk(rgb, 100), cr = rgbl(rgb, 70);
    int32_t i, k;
    cv_rect(x, y, w, h, base);
    ab_x0 = x; ab_y0 = y; ab_x1 = x + w; ab_y1 = y + h;
    switch (kind & 3u) {
    case 0: {
        int16_t mt[16] = {0, 62, 0, 44, 22, 18, 34, 32, 46, 12, 64, 40, 86, 30, 86, 62}, lit[14] = {22, 18, 34, 32, 46, 12, 64, 40, 86, 30, 86, 62, 40, 62};
        adisc(x + 63, y + 41, 15, cr);
        for (k = 0; k < 9; k++) aline(x + 63 + acs(180 + k * 45 / 2, 19), y + 41 + asn(180 + k * 45 / 2, 19), x + 63 + acs(180 + k * 45 / 2, 27), y + 41 + asn(180 + k * 45 / 2, 27), cr, 2);
        for (i = 0; i < 16; i += 2) { mt[i] = (int16_t)(mt[i] + x); mt[i + 1] = (int16_t)(mt[i + 1] + y); }
        for (i = 0; i < 14; i += 2) { lit[i] = (int16_t)(lit[i] + x); lit[i + 1] = (int16_t)(lit[i + 1] + y); }
        afill(mt, 8, dk, 0, 0);
        afill(lit, 7, ink, 3, 6);
        for (i = 0; i < 14; i++) ap(x + 8 + i * 11 / 2, y + 22 + i * 37 % 9, cr);
        break;
    }
    case 2:
        for (k = 0; k < 6; k++) aring(x + w / 2, y + h, 10 + k * 9, k & 1 ? 1 : 2, k & 1 ? cr : dk);
        astars(7, x + 4, y + 22, x + w - 4, y + h - 30, 10, cr);
        break;
    case 1:
        for (k = 0; k < 5; k++) {
            int16_t b[8] = {(int16_t)(x + 6 + k * 15), (int16_t)(y + h - 12 - k * 8), (int16_t)(x + 22 + k * 15), (int16_t)(y + h - 12 - k * 8),
                            (int16_t)(x + 22 + k * 15), (int16_t)(y + h), (int16_t)(x + 6 + k * 15), (int16_t)(y + h)};
            afill(b, 4, dk, 0, 0);
            afill(b, 4, ink, 3, 6);
            cv_rect(x + 6 + k * 15, y + h - 12 - k * 8, 16, 2, cr);
        }
        break;
    default: {
        uint32_t sd = 5;
        for (i = 0; i < 22; i++) {
            int32_t cx = x + 6 + (int32_t)rnd_n(&sd, (uint32_t)w - 12), cy = y + 22 + (int32_t)rnd_n(&sd, (uint32_t)h - 28), r = 1 + (int32_t)rnd_n(&sd, 5);
            uint32_t c = rnd_n(&sd, 3);
            adisc(cx, cy, r, c == 0u ? dk : c == 1u ? ink : cr);
        }
        break;
    }
    }
    mask_corners(x, y, w, h, 9);
    atext_halo(x + 6, y + 1, &FONT_T, NM[kind & 3u], cr, dk, 0);
}
static void draw_instr(uint32_t trk)
{
    static const char *const TAB[NPAGE + 1u] = {"INSTR", "MAIN", "OSC", "ENV", "LFO"};
    static const char *const DTAB[NDR + 1u] = {"KIT", "BD", "SD", "HH", "OH", "CP", "PC"};
    static const uint16_t YEL = RGB(255, 200, 60);
    const uint8_t *rgb = trk_rgb(trk);
    uint16_t col = rgbk(rgb, 100), dim = C_ROW;
    uint8_t ids[40], sub[40];
    uint32_t now = trk < NSYN ? set.snd[trk].preset : set.dkit, bk = id_bank(trk, now), n, m = 0, at = 0, i, half, top;
    uint32_t ntab = trk < NSYN ? NPAGE + 1u : NDR + 1u;
    const char *const *bkn = trk < NSYN ? BANK_NAME[TTYPE(trk)] : KBANK_NAME;
    uint32_t used = trk < NSYN ? set.mu.ub.snd[TTYPE(trk)][usr_slot % NUSND].preset != 0u : set.mu.ub.kit[usr_slot % NUKIT].used != 0u;
    char ty[16], gr[20], us[16], *p;
    n = trk < NSYN ? sound_list(TTYPE(trk), ids, now) : kit_list(ids, now);
    for (i = 0; i < n; i++) if (id_bank(trk, ids[i]) == bk) { if (ids[i] == now) at = m; sub[m++] = ids[i]; }
    top = at > 2u ? at - 2u : 0u;               /* the list: six of the group's sounds, the one playing among them */
    if (m >= 6u && top + 6u > m) top = m - 6u;
    if (m < 6u) top = 0;
    if (trk < NSYN) { p = put(ty, "< "); p = put(p, TYPE_NAME[TTYPE(trk)]); *put(p, " >") = 0; } else *put(ty, "DRUMS") = 0;
    p = put(gr, "< "); p = put(p, bk < NBANK ? bkn[bk] : "OLDER"); *put(p, " >") = 0;
    if (usr_ask) { p = put(us, "SAVE U"); *p++ = (char)('1' + usr_slot); *p++ = '?'; *p = 0; }
    else { p = put(us, "SAVE U"); *p++ = (char)('1' + usr_slot); p = put(p, used ? " *" : ""); *p = 0; }
    for (half = 0; half < 2u; half++) {
        cv_begin(240, H_PEDAL / 2, C_BG);
        cv_oy = -(int32_t)half * (H_PEDAL / 2);
        for (i = 0; i < ntab; i++) {            /* the pages, as pills, INSTR lit */
            int32_t tw = 232 / (int32_t)ntab, tx = 4 + (int32_t)i * tw;
            const char *tn = trk < NSYN ? TAB[i] : DTAB[i];
            cv_rrect(tx + 1, 2, tw - 3, 20, 10, i == 0u ? col : dim);
            cv_text_bg(tx + tw / 2 - 1 - text_w(&FONT_S, tn) / 2, 4, &FONT_S, tn, i == 0u ? C_ON : C_SUB, i == 0u ? col : dim);
        }
        art_print(TTYPE(trk), 4, 26, 86, 62, rgb);   /* the type's picture */
        cv_rrect(4, 92, 86, 18, 9, rgbm(rgb, 30));   /* K1 the type, K2 the group */
        cv_text_bg(47 - text_w(&FONT_S, ty) / 2, 93, &FONT_S, ty, trk < NSYN ? col : rgbl(rgb, 30), rgbm(rgb, 30));
        cv_rrect(4, 113, 86, 18, 9, col);
        cv_text_bg(47 - text_w(&FONT_S, text_fit(gr, 82)) / 2, 114, &FONT_S, text_fit(gr, 82), C_ON, col);
        cv_rrect(4, 136, 86, 22, 11, usr_ask ? YEL : C_BG);   /* K4 the USER slot (REC twice: save) */
        if (!usr_ask) { cv_rrect(4, 136, 86, 22, 11, col); cv_rrect(5, 137, 84, 20, 10, C_BG); }
        cv_text_bg(47 - text_w(&FONT_S, us) / 2, 139, &FONT_S, us, usr_ask ? RGB(16, 16, 20) : col, usr_ask ? YEL : C_BG);
        for (i = 0; i < 6u && top + i < m; i++) {   /* K3: the group's sounds */
            uint32_t id = sub[top + i], me = top + i == at;
            int32_t y = 26 + (int32_t)i * 22;
            const char *nm = trk < NSYN ? sound_name(TTYPE(trk), id) : kit_name_id(id);
            cv_rrect(94, y, 142, 20, 10, me ? col : dim);
            cv_text_bg(104, y + 2, &FONT_S, text_fit(nm, me ? 104 : 124), me ? C_ON : C_ROWTXT, me ? col : dim);
            if (me) glyph4(TTYPE(trk), 213, y + 6, 15, 8, C_BG, col);
        }
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
        cv_begin(240, H_PEDAL / 2, C_BG);
        cv_oy = -(int32_t)half * (H_PEDAL / 2);
        cv_rrect(0, 0, 240, H_PEDAL, 8, C_PANEL);
        for (i = 0; i < ntab; i++) {            /* the pages (drums: the drums), the chosen one lit */
            int32_t tw = 232 / (int32_t)ntab, tx = 4 + (int32_t)i * tw;   /* (7 drum tabs: 33 px each) */
            const char *tn = trk < NSYN ? TAB[i] : DTAB[i];
            cv_rrect(tx + 1, 6, tw - 2, 22, 11, i == sel ? col : C_KEYCAP);
            cv_text_bg(tx + tw / 2 - text_w(&FONT_S, tn) / 2, 9, &FONT_S, tn, i == sel ? C_ON : C_SUB, i == sel ? col : C_KEYCAP);
        }
        {
            const char *sn = (trk < NSYN ? sound_name(TTYPE(trk), set.snd[trk].preset) : kit_name());
            cv_text_bg(120 - text_w(&FONT_S, sn) / 2, 36, &FONT_S, sn, col, C_PANEL);
        }
        for (i = 0; i < 4u; i++) {              /* the four knobs, KNOB1-4 (drums: three) */
            int32_t cx = 31 + (int32_t)i * 59;
            if (!nm[i][0]) continue;
            draw_knob(cx, 84, v[i], mx[i] ? mx[i] : 1u, col, rgbk(rgb, 35));
            cv_text_bg(cx - text_w(&FONT_S, nm[i]) / 2, 106, &FONT_S, nm[i], ink, C_PANEL);
            lcd_value(cx, 126, 54, vs[i], C_LCDTXT);
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
static uint32_t gm_drum(uint32_t note)          /* a General MIDI drum note: the drum it plays (NDR: none) */
{
    return note == 36u || note == 35u ? DR_KICK : note == 38u || note == 40u ? DR_SNARE
         : note == 42u || note == 44u ? DR_HAT : note == 46u ? DR_OPEN : note == 39u ? DR_CLAP
         : note == 37u || note == 75u || note == 76u ? DR_PERC : NDR;
}
static void play_live(uint32_t trk, uint32_t note, uint32_t on)   /* a live note on a track (MIDI note numbers) */
{
    if (trk < NSYN) { live_note(trk, note, on); return; }
    if (on) {                                   /* DRUMS: the General MIDI drum notes */
        uint32_t d = gm_drum(note);
        if (d < NDR) hit_req[d]++;
    }
}

static uint8_t mrq_note[64], mrq_vel[64];        /* MIDI notes in, for the main loop to record (heard at once) */
static uint32_t mrq_w, mrq_r;
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
        else continue;
        if (mrq_w - mrq_r < 64u) {               /* (and recorded, as the keys are: the main loop takes it) */
            mrq_note[mrq_w % 64u] = d1 & 127u; mrq_vel[mrq_w % 64u] = ty == 0x90u ? d2 : 0u; mrq_w++;
        }
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
static int pat_transpose(uint32_t t, uint32_t p, int32_t by)   /* every note of a synth's pattern moved by semitones;
                                                   0 (nothing moved) if one would leave 1..127 */
{
    uint32_t s, i;
    for (s = 0; s < NSTEP; s++) {
        const uint8_t *nt = step_note(t, p, s);
        for (i = 0; i < STEP_NN(t); i++) if (nt[i] && ((int32_t)nt[i] + by < 1 || (int32_t)nt[i] + by > 127)) return 0;
    }
    for (s = 0; s < NSTEP; s++) {
        uint8_t *nt = step_note(t, p, s);
        for (i = 0; i < STEP_NN(t); i++) if (nt[i]) nt[i] = (uint8_t)((int32_t)nt[i] + by);
    }
    return 1;
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
 * the rows it cannot come from (drums for a synth, synths for the drums) faint. SELECT the track, PRESETS or
 * ALGORITHM the pattern; REC asks, REC again copies; SEQ back */
static void draw_copy(uint32_t dt, uint32_t dp, uint32_t ts, uint32_t ps, uint32_t ask)
{
    static const uint16_t YEL = RGB(255, 200, 60);
    uint32_t half, r, q;
    char v[2] = {0, 0}, s[32], *p;
    p = put(s, slot_short(ts)); *p++ = ' '; *p++ = (char)('A' + ps % NPAT); p = put(p, "  INTO  ");
    p = put(p, slot_short(dt)); *p++ = ' '; *p++ = (char)('A' + dp % NPAT); *p = 0;
    for (half = 0; half < 2u; half++) {
        cv_begin(240, H_PEDAL / 2, C_BG);
        cv_oy = -(int32_t)half * (H_PEDAL / 2);
        for (q = 0; q < NPAT; q++) { v[0] = (char)('A' + q); cv_text_bg(48 + (int32_t)q * 20 - text_w(&FONT_S, v) / 2, 0, &FONT_S, v, C_DIMC, C_BG); }
        for (r = 0; r < NTRK; r++) {
            uint32_t t = SEQ_ORDER[r], other = (t < NSYN) != (dt < NSYN), me = t == ts;
            int32_t y = 20 + (int32_t)r * 30;
            uint16_t tc = trk_c(t, 100), blk = other ? C_PANEL : me ? tc : trk_c(t, 24);
            cv_rrect(0, y, 34, 26, 4, blk);
            cv_text_bg(17 - text_w(&FONT_S, slot_short(t)) / 2, y + 5, &FONT_S, slot_short(t), other ? C_DIMC : me ? C_ON : tc, blk);
            for (q = 0; q < NPAT; q++) {
                int32_t x = 38 + (int32_t)q * 20;
                uint32_t u = !other && pat_used(t, q), src = me && q == ps, dst = t == dt && q == dp;
                uint16_t bg = src ? tc : other ? C_BG : u ? trk_c(t, 30) : C_EMPTY;
                uint16_t in = src ? C_BG : other ? C_PANEL2 : u ? trk_c(t, 90) : C_DIMC;
                if (dst) cv_rrect(x - 1, y - 1, 20, 28, 4, C_WHITE);
                cv_rrect(x + 1, y + 1, 16, 24, 3, bg);
                v[0] = (char)('A' + q);
                cv_text_bg(x + 9 - text_w(&FONT_S, v) / 2, y + 5, &FONT_S, v, in, bg);
            }
        }
        cv_text_bg(120 - text_w(&FONT_S, s) / 2, 143, &FONT_S, s, ask ? YEL : C_TXT, C_BG);
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
static uint32_t kit_list(uint8_t *ids, uint32_t cur)   /* the kits offered, group by group, then USER (cur first, if it
                                                   is one no longer offered) */
{
    uint32_t g, i, n = 0;
    if (kit_bank(cur) == 0xFFu && cur < NKIT) ids[n++] = (uint8_t)cur;
    for (g = 0; g < 4u; g++) for (i = 0; i < 6u && KIT_GRP[g][i] != 0xFFu; i++) ids[n++] = KIT_GRP[g][i];
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
static uint32_t id_bank(uint32_t t, uint32_t id) { return t < NSYN ? sound_group(TTYPE(t), id) : kit_bank(id); }
static void snd_step(uint32_t t, int32_t e, int bank_only)   /* e sounds on (bank_only: staying in its bank) */
{
    uint8_t ids[40], sub[40];
    uint32_t now = t < NSYN ? set.snd[t].preset : set.dkit, n = t < NSYN ? sound_list(TTYPE(t), ids, now) : kit_list(ids, now), i, k = 0, m = 0;
    for (i = 0; i < n; i++) if (!bank_only || id_bank(t, ids[i]) == id_bank(t, now)) sub[m++] = ids[i];
    if (!m) return;
    for (i = 0; i < m; i++) if (sub[i] == now) k = i;
    k = (uint32_t)(((int32_t)k + e % (int32_t)m + (int32_t)m) % (int32_t)m);
    if (t < NSYN) sound_load(t, sub[k]); else kit_load(sub[k]);
}
static int snd_bank(uint32_t t, int32_t e)      /* another bank: its first sound (an empty USER bank is passed by) */
{
    uint8_t ids[40];
    uint32_t now = t < NSYN ? set.snd[t].preset : set.dkit, n = t < NSYN ? sound_list(TTYPE(t), ids, now) : kit_list(ids, now);
    uint32_t b = id_bank(t, now), tries, i;
    if (b >= NBANK) b = e > 0 ? NBANK - 1u : 0u;   /* (from one no longer offered: the first group, or the last) */
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
        for (i = 0; i < sizeof set.sdyn; i++) if (d[i] >= NDYN) return 0;
        d = &set.ddyn[0][0][0];
        for (i = 0; i < sizeof set.ddyn; i++) if ((d[i] & 15u) >= NDYN || d[i] >> 4 >= NDYN) return 0;
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
    ws_home_all();                              /* (WRONG STATION: the real sounds are what is kept) */
    set.mute = (uint8_t)trk_mute;
    if (slot_save(i, &set, sizeof set)) return -1;
    proj_bpm[i] = set.bpm;
    return 0;
}
static int proj_load(uint32_t i)                /* 0 = loaded; -1 = empty or broken (the current work is untouched) */
{
    uint8_t th = set.mu.ub.theme;                /* (the theme stays as it is: it is the instrument's) */
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
    set.mu.ub.theme = th;
    trk_req++;                                  /* (the slots' instruments: the voices dealt again) */
    trk_mute = set.mute;
    clock_set_bpm(set.bpm);
    return 0;
}

/* the knobs that work by position (SELECT, ALGORITHM, PRESETS): where they stand in a turn of ALG_CLICKS clicks */
static uint32_t turn(uint32_t pos, int32_t e)   /* one knob position, e clicks on, round and round */
{
    return (uint32_t)(((int32_t)pos + e % (int32_t)ALG_CLICKS + (int32_t)ALG_CLICKS) % (int32_t)ALG_CLICKS);
}
static void knobs_sync(uint32_t *snd_pos, uint32_t *pat_pos)   /* ALGORITHM and PRESETS stand where what is chosen is */
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
static void step_write(uint32_t trk, uint32_t pt, uint32_t len, uint32_t n, uint32_t *cur, uint32_t *chord_n, uint32_t *chord_trk, uint32_t dyn)
{                                               /* the step recorder: a note at the cursor (a chord: the notes held together) */
    uint8_t *nt = step_note(trk, pt, *cur), *ln = step_len(trk, pt, *cur);
    if (STEP_NN(trk) > 1u) {                    /* a chord: the keys held together, the cursor moves when all are up */
        if (*chord_n == 0u) { syn_clear_step(trk, pt, *cur); *chord_trk = trk; set.sdyn[trk][pt][*cur] = (uint8_t)dyn; }
        if (*chord_n < 4u) {
            if (*chord_n == 0u) { *ln = (uint8_t)syn_room(trk, pt, *cur, len); nt[0] = (uint8_t)n; }
            else nt[*chord_n] = (uint8_t)n;
            (*chord_n)++;
        }
    } else {
        uint32_t L = syn_room(trk, pt, *cur, len);
        *ln = (uint8_t)L;                       /* (the length first: the interrupt may read the step meanwhile) */
        set.sdyn[trk][pt][*cur] = (uint8_t)dyn;
        nt[0] = (uint8_t)n;
        *cur = (*cur + L) % len;                /* (on past the note) */
    }
}
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
static void rec_note(uint32_t t, uint32_t p, uint32_t s, uint32_t note, uint32_t ln, uint32_t dyn)   /* add a note to a step (a chord:
                                                   a free place); dyn: its expression (a MIDI keyboard's velocity; the keys: NORMAL) */
{
    uint8_t *nt = step_note(t, p, s), *l = step_len(t, p, s);
    uint32_t i, n = STEP_NN(t), empty = !nt[0];
    for (i = 0; i < n; i++) if (nt[i] == note) break;   /* there already: only the length */
    if (i == n) { for (i = 0; i < n; i++) if (!nt[i]) break; if (i == n) i = n - 1u; }   /* (full: the last place) */
    if (n == 1u || empty || ln > *l) *l = (uint8_t)(ln > NSTEP ? NSTEP : ln);   /* (the length first: the interrupt may read the step) */
    if (n == 1u || empty) set.sdyn[t % NSYN][p % NPAT][s % NSTEP] = (uint8_t)dyn;
    nt[i] = (uint8_t)note;
}
static uint32_t vel_dyn(uint32_t v)             /* a MIDI note's velocity -> the expression it is recorded with */
{
    return v < 48u ? 2u : v < 100u ? 0u : v < 118u ? 5u : 4u;   /* SOFT, NORMAL, LOUD, PUNCH */
}

/* recording (the LIVE page, or REC from home): REC starts a take (a count-in bar first, when stopped; playing: from
 * the next bar), REC again ends it. Each take keeps a copy of the pattern first (ARP on the LIVE page puts it back).
 * An empty pattern is the first take's: it starts a bar long and grows a bar each time the take reaches its end (up
 * to 4); REC ends it at the bar it is in */
static uint32_t rec_wait;                       /* 1: the count-in (stopped), 2: to the next bar (playing) */
static uint32_t rec_wpos, rec_grow, rec_t, rec_p;
static struct {
    uint8_t ok, t, p, len;
    chord_t ch[NSTEP];
    uint8_t dy[NSTEP];
    uint16_t dp[NDR][NSTEP / PAGE];
    uint8_t dd[NDR][NSTEP / 2u];
} undo;
static void undo_take(uint32_t t, uint32_t p)
{
    uint32_t s, d;
    undo.t = (uint8_t)t; undo.p = (uint8_t)p; undo.len = set.plen[t][p];
    if (t < NSYN) for (s = 0; s < NSTEP; s++) { undo.ch[s] = *step_at(t, p, s); undo.dy[s] = set.sdyn[t][p][s]; }
    else for (d = 0; d < NDR; d++) {
        for (s = 0; s < NSTEP / PAGE; s++) undo.dp[d][s] = set.dpat[p][d][s];
        for (s = 0; s < NSTEP / 2u; s++) undo.dd[d][s] = set.ddyn[p][d][s];
    }
    undo.ok = 1;
}
static int undo_back(void)                      /* the pattern as it was before the last take; 0: nothing to undo */
{
    uint32_t s, d, t = undo.t, p = undo.p;
    if (!undo.ok) return 0;
    if (t < NSYN) for (s = 0; s < NSTEP; s++) { *step_at(t, p, s) = undo.ch[s]; set.sdyn[t][p][s] = undo.dy[s]; }
    else for (d = 0; d < NDR; d++) {
        for (s = 0; s < NSTEP / PAGE; s++) set.dpat[p][d][s] = undo.dp[d][s];
        for (s = 0; s < NSTEP / 2u; s++) set.ddyn[p][d][s] = undo.dd[d][s];
    }
    set.plen[t][p] = undo.len;
    undo.ok = 0;
    return 1;
}
static void rec_start(uint32_t t)               /* REC: a take on track t, in the pattern it plays (stopped: the chosen) */
{
    uint32_t p = (set.sseq_on && clk_run ? play_pat[t] : set.sel[t]) % NPAT;   /* (the pattern chosen: if it is only
                                                   queued, the take waits for it to come round; SUPER SEQ: the one playing) */
    undo_take(t, p);
    rec_t = t; rec_p = p;
    rec_grow = !set.sseq_on && !pat_used(t, p);   /* (empty: the first take sets its length) */
    if (rec_grow) set.plen[t][p] = PAGE;
    live_rec = 1;
    if (!clk_run) {                             /* stopped: a bar's count-in (silent, on the screen), then step 1 */
        rec_wait = 1;
        clk_pre = PAGE;
        clk_go++; clk_run = 1;
    } else if (play_pat[t] % NPAT != p) rec_wait = 3;   /* playing another: from when the chosen one starts */
    else { rec_wait = 2; rec_wpos = trk_pos[t]; }   /* playing it: from the next bar */
}
static void rec_stop(void)                      /* REC again (or STOP): the take ends; the first take's length set */
{
    if (live_rec && rec_grow) {
        if (rec_wait) set.plen[rec_t][rec_p] = undo.len;   /* (not begun: as it was) */
        else {
            uint32_t pos = trk_pos[rec_t], bars = pos / PAGE + 1u;
            if (pos % PAGE < 4u && bars > 1u) bars--;   /* (just over a bar line: the bar before was meant) */
            if (bars * PAGE < set.plen[rec_t][rec_p]) set.plen[rec_t][rec_p] = (uint8_t)(bars * PAGE);
        }
    }
    live_rec = 0; rec_wait = 0; rec_grow = 0;
}
static uint32_t rec_beats(void)                 /* the count-in's beats still to come (4 .. 1); 0: none */
{
    if (rec_wait == 1u) return clk_ack == clk_go && clk_count ? clk_count : clk_ack == clk_go ? 0u : 4u;
    if (rec_wait == 2u) return (PAGE - trk_pos[rec_t] % PAGE + 3u) / 4u;
    if (rec_wait == 3u) {                       /* (to the end of the pattern playing: the last four beats counted) */
        uint32_t L = pat_len(rec_t, play_pat[rec_t]), b = (L - trk_pos[rec_t] % L + 3u) / 4u;
        return b > 4u ? 4u : b;
    }
    return 0;
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
    uint32_t wipe_ask = 0;                      /* the RECORD page's CLEAR EVERYTHING: confirmed, to be done */
    uint32_t page_hold_t = 0, vpage = 0;        /* SEQ screens: when you last moved the page / cursor (it stays put 3 s); the page shown */
    uint8_t dyn_ui = 0;                         /* the step recorder's dynamic (KNOB3), by the knob's place (DYN_UI): NORMAL */
    uint32_t dheld = 0xFFu;                     /* drums editing: the white key held (KNOB2 sets that hit's dynamic) */
    uint32_t glo_t0 = 0, glo_dn = 0, glo_used = 0, rep_sel = 2;   /* GLO: when it went down; held; used (no page);
                                                   the beat repeat's length (white keys 1-5 while held: 1/2 .. 1/32; 1/8) */
    static const uint8_t REP_STEPS[5] = {8, 4, 2, 1, REP_HALF};
    static const char *const REP_MSG[5] = {"REPEAT 1/2", "REPEAT 1/4", "REPEAT 1/8", "REPEAT 1/16", "REPEAT 1/32"};
    const char *msg = 0;                        /* shown big for 1.5 s: SAVED 3, LOAD FAILED, ALL CLEARED ... */
    uint32_t pedal_mode = 0, head_drawn = ~0u, pedal_drawn = ~0u;   /* the pedal screen up?, what it shows */
    uint32_t thm_menu = 0, thm_full = 0;          /* the theme menu up; the screen to draw once under it (a new theme) */
    uint32_t batt_shown = 0, bt_hd = 0, bt_bd = 0, bt_pd = 0;   /* the battery's warning as shown (0 none, 1 off, 2 on); the top bar before */
    /* SELECT: a click either way goes to the next / the previous track, LEAD, PAD, BASS, DRUMS, round and round */
    static const uint8_t ALG_TRK[4] = {T_LEAD, T_PAD, T_BASS, T_DRUMS};
    /* ALGORITHM and PRESETS work the same way, one turn of ALG_CLICKS clicks per track: ALGORITHM in tenths (the 10 sounds),
     * PRESETS in tenths (A-J, two clicks each). Where each knob stands, per track; set from what is chosen (knobs_sync) */
    uint32_t snd_pos[NTRK], pat_pos[NTRK];
    uint32_t arp_used = 1, arp_t0 = 0;          /* ARP held and used with PRESETS / ALGORITHM: no arp action when let go; when it went down */
    int32_t arp_acc[3] = {0, 0, 0};             /* the arp knobs: clicks gathered towards the next step */
    int32_t seq_acc[2] = {0, 0};                /* edit mode: the length and page knobs, likewise */
    int32_t ped_acc[3] = {0, 0, 0};             /* the pedals' knobs, likewise (PEDAL_CLICKS) */
    int32_t sw_acc = 0;                         /* KNOB1 as the pedal / arpeggiator switch: clicks gathered */
    uint32_t preset_t = 0;                      /* a preset picked: when (its name shows) */
    uint32_t seq_dn = 0, seq_t0 = 0;            /* SEQ down in the step recorder / live page: a tap or a hold */
    uint32_t oct_t0[2] = {0, 0}, oct_long = 3;  /* OCT- / OCT+ down when; held (bit: no tap when let go) */
    uint32_t earp_t0 = 0, earp_used = 1;        /* the step recorder: ARP down when; used (a hold: the rest) */
    static char pmsg[24];                       /* "GLITCH ON" ... */
    uint32_t cur[NSYN] = {0, 0, 0};             /* each synth's step-recording cursor */
    uint8_t held_note[27] = {0}, held_trk[27] = {0};   /* what each held key plays, on which track (note 0: none) */
    uint8_t slot[27];                           /* a white key: its step on the page 0..15; a black key: 0..10 */
    uint8_t mx_trk[128], mx_at[128], mx_pp[128], mx_vel[128];   /* a MIDI note held: its track (0xFF: not held), its step,
                                                   its pattern, how hard */
    uint32_t rq_cur = 0, rq_ask = 0, rq_t = 0;  /* the RECORD page (REC held): the clear chosen, "sure?" and since when */
    uint32_t mx_held = 0;
    uint8_t rec_at[27], rec_pp[27];             /* live recording: the step each held key's note goes on (0xFF: none), its pattern */
#if BOARD_SMK37
    static const uint8_t WHITE[16] = {0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26};   /* white key -> key */
    uint8_t pad_note[16] = {0}, pad_trk[16] = {0}, ext_note[FM1_NKEY] = {0}, ext_trk[FM1_NKEY] = {0};
    smk_pad_events = 1;                         /* the pads as events (smk_pad_take); the pitch wheel is unused for now */
#endif
    theme_set(0);
#ifndef DINGHY_HOST
    uint32_t boot_t = fm1_ms;
    draw_title(0);                              /* the startup screen, while the work loads */
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
#ifdef HOST_THEME
    set.mu.ub.theme = HOST_THEME;               /* (a test build: start in this theme) */
#endif
    theme_set(set.mu.ub.theme);
    trk_req++;                                  /* (the slots' instruments: the voices dealt for them) */
    proj_scan();                                /* which project slots are full */
    knobs_sync(snd_pos, pat_pos);
    trk_mute = set.mute;
    clock_set_bpm(set.bpm);
    {
        uint32_t w = 0, bl = 0;
        for (k = 0; k < 27u; k++) { slot[k] = (uint8_t)(black(KEY0 + k) ? bl++ : w++); rec_at[k] = 0xFFu; }
        for (k = 0; k < 128u; k++) mx_trk[k] = mx_at[k] = 0xFFu;   /* (no MIDI note held) */
    }
#ifndef DINGHY_HOST
    {
        uint32_t fr = ~0u;
        while (fm1_ms - boot_t < 2500u && !fm1_in.buttons && !fm1_in.notes) {   /* the startup screen stays 2.5 s (a key
                                                   or a button skips it), animated; USB, the watchdog and updates go on */
            uint32_t f = (fm1_ms - boot_t) / 50u;
            if (f != fr) { fr = f; draw_title(fm1_ms - boot_t); }
            platform_poll();
        }
    }
#endif

    lcd_fill(0, 0, 240, 240, C_BG);

#define CHANGED() (dirty = 1, t_change = fm1_ms, edits++)
#define REC_NOW (live_rec && clk_run && !rec_wait)   /* recording: notes go in (not in the count-in) */
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
                if (rec_at[k] != 0xFFu && REC_NOW) {   /* live recording: in it goes, as long as it was held */
                    uint32_t t2 = held_trk[k], a, ll = pat_len(t2, rec_pp[k]), ln = (rec_step(t2, &a) + ll - rec_at[k] % ll) % ll;
                    rec_note(t2, rec_pp[k], rec_at[k], held_note[k], ln ? ln : 1u, 0u);
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
                    if (dbit(pt, drum, dpage * PAGE + slot[k])) ddyn_put(pt, drum, dpage * PAGE + slot[k], DYN_UI[dyn_ui % NDYN_UI]);   /* (at the dynamic set) */
                    dheld = k;                  /* (held: KNOB2 sets its dynamic) */
                    page_hold_t = fm1_ms;       /* (the page stays while you tap) */
                    if (!clk_run && dbit(pt, drum, dpage * PAGE + slot[k])) hit_req[drum]++;
                    CHANGED();
                }
            } else if (trk == T_DRUMS) {        /* drums, playing: the first 4 white keys hit them */
                if (!black(KEY0 + k) && slot[k] < NDR) {   /* (the knobs then set it) */
                    uint32_t ahead = 0;
                    drum = slot[k];
                    if (REC_NOW) {              /* live recording: on the nearest step (one just ahead: heard from there) */
                        uint32_t s2 = rec_step(T_DRUMS, &ahead), lp = play_pat[T_DRUMS] % NPAT;
                        set.dpat[lp][drum][(s2 / PAGE) % (NSTEP / PAGE)] |= (uint16_t)(1u << (s2 % PAGE));
                        ddyn_put(lp, drum, s2, 0u);
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
                if (REC_NOW) {                  /* live recording: it starts on the nearest step (written when let go) */
                    uint32_t a;
                    rec_pp[k] = (uint8_t)(play_pat[trk] % NPAT);
                    rec_at[k] = (uint8_t)rec_step(trk, &a);
                }
                if (syn_edit) { page_hold_t = fm1_ms; step_write(trk, pt, len, (uint32_t)n, &cur[trk], &chord_n, &chord_trk, DYN_UI[dyn_ui % NDYN_UI]); CHANGED(); }
            }
        }
        while (mrq_r != mrq_w) {                  /* notes from a MIDI keyboard (TRS or USB): recorded as the keys are */
            uint32_t n = mrq_note[mrq_r % 64u], on = mrq_vel[mrq_r % 64u] != 0u;
            mrq_r++;
            if (on) {
                if (mx_trk[n] != 0xFFu && mx_held) mx_held--;   /* (on again without an off: the one before ends) */
                mx_trk[n] = (uint8_t)trk; mx_at[n] = 0xFFu; mx_held++;
                if (trk == T_DRUMS) {           /* drums: live recording, on the nearest step (GM drum notes) */
                    uint32_t d = gm_drum(n), ahead;
                    if (d < NDR && REC_NOW) {
                        uint32_t s2 = rec_step(T_DRUMS, &ahead), lp = play_pat[T_DRUMS] % NPAT;
                        set.dpat[lp][d][(s2 / PAGE) % (NSTEP / PAGE)] |= (uint16_t)(1u << (s2 % PAGE));
                        ddyn_put(lp, d, s2, vel_dyn(mrq_vel[(mrq_r - 1u) % 64u]));   /* (how hard: its expression) */
                        CHANGED();
                    }
                } else {
                    mx_vel[n] = (uint8_t)mrq_vel[(mrq_r - 1u) % 64u];
                    if (REC_NOW) {              /* live recording: from the nearest step (written when let go) */
                        uint32_t a;
                        mx_pp[n] = (uint8_t)(play_pat[trk] % NPAT);
                        mx_at[n] = (uint8_t)rec_step(trk, &a);
                    }
                    if (syn_edit) { page_hold_t = fm1_ms; step_write(trk, pt, len, n, &cur[trk], &chord_n, &chord_trk, DYN_UI[dyn_ui % NDYN_UI]); CHANGED(); }
                }
            } else if (mx_trk[n] != 0xFFu) {
                if (mx_at[n] != 0xFFu && REC_NOW) {   /* in it goes, as long as it was held */
                    uint32_t t2 = mx_trk[n], a, ll = pat_len(t2, mx_pp[n]), ln = (rec_step(t2, &a) + ll - mx_at[n] % ll) % ll;
                    rec_note(t2, mx_pp[n], mx_at[n], n, ln ? ln : 1u, vel_dyn(mx_vel[n]));   /* (how hard: its expression) */
                    CHANGED();
                }
                mx_trk[n] = 0xFFu; mx_at[n] = 0xFFu;
                if (mx_held) mx_held--;
            }
        }
        if (chord_n && !keys && !mx_held) {     /* the chord's keys all up: the cursor on past it */
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
        /* OCT- / OCT+: a tap (let go within 0.5 s) the keys' octave (the drum recorder: the page); held 0.5 s: the
         * pattern being edited (the chosen track's) moves down / up an octave */
        for (k = 0; k < 2u; k++) {
            uint32_t bt = k ? BTN_OCT_UP : BTN_OCT_DOWN;
            if (b >> bt & 1u) { oct_t0[k] = fm1_ms; oct_long &= ~(1u << k); }
            if (fm1_in.buttons >> bt & 1u && !(oct_long >> k & 1u) && fm1_ms - oct_t0[k] >= 500u) {
                oct_long |= 1u << k;
                if (trk >= NSYN) { msg = "DRUMS: NO PITCH"; msg_ok = 0; }
                else if (pat_transpose(trk, set.sel[trk] % NPAT, k ? 12 : -12)) { msg = k ? "PATTERN OCT UP" : "PATTERN OCT DOWN"; msg_ok = 1; CHANGED(); }
                else { msg = "OUT OF RANGE"; msg_ok = 0; }
                msg_t = fm1_ms ? fm1_ms : 1u;
            }
            if (rel >> bt & 1u) {
                if (!(oct_long >> k & 1u)) {        /* a tap */
                    if (drum_edit) {
                        if (!k && dpage > 0u) { dpage--; page_hold_t = fm1_ms; }
                        if (k && dpage + 1u < len / PAGE) { dpage++; page_hold_t = fm1_ms; }
                    } else if (!k && oct > -2) { set.oct = (uint8_t)(oct - 1); CHANGED(); }
                    else if (k && oct < 2) { set.oct = (uint8_t)(oct + 1); CHANGED(); }
                }
                oct_long |= 1u << k;
            }
        }
        if (b & (1u << BTN_PLAY)) {             /* all tracks: start (from step 1) / stop */
            if (clk_run) clk_run = 0;
            else { clk_go++; clk_run = 1; }     /* (clk_go first: the interrupt restarts at step 1, then counts on) */
        }
        if (!clk_run && live_rec) rec_stop();   /* stopped: the recording ends */
        if (live_rec && rec_wait == 1u && clk_ack == clk_go && !clk_count) rec_wait = 0;   /* (the count-in over: step 1) */
        if (live_rec && rec_wait == 3u && play_pat[rec_t] % NPAT == rec_p) rec_wait = 0;   /* (the chosen pattern has
                                                   come round: from its step 1) */
        if (live_rec && rec_wait == 2u && trk_pos[rec_t] != rec_wpos) {   /* (playing: from the next bar) */
            if (trk_pos[rec_t] % PAGE == 0u) rec_wait = 0;
            rec_wpos = trk_pos[rec_t];
        }
        if (live_rec && !rec_wait && rec_grow && clk_run && play_pat[rec_t] == rec_p) {   /* the first take: a bar longer at
                                                   its end, while it goes on (4 bars: as long as it gets) */
            uint32_t L = set.plen[rec_t][rec_p];
            if (L >= NSTEP) rec_grow = 0;
            else if (trk_pos[rec_t] == L - 1u) set.plen[rec_t][rec_p] = (uint8_t)(L + PAGE);
        }
        clk_click = 0;                          /* (no click: the count-in is counted on the screen) */
        /* SEQ: the sequencer (the step recorder; REC there: live recording); SEQ again: SUPER SEQ; again: the step recorder */
        if (b & (1u << BTN_SEQ)) {
            if (spage == PG_COPY) { spage = PG_MAIN; if (cp_live) live_mode = 1; else edit = 1; cp_ask = 0; }   /* back */
            else if (edit || live_mode) { seq_dn = 1; seq_t0 = fm1_ms; }   /* (a tap or a hold: below) */
            else { spage = PG_MAIN; edit = 1; }     /* (from SUPER SEQ, or anywhere: the step recorder) */
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
            if (thm_menu) { thm_menu = 0; pedal_mode = ~0u; }   /* the theme menu: gone, the screen back whole */
            if (!home_used) { spage = PG_MAIN; edit = 0; live_mode = 0; live_rec = 0; chord_n = 0; }   /* (always home) */
            home_used = 1;
        }
        if ((e = fm1_enc_take(ENC_SELECT)) != 0 && spage == PG_COPY) {   /* the copy page: the track to copy from
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
            if (fm1_in.buttons >> BTN_HOME & 1u) {           /* HOME held: the theme, in a menu until HOME is let go */
                int32_t v = ((int32_t)set.mu.ub.theme + e % (int32_t)NTHEME + (int32_t)NTHEME) % (int32_t)NTHEME;
                set.mu.ub.theme = (uint8_t)v; theme_set((uint32_t)v);
                home_used = 1; pedal_mode = ~0u; thm_menu = 1; thm_full = 1;   /* (the screen in it once, then the menu) */
            } else if (spage == PG_COPY) {      /* the copy page: the pattern to copy from */
                cp_pat = (uint32_t)(((int32_t)cp_pat + e % (int32_t)NPAT + (int32_t)NPAT) % (int32_t)NPAT); cp_ask = 0;
            } else if (spage == PG_CHAIN) {     /* the chain: the chosen slot's pedal, through the whole library */
                uint32_t sl = chain_sl % NPED_SLOTS;
                ped_set(trk, sl, ped_any_step(trk, sl, set.ped[trk][sl] % NPD, e));
                if (set.fx[trk].on >> sl & 1u && !ped_fits()) { set.fx[trk].on &= (uint8_t)~(1u << sl); CPU_FULL(); }
            } else if (IS_FX_PAGE(spage)) {     /* a pedal page: the type (category), a click each */
                if (ped_cat_step(trk, spage - PG_FX, e)) CPU_FULL();
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
                    else if (!pat_used(trk, to)) set.plen[trk][to] = set.plen[trk][pt];   /* an empty one: as long as the one
                                                   before, so a part played straight on into it fits the same loop */
                    set.sel[trk] = (uint8_t)to;
                }
            }
            CHANGED();
        }
        if ((e = fm1_enc_take(ENC_ALGORITHM)) != 0) {   /* ALGORITHM: a move within the page (it never leaves it) */
            if (fm1_in.buttons >> BTN_HOME & 1u) {     /* HOME held: the MIDI channel */
                int32_t v = (int32_t)set.ch + e;
                set.ch = (uint8_t)(v < 0 ? 0 : v > 16 ? 16 : v);
                home_used = 1; ch_t = fm1_ms;
                CHANGED();
            }
            else if (spage == PG_CHAIN) { int32_t v = (int32_t)chain_sl + e; chain_sl = (uint32_t)(v < 0 ? 0 : v > 3 ? 3 : v); }
            else if (spage == PG_REC) { rq_cur = e > 0 ? 1u : 0u; rq_ask = 0; }   /* (the clear: the pattern, everything) */
            else if (spage == PG_COPY) { cp_pat = (uint32_t)(((int32_t)cp_pat + e % (int32_t)NPAT + (int32_t)NPAT) % (int32_t)NPAT); cp_ask = 0; }
            else if (spage == PG_PROJ) { int32_t v = (int32_t)proj_slot + e; proj_slot = (uint32_t)(v < 0 ? 0 : v >= (int32_t)NPROJ ? (int32_t)NPROJ - 1 : v); confirm = 0; }
            else if (spage == PG_SSEQ) {        /* SUPER SEQ: the box */
                int32_t v = (int32_t)sseq_cur + e, cl2 = (int32_t)set.clen[set.sseq_sel % NCHAIN];
                sseq_cur = (uint32_t)(v < 0 ? 0 : v >= cl2 ? cl2 - 1 : v);
            }
            else if (spage == PG_MIX || spage == PG_ARP) {}   /* (nothing to move) */
            else if (IS_FX_PAGE(spage)) {            /* a pedal page: the pedal within its type, a click each */
                uint32_t sl = spage - PG_FX, p = set.ped[trk][sl] % NPD;
                ped_set(trk, sl, ped_step(trk, sl, p, e));
                if (set.fx[trk].on >> sl & 1u && !ped_fits()) { set.fx[trk].on &= (uint8_t)~(1u << sl); CPU_FULL(); }
                CHANGED();
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
            if (edit) {                         /* the step recorder: KNOB3 the expression (NORMAL ... PLUCKED): the note at the
                                                   cursor (drums: the hit whose key is held), and what you write next */
                if (sy && *step_note(trk, pt, cur[trk])) dyn_ui = DYN_POS[sdyn_get(trk, pt, cur[trk])];   /* (from the note's) */
                if (knob(ENC_KNOB3, &dyn_ui, 0, NDYN_UI - 1u)) {
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
            if (ch) { CHANGED(); usr_ask = 0; }
        } else if (spage == PG_REC) {           /* the RECORD page: no knobs (ALGORITHM chooses the clear) */
            fm1_enc_take(ENC_KNOB1); fm1_enc_take(ENC_KNOB2); fm1_enc_take(ENC_KNOB3); fm1_enc_take(ENC_KNOB4);
        } else if (spage == PG_KEY) {           /* the song page: KNOB1 the lock, KNOB2 the lights, KNOB3 the swing */
            uint8_t lk = set.keyopt & 1u, li = set.keyopt >> 1 & 1u;
            if (knob(ENC_KNOB1, &lk, 0, 1) | knob(ENC_KNOB2, &li, 0, 1)) { set.keyopt = (uint8_t)(lk | li << 1); CHANGED(); }
            if ((e = fm1_enc_take(ENC_KNOB3)) != 0) {   /* swing 50 % (straight) .. 66 %, 1 % a click (set.swing 0..127) */
                int32_t pc = (int32_t)SWING_PCT(set.swing) + e;
                pc = pc < 50 ? 50 : pc > 66 ? 66 : pc;
                set.swing = (uint8_t)(((pc - 50) * 127 + 8) / 16);
                CHANGED();
            }
            fm1_enc_take(ENC_KNOB4);
        } else if (spage == PG_COPY) {          /* the copy page: no knobs */
            fm1_enc_take(ENC_KNOB1); fm1_enc_take(ENC_KNOB2); fm1_enc_take(ENC_KNOB3); fm1_enc_take(ENC_KNOB4);
        } else if (spage == PG_MAIN) {          /* home: KNOB1 the tempo, KNOB2 the chosen track's mix level (the mixer's),
                                                   KNOB3 its brightness (a synth's cutoff, EDIT's MAIN), KNOB4 the pedals'
                                                   fade in / out, 0-4 bars */
            int ch = 0;
            if (knob(ENC_KNOB1, &set.bpm, BPM_MIN, BPM_MAX)) { clock_set_bpm(set.bpm); CHANGED(); }
            if ((e = fm1_enc_take(ENC_KNOB2)) != 0) {   /* (4 a click: the whole way in about 30 clicks) */
                int32_t v = (int32_t)set.vol[trk] + e * 4;
                set.vol[trk] = (uint8_t)(v < 0 ? 0 : v > 127 ? 127 : v); ch = 1;
            }
            if ((e = fm1_enc_take(ENC_KNOB3)) != 0) {
                if (trk < NSYN) {
                    int32_t v = (int32_t)set.snd[trk].cut + e * 4;
                    set.snd[trk].cut = (uint8_t)(v < 0 ? 0 : v > 127 ? 127 : v);
                } else {                        /* drums: the kit's decay, all six together (each kept apart from the
                                                   others as far as the ends allow), tight to long */
                    uint32_t d;
                    for (d = 0; d < NDR; d++) {
                        int32_t v = (int32_t)set.dsnd[d].decay + e * 4;
                        set.dsnd[d].decay = (uint8_t)(v < 0 ? 0 : v > 127 ? 127 : v);
                    }
                }
                ch = 1;
            }
            if (set.mu.ub.pfade > 4u) set.mu.ub.pfade = 0;
            ch |= knob(ENC_KNOB4, &set.mu.ub.pfade, 0, 4);
            if (ch) CHANGED();
        }
        /* REC: a tap = a rest (synth step recorder) / recording on, off (the live page: it starts the music too); held
         * 1 s = clear the pattern (live: the one playing). HOME: held 1 s asks "CLEAR ALL?", REC again clears everything */
        {
            uint32_t home = !edit && !live_mode && spage < PG_FX;
            if (wipe_ask) {                     /* (the RECORD page's CLEAR EVERYTHING, asked and confirmed) */
                    wipe_ask = 0; rec_long = 1;
                    clk_run = 0; live_rec = 0; rec_wait = 0;
                    spage = PG_MAIN; edit = 0; live_mode = 0;   /* (home again, all fresh) */
                    wipe_all();
                    trk_mute = set.mute;
                    clock_set_bpm(set.bpm);
                    knobs_sync(snd_pos, pat_pos);
                    sseq_cur = 0; dpage = 0; drum = DR_KICK;
                    for (k = 0; k < NSYN; k++) cur[k] = 0;
                    msg = "ALL CLEARED"; msg_ok = 1; msg_t = fm1_ms ? fm1_ms : 1u;
                    CHANGED();
            }
            if (b & (1u << BTN_REC)) { rec_t0 = fm1_ms; rec_long = 0; }
            if (!rec_long && fm1_in.buttons >> BTN_REC & 1u && fm1_ms - rec_t0 >= 700u) {
                rec_long = 1;
                if (home || edit || live_mode) {   /* held: the RECORD page (the count-in, the click; clear) */
                    if (live_rec) rec_stop();
                    edit = 0; live_mode = 0; spage = PG_REC; rq_cur = 0; rq_ask = 0;
                }
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
            else if (!rec_long && spage == PG_SOUND) {   /* any page of the sound editor: save the sound to the USER slot (REC twice) */
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
            else if (!rec_long && live_mode && spage < PG_FX) {   /* the LIVE page: a take on, off */
                if (live_rec) rec_stop();
                else rec_start(trk);
                CHANGED();
            }
            else if (!rec_long && !edit && spage == PG_MAIN) {   /* home: straight into a take on the chosen track */
                live_mode = 1; chord_n = 0;
                rec_start(trk);
                CHANGED();
            }
            else if (!rec_long && spage == PG_REC) {   /* the RECORD page: REC asks, REC again clears */
                if (!rq_ask || fm1_ms - rq_t > 3000u) { rq_ask = 1; rq_t = fm1_ms; }
                else if (rq_cur == 0u) {
                    pat_clear(trk, set.sel[trk] % NPAT); rq_ask = 0;
                    msg = "PATTERN CLEARED"; msg_ok = 1; msg_t = fm1_ms ? fm1_ms : 1u;
                    CHANGED();
                } else { rq_ask = 0; wipe_ask = 1; }   /* (everything: as REC twice on home did) */
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
        /* ARP outside edit mode: a tap (not used with PRESETS or ALGORITHM) opens the arpeggiator; GLO: the mixer */
        if (b & (1u << BTN_ARP)) { arp_used = edit != 0u; arp_t0 = fm1_ms; }
        if (rel & (1u << BTN_ARP)) {           /* let go: as a pedal's button, a tap turns it on / off, a hold (0.5 s)
                                                   opens its page */
            if (!arp_used && !edit && live_mode && spage < PG_FX) {   /* the LIVE page: the last take undone */
                if (live_rec) rec_stop();
                if (undo_back()) { msg = "TAKE UNDONE"; msg_ok = 1; CHANGED(); }
                else { msg = "NOTHING TO UNDO"; msg_ok = 0; }
                msg_t = fm1_ms ? fm1_ms : 1u;
            } else if (!arp_used && !edit) {
                if (fm1_ms - arp_t0 >= 500u) spage = PG_ARP;
                else if (trk < NSYN) {
                    set.arp_on ^= (uint8_t)(1u << trk);
                    msg = set.arp_on >> trk & 1u ? "ARP ON" : "ARP OFF"; msg_ok = 1; msg_t = fm1_ms ? fm1_ms : 1u;
                    CHANGED();
                } else { msg = "ARP: SYNTHS ONLY"; msg_ok = 0; msg_t = fm1_ms ? fm1_ms : 1u; }
            }
            arp_used = 1;
        }
        /* GLO: a tap (let go within 0.25 s) opens the mixer, then the SONG page, then home; held: BEAT REPEAT, the slice
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
        ws_hold = (uint8_t)(spage == PG_SOUND);   /* (WRONG STATION: the real sound while it is being edited) */
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
        {                                       /* OCT- / OCT+: lit one octave down / up, blinking two (the drum grid:
                                                   lit while there is a page that way) */
            uint32_t blink = (fm1_ms / 300u) % 2u;
            fm1_led_key(BTN_OCT_DOWN, drum_edit ? dpage > 0u : oct == -1 || (oct <= -2 && blink));
            fm1_led_key(BTN_OCT_UP, drum_edit ? dpage + 1u < len / PAGE : oct == 1 || (oct >= 2 && blink));
        }
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
                             || (live_mode && live_rec && (!rec_wait || (fm1_ms / 150u) % 2u)) || (spage == PG_REC && rq_ask && (fm1_ms / 150u) % 2u));   /* (live: lit while
                                                   recording, blinking in the count-in; "sure?": blinking) */
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
        batt_poll();
        bt_hd = head_drawn; bt_bd = big_drawn; bt_pd = pedal_drawn;   /* (the top bar as drawn, for the battery's warning) */
        if (thm_menu && !thm_full) goto drawn;  /* the theme menu up: the screen under it as it is */
        /* each screen part is redrawn when what it shows changed. The middle shows one of: the sequencer (0), a pedal
         * (1), the arpeggiator (2), the mixer (3) */
        {
            uint32_t scr = IS_FX_PAGE(spage) ? 1u : spage == PG_ARP ? 2u : spage == PG_MIX ? 3u : spage == PG_SSEQ ? 4u : spage == PG_PROJ ? 5u
                         : spage == PG_KEY ? 7u : spage == PG_SOUND ? 8u : spage == PG_CHAIN ? 9u : spage == PG_COPY ? 10u
                         : spage == PG_REC ? 11u : 0u;
            if (scr != pedal_mode) {
                pedal_mode = scr;
                lcd_fill(0, 0, 240, 240, C_BG);  /* (all of it: the screens differ in their parts) */
                view_drawn = big_drawn = info_drawn = head_drawn = pedal_drawn = status_drawn = ~0u;
            }
        }
        if (pedal_mode == 11u) {                /* the RECORD page */
            uint32_t show_msg = msg && fm1_ms - msg_t < 1500u;
            if (rq_ask && fm1_ms - rq_t > 3000u) rq_ask = 0;
            st = mix(mix(mix(mix(mix(mix(trk, rq_cur), rq_ask), set.sel[trk]), show_msg ? msg_t : 0u), wipe_ask), 111u);
            if (st != pedal_drawn) { pedal_drawn = st; draw_rec(trk, rq_cur, rq_ask, show_msg ? msg : 0, msg_ok); }
        } else if (pedal_mode == 9u || pedal_mode == 10u) {   /* the signal chain (CHAIN), the copy page (COPY) */
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
            uint32_t show_msg = msg && fm1_ms - msg_t < 1500u;
            if ((st = mix(mix(mix(mix(trk, trk_mute >> trk & 1u), 8u), trk < NSYN ? set.styp[trk] * 7u + set.styp[(trk + 1u) % NSYN] * 3u + set.styp[(trk + 2u) % NSYN] : 0u), show_msg ? msg_t : 0u)) != head_drawn) {
                static char sn[8] = "SLOT 1";
                for (k = 0; k < NTRK && SEQ_ORDER[k] != trk; k++) {}
                sn[5] = (char)('1' + k % NTRK);   /* (the slots as on the screen, top to bottom: 1-4) */
                head_drawn = st;
                if (show_msg) head_text(msg, msg_ok ? C_WHITE : C_MUTE, msg_ok ? C_PLAYG : C_MUTE);   /* SAVED USER 3 */
                else { head_badge = TYPE_NAME[TTYPE(trk)]; head_track(trk, sn, 0); head_badge = 0; }
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
            st = mix(mix(mix(mix((uint32_t)(int32_t)set.key, set.keyopt), pcs), 7u), set.swing);
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
            static uint32_t home_tall;          /* home filled the strip under the view: clear it for the others */
            if (!drum_edit && !syn_edit && !live_mode) home_tall = 1;
            else if (home_tall) {               /* (above and below the view) */
                home_tall = 0;
                cv_begin(240, Y_HOME + H_HOME - Y_VIEW - H_VIEW, C_BG); cv_blit(0, Y_VIEW + H_VIEW);
                cv_begin(240, Y_VIEW - Y_HOME, C_BG); cv_blit(0, Y_HOME);
            }
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
            st = mix(mix(mix(mix(mix(st, show_msg), show_msg ? msg_t : 0u), wipe_ask), set.oct), rec_beats());   /* (the OCT badge; the count-in) */
            if (st != big_drawn) {
                big_drawn = st;
                uint32_t cb = rec_beats();
                if (cb) {                       /* the count-in: 4 . 3 . 2 . 1 */
                    uint32_t j;
                    head_begin(RGB(214, 48, 38));
                    for (j = 0; j < 4u; j++) {
                        char nb2[2] = {(char)('4' - j), 0};
                        uint32_t now = 4u - j == cb, past = 4u - j > cb;
                        cv_text_bg(36 + (int32_t)j * 52, 3, &FONT_L, nb2, now ? RGB(214, 48, 38) : past ? C_DIMC : C_SUB, C_PANEL);
                    }
                    cv_blit(0, Y_BIG);
                }
                else if (wipe_ask) head_text("CLEAR ALL?", RGB(255, 200, 60), RGB(255, 200, 60));
                else if (show_msg)              /* SAVED 3, LOAD FAILED, ALL CLEARED ... */
                    head_text(msg, msg_ok ? C_WHITE : C_MUTE, msg_ok ? C_PLAYG : C_MUTE);   /* (red: it did not happen) */
                else draw_big(trk, drum_edit, drum, flash);
            }
        }
        }                                       /* (end of the sequencer screen's parts) */
        {                                       /* the battery: LOW BATTERY blinking in the top bar (1 s on, 1 s off), over
                                                   what it shows; a message or CLEAR ALL? goes first; not on USB power */
            uint32_t busy = (msg && fm1_ms - msg_t < 1500u) || wipe_ask;
            uint32_t bw = batt_low && !usb_powered() && !busy ? 1u + ((fm1_ms / 1000u) & 1u) : 0u;
            if (bw != batt_shown) {
                if (batt_shown == 2u) head_drawn = big_drawn = pedal_drawn = ~0u;   /* (the top bar back as it was, next time round) */
                else if (bw == 2u) head_batt();
                batt_shown = bw;
            } else if (bw == 2u && (head_drawn != bt_hd || big_drawn != bt_bd || pedal_drawn != bt_pd)) head_batt();   /* (drawn over again) */
        }
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
                st = mix(mix(st, set.fx[trk].on), set.ped[trk][0] | set.ped[trk][1] << 8 | set.ped[trk][2] << 16 | (uint32_t)set.ped[trk][3] << 24);
                if (leg == 1u) st = mix(mix(mix(st, cur[trk]), ui_len), ui_dyn);   /* (synth step recorder: the cursor, its length) */
                if (st != status_drawn && spage != PG_SSEQ) { status_drawn = st; draw_status(oct, trk, show_ch, spage, leg == 1u ? cur[trk] : drum, pj, cpu_drawn); }
            }
        }
        if (!pedal_mode) {
            uint32_t mode = syn_edit ? 1u : drum_edit ? 2u : live_mode ? 3u : 0u, c = trk < NSYN ? cur[trk] : 0u;
            st = mix(mix(mix(mix(mix(mix(mix(mode, trk), set.bpm), run), (uint32_t)(oct + 2)), len), set.swing), trk_mute >> trk & 1u);
            if (mode == 0u) {
                uint32_t d, kd = 0;
                for (d = 0; d < NDR; d++) kd += set.dsnd[d].decay;
                st = mix(mix(mix(st, set.vol[trk]), trk < NSYN ? set.snd[trk].cut : kd), set.mu.ub.pfade);
            }
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
                   : DYN_UI[dyn_ui % NDYN_UI];
            st = mix(st, ui_dyn);
            ui_len = trk < NSYN && *step_note(trk, pt, c) ? nlen_idx(*step_len(trk, pt, c)) : len_ui;   /* (the LEN cell) */
            st = mix(mix(st, ui_len), set.sel[trk]);
            if (st != info_drawn) { info_drawn = st; draw_info(trk, mode, c, vpage, oct); }
        }
    drawn:
        if (thm_menu && thm_full) { thm_full = 0; draw_theme_menu(set.mu.ub.theme); }
        idle_ok = 0;

        if (dirty && quiet() && fm1_ms - t_change > 2000u) {   /* 2 s after the change, when silent (an erase stalls the sound) */
            set.mute = (uint8_t)trk_mute;
            ws_home_all();
            song_save();
            dirty = 0;
        }
    }
}
