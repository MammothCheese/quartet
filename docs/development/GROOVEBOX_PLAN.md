# Groovebox plan: what the FM-1 can do, what the spec asks, how to get there

> The build diary of QUARTET, kept as it was written. Until 2026-11-10 the firmware was called **ROCKBAND**, so the
> older entries use that name; mock-ups, commit ids and file names in it refer to the old project.

Companion to [SPEC.md](SPEC.md). Written 2026-11-09 from the Dinghy source and docs (API.md, RULES.md), before
any groovebox code. Numbers marked **(est.)** are estimates; **(hw)** means only the real FM-1 can answer it.

## 1. What we have to work with

| | The FM-1 / Dinghy platform |
|---|---|
| CPU | JieLi AC791N, pi32v2, **no FPU** (integers only), no 64-bit division. Second core exists, unused by Dinghy |
| Audio | 44.1 kHz stereo, `app_audio()` called every 256 frames = **5.8 ms budget**, aim to use under half (2.9 ms). Output clipped at ±32767 |
| CPU speed | Not documented as a number. Yardstick from the SLOOP family: ~1000-1500 host instructions per sample fit. **(hw)**: we must measure with `fm1_ticks()` |
| RAM | ~96 KB for variables + **~336 KB pool** for big buffers (delay lines, the screen canvas) |
| Flash for code | ~568 KB app slot. Dinghy uses a small fraction |
| Flash for data | settings 4 KB, 8 blobs of 4 KB, **8 slots of 16,372 B** (CRC-checked, a bad save reads as "nothing saved") |
| Saving | Erasing stops all sound ~50 ms per 4 KB sector: a 16 KB slot = **~200 ms silence** |
| Screen | 240 x 240 colour, draw only what changed; FONT_S (16 px), FONT_L (32 px, capitals only) |
| Keys | 27 keys F3..G5 (16 white, 11 black), no velocity on the FM-1 |
| Buttons | 14: OCT-, OCT+, FX, SEL, ENV, LFO, EDIT, GLO, HOME, SAVE, ARP, SEQ, PLAY, REC |
| Encoders | 7: SELECT, ALGORITHM, KNOB1-4, PRESETS (+ MASTER pot = volume, handled by the platform) |
| LEDs | One per key and per button, on / dim / off |
| MIDI | USB in/out, TRS in. Clock in already parsed |
| Testing | Desktop emulator (WAV + screenshots, scripted key presses), CI build on GitHub, rescue mode (hold OCT-) |

Already working in our firmware: 4-voice sine on the keys, MIDI in/out, a 4-drum 16-step drum machine with a
sample-accurate clock in the audio interrupt, tempo, edit mode on the white keys, settings saved to flash.

## 2. The spec against the hardware

| Spec item | Verdict | Notes |
|---|---|---|
| 4 tracks, 1+1+4 synth voices, 2 osc, filter, ADSR, LFO | **Probably OK (hw)** | ~6 voices x 2 oscillators + a resonant filter each. Biggest unknown is CPU. Fallbacks: pad voices with 1 oscillator, filter coefficients per 16 samples |
| Drum engine, 6 sounds | OK | Synthesised drums are cheap (we already have 4) |
| 16-64 steps, 8 patterns per track, 40-240 BPM | OK | Fits in RAM and in one slot (see section 5) |
| One clock, no drift | OK | Already done the right way: the clock counts samples inside the audio interrupt |
| Pattern switch quantised + queued | OK | |
| Live record + quantise, swing, accents, copy/paste | OK | Small additions once the sequencer exists |
| **All 6 effects independent per track** | **Not realistic** | 4 reverbs + 4 delays + 4 chorus + 4 pitch shifters would blow both CPU and RAM. Proposal: **drive and tremolo per track** (cheap), **reverb, delay, chorus, pitch shift shared** with a per-track send amount. The spec allows this ("use shared processing or send effects where necessary") |
| Reverb | **Reduced quality** | A small integer reverb, probably run at half rate (22 kHz) to save CPU. Will sound "lo-fi plate", not studio hall |
| Delay, tempo-synced down to 40 BPM | OK with a limit | A dotted quarter at 40 BPM is 2.25 s. At 22 kHz, 16-bit mono that is ~100 KB of the pool: fine, but delay time capped at ~2.5 s |
| Pitch shift | **Lo-fi, last** | Feasible as a simple two-tap granular shifter (cheap) but it warbles, especially on chords. Spec already puts it last |
| Save without dropouts | **Not realistic while playing** | Writing a project = ~200 ms of silence. Proposal: save/load **only when stopped** (or accept one short gap) |
| 8 projects, safe against corruption | OK | 8 slots of 16 KB, CRC-checked. Load into a spare buffer first so a bad slot never touches the current project |
| Effects adjusted live without clicks | OK with care | Smooth every parameter change over a few ms |
| Everything on the panel, few menus | OK, but tight | 14 buttons for 4 tracks x (sound + 6 FX + patterns + steps + projects) means **shift-style combos** (hold a button + press a key). Needs your decisions (section 7) |

## 3. Reuse

The spec says "reuse existing working firmware components". Options:

- **Stay on Dinghy (recommended).** We know it, it is small, it builds on GitHub, and it has the update path,
  rescue mode, and desktop emulator. We add modules to it.
- **Borrow pieces later.** The Felucca family has an MIT-licensed engine collection (`Felucca-Engines`: DRUM,
  NOISE, PHYS, FM6...). In Phase 4 and 5 I would read those and port what fits, instead of writing every drum
  and effect from scratch. Not read yet.
- **Fork a big existing groovebox (SLOOP / ORBIT / Felucca).** They already have sequencers, effects, and
  projects, but they are large codebases with their own UI ideas. Rewiring them to this spec could be harder than
  building it. Not evaluated in detail.

## 4. Architecture (the simplest one that reaches the spec)

```
 main loop (no timing duties)                    audio interrupt, every 256 samples
 ─────────────────────────────                   ──────────────────────────────────
 buttons, keys, knobs ──► UI / modes             CLOCK   counts samples ─► 16th steps (+ swing)
 screen (only what changed)                        │
 edits pattern data in RAM ───────────────────► SEQUENCER  4 tracks read their patterns,
 queues: live notes, pattern switches ────────►            apply queued switches at the boundary,
 project save / load (only when stopped)                   send note on/off to the voices
                                                   │
                                                 VOICES  lead (1), bass (1), pad (4): shared synth
                                                         engine; drums (6)
                                                   │
                                                 FX      per track: drive, tremolo
                                                         shared sends: chorus, delay, reverb, pitch
                                                   │
                                                 out ──► platform (MASTER volume, DAC, USB audio)
```

- **One clock, in the interrupt**, as now. Everything that must be in time (steps, note lengths, swing,
  tempo-synced delay and tremolo) is derived from that one sample counter: drift is impossible by design.
- **Sequencer logic is its own module**, separate from the sound code, but called from the interrupt so steps
  land on the exact sample. It only reads pattern data and triggers voices: a few hundred instructions per step,
  not per sample.
- **Main loop to interrupt**: plain variables, one writer each (the platform's rule). Pattern bytes are edited in
  place (a byte write is atomic); "queued pattern" is written by the main loop, "playing pattern" by the
  interrupt.
- **No dynamic memory at all.** Everything is a fixed-size static array, sized at build time.
- **Code layout**: `dinghy.c` stays the entry point and includes new files, one per module
  (`gb_clock.h`, `gb_seq.h`, `gb_synth.h`, `gb_drums.h`, `gb_fx.h`, `gb_ui.h`, `gb_project.h`). The build still
  compiles one file, so nothing in the build setup changes.
- **CPU meter from day one**: time `app_audio()` with `fm1_ticks()` and show the load on screen (a hidden page),
  so every phase is checked on the device before the next.

## 5. Data sizes (est.)

| Part | Layout | Size |
|---|---|---|
| Lead / bass step | note, velocity, length, flags (tie, slide) | 4 B |
| Pad step | 4 notes, velocity, length, flags | 8 B |
| Drum step | 6 drums x level (0 off, 1-3) | 6 B |
| Pattern | length + 64 steps | lead/bass 257 B, pad 513 B, drums 385 B |
| 8 patterns x 4 tracks | | **~11.3 KB** |
| Sounds, effects, tempo, mutes, selected patterns | | < 1 KB |
| **Project** | | **~12 KB of 16,372 B per slot: fits, 8 projects** |

RAM: the project (~12 KB) + a load buffer (16 KB) + delay (~100 KB) + reverb (~30 KB) + chorus/pitch (~8 KB) +
canvas (~30 KB) ≈ 200 KB of the ~336 KB pool.

## 6. Phases, adapted to what already exists

The spec's order, with our drum machine kept working throughout:

1. **Sequencer core**: move the clock into its own module with a fractional sample counter (exact tempo
   40-240 BPM). Add **one synth track** (bass, 1 voice, simple saw + filter) with a 16-step pattern and step
   entry. Drums keep running on the same clock. Add the CPU meter.
2. **Four tracks**: lead, bass, pad, drums on the clock; track select; mute.
3. **Patterns**: A-H per track, queued switching at the pattern end, copy/paste/clear, 16-64 steps (pages of 16
   on the white keys).
4. **Sound engine**: the shared synth (2 osc, 4 waves, SVF filter, ADSR, LFO), presets, pad polyphony, 6 drums
   (clap, percussion), maybe ported from Felucca-Engines.
5. **Effects**: drive + tremolo, then delay + chorus, then reverb, then pitch shift if the CPU meter allows.
6. **Projects + polish**: 8 project slots, confirmations, overwrite protection, the final button map.

**Status (2026-11-09):** Phase 1 written: `gb_clock.h` (one clock, Q8 fractional tick, 4 ticks a step, 40-240 BPM),
`gb_drums.h` (the drums moved out of dinghy.c), `gb_bass.h` (saw + 2x-oversampled SVF, filter envelope), step
recording (key = note, REC = rest / hold to clear, ARP = tie, SELECT = cursor, ALGORITHM = track), CPU meter on
the top line (device only). Compiles for FM-1 and SMK-37; test script `phase1_test.txt`. CI host tests pass.
**Tested on the FM-1: everything works as described, CPU peak 17%** (drums + 1 bass voice with the int64,
2x-oversampled filter + the 4 sines). Budget target is 50%: ~33% left for lead (1 voice), pad (4 voices) and
effects, so Phase 4 must keep voices lean (32-bit filter math, pad filter at 1x).

**Status:** Phase 2 written: `gb_synth.h` replaces `gb_bass.h` (one voice design for LEAD 1 voice, BASS 1,
PAD 4; the filter math is now 32-bit only, modelled for overflow and stability), live notes go main -> interrupt
through a small queue, ALGORITHM = track select, keys play the chosen track live, HOME = mute, pad chords in step
recording, a home view with all 4 tracks. Sines on the keys removed (the keys play the tracks now). Compiles for
FM-1 and SMK-37; test script `phase2_test.txt`. **Tested on the FM-1: mute, pad chords and live lead work, no
crackles, CPU peak 43-44%** with all 6 voices + drums. That is close to the 50% target before any effects, so
Phase 4 starts with a CPU pass: no per-sample clamps (bounded by the model), pad voices cheaper (1x filter or
half rate), and possibly the second core (Felucca's CPU2 code; not in Dinghy yet).

**Status:** Phase 3 written. Decided with the user: PRESETS picks a track's pattern A-H (queued while playing,
switched when the playing pattern comes round to step 1), in edit mode PRESETS sets the length 16/32/48/64, ARP
held + PRESETS copies, HOME held + PRESETS = MIDI channel, HOME released alone = mute. FX SEL ENV LFO EDIT GLO are
the top row (kept free for the effects). Each track has its own position (`trk_pos`) and playing pattern
(`play_pat`) in `gb_clock.h`; drums stored as 16-bit pages (no 64-bit shifts: no libgcc). The data (~9.5 KB) is
saved in blobs 0-2. Drum edit: OCT-/OCT+ turn the page; synth edit: the page follows the cursor. Compiles for
FM-1 and SMK-37; test script `phase3_test.txt`. **Tested on the FM-1: works, CPU peak 38-42%.**

**Status:** Phase 4 step 1 (CPU pass) written: the synth filter's safety clamps run once per 16 samples instead of
every sample, the pad filter runs once per sample (cutoff capped at index 112, ~4.3 kHz, where the 1x filter is
still stable; modelled), the envelope skips work at full level.
Phase 4 step 2 (engine) written: `gb_synth.h` now has 2 oscillators (sine/triangle/saw/square, osc 2 -24..+24
semitones + ~7 cents), mix, one ADSR for level and filter (per-16-sample exponential segments + a per-sample ramp;
modelled: reaches full, holds sustain, ends at exact silence, never negative), a per-track LFO (pitch/filter/amp,
tri/square/saw/random), 6 presets per track. `gb_drums.h` adds CLAP (3 bursts + tail) and PERC, and pitch / decay /
level per drum. Sound pages wired to SELECT + KNOB1-4. Compiles for FM-1 and SMK-37; test script
`phase4_test.txt`. **Tested on the FM-1 (commit a269f6e): CPU peak 39%, no crackles, sound pages work, clap and
perc good; the user finds the presets "a bit abstract"** (to refine). The CPU pass paid for the second oscillator.
Presets reworked ("hard to tell apart"): each now differs from its neighbours in two obvious ways (bright/dark,
short/long, clean/squelchy, still/moving) with names that say so (LEAD BRIGHT PLUCK HOLLOW SOFT SCREAM BELL, BASS
ACID SUB FAT GRIT THUMP WOBBLE, PAD WARM STRINGS GLASS DARK SWEEP PULSE). Picking one shows its name big for
1.5 s and, when stopped, plays a test note (pad: a chord) for 0.6 s.
Decided with the user for the engine: outside edit mode SELECT flips 5 sound pages of the chosen track, KNOB1-4
change the page's 4 settings, the bottom line shows them: MAIN (tempo, cutoff, resonance, filter env), OSC (wave 1,
wave 2, osc-2 tune, mix), ENV (attack, decay, sustain, release), LFO (rate, depth, target, wave), PRESET (KNOB1
picks one; it replaces the track's settings). Drums MAIN: tempo + pitch, decay, level of the chosen drum.

**Status:** Phase 5 written (not pushed, at the user's request). `gb_fx.h`: each track renders into its own mono
buffer (`tb[track]`), then `fx_process()` runs DIST (drive, tone, level, soft/hard) and TREM (rate, depth,
sine/square, free or synced to the clock, re-phased from the clock every block so it cannot drift) per track,
and sums the sends into DELAY (65536-sample line in the pool, synced 1/16..1/2, darkening feedback up to ~0.9),
CHORUS (stereo, two swinging taps), REVERB (4 combs + 3 allpasses at 22 kHz, Freeverb lengths halved) and PITCH
(two-tap cross-faded shifter). An effect no track uses costs nothing. Top buttons in the spec's order: FX reverb,
SEL delay, ENV dist, LFO chorus, EDIT trem, GLO pitch; tap = page, hold 0.5 s = on/off, lit = on. Modelled:
reverb stable at max decay (silent within 5 s), delay dies at max feedback, pitch within ~1-2 %, the largest
product 2.0e9 of 2.1e9 (the distortion tone filter). Pool use ~164 KB of ~336 KB. Compiles for FM-1 and SMK-37;
test script `phase5_test.txt`. **Tested on the FM-1 (commit fa2aa21): works, tap/hold feels good, the button
order matches the panel, effects sound decent; with many effects on, CPU peak ~70%** (over the 50% target: needs
an effects CPU pass or the second core before Phase 6 adds load).
Effects CPU pass written: `fx_process()` no longer loops per sample asking which effects are on; each insert runs
over the track's block in place, each send bus is filled only from the tracks that use it, the dry mix is one
loop, and each shared effect is its own loop (the reverb: one loop per comb / allpass at half rate). Same
arithmetic as before (the models still hold). Not yet measured.

**Status:** Phase 6 written (not pushed). Decided with the user: the firmware is **ROCKBAND** (APP_NAME, the update
screen, installer identity **FM-1_960**, its own flash tag STORE_MAGIC "RCKB"); projects use the SAVE page with a
confirm step. 8 project slots (the 16 KB slots; the project ~10 KB): KNOB1 slot, KNOB2 save/load, SAVE then SAVE
again within 3 s; only while stopped; a load is read into a spare copy and checked whole (CRC, then every value)
before it replaces the current work; the current work still autosaves to the blobs. The full controls are in
`docs/BUTTONS.md`. Compiles for FM-1 and SMK-37; test script `phase6_test.txt`. Changing STORE_MAGIC means the
current work starts from the defaults once more (and for good: the tag is now our own).

**v1 (2026-11-09, commit 00e325c):** tested on the FM-1 and called "working v1" by the user. Known issue: a track
with DIST on gets too loud (the clipped signal sits near full scale while clean tracks are ~1/3 of it).

**After v1 (user request): the effects as guitar pedals.** Three knobs each, named as on a pedal: REVERB level /
tone / time, DELAY repeat rate / echo / intensity, DISTORTION level / tone / dist, CHORUS rate / depth / level
(read as such: the user wrote "rate, depth, depth"), TREMOLO rate / wave / depth (tempo sync dropped: the three
knobs are the user's; a spec item, noted). PITCH removed (GLO is free). Distortion volume fixed: the output falls
with the drive (0.17 + 0.50 / drive, after the curve's x1.5); modelled on a saw lead: 1.0-1.2 x the clean RMS at
any drive (was ~3.3 x). A pedal screen replaces the sequence while a pedal page is open: the track's name big, the
pedal (its colour, ON lamp, three drawn knobs with a ring of dots, names, values); mock-up `docs/pedal_mockup.png`
drawn from the same geometry. The saved layout changed: v1 projects and the current work read as empty / defaults.

**Next requests (user):** ALGORITHM by angle: the knob is an encoder (clicks, no absolute position), so a turn is
taken as `ALG_CLICKS` = 20 clicks, four quarters LEAD, PAD, BASS, DRUMS, wrapping (to adjust if the user's knob has
another count). GLO = PHASER (per track: 4 allpasses swept by a triangle LFO, feedback up to ~0.78, volume trimmed
with feedback; modelled: products < 1.8e9, dies away within 1 s at full feedback). HOME and SEQ on a pedal or the
project page go back to the sequencer. **20 patterns per track (A-T):** made to fit by storing LEAD / BASS steps
as 1 note (2 bytes) and PAD steps as 4 (5 bytes), via `step_note()` / `step_len()`; the project is now ~12.7 KB
(16 KB slots, 8 projects kept; the build's size checks pass). Saved data from before reads as empty / defaults.

**SELECT revamp (user):** SELECT now picks each instrument's sound by quarter turns (positional, like ALGORITHM;
the sound changes only on entering a new quarter): LEAD piano / synth / xylophone / guitar, PAD warm synth /
strings / organ / choir, BASS sub / acid / plucked / fuzz, DRUMS kits 808 / CR-78 / LM-1 / LO-FI (kits = per-drum
pitch, decay, level; LO-FI also holds every 3rd sample and drops to 6-bit steps). The detailed pages (MAIN, OSC,
ENV, LFO) moved to ARP held + SELECT (the user's choice: keep them, hidden); the PRESET page is gone. PRESETS:
two clicks per pattern, A-J around one turn (NPAT 10). All three knobs share `ALG_CLICKS` (20, assumed).

**ARP, mixer, layout (user):** ARP tap (outside edit) opens the ARPEGGIATOR of the chosen synth (KNOB1 on/off,
KNOB2 rate 1/4..1/32; held keys play low to high; ticks come from the master clock while playing, from a free
counter at the same tempo while stopped). Tremolo removed, PHASER moved to EDIT. GLO opens the MIXER: four strips,
KNOB1-4 the volumes of LEAD, PAD, BASS, DRUMS (100 = as is, after the pedals, before the sends). The ROCKBAND title
and the "groovebox v0.0 CPU" line are gone from every screen: the big line is at the top, the sequencer view is
now 128 px tall (rows of 32 px), and the CPU meter is on the mixer's bottom line. Rows, mixer and ALGORITHM all use
the order LEAD, PAD, BASS, DRUMS (`SEQ_ORDER`). Mock-up: `docs/screens_mockup.png`.

**Polyphonic lead, arp v2 (user):** LEAD has 4 voices (NVOX 9) and chord steps like the PAD (lead and pad share
`chord_t` storage, bass keeps 1 note); the lead's filter now runs at 1x like the pad's (cutoff capped ~4.3 kHz) to
pay for the extra voices. ARP: tap = the screen, hold 0.5 s (acted on when let go, unless used with PRESETS /
SELECT) = on / off. Knobs: RATE (1/4..1/32), OCTAVES (1-4), DIRECTION (up / down / random), each taking 3 clicks
a step (`knob_slow`, `ARP_CLICKS`).

**Mixer mutes, HOME, title, sounds (user):** GLO toggles the mixer; on it (only) SAVE / ARP / SEQ / PLAY mute LEAD /
PAD / BASS / DRUMS, lit = playing (their usual jobs are blocked there). HOME no longer mutes: it always goes home
and toggles home <-> a title screen (original pixel-art pedalboard in four greens, drawn in two 240x120 halves);
the FM-1 starts on the title. Engine: a NOISE wave and an FM mode (osc 1 = a sine phase-modulated by osc 2, depth
= MIX + envelope x FILTER ENV, kept per voice). Sounds: GUITAR -> BRASS, SUB -> FM bass, CHOIR = saw + noise
through a resonant ~800 Hz "aah" with vibrato.

**SEQ screen knobs, swing (user):** in edit mode PRESETS picks the pattern (as outside), KNOB1 the length, KNOB2 the
step (synths), KNOB3 the page, KNOB4 swing (new, saved; every 2nd 16th late, up to 66 %, pairs of steps keep their
length). The info line under the title shows LEN / STEP / PAGE / SWING; the bottom line a knob legend. The tempo is
off the HOME bottom line. (Saved data from before resets once: a new field.)

**v1.0** (tag v1.0, commit 58cfde2): the first release.

**Visual overhaul (user):** no feature or button changes. Dark rounded panels; a colour per track (LEAD gold, PAD
violet, BASS cyan, DRUMS green; red when muted) running through the top bar (name, pattern tiles, MUTE badge), the
info line (play sign, tempo, beat lights, octave; SEQ: one cell per knob), the steps and the bottom bar. Drum grid
with short names, piano roll with a keyboard and C lines, pedals and the arpeggiator as stompboxes (screws, lamp, name
plate, value screens, footswitch), mixer strips with level meters and mute keys, a colour title screen. gfx.c gained
cv_text_bg (text on a coloured panel) and cv_rrect. Pedals and the mixer are 164 px tall, drawn in two halves.
Mock-ups: docs/overhaul_mockup.png.

**SUPER SEQ (user):** replaces the title screen (HOME from home; the FM-1 now starts on the sequencer). 10 songs
(PRESETS), each 4 tracks x 8 boxes (0 = blank, 1..10 = A..J; new: box 1 = A). KNOB1 on/off, KNOB2 box, KNOB3 its
pattern; ARP + PRESETS copies a song. gb_clock.h: at each track's step 1 the next box starts (chain_at); a blank
goes on with the box before; blanks at the start rest (chain_rest silences the track, as long as the first
pattern). Off: set.sel as before. The new fields sit at the end of set: v1.0 saves and projects (SET_V10 bytes) still
load, with SUPER SEQ as new. Mock-up: docs/superseq_mockup.png.

**X rest, distortion, save page (user):** SUPER SEQ box value 11 = X, a rest; blanks after an X rest too; a rest lasts
as long as the pattern before it. Distortion LEVEL starts at 32 (new tracks / defaults). The project page became a
save page: 4 slots (NPROJ 4; slots 1-4 of before still load), KNOB1 the slot, REC save, PLAY load, each confirmed by
the same button again within 3 s (it blinks; other buttons cancel); playing stops first, the write waits for quiet
(up to 1.5 s); SAVE toggles the page and home. Mock-up: docs/save_mockup.png.

**Live recording (user):** SEQ from home = the step recorder; SEQ there = live recording (live_mode; SEQ blinks); SEQ
again = step recorder; HOME = home. On the live page REC toggles recording (and starts the clock); PLAY stops it.
Keys play as outside edit mode; while recording, each note goes into play_pat of the chosen track on the nearest step
(gb_clock.h clk_sub: past half the step = the next step), written on key release with its rounded length (rec_note:
added to a chord's free place; BASS replaces). Drums are written on the hit; one rounded forward is not played live
(the step plays it). Screen: the playing page of play_pat, info REC/PG/STEP/BPM, LIVE/REC badge.

**Live fixes, SUPER SEQ length (user):** live rounding measures the position finely (fxc_tick / fxc_pos at the audio
block) and takes REC_LAG (~20 ms) off it, then goes to the closest step, earlier or later. The live page edits the
pattern playing with the step recorder's KNOB1-3 and view (cells LEN STEP PG REC). Hold REC 1 s clears the pattern on
both recorders (drums: the whole pattern, no longer only the chosen drum); a tap toggles live recording. Home under
SUPER SEQ: the pattern tile blacked out, "SUPER SEQ ON" badge. SUPER SEQ page: SELECT right on / left off; KNOB1 the
song length 1-8 (set.clen, appended; saves without it load with 8).

**Arp on patterns, page follow, KNOB4 (user):** with the arpeggiator on, synth_step hands the step's notes to it
(seq_arp, as if held; let go when the note ends, stops or the arp goes off) instead of playing them; arp_tick now runs
after the step so it starts on the step's tick. The SEQ screens (step and live) follow the playing page when the
pattern is longer than 16 steps (drums: dpage follows, so the white keys too), paused 3 s after a cursor / page move or
a written step (page_hold_t; vpage = the page shown). SUPER SEQ on / off moved from SELECT to KNOB4.

**Six sounds (user):** NPRESET 6 / NKIT 6, SELECT in sixths. LEAD + ELEC GUITAR (saw + square pluck), HAMMOND (square
+ sine at +19, fast pitch LFO); PAD + AC GUITAR (saw + tri octave, no sustain), ELEC PIANO (FM 2:1, tremolo); BASS +
UPRIGHT (tri + sine, dark), SLAP (saw + square octave, filter snap). Kits ROCK (kick gets a beater click: amp2 noise
burst) and JAZZ (brushed snare: low-passed noise, no tone; OPEN HAT = ride: two square tones ~3.2 / 4.6 kHz over
less hiss). Saves keep working (indices only grew).

**Ten sounds (user):** NPRESET / NKIT 10, SELECT 2 clicks each by position (snd_pos = sound * 2). LEAD + THEREMIN,
CHIPTUNE, SQUARE LEAD, MUSIC BOX; PAD + GLASS PAD, DRONE, SWEEP, HANDPAN; BASS + SUB, REESE, WOBBLE, SYNTH BASS; kits
909 (kick click), WOODEN (snare = tone only), HIP-HOP and INDUSTRIAL (crushers per kit: KIT_CRUSH_N / _M, LO-FI's
generalised). UPRIGHT louder (a little saw, filter more open); JAZZ louder (levels, brushes x1.3, ride ping 8000). The
drum bottom line leaves the kit name out when kit + drum would not fit.

**Sound fixes (user):** UPRIGHT louder again (saw-based now, sustain 85, the filter more open). TUBA replaces SUB
(dark saw + square, 20 ms swell, the filter opening). JAZZ OPEN HAT is a soft brush swirl (low-passed noise, long;
a closed hat no longer cuts it) instead of the ride. 606 replaces WOODEN (no special engine part: params only).

**Live timing, pedal steps (user):** notes landed too early: REC_LAG 20 ms -> 10 ms, and a press counts for the next
step from 40 % of the way there (was 50 %). Pedal knobs (the 0..127 ones) move in 20 steps of 2 clicks
(knob_pedal: 0, 6, 12 ... 127) and show 0..20; the stored values and the sound are unchanged.

**Clicks, startup screen (user):** pedal knobs one click a step (PEDAL_CLICKS 1). ALGORITHM no longer works by quarters:
2 clicks either way = the next / previous track (alg_acc). A startup screen (the old title's pixel pedalboard in the
track colours, ROCKBAND, 4-TRACK GROOVEBOX) shows while the work loads, 1.5 s or until a key / button (FM-1 only, not
in the host build, so the host tests keep their timing).

**Mini Pops, clear all (user):** MINI POPS replaces JAZZ (kit 5; snare mostly tone, HAT a low-passed maraca shake;
the jazz brushes and brush swirl are gone). HOME: hold REC 1 s = "CLEAR ALL?" (head and bottom bar, REC blinks); REC
again within 3 s = wipe_all(): set_defaults, every pattern cleared, MIDI channel and octave kept, save slots untouched,
the clock stopped. On the SEQ pages hold REC still clears the pattern.

**QUARTET, the new order (user):** the firmware is now called QUARTET (APP_NAME, the update loader's screen, the
startup screen, the pedals' case). The identity stays FM-1_960 and STORE_MAGIC stays "RCKB", so installs and saves go
on. The sounds and kits are in the user's order (the first of each is the starting sound: PIANO, STRINGS, UPRIGHT,
ROCK). set gained sound_order at its end: a save without it (shorter) is from before, and sound_reorder() moves its
sound numbers and kit to their new places (songs, sounds and settings themselves are unchanged).

After each phase: desktop emulator test (WAV + screenshots + a script per feature), a GitHub build, and a
check on the device (CPU meter, crackles).

## 7. Decisions

Decided 2026-11-09:

1. **Base**: stay on Dinghy; borrow MIT-licensed pieces (Felucca-Engines) where they help.
2. **Effects**: distortion + tremolo per track; reverb, delay, chorus, pitch shift shared, with a per-track send.
3. **Synth note entry**: step recording (play a note / chord, it is written, the cursor moves on; a rest button,
   a tie button). Drums keep "white keys = steps".
4. **Saving**: only while stopped.

Still open:

5. **The top six buttons** (2026-11-09): FX, SEL, ENV, LFO, EDIT, GLO. They become the effect buttons in Phase 5.

## 8. Only the real FM-1 can test

CPU load and crackles (every phase), the feel of buttons and long presses, LED readability, the ~200 ms save gap,
clicks at the DAC, and real-world tempo accuracy against another device. Everything else (sequencing, pattern
logic, project save/load, including corrupted data) is tested in the desktop emulator first.
