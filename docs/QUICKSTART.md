# Phantom Tracker — quick start (v0.10.5 beta)

A groovebox tracker for the M-VAVE FM-1: Phantom Cosmonaut's look, workflow and FX, with Dungeon Tracker's synth
engines back. Five FEEDs (drums, percussion and three synths, each synth with an optional B side) on nine engines (the wavetable SIGNALs you scan, warp and fold by hand, DEBRIS, FM, and
GENESIS, ANALOG and SID from Dungeon Tracker, GLORP the frog synth, VULT, plus the NOISE kit), PHANTOM to jam over them, and two ways to tear it
all apart. This page gets you from install to a jam; the full manual is [PHANTOM-COSMONAUT.md](../PHANTOM-COSMONAUT.md)
(Phantom Tracker's, under its old name), and what isn't there works as in [Dungeon Tracker 1.5](DUNGEON-TRACKER-1.5.md).

> **Beta.** v0.10 has run in the emulator and on the host tests; 0.9.13 and 0.9.14 have run on an FM-1. Install
> at your own risk, and keep the way back (below) ready before you start.
>
> **New in 0.10.5: no roles.** Any synth sound loads on any of the three synth FEEDs, and PRESETS on HOME scrolls
> them all (kits on DRUMS and PERC). Each sound brings its own voice count (BASS, LEAD, PLUCK, FX: 1; PAD, KEYS: 4;
> OTHER: 2); the three synth FEEDs share six, so how you spend them is up to you.
>
> **New in 0.10.4:** **PRESETS on HOME steps whole sounds on every engine** (MORPH, DEBRIS and FM too: no more stopping
> at SIG 16), the ones that fit the FEED's role; the SIGNAL is on the engine's first EDIT page. On the glitch grid
> **FREEZE replaces REVERSE** (C4, and USER 2 = FREEZE + REVERB); ATTRITION's JUMP still reverses a beat now and then.
>
> **0.10.3:** fixes pops from the echo when effects are stacked (a 0.10.2 bug), and the echo rounds off instead of
> crackling at full feedback.
>
> **New in 0.10.2:** the glitch layer's **black keys are switches** (tap: that FEED dry, tap again: back). Busy songs
> pop less under the glitch layer's ECHO / REVERB: with **2ND CORE** on (MENU > SYSTEM, then power off and on) the
> echo and reverb run on the second core alongside the glitch effects, voices thin sooner while effects run, and
> ROOM, SPRING and the echo have more headroom.
>
> **New in 0.10.1 (fewer presses):** **ALGORITHM stops at each B side** (3, 3B, 4, 4B ...; on a B stop you edit B,
> and the MIXER's LEVEL / PAN are B's). **PERC's HOME knobs** are its two sounds (1 KIT, 1 SOUND, 2 KIT, 2 SOUND).
> **OCT+ / OCT−** turn the strip's page on the MIXER and HOME TRACKS / LEVELS. In the GRID's **FX** column, **PAN**
> comes right after the commands (then LEVEL and HOME's four knobs), and each FEED remembers its last pick.
>
> **New in 0.10:** **saves from before 0.10 don't load** (back them up with the web editor if you want to keep them
> as files). Five FEEDs: **1 DRUMS, 2 PERC** (two drum sounds from any kit), **3–5 the synths**. The synths can have a
> **B side** (EDIT held, the key after CLEAN): a second sound on the same notes, with its own transpose, level and
> pan. **Per-step pan:** lock PAN on a step and each hit keeps that pan. The mixer is two pages of four: DRUMS PERC
> SYNTH 1 SYNTH 2, then SYNTH 3, PHANTOM and the MASTER meters. RANDOM SONG rolls a style (GLITCH HOP, CHILLWAVE,
> ELECTRO, LO-FI) with all of it.
>
> **0.9.15:** the FX PAD is each FEED's again (the selected FEED's; ALGORITHM moves it) and the glitch keys
> and ATTRITION work on the whole mix, a black key keeping its FEED dry. The drums are 10 dB louder against everything
> else (turn FEED 1 back down in songs you balanced by hand). PHANTOM is quieter and has a LEVEL knob: focus it, tap
> EDIT, KNOB 4.
>
> **0.9.14:** GENESIS fixed: every preset sounds as it should (12 of 24 were static). A GENESIS sound saved
> with an earlier build keeps the static: turn PRESETS off it and back.
>
> **0.9.13:** the FM kits' hi-hats, rides and crashes rebuilt: each its own sound, each kit its own cymbals.
>
> **New in 0.9.12:** ten kits after real drum kits: FM ROCK, FM JAZZ, FM FUNK, FM EIGHT, FM NINE, SID ROCK, SID
> EIGHT, SID NINE, SID JAZZ, SID BREAKS.
>
> **New in 0.9.11:** ten new drum kits whose voices borrow the engines' sounds (FM, CHIP, COMB, BLOOP), and EDIT >
> MODEL to set each drum voice's model yourself.
>
> **New in 0.9.10:** **VULT** (EDIT layer key 8; NOISE moves to 9): a phase-distortion oscillator into a resonant comb
> and a diode ladder, from Vult's open DSP.
>
> **New in 0.9.9:** newer FM-1s (with the UC25HQ80 flash chip) save and update again.
>
> **New in 0.9.8:** **GLORP**, the frog synth (EDIT layer key 7): a vowel wah on every note that opens wider the harder
> the note is played.
>
> **New in 0.9.7:** the sampled drum sets are gone; four synthesized kits take their places (GLITCH HOP, CHILLWAVE,
> TAPE DECK, WONK).
>
> **New in 0.9.6:** the two FX swapped places. The **FX PAD** (tap FX) is now on the whole mix, with four effects:
> FILTR, GATE, ECHO, FLANG. The **glitch effects** (hold FX) now hit only the FEED you've selected, plus any whose black
> key you hold. New white-key layout, and four USER keys that each fire two effects. Section 5 below has it all.
>
> **New in 0.9.5:** the speech is gone (NUMBERS and PHANTOM speaking). A song that used them loads, with those sounds
> on MORPH instead. GENESIS, ANALOG and SID are back on the EDIT layer's keys.

---

## 0. Install

**You need:** an FM-1 on M-VAVE's official **V15** (keep that `FM-1.fwsc` from m-vave.com/download), a USB cable,
and `phantom-tracker-0.10.fwsc`.

1. **Back up first.** 0.10 doesn't load songs, projects or patterns saved before it (Dungeon Tracker's, Phantom
   Cosmonaut's or Phantom Tracker 0.9's): they show as empty. Save anything you care about with the web editor's backup.
2. **Install over USB**, from a checkout of the repository (needs `pip3 install mido python-rtmidi`):
   ```
   python3 tools/fm1_install.py phantom-tracker-0.10.fwsc
   ```
   Or make a local copy of the web installer and use it in Chrome or Edge (BUILDING.md, *Install*).
3. **The way back:** install M-VAVE's `FM-1.fwsc` the same way. If a build ever misbehaves, hold **OCT− + OCT+** for
   5 seconds for UPDATE MODE; two failed boots in a row enter it by themselves. If the FM-1 can't reach either, recovery
   needs [FM-1 Transporter](https://github.com/kurogedelic/FM-1-transporter).

---

## 1. Listen to the transmission

Switch on. HOME is a **starfield**, with the telemetry under it: `FEED 01 > CAPSULE` and `[STANDBY]`.

**Press PLAY.** Five FEEDs come in, two bars in D minor:

| FEED | Sound | Plays |
|---|---|---|
| 01 | NOISE CAPSULE (the drum kit) | the beat |
| 02 | PERC: GLITCH HOP's CLAP and SHAKER | the shaker on the off-beats, left and right in turn |
| 03 | DEBRIS GLASS PAD | the chords: Dm, B♭, C (its keys play chords: CHORD KEYS, below) |
| 04 | DEBRIS ROUND BASS | the pulse on D, then B♭ and C |
| 05 | FM CRYSTAL BELL | a few high blips, far apart |

`[RECEIVING]` means it's playing; **HBT** is the tempo (**GLO + SELECT** changes it). The stars fly faster while it plays.

---

## 2. The panel at a glance

| Control | Tap | Hold |
|---|---|---|
| **HOME** | HOME (tap again: MIXER, CLOCK) | the MENU |
| **EDIT** | the FEED's sound pages and its oscilloscope | the EDIT layer: white keys 1–9 pick the engine, **OCT+ SCRAMBLE**, **OCT− undo** |
| **FX** | the **FX PAD** (the selected FEED) | the glitch layer on the whole mix: **ATTRITION** on KNOB 1–4, effects on the white keys, black keys switch FEEDs dry / back |
| **LFO** | **RELAY LOCK** on / off | (0.6 s, let go) the LFO pages · turn a knob while holding: MOMENTARY |
| **SEQ** | the **GRID** (the tracker; again: AUTOMATION) | SEQ TOOLS (LEN, DIV, SWING ..) |
| **ENV** | the envelope pages | |
| **GLO** | **PATTERNS** (FREQ A–F), tap again: **ORBIT**, the song chain | the FEED levels, mutes, tap tempo · + SELECT: the tempo |
| **ARP** | the arpeggiator | KNOB 1–4 ride the FEED's INSERT effect |
| **SCL** | the scale keyboard (QNT, ROOT, SCALE, TRN) | |
| **SAVE** | its pages: USER, PRESETS, PHRASES, PROJECT (the song slots), TOOLS | undo the last load |
| **REC** | arm the FEED (and record knob moves, or print the FX PAD) | |
| **PLAY** | play / stop | |

| Knob | Does |
|---|---|
| **SELECT** | the pages of the button you're in; in the MENU, every row |
| **ALGORITHM** | the FEED, 01–05 (the keys play it), stopping at each B side (3B, 4B, 5B: you edit B); past the last on HOME: **PHANTOM** (the keys play it) |
| **PRESETS** | on HOME: every sound of every engine (DRUMS and PERC: the kits); SAVE > PRESETS > LIST narrows it to FAV or one category if you like |
| **KNOB 1–4** | SCAN, WARP, FOLD, CLARITY on HOME (GENESIS ANALOG SID: their own four, below); whatever the page shows elsewhere |
| **OCT+ / OCT−** | the octave; on the MIXER and HOME TRACKS / LEVELS: the strip's page 2 / 1 (the selection stays) |
| **MASTER** | the volume |

---

## 3. Play PHANTOM over it

**Turn ALGORITHM past FEED 05** (on HOME): `PHANTOM > SIG 01 CARRIER`. Now **play the keys.** That's PHANTOM, the
jam voice: a theremin that slides from note to note whenever you hold one key over another. It follows FEED 05's
scale and the octave buttons, never steals a FEED's voice and is never recorded. Its notes also go out over USB MIDI
on channel 6 (MENU > MIDI > PHANTOM CH).

While it's focused, **PRESETS** picks its SIGNAL, **KNOB 1–4** contort it, and **EDIT** flips the knobs to GLIDE,
VIBRATO, SUB and **LEVEL** (how loud it plays over the loop; saved with the project). **Turn ALGORITHM back** (or open any page) and the keys play the selected FEED again.

> **The keys play PHANTOM only while it's focused.** On any FEED they play that FEED's sound, so you can audition
> sounds anywhere, and record a FEED live from HOME (tap REC, then play).

---

## 4. Contort a FEED

Pick a FEED with **ALGORITHM**, then on HOME:

| Control | Does |
|---|---|
| **PRESETS** | whole sounds, any engine's. The SIGNAL itself, SIG 01–16 (from CARRIER to DARK SIDE; on DEBRIS also SIG 17–20, four recorded tapes), is **EDIT**'s first page, KNOB 1 |
| **KNOB 1 SCAN** | moves through the SIGNAL's frames |
| **KNOB 2 WARP** | bends how it's read |
| **KNOB 3 FOLD** | folds it back on itself |
| **KNOB 4 CLARITY** | left of centre a low-pass, right of centre static |

- **Hold EDIT, press OCT+** to **SCRAMBLE** all four knobs (press again for another throw); **EDIT + OCT−** takes it back.
- **Hold EDIT, press white key 1–9** to change the FEED's engine: **MORPH** (the wavetable), **DEBRIS** (grains of
  the SIGNAL, each folded: FOLD goes warm, hollow, glassy; CLARITY's right side shimmers an octave up), **FM** (one
  frame bending another), **GENESIS**, **ANALOG**, **SID** (Dungeon Tracker's, below), **GLORP** (the frog synth), **VULT** (comb and ladder), **NOISE** (the drum kit).
- **GLORP:** every note opens a wah-like mouth and closes it again, wider the harder it's played. KNOB 1 **VOWEL**
  (where it opens to), KNOB 2 **WAH** (how far), KNOB 3 **SPEED** (how fast it closes), KNOB 4 **GLOOP** (resonance and
  drive). The FM-1's keys have one velocity (SELECT sets it), so the GRID's step velocities and accents, or a MIDI
  keyboard, are where the soft/hard difference comes from; EDIT 2's **SENS** at 0 opens every note like a firm one.
- **VULT:** KNOB 1 **CUTOFF**, KNOB 2 **RESO** (it sings by itself near the top), KNOB 3 **COMB** (a metallic ring tuned
  to the note), KNOB 4 **DRIVE**. EDIT 2: **SHAPE** (the oscillator's sweep), **TONE** (left hollow, right bright),
  **TUNE** (the comb's pitch), **KEYTRK**.
- **EDIT > VOICE > VOICES:** how many notes the FEED plays at once: AUTO (the sound's own: 1 for basses, leads, plucks
  and FX, 4 for pads and keys, 2 otherwise), MONO, 2, 3 or 4. The three synth FEEDs share six voices: ask for more
  and they're shared out in proportion.
- **Drum kits:** on FEED 1, PRESETS steps through 40 synthesized kits: among them **GLITCH HOP**, **CHILLWAVE**,
  **TAPE DECK**, **WONK**, then ten on the engines' voices: **SIXTEEN BIT** (FM), **BREADBIN** (chip), **RESONATOR**
  (comb), **POND** (bloops), **GLITCHWAVE**, **VAPOR FM**, **ARCADE**, **FROGBEAT**, **FOUNDRY**, **TIN CAN**, then ten
  after real kits: **FM ROCK**, **FM JAZZ**, **FM FUNK**, **FM EIGHT**, **FM NINE**, **SID ROCK**, **SID EIGHT**,
  **SID NINE**, **SID JAZZ**, **SID BREAKS**.
- **EDIT > MODEL** (a kit): each drum voice's model: **NOISE**, **FM**, **CHIP**, **COMB** (a ringing comb, like VULT's)
  or **BLOOP** (GLORP's mouth on a hit). The voice's PITCH DECAY COLOR SWEEP SHAPE then shape that model.

---

### Dungeon Tracker's engines: GENESIS, ANALOG, SID

These three are back from Dungeon Tracker as they were. They have no SIGNALs: on HOME, **PRESETS steps sounds** as on
every engine (the same list as the SAVE > PRESETS page), and **KNOB 1–4** turn each one's own four:

| Engine | What it is | KNOB 1–4 on HOME | Sounds |
|---|---|---|---|
| **GENESIS** (key 4) | four-operator FM after the Mega Drive's YM2612 | FB (feedback), OP1 LEVEL (the brightness), DTUNE, GRIT (the DAC's steps) | 24: GEN BASS, SLAP BASS, GEN PIANO, CLAV, ORGAN, HARP, HERO LEAD, BRASS, STRINGS, CHOIR .. |
| **ANALOG** (key 5) | a subtractive synth: two oscillators, noise, a resonant filter | CUT, RES, ATTACK, RELEASE | 25: SAW LEAD, SQR BASS, ACID, MOOG BASS, REESE, SUPERSAW, DARK PAD, SWEEP PAD .. |
| **SID** (key 6) | after the Commodore 64's sound chip: pulse width, sync, ring, its drums | PW, CUT, RES, SWEEP | 46: MONTY BASS, SYNC LEAD, C64 ARP, SID KICK, SID SNARE, TITLE SCREEN, ARCADE LEAD, LASER .. |

EDIT opens their two pages (GENESIS: ALG FB MUL1–4 DTUNE GRIT; ANALOG: WAVE DTN MIX NOIS CUT RES DRV KTR; SID: WAVE
PW PWM MOD TUNE SWEEP CUT RES). Everything else works the same on them: the FX PAD, ATTRITION, INGEST, the GRID, CHORD
KEYS, VOICES.

### Chords on the keys: CHORD KEYS

FEED 3 (the pads) powers on with **SCL > CHORD > KEYS**: the **white keys play the scale's chords** (in C: C Dm Em F G Am Bdim),
and the **black keys are held modifiers** (alone they play nothing), combined as you like:

| Hold | Makes |
|---|---|
| **C#** | the seventh (Am → Am7) |
| **D#** | sus4 (the upper D#: sus2) |
| **F#** | add9 (C# + F#: a ninth chord) |
| **G#** | major ↔ minor (Am → A) |
| **A#** | spread: open, the root an octave down |

REC records the chord into the step. The CHORD page (tap SCL twice) names the chord as it plays. Any FEED can use
KEYS (SCL > CHORD, KNOB 1); the others power on with chords OFF.

### PERC, a B side, a pan per step (0.10)

- **PERC (FEED 02):** select it (ALGORITHM) and, on HOME, KNOB 1 / 2 pick the first sound's kit and sound (any of
  the 40 kits: KICK, SNARE, RIM, CLAP, hats, CRASH, RIDE, BELL, toms, COWBELL, CLAVE, CONGA, SHAKER), KNOB 3 / 4 the
  second's (EDIT > SOURCE has the same four). On the GRID it has two lanes, named by the sounds.
- **A B side:** select FEED 03, **hold EDIT** and press the white key **B SIDE** (after CLEAN): the pad now plays a
  copy of itself an octave up. Turn **ALGORITHM** one click: you're on **3B** (the header shows `/B`), and PRESETS, the
  engine keys and the knobs change the B sound — try GLORP on it. On the MIXER there, KNOB 1 / 2 are B's level and
  pan. One more click is FEED 04 (back to A on 03). EDIT > **B SIDE** has its transpose, level and pan; **EDIT A / B**
  (the key after B SIDE) flips sides too.
- **A pan per step:** on the GRID, KNOB 2 to the **FX** column, KNOB 4 a click past the commands to **PAN** (the FEED
  remembers it after), then KNOB 3 on a row: that step's hits sit there, and each stays put while it rings, whatever
  the next step does.
- **The other strip page:** on the MIXER or HOME LEVELS, **OCT+** shows SYNTH 3, PHANTOM and the MASTER meters,
  **OCT−** goes back — the selected FEED doesn't change.

## 5. Break the signal

There are two effect layers. **Holding FX** glitches the whole mix (leave FEEDs out with the black keys). **Tapping
FX** opens the FX PAD on the FEED you've selected.

### Hold FX: glitch the mix

Everything here hits **every FEED** together, so they stutter and jump as one. **Tap a black key** (F#3 G#3 A#3 C#4 D#4
= FEED 1–5, in the order shown) to keep that FEED **dry** (its cell reads `DRY`), tap it again to bring it back: the
drums steady under a glitched bed. They stay set when you let go of FX; **FX + OCT−** clears them with everything else.
PHANTOM and the reverb and delay keep playing clean.

**ATTRITION — hold FX, turn KNOB 1–4:**

| Knob | Does |
|---|---|
| 1 STUTTER | repeats on the grid, 1/8 → 1/16 → 1/32 as it turns |
| 2 TUNNEL | closes into a tunnel with echoes |
| 3 JUMP | the FEEDs' playheads skip to other beats, and some beats play backwards |
| 4 CRUSH | bits and rate down |

Let go of FX and everything springs back. **FX + OCT+** latches it where it is; **FX + OCT−** resets.

**The white keys — hold FX, hold a key:**

| | | | | |
|---|---|---|---|---|
| **F3 G3 A3 B3** | REPEAT 1/4 | 1/8 | 1/16 | 1/32 |
| **C4 D4 E4 F4** | FREEZE | ECHO | OCT+ | OCT− |
| **G4 A4 B4 C5** | HPF | LPF | REVERB | STOP |
| **D5 E5 F5 G5** | USER 1 | USER 2 | USER 3 | USER 4 |

**USER keys fire two effects at once.** Out of the box: USER 1 = 1/16 + HPF, USER 2 = FREEZE + REVERB, USER 3 = STOP
+ ECHO, USER 4 = OCT+ + REVERB. To change one, hold FX and the USER key, then turn **ALGORITHM** (first effect) and
**SELECT** (second effect). **EDIT** puts the default pair back. Pair a repeat/reverse/stop/octave with a filter, ECHO
or REVERB: two of the repeat-type effects can't play at once (only the second does).

Any key can hold any effect: hold FX and the key, turn **PRESETS**. **EDIT** resets that key.

**Print it:** tap **REC** (armed), press PLAY, then hold FX and play: the header says `[FX] ● PRINT`, and every step
that plays is printed with ATTRITION as it is and the white-key effects you hold, on the FEEDs you're hitting (all
but those you leave out). Let go:
it springs back between the printed steps, and they replay every loop. Hold FX again with nothing up to print over
them (an empty pass erases); **FX + OCT−** with REC armed clears them all. The prints go with the pattern and the song
slots.

### Tap FX: the FX PAD on a FEED

The pad works on the **selected FEED** only (its number is in the header); **ALGORITHM** moves it to another FEED.
Two crosshairs, a knob per axis, each one effect. At the centre an effect is off; push either way for more:

| Knob | Effect | ← / ↓ | → / ↑ |
|---|---|---|---|
| **KNOB 1** | FILTR | low-pass, closing | high-pass, opening |
| **KNOB 2** | GATE | chops in 1/8s, then 1/4s | 1/16s, then 1/32s |
| **KNOB 3** | ECHO | 1/8 repeats | dotted-1/8 ping-pong |
| **KNOB 4** | FLANG | hollow | jet |

The cards show where each knob sits (`LP 53`, `1/16`, `PING`, `JET`). Leave the pad and the crosshairs reset, but
ECHO's repeats ring out. Nothing stays unless you **tap REC on the pad while playing**: each of the FEED's steps is
printed with the pad as it is, and the prints play with the pad closed. **OCT−** on the pad clears that FEED's.

---

## 6. Change the pattern without stopping

**Tap LFO: RELAY LOCK.** An amber lock replaces the play mark in the header, and the bar keeps looping exactly as it
was. Change steps on the GRID, record new ones: you see your edits, you don't hear them yet. **Tap LFO again** and
they come in on the next step.

**The GRID (tap SEQ):** KNOB 1 the row, KNOB 2 the column, KNOB 3 the value, KNOB 4 the column's second value. The
line above the footer reads the row's FX column in plain words: `07 > LOOP: BACK 2 ROWS, 3 TIMES`.

**Patterns (FREQ A–F):** tap **GLO** for the **PATTERNS** page and press one of the first six white keys. Playing, the
new one starts when the current one ends (**OCT−** changes your mind: stay). An empty key starts as a copy of the
current pattern; **OCT+ held + a key** copies the current one there, **OCT− held + a key** clears it. Tap GLO again for
ORBIT, the song.

---

## 7. Record knob moves (INGEST)

**Tap REC** to arm the selected FEED, press PLAY and turn KNOB 1–4: the moves are recorded (`[● INGEST]`, red) and play
back every loop. Play the keys while it's armed and playing to record notes into the FEED, from HOME or the GRID.
Tap REC again to disarm.

---

## 8. Save

- **The autosave** keeps your session: it writes **3 seconds after you STOP**, never while anything plays. An
  asterisk after the pattern letter (`A*`) means the latest changes aren't kept yet: stop, wait for it to go, then
  switch off.
- **Song slots A–K:** tap **SAVE**, turn **SELECT** to **PROJECT**, pick a slot with **KNOB 1**, and press **OCT+**
  to save (stopped). A slot holds the whole song: sounds, tempo, the six patterns, the FX PAD's prints, PHANTOM.
- **Undo a load:** hold SAVE.
- **Start over:** on SAVE > TOOLS, turn **KNOB 1** past CLEAR to **NEW** (a blank song) or **NEW+S** (and the
  default sounds), then **OCT+** twice. **MENU > SYSTEM > RANDOM SONG** (the last row, OCT+) rolls a new song.
  Either one undoes with SAVE held.

---

## Words on the screen

| Screen | Means |
|---|---|
| FEED 01–05 | the five tracks: DRUMS, PERC, three synths |
| SIGNAL / SIG 01–16 | the wavetables |
| GENESIS / ANALOG / SID | Dungeon Tracker's engines: FM chip, subtractive, C64 |
| HBT | the tempo |
| FREQ A–F | the six patterns |
| ORBIT | the song chain |
| GRID | the tracker |
| INGEST | recording knob moves |
| PHANTOM | the live jam voice |
| RELAY LOCK | the held bar |
| `[STANDBY]` / `[RECEIVING]` | stopped / playing |

## If something's wrong

- **No sound:** MASTER up; HOME > MIXER for the FEEDs' levels; a FEED muted on the EDIT layer (black keys, on the kit).
- **The keys don't play PHANTOM:** they only do while it's focused: on HOME, turn ALGORITHM past FEED 05.
- **The LFO page won't open:** it's a hold now (0.6 s, then let go); a tap is RELAY LOCK.
- **Too many effects, the sound breaks up:** that's the CPU; ease off the FX PAD or the INSERT effects (and tell us
  which ones: see below).
- **A glitch hits the wrong FEED:** the glitch layer follows the selected FEED; pick it with ALGORITHM before you hold FX.
- **Lost changes after switching off:** wait for the `*` after the pattern letter to go before you switch off.

## Beta feedback worth sending

1. Does it boot, and does the font look right on the screen?
2. Does the default transmission play cleanly, with no crackles? Then with all four FX PAD knobs pushed out?
3. RELAY LOCK and LFO's hold: do they feel right in your hands?
4. The glitch layer per FEED: the black keys adding FEEDs, REPEAT 1/4, the USER keys; ATTRITION's spring-back.
5. PHANTOM: the glide and its MIDI out on channel 6.
6. GENESIS, ANALOG and SID: do they sit right in the mix next to the SIGNAL engines, and does a GENESIS pad next to a
   DEBRIS pad crackle (the CPU)?
7. CHORD KEYS on FEED 3, the PATTERNS page on GLO, ATTRITION prints.
8. 0.10: the CPU with B sides on all three synths (a chord on DEBRIS with a GENESIS B side, say) and the glitch layer
   on; PERC's sounds against the kits'; per-step pan on headphones; anything that hangs.
