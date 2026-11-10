# M-VAVE FM1 Custom Groovebox — Target Specification v1.0

## 1. Project Goal

Create a complete, self-contained, four-track groovebox firmware for the M-VAVE FM1.

The instrument should combine a traditional drum machine, three synthesiser tracks, pattern-based sequencing and guitar-pedal-inspired audio effects.

**Design philosophy:**
- Simple, immediate and enjoyable to use.
- Minimal menu-diving.
- Prioritise live jamming and experimentation.
- All essential functions accessible using the FM1's existing buttons, knobs and keyboard.
- Stable audio playback with no timing drift.
- No computer required once firmware is installed.
- Reuse existing working firmware components wherever possible.

## 2. Four Instrument Tracks

### Track 1 — Lead Synth
- Monophonic synthesis.
- Melodies, arpeggios and plucked sounds.
- Short and sustained notes.
- Selectable sound presets.

### Track 2 — Bass Synth
- Monophonic synthesis.
- Deep bass, acid bass, warm bass and distorted bass.
- Selectable sound presets.

### Track 3 — Pad Synth
- Polyphonic synthesis, targeting four simultaneous notes.
- Chords and sustained atmospheric textures.
- Adjustable attack and release.
- Selectable sound presets.

### Track 4 — Drum Machine
- Dedicated drum engine.
- Kick, snare, closed hi-hat, open hi-hat, clap and percussion.
- Multiple drum sounds can play simultaneously.
- Traditional drum-machine step sequencing.
- At least one usable electronic drum kit.

## 3. Synthesiser Engine

Use one shared subtractive synthesis engine design for the lead, bass and pad tracks, with independent settings for each.

Target features:
- Two oscillators per voice, subject to performance.
- Sine, triangle, sawtooth and square/pulse waveforms.
- Adjustable oscillator tuning.
- Low-pass filter with cutoff and resonance.
- ADSR envelope.
- Basic LFO modulation.
- Independent instrument presets.

The lead and bass should each support one voice. The pad should target four voices.

Use efficient synthesis algorithms suitable for the FM1's actual CPU and memory constraints.

## 4. Sequencing

Each track has its own independent sequencer.

- Pattern lengths of 16, 32, 48 or 64 steps.
- Default 16 steps.
- Standard 16th-note step resolution.
- Tempo adjustable from 40–240 BPM.
- One shared master clock for all tracks.
- No drift between drums, synths, arpeggiators and tempo-synchronised effects.
- Patterns loop continuously.
- Independent track mute and unmute.
- Step-entry note recording.
- Step editing and clearing.
- Notes can sustain across multiple steps.
- Chord sequencing on the pad track.
- Adjustable note length.
- Playback continues while editing.

Desirable additional features:
- Live keyboard recording with quantisation.
- Swing.
- Per-step velocity or accents.
- Pattern copy and paste.

## 5. Pattern Management

Each instrument should support at least eight patterns (A–H).

- Each track can use a different pattern.
- Patterns can be edited independently.
- Switch patterns while playback continues.
- Pattern changes are quantised to a musical boundary to avoid timing glitches.
- Queue a pattern change before it occurs.
- Copy, paste and clear patterns.
- Show the currently playing pattern and any queued pattern.

Changing the bass pattern must not interrupt the drum, pad or lead tracks.

## 6. Audio Effects

Six dedicated effect categories, inspired by guitar pedals.

### Top Six Buttons

1. Reverb
2. Delay
3. Distortion / Overdrive
4. Chorus
5. Tremolo
6. Pitch Shift

Each button opens the corresponding effect menu for the selected track.

Where practical, a long press toggles the effect on or off.

### Effect Parameters

**Reverb**
- Mix
- Decay
- Room size

**Delay**
- Mix
- Feedback
- Time
- Tempo-synchronised subdivisions

**Distortion / Overdrive**
- Drive
- Tone
- Output level
- Selectable soft/hard clipping

**Chorus**
- Rate
- Depth
- Mix

**Tremolo**
- Rate
- Depth
- Smooth or square-wave modulation
- Tempo synchronisation

**Pitch Shift**
- Semitone adjustment
- Wet/dry mix

Effects should be adjustable during playback.

Prefer independent per-track effect settings, but use shared processing or send effects where necessary for performance.

Audio must not click, crash or become unstable when effects are adjusted.

## 7. Physical Controls and Interface

### Top Buttons — Sound Editing

The six top buttons provide direct access to the six effect menus.

### Bottom Buttons — Sequencing and Projects

Allocate the bottom buttons to:
- Track selection
- Pattern selection
- Step sequencing
- Recording / editing
- Project management
- Saving

These are functional requirements, not final button assignments.

Inspect the actual FM1 hardware controls and existing firmware mappings before choosing exact button combinations.

### Screen

The home screen should display:
- Current tempo.
- Playback status.
- Selected instrument.
- Current pattern.
- Current sequencer step.
- Track mute status.

Keep the interface minimal, readable and responsive.

Avoid complicated nested menus.

## 8. Project Saving

Support saving and loading complete groovebox projects.

Each project should contain:
- All four instrument settings.
- All patterns and note sequences.
- Tempo.
- Effect parameters.
- Track mute states.
- Currently selected patterns.

Requirements:
- At least eight project slots, if storage permits.
- Saved projects persist after powering off.
- Clear save/load confirmation.
- Protection against accidental overwriting.
- Safe handling of corrupted project data.

## 9. Playback and Performance

- Global play/stop.
- All tracks remain synchronised.
- Keyboard can play the selected synth instrument.
- Live keyboard playing should work during sequencer playback.
- Independent track muting.
- Change patterns without stopping.
- Adjust effects while playing.
- No audible timing drift.
- Avoid clicks or pops when changing sounds or patterns.

## 10. Technical Requirements

- Build upon the existing working FM1 firmware template.
- Reuse existing hardware drivers and audio infrastructure.
- Keep audio processing separate from interface and sequencer logic.
- Use a single shared timing system.
- Avoid blocking operations during audio processing.
- Minimise dynamic memory allocation during playback.
- Monitor CPU and memory usage.
- Provide a desktop testing environment where practical.
- Test sequencing and project management without requiring the physical device.
- Clearly identify features that require real hardware testing.
- Maintain a recoverable, known-working firmware build.

## 11. Development Priorities

Build incrementally in the following order:

**Phase 1 — Sequencer**
- Single synth track.
- 16-step looping pattern.
- Tempo control.
- Start/stop.
- Step-entry editing.

**Phase 2 — Four Tracks**
- Lead, bass, pad and drums.
- Shared master clock.
- Independent sequences.
- Track selection and muting.

**Phase 3 — Pattern Management**
- Multiple patterns per instrument.
- Pattern switching.
- Pattern copying.
- 16–64-step patterns.

**Phase 4 — Sound Engine**
- Shared synth engine.
- Independent presets.
- Pad polyphony.
- Drum sound refinement.

**Phase 5 — Effects**
- Distortion and tremolo.
- Delay and chorus.
- Reverb.
- Pitch shifting if hardware performance permits.

**Phase 6 — Projects and Interface**
- Project saving and loading.
- Complete button mapping.
- Interface improvements.
- Performance optimisation.

## 12. Definition of Finished

The firmware is considered complete when I can:

1. Turn on the FM1 and load a project.
2. Create a 16-step drum beat.
3. Sequence a bassline.
4. Add a four-note pad chord progression.
5. Add a lead melody.
6. Extend patterns up to 64 steps.
7. Create alternative patterns for individual tracks.
8. Switch between patterns during playback.
9. Apply and adjust effects using the top six buttons.
10. Mute and unmute tracks.
11. Save the entire project.
12. Turn the FM1 off and reload the project with everything intact.

All four tracks must play together without significant glitches or timing drift.

## 13. How to approach the build

Treat this document as the target product specification, not a request to implement everything immediately.

First inspect the existing FM1 project and identify:
- Hardware capabilities and constraints.
- Existing working functionality.
- Available memory and CPU resources.
- Button and keyboard mappings.
- Audio architecture.
- Display capabilities.
- Firmware build and testing process.

Identify any requirements that are unrealistic on the actual hardware.

Propose the simplest architecture that can eventually satisfy this specification.

Implement one phase at a time, compiling and testing after each change.

Do not replace working functionality unnecessarily.

Ask for clarification before making significant interface or architectural decisions.

Prioritise a stable, playable instrument over adding every possible feature.
