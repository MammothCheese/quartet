<p align="center"><img src="assets/logo.svg" width="96" alt="QUARTET"></p>

# QUARTET

**A pedal-driven groovebox for the M-VAVE FM-1.** Drums, bass, pads and a lead, each with its own pedalboard. Make a beat, stomp on fuzz or spring reverb while it plays, jam over the top, turn it into a song.

### 👉 [Read the guide](https://mammothcheese.github.io/quartet/) · [Download](../../releases/latest) · [Try it in your browser](https://joakimeriksson.github.io/fm1-emu/?fw=quartet&mhz=60)

<p align="center"><img src="docs/screens.png" width="900" alt="QUARTET's screens: home, the sound browser, the pedalboard, a pedal, the step recorder, the drum grid, the mixer, SUPER SEQ"></p>

## Play in a minute

1. **PLAY** starts the demo band.
2. **SELECT** picks an instrument, **ALGORITHM** changes its sound.
3. Tap **FX SEL ENV LFO** to stomp on its four pedals; hold one to open and tweak it.
4. Drums, empty pattern, **SEQ**: black keys pick a drum, white keys place the hits.
5. **SEQ**, **REC**, **REC** and play: your part goes in, on the beat.

The rest is in **[the guide](https://mammothcheese.github.io/quartet/)**.

## What's inside

- **30 pedals**, four on every instrument: drive, filter, modulation, space, pitch and some strange ones (Loose, Old Media, Glitch). Each drawn as its own boutique pedal.
- **72 sounds and 23 drum kits**: pianos, organs, guitars, strings, choirs, basses, synths, and kits from Rock and Jazz to 808, 909 and a sampled Linn LM-1. Tweak any sound and save your own.
- **Beats and parts**: ten patterns per instrument, drum grid, step and live recording, note lengths and expression, swing, copy.
- **Play live**: arpeggiator, beat repeat, mixer with mutes, key lock.
- **Songs**: SUPER SEQ chains patterns, each instrument following its own part. Work saves itself; four save slots.

## Install

Get `quartet-X.Y.fwsc` from the [releases](../../releases) (newest test build: the `quartet-fwsc` artifact under [Actions](../../actions)), then:

- **[M-Flasher](https://github.com/jvitkauskas/m-flasher)** (Windows, macOS, Linux): connect the FM-1, **DOWNLOAD MODE**, **back up the whole flash first**, then **Install firmware (.fwsc)**.
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
