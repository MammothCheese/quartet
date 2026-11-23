<p align="center"><img src="assets/logo.svg" width="96" alt="QUARTET"></p>

# QUARTET

**A pedal-driven groovebox for the M-VAVE FM-1.** Drums, bass, pads and a lead, each with its own pedalboard. Make a beat, stomp on fuzz or spring reverb while it plays, jam over the top, turn it into a song, and bridge live from one song into the next.

### 👉 [Cheat sheet](https://mammothcheese.github.io/quartet/) · [Quick start](https://mammothcheese.github.io/quartet/start.html) · [Full guide](https://mammothcheese.github.io/quartet/guide.html) · [Play it in your browser](https://mammothcheese.github.io/quartet/emu/) · [Download](../../releases/latest)

<p align="center"><img src="docs/screens.png" width="900" alt="QUARTET's screens: home, the sound editor, the pedalboard, a pedal, the step recorder, the drum grid, the BRIDGE page, SUPER SEQ"></p>

## Play in a minute

1. **PLAY** starts the demo band.
2. **SELECT** picks an instrument, **ALGORITHM** changes its sound.
3. Tap **FX SEL ENV LFO** to stomp on its four pedals; hold one to open and tweak it.
4. Drums, empty pattern, **SEQ**: black keys pick a drum, white keys place the hits.
5. **REC** on the home screen: a bar's count-in, then play. Your part goes in, on the beat; **REC** again stops.

Every control is on **[the cheat sheet](https://mammothcheese.github.io/quartet/)**; a first song from nothing is in **[the quick start](https://mammothcheese.github.io/quartet/start.html)**; the rest is in **[the full guide](https://mammothcheese.github.io/quartet/guide.html)**.

## What's inside

- **40 pedals**, four on every instrument, in seven kinds: drive (with a Compressor), filter (DJ Filter, Acid), modulation (Rotary), space, pitch (Ring Mod, Music Box), texture (Old Media, Wrong Station) and behaviour, pedals that change how a part plays (Loose, Mech Failure, Memory Loss, Tape Worm, Bloom). Each drawn as its own boutique pedal.
- **86 sounds and 26 drum kits**: pianos, organs, a Clavioline, horns, guitars, strings, choirs, basses, synths, and kits from Rock, Jazz and Toms to 808, 909, a gated '80s kit and a sampled Linn LM-1. Every sound has its own filter envelope, key and velocity response, pitch envelope and delayed vibrato, plus a noise layer you can add. Shape any of it in an eight-page sound editor with charts, and save your own.
- **Beats and parts**: ten patterns per instrument, a drum grid and a step recorder with every knob setting on screen; per-step **chance, ratchet and nudge** (on every drum hit too); note lengths and expression; swing and time signatures (4/4, 3/4, 5/4, 6/8, 7/8); copy.
- **Recording**: REC from home with a count-in, the first take setting a pattern's length, undo a take, MIDI keyboard velocity.
- **Play live**: arpeggiator, beat repeat, mixer with mutes, key lock.
- **Songs**: SUPER SEQ chains patterns, each instrument following its own part. Work saves itself; twelve save slots, each loading on the patterns you choose.
- **Bridges**: queue another save while playing and the band plays its way into it (1 to 8 bars; smooth, breakdown, fill or cut; the tempo gliding or jumping), landing on its beat one without stopping.
- **Four looks**: Paper, Dark, Neon and LCD (hold HOME, turn PRESETS).

## Install

Get `quartet-X.Y.fwsc` from the [releases](../../releases) (newest test build: the `quartet-fwsc` artifact under [Actions](../../actions)), then:

- **[The Firmware Hub](https://thefirmwarehub.com/)**, in a web browser (back up first),
- or **[M-Flasher](https://github.com/jvitkauskas/m-flasher)** (Windows, macOS, Linux): connect the FM-1, **DOWNLOAD MODE**, **back up the whole flash first**, then **Install firmware (.fwsc)**.
- or **`tools/fm1_install.py`** over USB-MIDI: `pip install mido python-rtmidi`, then `python tools/fm1_install.py quartet-X.Y.fwsc`.

The FM-1 then reports itself as `FM-1_988`.

**Going back:** restore your backup, or install M-VAVE's `FM-1.fwsc` (the stock presets may then need *System → Reset Patches*). **If something goes wrong:** hold **OCT-** while switching on for USB rescue, then install again. Don't unplug while an install is writing.

Installing firmware is at your own risk. QUARTET is not made or endorsed by M-VAVE.

## Build it

Every push to `main` builds on GitHub Actions, with host tests; a `vX.Y` tag publishes a release. To build yourself (Linux or Docker): the JieLi toolchain (`tools/get_toolchain.sh`), the AC79 SDK (`AC79NN_SDK_V1.2.1_2023-12-13`) and Pillow, then `./build.sh` → `build/quartet.fwsc`. `tests/run_host.sh` runs it on your computer (screens as PNG, sound as WAV).

```
firmware/src/quartet.c     the screens, controls, data and saving
firmware/src/gb_clock.h    the clock, patterns and SUPER SEQ
firmware/src/gb_synth.h    the synth engine and its sounds
firmware/src/gb_drums.h    the drums and kits
firmware/src/gb_fx.h       the pedals and mixer
firmware/src/platform.c    boot, USB, MIDI, updates, audio, storage (from Dinghy)
docs/development/          how it was planned and built
```

## Built on

**[Dinghy](https://github.com/zvenson/dinghy)** by zvenson, carrying the platform of **[Felucca](https://github.com/hugelton/Felucca)** by Leo Kuroshita (Hügelton Instruments), with the changes of **[SLOOP](https://github.com/isod89/sloop-fm1)** (isod89), OberVave's storage and loader screen (zvenson), the SMK-37 layer of M. Geisler (genkigenki) and the loader idea of [Skiff](https://github.com/illuminus5/skiff) (illuminus5). Thank you all.

## License

GPL-3.0-only: use it, change it, share it, and pass on the source with any firmware built from it. The LM-1 samples (rossf, Sampling+ 1.0) and other third-party parts: [LICENSING.md](LICENSING.md).
