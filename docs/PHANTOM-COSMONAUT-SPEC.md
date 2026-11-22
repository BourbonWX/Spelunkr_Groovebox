# PHANTOM TRACKER (once PHANTOM COSMONAUT) — firmware spec and handoff

**Phantom Tracker (Ed, Nov 2026: "scrapping the gimmicks of each to create one unified excellent groove box tracker"):**
since 0.9.5 this is Phantom Cosmonaut's look, workflow and FX with Dungeon Tracker's GENESIS, ANALOG and SID back and
the speech (0.9.4's NUMBERS, PHANTOM speaking) gone. Since 0.9.6 the FX are flipped: the FX PAD is the whole mix's
(four effects, one an axis), the glitch layer the targeted FEEDs' (§5 Controls, M5, M6). The sections below keep the
Cosmonaut plan as it was decided, with these changes marked where they apply.

An intercept-terminal groovebox for the M-VAVE FM-1. You tune into ghost telemetry from lost Soviet capsules: four
sequenced telemetry channels, a live Phantom channel on the keys, wavetable "signals" you scan, warp and
fold by hand, and glitches that tear the signal apart.

This document is for Claude Code (and for Ed, the owner). Read it whole before writing code. Keep it up to date as
decisions change; it plays the role `CLAUDE.md` plays in Dungeon Tracker.

---

## 1. How it is built: a fork of Dungeon Tracker 1.5

Do **not** start from scratch. Fork Dungeon Tracker 1.5 (itself a fork of Felucca 1.6.1, GPL-3.0) and keep its
platform, which already works on the hardware. Replace what makes it Dungeon Tracker: the UI, the engines, the
dungeon game.

| Keep (platform and core) | Replace or remove |
|---|---|
| `firmware/hal/*`: keys, encoders (the decoder), LCD, audio, flash, USB, timers, the second core | **All of the UI**: `ui*.c`, `ui_draw.c`, `ui_graph.c`, `ui_layer.c`, `ui_dungeon.c`, `ui_dice.c`, `icons.c`, palettes |
| `lcd.c` and `gfx.c`: the strip renderer, text and icon drawing (re-skinned, not rewritten) | The dungeon game, the party and classes, the bestiary (`ui_dungeon.c`, `dg_monsters.h`, `tools/gen_monsters.py`) |
| `seq.c`: the sequencer, its clock, steps, patterns, the song chain | DICE (`ui_dice.c`) and the Dungeon Tracker engines (GENESIS, GEN KIT, WAVES, WAVE 32, MOD, MOD KIT, SID, ANALOG) as engines; their DSP is reused as code below |
| `motion.c`: automation recording and playback; parameter locks | Inter Tight fonts (swap for a pixel font, §6) |
| `voice.c`: voices, envelopes, glide (the 1.5 rework), the mixer | `boot_song.c` (write a new default "transmission") |
| `fx.c`: insert and master effects, beat repeat, tape stop, freeze | The editor's Dungeon Tracker pages (adapt later, §8) |
| `project.c` and `song_store.c`: saving, power-safe slots, autosave | |
| `midi_*.c`, `editor.c`: MIDI in and out, the editor protocol | |
| `tests/` harness (`ui_test.c` host build, `run_rondo.sh`), the browser emulator, the installer, the release build | |

Rename everything user-visible. Keep the credits: Felucca by Leo Kuroshita (Hügelton Instruments), Dungeon Tracker
by Bourbon Weather. GPL-3.0 throughout.

---

## 2. Hardware facts (measured on Dungeon Tracker 1.5, not estimates)

| | |
|---|---|
| CPU | JieLi AC79 (`pi32v2`), dual core. **No 64-bit division** in the runtime (`__udivdi3` missing): keep every division 32-bit. **Integer and fixed-point only**: Felucca and Dungeon Tracker use no float; do not rely on an FPU unless it is proven on hardware. |
| Firmware image | **Cap 581,564 B (~568 KB).** Dungeon Tracker 1.5 is 556 KB. The rest of the 1 MB flash is the bootloader, song slots, saved sounds and the user area. |
| RAM | **~56 KB** for program data (`.data + .bss`, Dungeon Tracker uses 43 KB) plus a **385 KB pool** for voices, delay lines and buffers (it uses 364 KB). |
| Flash reads | Flash is memory-mapped: read `const` tables directly. No RAM cache is needed. |
| Display | 240 × 240 ST7789, RGB565 over SPI at 12 MHz, DMA. **No full framebuffer** (115 KB would not fit): `gfx.c` draws into a strip in RAM and sends it, strip by strip. |
| Keys | 27 note keys and 14 buttons: FX, SCL, ENV, LFO, EDIT, GLO, HOME, SAVE, ARP, SEQ, PLAY, REC, OCT−, OCT+ (`firmware/src/panel.c`). SCL (scales) and LFO (LFO pages) can be given new jobs here. |
| Encoders | 7: SELECT, ALGORITHM, PRESETS, KNOB 1–4. Push-switches on encoders: **unverified**; do not design around them until tested. |
| Audio | 44.1 kHz, 32-sample control blocks (`CTL`). |

Budget for the new image: removing the dungeon, the Dungeon Tracker engines' tables (MOD's samples alone are 64 KB)
and the Inter Tight fonts frees well over 150 KB. Spend it on the signals (§4) and keep **≥ 20 KB of headroom**.

---

## 3. Names (use these everywhere: UI, code comments for new code, the manual)

| Generic | Phantom Cosmonaut |
|---|---|
| Tempo / BPM | **HBT** (heartbeat, transmission rate) |
| Tracks 1–4 | **FEED 01–FEED 04** (the four sequenced channels) |
| Live keyboard voice | **PHANTOM** channel |
| Song mode / arrangement | **ORBIT** (e.g. ORB 1/6) |
| Patterns A–F | **FREQ A–FREQ F** (frequency bands) |
| Wavetables / instruments | **SIGNAL 01–SIGNAL 16** (`SIG 07` where space is short) |
| Record automation | **INGEST** (flashing `● INGEST`) |
| Freeze/hold sequence state | **RELAY LOCK** |
| Glitch effects | **ATTRITION** |

---

## 4. Sound

### 4+1 voices
* **FEED 01–04**: sequenced, one engine per channel, **monophonic by default** (one note at a time, legato and glide
  from `voice.c`). *Decided (Ed, Nov 2026):* a per-channel **VOICES** setting on EDIT > ENV / VOICE: **MONO / 2 / 3**
  for chords and pads. The voice budget caps the total so PHANTOM always keeps its own notes: a setting that would
  take them is refused (or the channel plays fewer notes) rather than stealing from PHANTOM.
* **CHORD KEYS** (0.9.4, Ed: "white keys play chord and black keys assign a variant"): SCL > CHORD's mode KEYS
  (`chord.c CH_KEYS`, `core.h CHRD_KEYS`), FEED 2's power-on default: the white keys walk the scale's degrees and play
  its diatonic triads; the black keys are held modifiers (`ch_mods`, the ISR's): C# 7, D# sus4 (upper D# sus2), F#
  add9, G# flip, A# spread; alone they play nothing. FEED 2's VOICES AUTO is 4 on KEYS.
* **PHANTOM**: a fifth voice, played live from the 27 keys, outside the sequencer. It never steals a FEED voice and
  never stops the loop. *Decided (Ed, Nov 2026):* **its own voice, one note, a theremin by default**: monophonic,
  legato (a new key held over the last one slides, no new attack), a glide always on, a soft attack and release, a
  gentle delayed vibrato, a near-sine SIGNAL. It has its own sound, not the focused FEED's, shaped by **the same
  contort knobs** (SCAN WARP FOLD CLARITY) on its own SIGNAL, plus GLIDE and VIBRATO. ALGORITHM steps FEED 01..04
  then **PHANTOM**: focused there, KNOB 1..4, PRESETS (its SIGNAL) and EDIT act on PHANTOM. *Decided (Ed, Nov 2026):*
  **PHANTOM is never recorded.** It is the jam: the FEEDs loop what you built, PHANTOM is what you play over them
  live, fleeting by design. Its notes go into no FEED, no pattern and no song; REC (INGEST) records knob moves into
  the FEEDs only. Its sound settings (SIGNAL, knobs, GLIDE, VIBRATO) are kept with the project.

### Engines (Ed, Nov 2026: four, all on the SIGNALs, and a drum kit; nothing else)
*Decided (Ed, Nov 2026):* diverge from Dungeon Tracker's engines as far as possible. **Four engines**, each reading the
16 SIGNALs (no table memory of its own), plus the **NOISE** drum kit. Every other engine is retired: its number stays
reserved (projects, user presets, the editor) and its sounds load on the nearest of these by category.

| # | Engine | Built from | What it is |
|---|---|---|---|
| 20 | **MORPH** | WAVE 32's DSP (`eng_morph.c`) | Classic morph: two neighbouring frames cross-faded, linear interpolation. Pads, drones, cold sweeps. |
| 22 | ~~FOLDER~~ | retired in 0.9.3 (its sounds load on DEBRIS) | West-coast: the signal drove a sine past a quarter turn and folded back, up to ~x16. |
| 23 | ~~GRANULAR~~ | retired in 0.9.3 (its sounds load on DEBRIS) | Bit-reduced granular pointer scanner: two triangle-windowed grains hopping around the signal's frames. |
| 25 | **DEBRIS** | FOLDER and GRANULAR as one (`eng_morph.c`, 0.9.3; Ed: "combine folder and granular ... more unique", then "less harsh") | Folded granular: two Hann-windowed grains in phase with the note, each a few cents off (DRIFT), each folded through a sine (x1 .. x4 only); the mip level drops with the fold so it never aliases; CLARITY's right SHIMMER (octave grains). The tapes on SIG 17..20. Prototypes: `tools/debris_proto.py` (the harsh first pass), `tools/debris_soft.py`. |
| 26 | ~~VOX~~ | retired in 0.9.4 (its sounds load on NUMBERS; since 0.9.5 on MORPH; `eng_vox.c` kept, not compiled; prototype `tools/vox_proto.py`) | A voice: the knobs a mouth, a phoneme a note. Ed: "not landing" (a sentence couldn't be sequenced). |
| 27 | ~~NUMBERS~~ | retired in 0.9.5 (Phantom Tracker: its sounds load on MORPH; the files kept, not compiled). Was: new (`eng_numbers.c`, 0.9.4; prototype `tools/numbers_proto.py`, tables `tools/gen_numbers.py` -> `numbers_tables.h`) | A numbers-station voice, **a word a note**: Flite's rms voice coded as LPC-10 (25 ms frames: energy 5 bits, repeat, voiced, pitch movement 4, K1..K10 6 6 5 5 4 4 4 4 3 3), 100 words in 10.5 KB; a ten-stage lattice at 8 kHz, the robot chirp or a Rosenberg pulse, de-emphasis, upsampled, a radio (band, drive, static, crackle). KIT (the default, Ed: "more like a drum kit ... all possible words in 1 preset": a key a word, OCT the page, 4 x 27; note 20 + the word; KNOB 1 PITCH; a word a one-shot, `ownenv`) / WORD / PHRASE modes, 16 phrases. PHANTOM speaks on it. |
| 24 | **FM** | new (`eng_morph.c`) | Pseudo-FM: one signal frame (the modulator, at RATIO) phase-modulates the read of another frame (the carrier). Crystalline bells. |
| 18 | **GENESIS** | Dungeon Tracker 1.2 (`eng_genesis.c`; back in 0.9.5) | Four-operator FM after the YM2612. |
| 0 | **ANALOG** | Felucca-Engines (`eng_analog.c`; back in 0.9.5) | Subtractive: two oscillators, noise, a resonant filter. |
| 17 | **SID** | Dungeon Tracker 1.3 (`eng_sid.c`; back in 0.9.5) | After the MOS 6581: pulse width, sync, ring, a pitch sweep, the filter, drums. |
| 14 | **NOISE** (kit) | GEN KIT's controls, map and lanes, new voices (`eng_cycle.c`) | FEED 1's drums: a sine-sweep kick, two-sine + noise snare, sample-and-hold noise hats, a ring-modulated perc. No samples. |

RESONANCE (Karplus-Strong) is dropped (Ed, Nov 2026); ANALOG came back in 0.9.5. PHANTOM plays MORPH (0.9.3 sang
VOX, 0.9.4 spoke NUMBERS' phrases; both gone). The EDIT layer's keys (0.9.5): 1 MORPH, 2 DEBRIS, 3 FM, 4 GENESIS,
5 ANALOG, 6 SID, 7 NOISE.

### Signals
16 signals, each a bank of frames. **Budget ≤ 96 KB** including mipmap levels (WAVE 32's generator makes the levels;
reuse it). Suggested: 16 signals × 16 frames × 256 samples, 16-bit, with the mip levels sized down. Theme the signals
(e.g. SIGNAL 01 CARRIER, SIGNAL 07 SOLAR WIND, SIGNAL 13 LAST WORDS) and keep the names ≤ 12 characters.
*Decided (Ed, Nov 2026):* **mostly script-designed** (additive harmonics, formants, sync sweeps, bit-reduced and
noise-dusted waves, each bank sweeping on purpose across its frames; a generator script in `tools/`). At most a few
signals from recordings, and only from sources we may ship (Ed's own, public domain or CC0), credited.

### The four contort knobs (KNOB 1–4, same names on every engine)
| Knob | Name | MORPH | DEBRIS | FM |
|---|---|---|---|---|
| 1 | **SCAN** | frame position | grain read point | modulator frame |
| 2 | **WARP** | phase distortion | grain size (17 .. 274 ms) | index (mod amount) |
| 3 | **FOLD** | wavefolder | each grain's sine fold (x1 .. x4) | feedback |
| 4 | **CLARITY** | low-pass ↔ static | low-pass ↔ SHIMMER | low-pass ↔ static |

EDIT's other four: MORPH RES SENV SUB; DEBRIS SCATTER FENV DRIFT; FM RATIO CAR MENV. GENESIS ANALOG SID (not SIGNAL
engines) keep their own four on HOME: FB, OP1 LEVEL, DTUNE, GRIT; CUT, RES, ATTACK, RELEASE; PW, CUT, RES, SWEEP. (FOLDER's, until 0.9.3: RES SYM FENV;
GRANULAR's JIT SENV CRSH.)

* CLARITY is one knob that goes from a clean low-pass at the left, through open in the middle, to bit-crush,
  sample-rate drop and noise ("static") at the right.
* All four are automatable (INGEST) and lockable per step. A turn while INGEST is armed records, like any knob in
  Dungeon Tracker.
* **SCRAMBLE**: hold **EDIT** and press **OCT+** to throw all four to random positions (the "surprise me" that
  replaces DICE). OCT− undoes it.
* Envelope, glide and voice settings live on a second EDIT page (§6), not on the knobs.

---

## 5. Controls

### Always
| Control | Does |
|---|---|
| **PRESETS** | SIGNAL select for the focused FEED (SIGNAL 01–16) |
| **ALGORITHM** | FEED focus (FEED 01–04) |
| **SELECT** | moves within the current screen (rows on the grid, pages in EDIT) |
| **KNOB 1–4** | SCAN, WARP, FOLD, CLARITY for the focused FEED. Exceptions: on the grid they edit the cell (as Dungeon Tracker's tracker); with FX held they are ATTRITION |
| **PLAY** | start / stop |
| **REC** | arm INGEST (knob moves record into the focused FEED; PHANTOM's notes are never recorded) |
| **FX held** | 0.9.6: the glitch layer works on the **targeted FEEDs** only: the selected one, plus those whose black key (F#3 G#3 A#3 C#4) is held (Ed: "the pikocore glitch effects should be on a per track basis"). They are summed on their own (the glitch bus) and only that sum goes through the layer's chain; the other FEEDs, PHANTOM and the buses' returns play clean. The black keys no longer mute |
| **FX held + KNOB 1–4** | ATTRITION: STUTTER, TUNNEL (filter sweep into a tunnel echo), JUMP (playhead skips), CRUSH. Spring back on release, or stay while FX + OCT+ latches. FX + OCT− resets. 0.9.4: REC armed and playing, held FX prints ATTRITION and the gates onto the steps that play (fxp events of type 0, the pattern's); FX + OCT− with REC armed clears them |
| **GLO tapped** | 0.9.4: **PATTERNS** (FREQ A–F on keys F3–D4, queued to the pattern's end while playing; OCT+ held + a key copies, OCT− held + a key clears, OCT− tapped stays), tapped again **ORBIT** (the song chain). ENV held opens nothing now (it held the patterns until 0.9.3) |
| **GLO held** | the FEED levels, mutes, tap tempo (the GLO layer); GLO + SELECT sets HBT |
| **SAVE** | save, load, slots (Dungeon Tracker's SAVE page, re-skinned); SAVE held = undo |
| **ARP held + KNOB 1–4** | the focused FEED's insert effect (Dungeon Tracker's ARP rides) |
| **RELAY LOCK** | **LFO** (reassigned): the sequence keeps looping its current bar state, edits are buffered, a second press applies them |

### The 27 keys
* **The keys play PHANTOM only while it is focused** (ALGORITHM past FEED 04, on HOME); otherwise the selected
  FEED, as Dungeon Tracker's (Ed, Nov 2026, after v0.9: "not being able to sample presets unless you're in the
  tracker is frustrating"; this also lets a FEED be recorded live from HOME). No screen takes the keys away from
  playing.
* **Performance actions** go through a held button: ENV + keys = FREQ bands (above); **FX + a white key** = a
  momentary effect for as long as the key is held. 0.9.6 (Ed's layout): F3–B3 REPEAT 1/4 1/8 1/16 1/32; C4–F4 REVERSE,
  ECHO, OCT+, OCT−; G4–C5 HPF, LPF, REVERB, STOP; D5–G5 USER 1–4, each holding two effects (by default 1/16 + HPF,
  REVERSE + REVERB, STOP + ECHO, OCT+ + REVERB; ALGORITHM / SELECT with the key held set them). The repeats' loop is
  mono, 743 ms (a 1/4 to ~81 BPM). Any key can hold any effect (PRESETS with the key held).
* **On the SEQ grid with transport stopped**: a key writes its note into the cursor's step, stamps the current
  SIGNAL, and moves down a row (Dungeon Tracker's step entry).

---

## 6. UI: built from scratch for the theme

The visual design is new. Under it, reuse `gfx.c` (strips, text, icons) and `lcd.c` (the panel). The tracker's
*behaviour* (rows, columns, cells, commands, locks, step entry) is kept from Dungeon Tracker; only its look changes.

### Visual language
* **Colours (RGB565)**: background charcoal `0x18C3`; phosphor green `0x07E0`; dim green `0x0320`; signal amber
  `0xFBE0` (rgb 255,125,0); alarm red `0xF800` for INGEST and errors only. Flat: no gradients, no rounded "cards".
* **Font**: a pixel font, monospaced feel. **DotGothic16** is already in the repo (SIL OFL, used by the emulator):
  generate it with `tools/gen_aa_font.py` at its native pixel size so it stays crisp. At most three sizes.
* **Terminal grammar**: labels in caps, values in brackets or after a colon (`SCAN: 042`, `[● INGEST]`), status
  lines like telemetry (`FEED 02 > SIG 07 SOLAR WIND > CARRIER LOCK`).
* **Signal breakup (jitter)**: on ATTRITION, an INGEST stamp or a PHANTOM accent, shift random pixel rows
  sideways for a few frames. With the strip renderer this is cheap: offset a row's start as the strip is sent.
  Keep it under ~150 ms so the screen stays readable.

### Screens
1. **HOME: Performance starfield.** Top 75 %: a 3D field of tumbling debris (points and short line segments)
   flying toward the viewer, speed from HBT. PHANTOM notes and ATTRITION fire a shockwave burst and a short screen
   shake. Bottom 25 %: the focused FEED (`FEED 02 SIG 07`), the four contort values, transport and FREQ. Draw only
   what moved: each star's old pixels cleared, new ones drawn, strip by strip; no whole-screen clears.
2. **SEQ: Telemetry grid.** The tracker, full screen, for the focused FEED: big note and cell text, few columns at
   a time, the playhead row highlighted. FX column codes read in plain words beside them (`A3 → TAPE RUPTURE`).
   Redraw only on a row change or an edit; never from the audio path.
3. **EDIT: Synth lab.** A large oscilloscope of the focused FEED's current cycle, after SCAN, WARP and FOLD (and
   CLARITY's crush), redrawn as the knobs turn; the four contort values under it. SELECT pages to **ENV / VOICE**
   (attack, decay, sustain, release, glide, voice mode) and **INSERT** (the insert effect).
4. **SAVE / GLO / MENU**: functional screens in the same grammar (slots, settings, ORBIT editor).

Every screen needs a `tests/ui_render.c`-style render scene and must pass the text-fit lint (no text cut or
overlapping) before it ships.

---

## 7. Rules for Claude

* Integer / fixed-point only; no 64-bit division; no float.
* Check the image size on every build (`tools/build.py` prints it); keep ≥ 20 KB headroom; stop and report if a
  change would exceed the cap.
* Never draw from the audio path; the UI never blocks the audio callback.
* Keep the host test suite green; add a test with every feature (engine output levels and aliasing, knob ranges,
  INGEST recording, PHANTOM never stealing a FEED voice, save and load round-trip).
* Keep the browser emulator building (`tools/build_emu_clang.sh`): it is the fast sandbox, and the hub's Play
  button.
* Update the manual (`PHANTOM-COSMONAUT.md`) with every user-visible change.
* Ed commits and pushes; ask before any git push or release.

---

## 8. Milestones

Status (Nov 2026): **M0–M6 done, M7 done but the hardware test and the listing** (v0.9 beta: VOICES, SCRAMBLE,
PRESETS = SIGNAL on HOME, RELAY LOCK, PHANTOM CH, USB LEVEL hidden, the GRID's FX in plain words, the default
transmission, the manual, `docs/QUICKSTART.md`, the editor in the device's colours and font, `fmhub.json` +
`docs/card.png`), on the host and in the emulator, not yet tested on the FM-1. M3
keeps Dungeon Tracker's live note recording: with REC armed on a FEED, the keys play and record that FEED. Since
v0.9.1 the keys play PHANTOM only while it is focused (§5). M1 was
done as a re-skin (palette, pixel font, names, HOME and EDIT redrawn); the GRID's plain-word FX codes came at M7
(its footer line); a from-scratch SAVE / GLO is still open. See `CLAUDE.md`.

Cleanup after M3 (Ed, Nov 2026): USB audio removed (USB MIDI stays); WAVES, MOD and MOD KIT retired, the power-on
fanfare removed, the retired engines no longer compiled and the engine memory sized by the kept ones. Old Dungeon
Tracker projects may sound different.

**M4 done (v0.4, Nov 2026)**, redefined by Ed: FOLDER, GRANULAR and FM on the SIGNALs, the NOISE kit in GEN KIT's
place; GENESIS, SID and ANALOG retired too, so the engines are exactly MORPH FOLDER GRANULAR FM and the kit. The image
is 399 KB of 581 KB (about 180 KB free).

**Focus (Ed, Nov 2026):** the FEEDs loop, the FX PAD (M5) dials effects into them live and prints them per step, PHANTOM
plays over it: "another layer to feel like you're dialing in or tuning into the feed".

| # | Milestone | Done when |
|---|---|---|
| M0 | **Fork and rename.** New repo, Dungeon Tracker 1.5 builds and runs under the new name; dungeon, party, DICE removed; tests green | the image boots on the FM-1 and in the emulator |
| M1 | **UI shell.** New theme, pixel font, palette; HOME, SEQ, EDIT, SAVE screens drawn from scratch over the existing sequencer and engines | step entry, play, save and load work through the new screens; render lint clean |
| M2 | **MORPH + signals + contort knobs.** 16 signals, SCAN WARP FOLD CLARITY; the EDIT oscilloscope | the knobs record (INGEST) and lock per step; tests for aliasing and levels |
| M3 | **PHANTOM channel.** The 5th voice: a mono legato theremin-like lead, its own SIGNAL and knobs, a fifth focus on ALGORITHM | PHANTOM never cuts the loop or takes a FEED voice; nothing it plays is recorded, even with REC armed; its sound is saved with the project |
| M4 | **FOLDER, GRANULAR, FM, the NOISE kit**; every other engine retired | each engine passes level and CPU tests; done (v0.4) |
| M5 | **FX PAD** (below; the glitch looper dropped) | two X/Y effects per FEED, printed per step with REC; done (v0.5); on the whole mix since 0.9.6 |
| M6 | **ATTRITION + jitter + starfield** | glitches spring back cleanly; the starfield holds frame rate while playing; done (v0.6) |
| M7 | **Release.** Manual, Quick Start, editor pages, fmhub.json, the hub listing | v1.0 on GitHub; listed on the Firmware Hub |
| M8 | **CAPTURE** (Ed Nov 2026; the factory sets DERELICT and SHORTWAVE built in v0.9.2): upload a clip in the editor, auto-sliced into the kit's 8 drums; two grain sources for GRANULAR | a clip becomes a playable kit and grain tape in one upload; the device still boots into a kit with nothing uploaded |

---

### M5: the FX PAD (Ed, Nov 2026; done in v0.5; on the whole mix 0.9.6–0.9.14; per FEED again since 0.9.15)
**0.9.15 (Ed: "flip the per track - master effects back to how it was with the X/Y being per track and the glitch
effects being over all. The vibe of a track falls apart when the drums are randomly hopping and the bass and keys are
hopping differently"; he kept 0.9.6's four):** the pad is the selected FEED's (ALGORITHM moves it, re-centred), its
FILTR GATE FLANG on that FEED's sum after the SLICER, its ECHO a send from each FEED into one shared echo (32 KB: four
would not fit), its time and feedback the strongest sender's. Prints per FEED on the FEED's own steps (a project marks
them 0xF1); 0.9.6–0.9.14's prints of the mix play on every FEED, split per FEED when printed into.

**0.9.6 (Ed: "our X/Y Kaosspad emulator should be for the sum of the tracks"; he chose four of the fifteen and "option
B"):** one pad on the whole mix (after the glitch bus, before the limiter), its crosshairs playing two effects each,
one an axis: KNOB 1 FILTR (left a low-pass, right a high-pass), KNOB 2 GATE (up 1/16 then 1/32, down 1/8 then 1/4),
KNOB 3 ECHO (left 1/8, right dotted-1/8 ping-pong; the feedback rising outwards, ringing out when let go), KNOB 4 FLANG
(up a jet, down hollow); the centre off. Prints on FEED 1's steps; 0.9.4's per-FEED prints load as the mix's. The
rest of this section is the per-FEED pad as first built.

Ed dropped the glitch looper (a few seconds of 8-bit buffer is too short) for **a Kaoss Pad baked in, per step**:
* **Per FEED, two X/Y effects** after its INSERT (DIST -> INSERT -> PAD 1 -> PAD 2 -> SLICER). If the CPU can't carry
  eight on the FM-1, turn the second core on (MENU > SYSTEM > 2ND CORE); failing that, fall back to one pad on the
  master.
* **FX's first page is the pad:** two crosshairs, effect 1 solid (KNOB 1 X, KNOB 2 Y), effect 2 dithered (KNOB 3, 4);
  **PRESETS picks effect 1, ALGORITHM effect 2.** The other FX pages follow it (tap FX again).
* **The effects (15):** the INSERT's eleven (FILTR CRUSH REPT PHASR FLANG CHOR SOFT HARD FOLD FUZZ DUCK) and four new:
  RING (ring modulator), GATE (chopped on the grid), ECHO (an 11 kHz tape echo, 30–370 ms), TAPE (a tape stop with wow).
* **Nothing is kept by playing:** an effect sounds once its knobs are touched; the crosshairs reset to the centre,
  untouched, whenever the pad opens or is left. **REC on the pad prints:** while the sequencer plays, each step of the
  FEED that plays is written with the pad's two effects (types and X / Y). A printed step plays its effects from then
  on, the pad closed. OCT- on the pad clears the FEED's prints.
* **Storage:** the prints are a pattern's (as its motion is), at most 80 printed steps over the four FEEDs: in a pattern
  bank object (its spare bytes; version 2), a song slot (version 2; Dungeon Tracker's version-1 songs read as none), a
  project (the 512 bytes the retired FM6's patches had, marked 0xF0; a project of before reads as none).

### M8: CAPTURE (in progress; Ed's idea after a Twisted Electrons deTON8-style kit)
**Scrapped in v0.9.7 (Ed: "scrapping our four sampled drum sets in order to keep FM and the Signal knobs"):** the
sampled sets below are no longer built (`gen_factory_kits.py --header` writes only DEBRIS's tapes; `--sets` restores
the hits; `eng_cycle.c`'s SET path under `#ifdef FK_NSET`). SET reads 0 on every kit; a sound with SET 1/2 plays the
synthesis. Kits 16..19 are synthesized: GLITCH HOP, CHILLWAVE, TAPE DECK, WONK. 66 KB freed for engine-built kits and
the formant ("glorpy" frog) synth. The 0.9.2 text is kept for the record:
**Built in v0.9.2 (Ed: "bake Derelict and Shortwave into the firmware"; Balanced budget; a SET knob, the NOISE
synthesis kept):** the kit's **SET** (eng_cycle.c, `P_LN7`, a DRUM lane level the kit never used; 127 from older sounds
reads NOISE): NOISE (the synthesis), DERELICT, SHORTWAVE; the kit presets DERELICT SCRAP SHORTWAVE DEAD AIR (16..19)
set it. The hits: `firmware/src/factory_tables.h` (generated by `tools/gen_factory_kits.py --header`; 16 x one per
GRID row, 16 kHz IMA ADPCM, 66 KB), played by the kit's four voices (`fk_render`: PITCH/TUNE the rate, DECAY a fade,
OPEN the open hat's, COLOR the crunch). GRANULAR's SIG 17..20: the four tapes (16384 samples each at 11.025 kHz, 8-bit,
looped, 64 KB; `gran_render`, `morph_cycle`, `sig_max` / `sig_name`). Image 537,088 B (44 KB free). **Still to build:**
the uploads (the editor's slicer, USR1's 80 KB, CAPTURE's own set) and, with them, the NOISE kit rendered as a set.
Decided by Ed (Nov 2026): **CAPTURE replaces the NOISE kit** (its sounds kept as the factory clip, below); the grain sources are **GRANULAR's only** (not new
SIGNALs for the other engines); the 8-bit crunch is **a knob**, not baked in. Claude's reading: the grain sources are
their own uploads, separate from the drum clip.

**What exists already** (from Felucca / Dungeon Tracker): the editor's Samples tab and its upload protocol
(`SMP_BEGIN / WRITE / END`, web/EDITOR_PROTOCOL.md), trimming and resampling in the browser, the flash writer, and the
SAMPLE engine (still compiled) playing IMA ADPCM zones straight from flash (XIP). **One user slot is free: USR1, 80 KB
at flash 0xA0000**, 16 zones (USR2 and USR3's flash went to the song slots and the pattern bank).

**The plan:**
* **The editor slices** (no slicing on the device): drop or record a clip; it finds the 8 strongest onsets, cuts
  each to its decay (max ~0.5 s), normalises, and shows the 8 markers to drag before sending. Two more drop zones for
  the grain sources (max ~1.5 s each). Resampled to 16 kHz.
* **Storage, one slot, a new format version:** the 8 drums as IMA ADPCM (4 bits a sample: 8 x 0.5 s = 32 KB); the two
  grain tapes as **plain 8-bit PCM** (16 KB a second each: ~48 KB for 2 x 1.5 s). Grains jump around their tape, and
  ADPCM can only be read from the start of a zone, so the grain tapes must be raw. Total ~80 KB: the slot, exactly;
  the editor shows what is left as it goes.
* **CAPTURE, the kit (engine 14 in NOISE's place):** the grid's 8 lanes are the 8 slices (KICK SNARE HAT PERC become
  slice 1–8 on the GRID; the GM map stays: 36 38 42 46 ..), 4 voices at once as now, the kit's controls per voice
  re-read for samples: PITCH, DECAY (a fade), START (into the slice), REVERSE, LEVEL; for the kit CRUNCH (bits and rate,
  the 8-bit knob), TUNE, DRIVE. **With nothing uploaded it plays a factory clip** built into the firmware (~30 KB of the
  179 KB free), so power-on and the default transmission still have drums. **Decided (Ed, Nov 2026): the factory clip
  is the NOISE kit rendered once** — its eight lane sounds (kick, snare, closed and open hat, the perc voices) played
  through `ck_render` at the CAPSULE kit's settings by a build tool (a host program like `tests/finish_test.c`'s
  renderer), stored in the same ADPCM format as an upload, so NOISE's sound lives on as the default and the engine code
  can go. The factory grain tapes: two of the SIGNALs played as a sweep (e.g. SOLAR WIND and DARK SIDE), the same way.
* **Factory sets (references rendered, Nov 2026; Ed to choose):** NOISE (the default: `tests/factory_render.c`
  renders the kit's CAPSULE and two scanned SIGNALs from the firmware itself), and two more synthesized offline by
  `tools/gen_factory_kits.py` (no 808/909, no chip/FM, after the deTON8's odd hits): **DERELICT** (HULL RIVET PIPE
  RELAY VALVE CABLE SPRING SERVO; tapes GROAN, VENT) and **SHORTWAVE** (SONAR SQUELCH CHIME GEIGER TAPE PING DIAL TUNE;
  tapes HETERODYNE, IONOSPHERE), in GRID row order BD SD CP CH OH TM RS CB. Masters at 44.1 kHz in `build/factory/wav`.
* **GRANULAR's sources:** SIG 01–16 as now, then **TAPE A** and **TAPE B** (the uploads) at the end of its SIG list;
  a grain reads its tape directly from flash. SCAN where in the tape, WARP the grain size, FOLD the scatter, CLARITY
  as now (its right side is the crunch).
* **Saving:** a project names the slot, it does not hold the audio (80 KB); a project from another FM-1 plays that
  device's clip. Flagged in the manual. The editor's backup should take the slot too.
* **Risks to measure on the FM-1 first:** flash reads in the audio path for 4 drum voices + grains (the SAMPLE engine
  already does it, so likely fine); the editor's upload time for 80 KB.

### M6: ATTRITION, the jitter, the starfield (done in v0.6)
**0.9.15:** the FX layer (ATTRITION, the white keys) works on every FEED again; a black key held leaves its FEED out,
dry (0.9.6–0.9.14: the selected FEED, black keys adding). PHANTOM and the buses' returns stay clean.

Built as the spec's §5 and §6 say, on Dungeon Tracker's FX layer (Ed was away: the open choices taken as below, his
answer to questions "yes"):
* **ATTRITION** is the FX layer's four knobs by default: STUTTER (perform's REPEAT on the grid, 1/8 → 1/16 → 1/32 as
  it turns), TUNNEL (the master LPF closing to 75 % with ECHO THROW's sends and feedback in proportion), JUMP (seq.c
  `seq_jump`: a step plays, by chance, the same 1/16 of another beat; the sequencer's place never moves; and, Ed's
  addition, a beat by chance (35 % at full) played backwards: perform.c `mk_jrev_bits`, the REVERSE key held for it) and CRUSH.
  They join the pool FX > MASTER picks from (eleven; the project's `G_ENGGO` holds 4 bits a knob now, 0 =
  ATTRITION's four). **Every knob of the layer springs back** when FX is let go (Dungeon Tracker's stayed); FX + OCT+
  latches, FX + OCT− resets (and unlatches); MENU > FX LATCH ON keeps them as before.
* **FX + keys** stay Dungeon Tracker's buffer effects (REPEAT, REVERSE, TAPE STOP, FREEZE, …): they are the momentary
  gates the spec asks for; the layer's header reads `[FX] ATTRITION`.
* **The jitter** (gfx.c `cv_jitter`): canvas rows shifted sideways as they are sent, in bands, for 140 ms (weak: few
  rows, ≤ 3 px; strong: more, ≤ 9 px), the page drawn whole each frame of it and once more clean. Kicked by ATTRITION
  (a knob, a gate), an INGEST stamp and a PHANTOM phrase's first note (not a legato slide). MENU > ANIM OFF: none.
* **The starfield** (ui_graph.c `graph_stars`): HOME's panel, 56 bodies seen through a pinhole, speed from HBT
  while playing, near ones streaks, one in eight a turning segment; a shockwave ring and a shake on a PHANTOM phrase
  or ATTRITION from rest. The telemetry lines sit under it (the spec's bottom quarter); the contort values are the
  cards above, as before. MENU > HOME: STARS (0, the default: a Dungeon Tracker device's SCOPE comes up as it),
  TRACKS, LEVELS, SCOPE. Drawn whole every other frame as the scope was: the same cost on the host.

### GLORP, the frog synth (0.9.8, built)
Ed: "the viral glorpy frog synth effect" on the NUMBERS / vocal work, then "an almost automatic wah effect that opens and
closes based on how hard the keys are pressed"; of six sketches (`tools/glorp_proto.py`) he picked F, the vowel wah.
Engine 28, `eng_glorp.c`, EDIT layer key 7 (NOISE moves to 8): a buzzing glottal pulse with a saw through three vowel
band-passes, driven; each note's mouth rests on OO, opens towards VOWEL and closes, wider and slower the harder the note
(velocity; SENS 0 for the FM-1's single-velocity keys), the pitch dipping in. HOME: VOWEL WAH SPEED GLOOP; EDIT 2: DIP
BUZZ SENS ATTACK. Not done: following the loudness instead of the velocity (an envelope follower), for live playing on
the FM-1's keys: ask Ed after he has played it.

### VULT (0.9.10, built)
Ed: "add the Vult engine" (written "Vault DSP" first). Engine 29, `eng_vult.c`, EDIT layer key 8 (NOISE 9): Vult's MIT
examples ported to fixed point (phase-distortion oscillator, resonant comb, diode ladder, soft saturation) and checked
against Vult's own renders (`tests/vult_test.c`). Next (Ed): with the space left, premade drum kits.

### The kit's MODELs and ten kits (0.9.11, built)
Ed: "engine flavoured voices plus kits that use them ... 10 usable but also unique bespoke kits". EDIT > MODEL: each
of the kit's four voices NOISE, FM, CHIP, COMB or BLOOP; kits 20..29 SIXTEEN BIT .. TIN CAN use them.

### Levels (0.9.15, built)
Ed: "all of our drums need to be increased in volume as they are very quiet in almost every mix" and "the phantom
theremin track is way too loud ... it doesn't have a volume control". The kit's output x2 (+6 dB), every other sound
4 dB under it in the mix (a chord FEED drove the master limiter ~8 dB, the drums ducked under it); PHANTOM's LEVEL on
its EDIT page (KNOB 4, SIG's place), 86 (-13 dB) by default, saved in the project.

### Ten real kits (0.9.12, built)
Ed: "five more genesis kits that try to emulate actual drum kits and five more SID kits that do the same": kits 30..39,
FM ROCK .. FM NINE on FM, SID ROCK .. SID BREAKS on CHIP.

### 0.10: a percussion FEED, B sides, per-step pan, PHANTOM and MASTER on the strip (Ed, Nov 2026; built, see "As built" below)
Ed asked whether to use the free space for more tracks ("treating the 8 tracks like four stereo tracks"). Measured on
0.9.15: flash is not the limit, RAM is (a sequenced track ~1.9 KB of main RAM and ~4.2 KB across the six FREQ
patterns; 14.6 KB and 20.6 KB free). A first plan of six sequenced FEEDs (two drum FEEDs, 3 voices each) gave way,
once per-hit panning turned out cheap, to Ed's final one ("see the vision?"):
* **Two pages of four columns:**
  1. **DRUMS** (FEED 01): the kit as now, four lanes, **4 voices**.
  2. **PERC** (new, sequenced): **two lanes, PERC 1 and PERC 2, each any drum sound of any factory kit** (any of the
     40 kits' four voices, any MODEL: Ed, "all the possible sounds from the kits we've already created"), **2 voices**.
  3. **FEED 03–05: the three synths** (today's FEED 2–4), any engine, each with an optional **B side**: a second engine
     and sound playing the same notes in full (MORPH blends two sounds; B plays both), its own LEVEL, PAN and
     transpose; the INSERT, sends, FX PAD and glitch share are the FEED's (one channel strip). Off by default. They
     share **6 voices**, a B side's notes counting.
  4. **PHANTOM**: the jam voice as now (no sequencer, never recorded), on the strip with its LEVEL.
  5. **MASTER**: two columns, the left and right levels (a stereo meter; the master level and the limiter).
* **Per-step pan** (Ed: "If we can get per step panning THAT would be slick"): a step's PAN taken by each hit (voice)
  at note-on and kept while it rings: a kick left and the snare after it right, each staying put. On every FEED, not
  only the drums. How: SPREAD's stereo path (fx.c mix_spread: the mono sum b plus a side signal d) generalised so each
  voice adds itself to the side by its own pan: a multiply-add per voice, no new buffers.
* **Voices:** 4 + 2 + 6 = 12, as now; more only if the FM-1's CPU allows (measure first; the second core the fallback).
* **Cost:** one more sequenced FEED (~6 KB of the ~35 KB of RAM left); B sides small (a sound's values and engine state
  each, no steps). Four FEEDs are assumed in ~360 places and ~60 four-bit masks (mutes, black keys, prints, glitch
  targets, MIDI channels, the mix): all to five. **New save formats** (projects, song slots, FREQ patterns). **Old
  saves are scrapped** (Ed: "Don't worry about old songs. Scrap all of our old songs"): no loading of 4-FEED projects,
  song slots or patterns; a save from before 0.10 is refused cleanly (never half-loaded) and the retired-engine
  conversions for old songs can go.
* **RANDOM SONG** (MENU > SYSTEM) rewritten to show the new features off (Ed: "take your own creative liberties"):
  per-step pan on the drums and PERC (hats and percussion moving across the field), PERC's two lanes drawn from other
  kits than DRUMS', B sides on some synths (a bass doubled and panned apart, a pad layered on another engine), PHANTOM's
  sound and LEVEL set for the key; in the glitch-hop / chillwave direction.
* **Controls:** HOME and the mixer two pages of four; the glitch layer's black keys keep each of the five FEEDs dry;
  MIDI OUT a channel per FEED plus PHANTOM CH.
* **Before building:** Ed flashes 0.9.15 and tries a heavy patch (chords on DEBRIS / VULT on every FEED, the pad and the
  glitch layer) for crackles, to know the CPU headroom.

**As built (0.10, Nov 16 2026).** What the build settled, where the plan left room:
* **Indices:** PERC is `trk[4]` (`core.h PERC_TRK`), shown second (`FD_AT {0, 4, 1, 2, 3}`, `fd_pos`): FEED numbers on
  screen, ALGORITHM, the SEQ picker, the SONG lanes, MENU > MIDI FEED n CH, the black keys and the strip all go in the
  shown order DRUMS PERC SYNTH 1–3, while the code and the save format keep FEEDs 1–4 where they were.
* **The strip:** MIXER, HOME > TRACKS, HOME LEVELS and GLO's knobs show a page of four (`ui.c strip_page / strip_at`):
  DRUMS PERC SYNTH 1 SYNTH 2, or SYNTH 3 PHANTOM MASTER L MASTER R; the page follows the selection (the fifth FEED or
  PHANTOM focused: the second). MASTER is a meter (the output's peaks, `fx.c mst_pk`, no knob); PHANTOM's column is its
  LEVEL. GLO's black keys mute the five; its white keys F G A B solo the shown 1–4 and G4 the fifth (C4 stays UNMUTE
  ALL).
* **PERC:** the kit engine with two slots (`eng_cycle.c` PC_*): a source each (kit 0–39, one of 16 sounds: KICK SNARE RIM
  CLAP CL / PD / OP HAT CRASH RIDE BELL LO / HI TOM COWBELL CLAVE CONGA SHAKER, each a GM note of the kit's map), its
  values copied from that kit's voice. EDIT > SOURCE / PERC 1 / PERC 2 / PERC MIX / PERC MDL; the roll and the
  tracker show two lanes named by the sounds; PRESETS on PERC picks the kit for both. PERC plays kits only.
* **Per-step pan:** a PAN lock on a step (motion, as any lock) is taken by each voice that step starts (`voice_t.pan`,
  `pfix`) and kept while it rings. The mix: `voice.c vpan_render` renders each voice alone and adds its mid and half
  side by PAN's own law (`fx.c mix_spread`: L = b + d, R = b − d); SPREAD is now the same path.
* **B sides** (FEEDs 3–5, `trk[1..3]`): the B sound (engine, preset, the values `voice.c b_own` names: envelope, voice
  mode, glide, the engine's values, SPREAD …) swaps with the track's for each note and each block (`b_swap`), so the
  engines and every page work on it unchanged; LEVEL PAN sends INSERT SLICER ARP scale MUTE stay the FEED's. Its own
  TRN, LEVEL (104 as loud as A) and PAN on EDIT > B SIDE; EDIT layer: B SIDE (on / off) and EDIT A / B (FLIP: the
  UI edits B; the header says `+B` / `/B`). Voices: A's 0–3, B's 4–7 while on. Not with a kit on either side.
* **Saves:** FUNB projects (run-length coded as written, `project.c rw_put / rr_get`: no 6 KB buffer), B sides at the
  end (unflipped); FREQ patterns rle-coded (bank ver 3); song slots ver 3. Everything before 0.10 refused.
* **Room:** the SLICER's rows four for five FEEDs (PERC borrows DRUMS' while DRUMS' SLICER is OFF and its INSERT not
  REPT), PERC's COMB lines its own 1.5 KB (VULT never plays there): pool 375,792 of 385,024; RAM 47,304 of 57,344;
  image 509,772 B.
* **RANDOM SONG:** four styles (GLITCH HOP, CHILLWAVE, ELECTRO, LO-FI: tempo and kits), PERC from two other kits with a
  ping-ponging shaker and a fill swept across, off-beat hats panned per step, CHORDS always with a B side an octave up
  panned apart, BASS / LEAD sometimes.
* **Not yet:** the web editor knows five FEEDs (lists, mixer, SONG lanes) but not B sides or PERC's sources; PHANTOM's
  sound in RANDOM SONG unchanged.

**As built (0.10.5, Nov 17 2026): no roles** (Ed: "assign voice # values to all the presets ... the onus is on the
user"; he chose kits on DRUMS / PERC only, voices by category with PLUCK FX 1 and PAD KEYS 4, LIST kept with ALL the
default). `core.h trk_avox[NTRK]` (AUTO, from `ui.c auto_vox_sync` each UI pass: `CAT_VOX` by `track_cat`), `vox_req`
(P_VOICES 0..4; CHRD_KEYS still 4), `trk_cap` (requests to 6; past it in proportion, floors >= 1, the rest to the
furthest short). `params.c N_VOICES` + "4" (the editor's too). `ui.c list_mode` ALL by default (ROLE gone: `list_pos`
/ `list_of` plain), `list_keep` kits only on kit FEEDs and none on the synths, applied always (`preset_pos` /
`preset_at`). `trk_cls` stays (RANDOM SONG, MIDI, the strip). `finish_test` 1, `ui_test` categories / browse.

**As built (0.10.4, Nov 17 2026): FREEZE for REVERSE, PRESETS through every sound** (Ed). `perform.c PF_DEF[4]` and
`PF_UDEF[1][0]` PF_FRZ (stored maps of 0 follow the default); `ui_layer.c` assign / USER turns skip PF_REV; JUMP's
`mk_jrev_bits` keep REVERSE. `ui_input.c presets_turn`: the SIGNAL branch off, so HOME's PRESETS is `preset_step` on
every engine (the LIST: ROLE by default). `finish_test` 3, `ui_test` FX map / USER keys.

**As built (0.10.3, Nov 17 2026): the echo's return.** 0.10.2's `mulq15(x << 2, dmix)` overflowed past half scale at a
high DMIX (int16 x 4 x 32766 > 2^31): now `(x * dmix) >> 13`. The delay line's write through `fx.c dly_knee` (linear to
24576, then 24576 + 8192 e / (e + 8192)). Found with a host harness stacking the FX PAD on every FEED, three glitch
keys, ATTRITION and the layer's knobs at random, built with -fsanitize=signed-integer-overflow (the only other report:
FM's phase modulation, `eng_morph.c` 411, a phase wrapping as meant). `glitch_test` 10.

**As built (0.10.2, Nov 17 2026): the glitch layer lighter** (Ed: pops on busy songs under ECHO / REVERB, also with
the second core on; ~75 % CPU playing with it):
* **The buses on core 1** (`fx.c mix_block`, `c1_buses`, `perf_glb`): with the FX layer at work and the second core
  answering, `fx_buses` (chorus, delay, reverb: the sends in, `wet` / `rev_side` out) runs there while core 0 runs the
  glitch bus (`perf_glb`: MASTER on `glb_l` / `glb_r`, `perf_block`); `perf_master` skips it then. Neither touches the
  other's state (the throws' `pf.ew1` is set in `perf_begin`, before). Bit for bit one core's: `dualcore_test`'s GLITCH
  scenes (ROOM SPRING HALL, the throws, REPEAT, REVERSE, a black key), clean under ThreadSanitizer. Console
  `cpu2_bus_blocks`.
* **Shedding** (`audio.c shed_check`): `SHED_TICKS_FX` 75 % while `pf.live` or `xy_want()`, 85 % otherwise.
* **Headroom:** ROOM's and SPRING's input x 1290 (was 2580), their outputs x 2; the delay line takes `dly_in >> 2`
  (was >> 1), its read x 4 (was x 2). At every FEED's REV / DLY 127: ROOM's clipped writes ~0.94 % -> ~0.01 %.
* **Black keys as switches** (`perform.c perf_press`): a black key's press toggles its bit in `perf_latched` (FX LATCH
  or not), its release does nothing; a menu (`perf_kill`) keeps them (`perf_latched &= PF_MUTE`), FX + OCT- clears
  them. `glitch_test` 9.

**As built (0.10.1, Nov 16 2026): fewer presses** (Ed picked four of nine workflow proposals):
* **FX column list** (`ui_tracker.c tk_fx_list` over `tk_fx_list0`): the commands, then PAN, LEVEL, the engine's four
  HOME knobs (`engine_t.knob`), then the rest in the old order. `tk.fx[k]` (per FEED, an index) keeps the last pick.
* **PERC's HOME knobs** (`ui.c home_param`): PC_KIT(0) PC_SND(0) PC_KIT(1) PC_SND(1); turned through `pc_source`, not
  recorded as motion or MIDI-learned.
* **B as an ALGORITHM stop** (`ui_input.c algo_stops` / `algo_step`): stops `k << 1 | side` in shown order, a B stop
  where `b_on`; arriving flips (`b_flip`), leaving unflips; PHANTOM after the last stop. The MIXER on a B stop:
  `tracks_edit` slots 0 / 1 turn `bsr.s.lv` / `s.pn` (unrecorded; LFO-held momentary refused there), `ui_draw.c` shows
  B LEVEL / B PAN; the FEED cards (`ui_graph.c draw_tracks`) show `+B` / `/B` and a 2 px bar of the other side's level.
* **OCT paging** (`ui.c strip_page` / `strip_flip`, `ui.spg_*`): OCT+ / OCT− on MIXER and HOME TRACKS / LEVELS set
  the page; it holds while the selection (`song.sel | ph_focus << 7`) is unchanged, then follows the selection again.
  There OCT doesn't change the octave.

## 9. References

* Dungeon Tracker 1.5 (this fork's base) and Felucca 1.6.1: the platform, sequencer and engines above.
* Mutable Instruments **Plaits** (MIT): wavetable and wavefolder ideas, `wavetable_oscillator`.
* **Mozzi** (LGPL-2.1): phase-accumulator oscillators (`Oscil.h`), for ideas.
* Other repositories (e.g. FM-1 firmware by other makers) only after checking that they exist and what their
  licence allows.

---

## 10. Open questions for Ed

1. ~~FEEDs: strictly monophonic, or allow small chords?~~ Decided: mono by default, per-channel VOICES MONO / 2 / 3,
   PHANTOM's notes protected (§4).
2. ~~PHANTOM: the focused FEED's sound or its own?~~ Decided: its own, a mono legato theremin-like lead with the
   contort knobs, GLIDE and VIBRATO; a fifth focus on ALGORITHM (§4).
3. ~~RELAY LOCK~~ Built (v0.9, Claude with the con, to confirm on the device): LFO tapped snapshots every FEED's steps
   and the sequencer plays the snapshot while edits go to the real steps (seen on the GRID); LFO tapped again, the
   edits play. LEN, knobs and automation stay live. LFO's pages moved to LFO held 0.6 s; LFO + a knob is MOMENTARY.
6. ~~PHANTOM on MIDI OUT?~~ Built (v0.9): MENU > MIDI > PHANTOM CH, 5 by default, OFF.
7. ~~The keys with REC armed~~ Decided (Ed, after v0.9): the keys play PHANTOM only when it is focused, the selected
   FEED otherwise. Still open: a from-scratch SAVE / GLO / MENU
   layout (they carry the M1 re-skin); the Firmware Hub listing needs the GitHub repo (fmhub.json assumes
   BourbonWX/Phantom-Cosmonaut).
4. ~~Signals (wavetables): scripted or from recordings?~~ Decided: mostly scripted, a few from shippable recordings at most (§4).
5. ~~Keep Dungeon Tracker's PARTY/class idea?~~ Dropped at M0 (the roles are fixed); the sequence dice (SEQ > VARY) went too
   (Ed, Nov 2026).