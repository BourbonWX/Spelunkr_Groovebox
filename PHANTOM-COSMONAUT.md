# Phantom Tracker — a groovebox tracker, firmware for the M-VAVE FM-1

Phantom Cosmonaut's look, workflow and FX with Dungeon Tracker's synth engines back: the gimmicks of each dropped
(Cosmonaut's speech, Dungeon Tracker's dungeon crawl) for one groovebox tracker (Ed, Nov 2026). Four sequenced FEEDs
on nine engines, a live PHANTOM channel on the keys, wavetable SIGNALs you scan, warp and fold by hand, and glitches
that tear the signal apart. (This file keeps its old name, PHANTOM-COSMONAUT.md, so links to it still work.)

**Status: v0.9.15 beta (flash at your own risk).** From Phantom Cosmonaut 0.9.4 (milestones M0–M7, part of M8): the
terminal look (M1), MORPH and its 16 SIGNALs with the four contort knobs (M2), PHANTOM, the live jam voice (M3), the
engines DEBRIS (folded granular), FM and the NOISE kit (M4), the FX PAD (M5), ATTRITION, the jitter and HOME's
starfield (M6), the finishing pieces (M7): VOICES, SCRAMBLE, RELAY LOCK, PHANTOM on MIDI, the default transmission;
CHORD KEYS, the PATTERNS page and ATTRITION prints (0.9.4). New in 0.9.5: **GENESIS, ANALOG and SID back** from
Dungeon Tracker, **the speech gone** (0.9.4's NUMBERS and PHANTOM speaking; their sounds load on MORPH), and the name.
New in 0.9.6 (Ed: "flip how we're doing the two FX options"): **the FX PAD is the whole mix's** (FILTR GATE ECHO
FLANG, a knob each) and **the glitch layer works on the FEEDs you target** (the selected one; the black keys add
others), with a new key layout and four USER keys that each hold two effects. New in 0.9.7: the sampled drum sets
are gone (66 KB freed) and four synthesized kits take their places: GLITCH HOP, CHILLWAVE, TAPE DECK, WONK.
New in 0.9.8: **GLORP**, the frog synth: a vowel wah on every note that opens as hard as the note is played.
0.9.9: **newer FM-1s save again** (their UC25HQ80 flash chip is recognised, as in Felucca 1.8.2; before, they ran
without saving and could not update to another firmware but through the official one).
New in 0.9.10: **VULT**, built from Vult's open DSP: a phase-distortion oscillator into a resonant comb and a diode ladder.
New in 0.9.11: **the kit's voices take the engines' flavours** (each lane's MODEL: FM, CHIP, COMB, BLOOP) and **ten new
kits** use them. New in 0.9.12: **ten kits after real ones**, five on FM, five on CHIP. 0.9.13: **the FM cymbals rebuilt**: hats, rides and
crashes each their own sound, and each FM kit its own cymbals. 0.9.14: **GENESIS fixed**: half its presets played
static (their operators were never loaded since 0.9.5); a GENESIS sound saved with 0.9.5–0.9.13 keeps the static:
step PRESETS off it and back. **0.9.15: the FX flipped back** (Ed: "the vibe of a track falls apart when the drums
are randomly hopping and the bass and keys are hopping differently"): the FX PAD is each FEED's again and the glitch
layer is the whole mix's, a black key leaving its FEED out; **the drums louder** (the kit 6 dB up, every other sound
4 dB down in the mix); **PHANTOM quieter, with a LEVEL knob** (EDIT, KNOB 4).
New here? Start with the [quick start](docs/QUICKSTART.md). The plan: [the spec](docs/PHANTOM-COSMONAUT-SPEC.md).
Everything not described here still works as in Dungeon Tracker: its manual,
[docs/DUNGEON-TRACKER-1.5.md](docs/DUNGEON-TRACKER-1.5.md), covers the sequencer, the tracker, the FREQ bands (its
pattern bank), ORBIT (its song mode), the effects and saving.

Phantom Tracker (Phantom Cosmonaut until 0.9.4) is a fork of [Dungeon Tracker](https://github.com/BourbonWX/Dungeon-Tracker)
1.5 by Bourbon Weather, itself a fork of [Felucca](https://github.com/hugelton/Felucca) 1.6.1 by Leo Kuroshita
(Hügelton Instruments). GPL-3.0-only. Not affiliated with or endorsed by M-VAVE.

## The words

| On screen | Means |
|---|---|
| **FEED 01–04** | the four sequenced tracks (each its own sound, steps and insert effect) |
| **SIGNAL 01–16** (SIG 07) | the wavetables the SIGNAL engines read (MORPH, DEBRIS, FM) |
| **SCAN WARP FOLD CLARITY** | KNOB 1–4 on HOME: the contort knobs |
| **HBT** | the tempo (heartbeat) |
| **FREQ A–F** | the six patterns (GLO > PATTERNS: one of the first six white keys) |
| **ORBIT** | the song chain (GLO tapped twice) |
| **INGEST** | recording knob moves (REC armed while playing) |
| **PHANTOM** | the jam: a voice of its own on the keys, never recorded |

## The look (M1)

- **Palette PHANTOM** (the default; MENU > DISPLAY > COLOR has the others): charcoal, phosphor green for values, pale
  green text, signal amber for the one active thing (cursor, playhead, hot knob), alarm red only for INGEST.
- **A pixel font** (DotGothic16) on every screen, set on its own pixel grid so the values and titles stay crisp.
- **HOME is the intercept terminal:** the focused FEED and its sound (`FEED 02 > SIG 07 SOLAR WIND`), the state
  (`[STANDBY]` stopped, `[RECEIVING]` playing, `[● INGEST]` in red while the FEED records), the tempo and the FREQ
  band (`HBT 124 FREQ A`; in ORBIT `ORB 2/6`), under the starfield (M6, below). MENU > DISPLAY > HOME SCOPE puts the
  output's scope back instead, under the lines.
- **The names on the pages:** GLO's page is ORBIT, SEQ's tracker is GRID, the tempo is HBT (HOME > CLOCK, MENU >
  CONTROL > SELECT, HBT LOCK), messages say FREQ and ORBIT, MENU > MIDI's channels are FEED 1–4 CH.

## MORPH and the SIGNALs (M2)

MORPH is the first of the SIGNAL engines and PHANTOM's (at M2 FEEDs 2–4 started on it; they start on DEBRIS and FM
now).

- **The SIGNAL is KNOB 1 of the engine's first EDIT page** (SIGNAL), on every SIGNAL engine (MORPH, DEBRIS, FM):
  SIG 01–16. (Until 0.10.3 PRESETS on HOME stepped it and stopped at SIG 16; since 0.10.4 PRESETS on HOME steps
  whole sounds on every engine: each of MORPH's 16 is one signal set up for its role.)
- **Each signal is eight frames**, single cycles that sweep somewhere: a sine opening into a saw (CARRIER), a pulse
  narrowing (PULSAR), a sine through an ever coarser converter (TELEMETRY), vowels in a helmet (VOSTOK), an organ
  warming up (COLD START), a hard-sync tear (ORBIT DECAY), noise drifting up the spectrum (SOLAR WIND), a moving comb
  (IONOSPHERE), a glassy cluster climbing (DEAD STAR), a saw buried in static (STATIC), metal under stress (HULL
  GROAN), binary spectra (NUMBERS), a breathy voice (LAST WORDS), the beacon (SPUTNIK), phase distortion (RELAY) and
  a wavefolder (DARK SIDE). They are band-limited, so high notes stay clean, and 8-bit, for a cold digital edge.
- **The contort knobs** (KNOB 1–4 on HOME, EDIT's first page too):
  - **SCAN** moves through the signal's frames, cross-fading neighbours. SENV (EDIT's second page) moves it with the
    envelope.
  - **WARP** reads the frame at a bent phase: brighter, a little grit.
  - **FOLD** folds the wave back on itself: metallic.
  - **CLARITY** is one knob both ways: left of the middle a low-pass closes (RES its resonance), the middle is open,
    right of it the signal breaks up into static (fewer bits, a lower sample rate, noise).
  - SUB (EDIT's second page) adds a sine an octave down.
- **INGEST:** with REC armed and the sequencer playing, turning KNOB 1–4 records the moves into the FEED, as any knob
  in Dungeon Tracker. Stopped, the sound's own values come back; played again, the moves play back.
- **Locks per step:** SCAN, WARP, FOLD and CLARITY can be locked on a step like any parameter (SEQ > AUTOMATION, as in
  Dungeon Tracker). Locks and recorded moves are saved with the song.
- **EDIT** on a MORPH FEED shows the synth lab: the signal's number and name over a large oscilloscope of the cycle
  as SCAN, WARP, FOLD and CLARITY shape it, redrawn as you turn, and the four values under it.

## The engines (M4; DEBRIS in 0.9.3; GENESIS ANALOG SID back in 0.9.5; GLORP in 0.9.8; VULT in 0.9.10)

Nine engines: three reading the 16 SIGNALs, three from Dungeon Tracker, GLORP, VULT and a drum kit. PRESETS' ENG picks
among them, as do the EDIT layer's first nine white keys: **1 MORPH, 2 DEBRIS, 3 FM, 4 GENESIS, 5 ANALOG, 6 SID, 7 GLORP,
8 VULT, 9 NOISE**. On
the SIGNAL engines **KNOB 1–4 are SCAN WARP FOLD CLARITY**, with a meaning of its own on each; EDIT shows the
oscilloscope and the other four. The EDIT layer's **No.** (KNOB 2) steps through every sound of the engine one by one
(0.9.4: the PRESETS LIST's ROLE filter no longer applies there).

| | MORPH | DEBRIS | FM |
|---|---|---|---|
| **What** | two neighbouring frames cross-faded | grains of the signal, each folded through a sine: FOLDER and GRANULAR as one | one frame (the modulator) bending the read of another (the carrier) |
| **SCAN** | the frame | where the grains read | the modulator's frame |
| **WARP** | phase distortion | the grains' size (17 to 274 ms) | the index (how far it bends) |
| **FOLD** | a folder | the fold, x1 to x4: warm, hollow, glassy | feedback (to the edge of noise) |
| **CLARITY** | left a low-pass, right static | left a low-pass, right **SHIMMER** (grains an octave up) | left a low-pass, right static |
| **EDIT's other four** | RES, SENV (the envelope moves SCAN), SUB | SCATTER (grains jump and fold their own way), FENV (the envelope moves FOLD), DRIFT (a chorus: each grain a few cents off) | RATIO (the modulator's, 0.5 to 8 and four bell ratios), CAR (the carrier's frame), MENV (the envelope moves the index) |
| **Sounds** | 16, one a SIGNAL | 12: GLASS PAD, ROUND BASS, MUSIC BOX, SLOW DRONE, HALO LEAD, GHOST CHOIR, WRECK PAD, FOLD BASS, GRAIN PAD, TIDE KEYS, VENT HAZE, SKYWAVE | 10: CRYSTAL BELL, ICE TINE, GLASS PAD .. |

- **DEBRIS** (Ed: "combine folder and granular into one", then "less harsh"): two grains read the SIGNAL half a grain
  apart under smooth windows, each starting in phase with the note, so they never fight; each is folded through a sine
  on its way out. FOLD stops at four folds, and the fold never aliases: there is **no static anywhere in its range**,
  only tone. FOLD at 0 is plain granular; WARP all the way up is long grains, near a plain folder. Past SIG 16 DEBRIS
  reads the four factory tapes: **SIG 17 GROAN** (bowed sheet metal), **18 VENT** (air handling), **19 HETERODYNE**
  (two carriers beating), **20 SKYWAVE** (a breathy, voice-like hum); middle C plays a tape at its own speed, SCATTER
  moves the grains around it. FOLDER's and GRANULAR's harsh ends are gone.
- **GENESIS, ANALOG, SID** (0.9.5, Ed: "I miss the synth engines we had in Dungeon Tracker"): back as they were in
  Dungeon Tracker 1.5, their sounds and pages unchanged (GENESIS's presets load their four operators' envelopes and
  levels, OPS: until 0.9.14 they did not, and 12 of the 24 sounded as static). They have no SIGNALs, so **on HOME PRESETS steps their
  sounds** and **KNOB 1–4 turn each one's own four**:

  | | GENESIS | ANALOG | SID |
  |---|---|---|---|
  | **What** | four-operator FM after the YM2612 (the Mega Drive's): eight algorithms, feedback, the DAC's grit | subtractive: two oscillators (SAW SQR TRI SIN PWM SYNC SUB), noise, a resonant filter (LP / BP / HP) with drive | after the MOS 6581 (the C64's): TRI SAW PLS NSE and the combined waves, pulse width and its LFO, SYNC and RING, a pitch SWEEP, the filter; DRM for its drums |
  | **KNOB 1–4 on HOME** | FB, OP1 LEVEL (the brightness), DTUNE, GRIT | CUT, RES, ATTACK, RELEASE | PW, CUT, RES, SWEEP |
  | **EDIT's pages** | FM: ALG FB MUL1 MUL2; OPS: MUL3 MUL4 DTUNE GRIT | OSC: WAVE DTN MIX NOIS; FLT: CUT RES DRV KTR | OSC: WAVE PW PWM MOD; MOD: TUNE SWEEP CUT RES |
  | **Sounds** | 24: GEN BASS, SLAP BASS, SUB THUMP, ROCK BASS, GEN PIANO, CLAV, ORGAN, MUSIC BOX, HARP, MARIMBA, SITAR, GUITAR, SQUARE LEAD, SAW LEAD, HERO LEAD, SYNC SCREAM, FLUTE, HARMONICA, BRASS, STRINGS, CHOIR, WARM PAD, BELL PAD, LASER | 25: SAW LEAD, SOFT PAD, SQR BASS, PWM STR, ACID, SINE KEY, RAVE, SUB BASS, PLUCK, BRASS, WIND, STRINGS, MOOG BASS, SYNC LEAD, PWM BASS, REESE, SUPERSAW, POLY KEYS, BLIP, DARK PAD, SWEEP PAD, ACID 2, SUB SWEEP, NOISE HIT, SOLO SQUARE | 46: basses (MONTY BASS, SYNC BASS, FUNK BASS ..), leads (SYNC LEAD, ARCADE LEAD, SID SOLO ..), drums (SID KICK, SNARE, HAT, TOM, CLAP, COWBELL, BOOM), pads, keys and effects (ZAP, LASER, EXPLOSION, TELEPORT, ALARM) |

  EDIT shows the oscilloscope. Everything else works on them as on the SIGNAL engines: the FX PAD, ATTRITION and its
  prints, INGEST, locks, the GRID, CHORD KEYS, VOICES, SCRAMBLE.
- **CHORD KEYS** (0.9.4; SCL > CHORD, KNOB 1: **KEYS**; FEED 2 powers on with it): the **white keys play the diatonic
  chords** of the FEED's scale (C major: C Dm Em F G Am Bdim; a chromatic scale plays C major's), and the **black keys
  are held modifiers**, playing nothing alone, combined freely: **C# the seventh** (in key: Am7, G7, Cmaj7), **D# sus4**
  (the upper octave's D#: sus2), **F# add9** (C# + F#: a ninth chord, G9, Am9), **G# major ↔ minor**, **A# spread**
  (open, the root an octave down). REC records the chord into the step, as any chord; MONO plays its root. The CHORD
  page names it as it plays. FEED 2's VOICES AUTO is four on KEYS (three otherwise).
- **NOISE** is FEED 1's drum kit, synthesized, no samples: KICK a sine swept down (COLOR a knock), SNARE two sines
  under noise, HAT noise held at a rate (gritty; SHAPE coarser), PERC a sine rung against another (a tom to a clank).
  The controls are GEN KIT's (PITCH DECAY COLOR SWEEP SHAPE LEVEL per voice; TUNE DECAY COLOR OPEN CLICK VELO DRV
  VARI for the kit), as are the drum notes and the EDIT layer's mutes; 16 kits (CAPSULE, LOW ORBIT, HARD DOCK ..).
- **Louder since 0.9.15** (Ed: "all of our drums need to be increased in volume as they are very quiet in almost every
  mix"): every kit plays 6 dB louder, and every other sound (the other FEEDs, PHANTOM) sits 4 dB lower in the mix, so
  the drums come up 10 dB against the rest at the same LEVELs. The overall loudness hardly moves: chords used to drive
  the master limiter hard, pulling the drums down with them. A song you balanced by turning FEED 1 up and the rest
  down will now have the drums far too loud: bring FEED 1 back towards its default LEVEL (-4 dB).
- **20 kits, all synthesized (0.9.7):** the last four are new, for glitch hop meets chillwave: **GLITCH HOP**
  (punchy, driven, crisp hats), **CHILLWAVE** (soft round kick, roomy snare, dark hats, chorus and reverb),
  **TAPE DECK** (lo-fi and dusty) and **WONK** (metallic, clicky, driven). They take the places of 0.9.2's sampled
  factory sets DERELICT, SCRAP, SHORTWAVE and DEAD AIR, scrapped to free 66 KB (Ed: "scrapping our four sampled drum
  sets"). A song that used a sampled set plays its kit on the synthesis; DEBRIS keeps its four tapes (SIG 17–20).
- **Each lane's MODEL (0.9.11)** (Ed: "engine flavoured voices plus kits that use them ... 10 usable but also unique
  bespoke kits"): EDIT > **MODEL** sets how each of the kit's four voices (KICK SNARE HAT PERC) is made. The lane's
  PITCH DECAY COLOR SWEEP SHAPE keep their places and take the model's meaning:
  - **NOISE**: the synthesis above (every kit before 0.9.11, and older songs).
  - **FM**: two operators, as GENESIS and FM: SHAPE the modulator's ratio (the hat's eight inharmonic ones), COLOR the
    index, which falls faster than the hit; the snare keeps its noisy wires.
  - **CHIP**: SID's drums: SHAPE's first frames are noise, then the tone, its pitch stepping a frame (10 ms) at a time;
    COLOR from a triangle to a narrowing pulse; 32 volume steps, 7 bits. The hat: noise at its pitch, SHAPE past the
    middle a short loop (metallic).
  - **COMB**: VULT's resonant comb, struck (the kick by one period of a sine, the others by a burst of noise): COLOR
    how long it rings, SHAPE its tone (left hollow, right bright), PITCH and SWEEP its tuning.
  - **BLOOP**: GLORP's mouth on a hit: a buzzing pulse through a resonant band that opens from OO and closes again;
    COLOR how far it opens, SHAPE the resonance, SWEEP the dip.
- **The ten kits on them (0.9.11):** **SIXTEEN BIT** (all FM), **BREADBIN** (all CHIP), **RESONATOR** (all COMB),
  **POND** (all BLOOP), **GLITCHWAVE** (NOISE kick, FM snare, CHIP hats, COMB perc), **VAPOR FM** (soft FM kick and hats,
  roomy snare, BLOOP perc), **ARCADE** (CHIP with an FM bell), **FROGBEAT** (glorpy snare and perc), **FOUNDRY** (FM kick
  and hats, COMB snare and perc: metal) and **TIN CAN** (CHIP kick and hats, COMB snare, BLOOP perc).
- **Ten real kits (0.9.12)** (Ed: "five more genesis kits that try to emulate actual drum kits and five more SID kits
  that do the same"): all four voices FM (GENESIS's way) or all CHIP (SID's way), tuned like the real thing (a kick
  around 50–75 Hz, a snare around 190 Hz, toms on PERC, the open hat and cymbals ringing on, a little VARI for a
  human feel): **FM ROCK**, **FM JAZZ** (brushy snare), **FM FUNK** (tight, snappy), **FM EIGHT** (the classic
  long-boom drum machine), **FM NINE** (the punchy one with a click), and **SID ROCK**, **SID EIGHT**, **SID NINE**,
  **SID JAZZ**, **SID BREAKS** (a funk / hip-hop break kit). PRESETS on FEED 1 steps through all forty kits.
- **The FM cymbals (0.9.13)** (Ed: "the rhythmic dings ... sound so similar across each of those kits ... some variety
  of high hats, ride cymbals, and crashes"): the FM model's HAT voice is the YM2612's cymbal: a modulator fed back on
  itself into noise, a carrier at SHAPE's inharmonic ratio, the noise bending the carrier too, and a sizzle. The note
  picks the recipe: **hats** (closed, pedal, open) bright and noisy; a **ride** (GM 51, 59) a ping over a little wash
  that outlasts its hit; a **crash** (49, 52, 55, 57) all wash, its metal held. COLOR how much metal and sizzle, PITCH
  how bright, SHAPE the metal's ratio, OPEN how long the open hat and the cymbals ring. Each FM kit its own: ROCK
  bright and trashy, JAZZ dark with a warm pinging ride, FUNK crisp and tight, EIGHT metallic and short, NINE the
  brightest. The 0.9.11 kits' FM hats (SIXTEEN BIT, VAPOR FM, FOUNDRY) play the new voice too.
- **GLORP (0.9.8), the frog synth** (Ed: "the viral glorpy frog synth", "an almost automatic wah effect that opens
  and closes based on how hard the keys are pressed"). Every note is a mouth: it rests on **OO**, opens towards a
  vowel and closes back, by itself. **The harder the note, the wider and the slower it opens**, and the hardest open
  past their vowel; the pitch dips into the note from above. A buzzing voice with a saw in it through three vowel
  resonances, driven: wet, throaty, gloopy. HOME's knobs: **KNOB 1 VOWEL** (where it opens to: OH, AH, EH, EE),
  **KNOB 2 WAH** (how far: 0 holds OO), **KNOB 3 SPEED** (how fast it closes), **KNOB 4 GLOOP** (the resonance and the
  drive). EDIT 2 (THROAT): **DIP** (the pitch's drop into the note), **BUZZ** (the saw under the voice), **SENS** (how
  much the velocity matters: 0 opens every note like a firm one) and **ATTACK** (how fast it opens). **The FM-1's own
  keys have one velocity** (SELECT sets it), so played live every note opens alike; the velocity comes from the
  GRID's steps (their VEL, accents) and from a MIDI keyboard. Eight sounds: GLORP (Ed's pick, sketch F), FROG BASS,
  TOAD, GLOOP LEAD, BUBBLE KEYS, WAH CHORDS, OOZE, SWAMP TALK. MIDI: CC74 VOWEL, CC71 GLOOP.
- **VULT (0.9.10)** (Ed: "add the Vult engine"): a voice built from the open DSP of [Vult](https://github.com/vult-dsp/vult)
  (Leonardo Laguna Ruiz's DSP language, its examples MIT), ported to the FM-1's fixed point and checked against Vult's
  own renders. A **phase-distortion oscillator** (a sine racing ahead of the note and restarting with it: CZ's
  resonance sweep) into a **resonant comb** tuned to the note, then Vult's **diode ladder** (self-oscillating at the
  top) and its soft saturation. HOME's knobs: **KNOB 1 CUTOFF**, **KNOB 2 RESO**, **KNOB 3 COMB** (the comb's
  feedback: 0 off, up to a metallic ring), **KNOB 4 DRIVE** (into the ladder's clippers). EDIT 2 (COMB): **SHAPE** (the
  phase distortion's sweep, 0 to 60 semitones), **TONE** (the comb's colour: right brighter, left hollow and
  odd-harmonic), **TUNE** (the comb's pitch against the note, -24..+24 semitones), **KEYTRK** (the cutoff following
  the note). The FEED's ENV / LFO -> FILTER move the cutoff. Eight sounds: LATERAL BASS, RESO PLUCK, ACID VULT,
  METAL COMB, CZ SWEEP, HOLLOW PAD, DUST KEYS, SQUELCH LEAD. MIDI: CC74 CUTOFF, CC71 RESO.
- **Power-on:** FEED 1 NOISE CAPSULE on the beat, FEED 2 DEBRIS GLASS PAD, FEED 3 DEBRIS ROUND BASS, FEED 4 FM
  CRYSTAL BELL, playing the default transmission (below); PHANTOM is MORPH's CARRIER.
- **Every other engine is gone** (Dungeon Tracker's GEN KIT, WAVES, MOD, MOD KIT and the ones it had retired; Phantom
  Cosmonaut's FOLDER and GRANULAR since 0.9.3; its VOX and NUMBERS since 0.9.5). A song or saved sound that used one
  loads on the nearest of these by its kind: a bass a bass, a pad a pad, a kit the NOISE kit (FMTONE's sounds on
  GENESIS as in Dungeon Tracker 1.5; TRIO's and CHIP's on SID; FOLDER's, GRANULAR's and grain sounds on DEBRIS; FM6's
  and the organs on FM; VOX, NUMBERS, Felucca's VOICE and the rest on MORPH). **It sounds different**: a Phantom
  Cosmonaut 0.9.4 song plays its NUMBERS FEEDs on MORPH's sound of the same kind. A Dungeon Tracker song's GENESIS,
  ANALOG and SID FEEDs play as they did there.
- **RIDE** (the FX layer) brightens every FEED: the kit's COLOR, MORPH's WARP and FOLD, DEBRIS's FOLD, FM's index.
- **Favourites** (PRESETS' FAV) work on all seven.

## The FX PAD (M5; the FEED's again since 0.9.15)

A Kaoss Pad on a FEED: **tap FX** and the pad opens on the **selected FEED** (the number in the header); **ALGORITHM**
moves it to another FEED (its crosshairs back at the centre). 0.9.6–0.9.14 put it on the whole mix; 0.9.15 gives it
back to each FEED (Ed: "the X/Y being per track"), keeping 0.9.6's four effects, each crosshair playing two at once,
one on each axis:

| Knob | Effect | Left / down of centre | Right / up of centre |
|---|---|---|---|
| **KNOB 1** (crosshair 1's X) | **FILTR** | a low-pass closing, down to ~80 Hz | a high-pass opening, up to ~5 kHz |
| **KNOB 2** (crosshair 1's Y) | **GATE** | the FEED chopped in 1/8s, then 1/4s further down | 1/16s, then 1/32s further up |
| **KNOB 3** (crosshair 2's X) | **ECHO** | 1/8 repeats | dotted-1/8 ping-pong (left, right, ..) |
| **KNOB 4** (crosshair 2's Y) | **FLANG** | hollow (negative feedback) | a jet (positive feedback) |

- **Only that FEED** is touched: filter the drums and the bass plays on as it was. ECHO is one echo that each FEED
  sends into by its own crosshair, so only the FEEDs you put it on repeat (its time and feedback follow the one sending
  the most). A SPREAD chord narrows towards mono as FILTR or FLANG comes in.
- **At the centre each is off**, and the further a knob goes from it, the stronger the effect: deeper GATE, more ECHO
  feedback (up to 0.85), more FLANG feedback, the filter further closed or open. GATE and FLANG keep time with the
  tempo (FLANG sweeps over two bars); ECHO's time follows the tempo and glides when it changes, like a tape.
- **The cards** say where each knob is: `LP 53`, `1/16`, `PING`, `JET` (`--` at the centre).
- **Playing changes nothing:** an axis sounds once you turn its knob, and when you leave the pad, open it again or move
  it to another FEED, the crosshairs go back to the centre. ECHO's repeats **ring out** for a few seconds after.
- **PRESETS** on the pad just names the four (`FILTR GATE ECHO FLANG`).
- **REC prints:** tap REC on the pad and it says `[● PRINT]` (it starts the sequencer if stopped). Every step of the
  FEED's that plays is written with the pad as you have it; turn the knobs as it plays to print a movement. Tap REC
  again, or leave the pad, to stop. Printed steps are lit under the pad (the FEED's steps). Each FEED keeps its own.
- **Alive with the tempo:** a ring pulses out of each crosshair on every beat (wider on the bar); crosshair 1 blinks
  on the sixteenths while GATE is on, crosshair 2 throws rings at ECHO's time and wobbles while FLANG is on.
  Stopped, it holds still (and MENU > ANIM OFF keeps it still).
- **Printed steps play** with the pad closed, every loop. With the pad open on that FEED and touched, your hands play
  over them.
- **OCT- on the pad** clears that FEED's prints (ATTRITION's and the other FEEDs' stay; the octave too).
- **Saved with the pattern:** prints belong to a FREQ band's pattern like its automation, and go with songs and
  projects. Up to 80 printed steps per pattern over the four FEEDs, shared with ATTRITION's ("PRINTS FULL" past that).
  Prints made on 0.9.6–0.9.14 (the whole mix's) play on every FEED; print into such a pattern and each becomes one per
  FEED. A 0.9.4 song's prints load as those did.
- FX's other pages (sends, SLICER, INSERT, master effects) follow the pad: tap FX again.

## ATTRITION, the jitter and the starfield (M6)

- **On the whole mix, a black key leaving a FEED out (0.9.15; Ed: "the vibe of a track falls apart when the drums are
  randomly hopping and the bass and keys are hopping differently").** Everything the FX layer does (ATTRITION's
  knobs, the white keys) works on **every FEED** together, so they stutter, jump and reverse as one. **Tap a black
  key** (F#3 G#3 A#3 C#4 D#4: FEED 1–5 in the order shown) to **keep that FEED dry**; tap it again to put it back
  (since 0.10.2 they are switches, not held: before, you held them down). They stay set when you let go of FX, through
  a menu too, until you tap them again or press **FX + OCT−** (which resets everything): the drums steady under a
  glitched bed, say. The black keys' cells read `FX`, and `DRY` (lit) for a FEED left out;
  they do not mute. PHANTOM and the reverb and delay returns play on untouched. A FEED glides in and out over 3 ms,
  so leaving one out while an effect runs makes no click. (0.9.6–0.9.14 worked on the selected FEED only, the black
  keys adding others.) The FX PAD (above) is each FEED's own.
- **ATTRITION: hold FX, turn KNOB 1–4.** The FX layer's knobs are **STUTTER, TUNNEL, JUMP and CRUSH**:
  - **STUTTER** repeats the mix on the grid, shorter as you turn: 1/8, then 1/16, then 1/32 (as the REPEAT keys).
  - **TUNNEL** closes a low-pass and sends everything into a feeding-back echo: the feed fading down a tunnel.
  - **JUMP** makes the playhead skip: each step, by chance, a targeted FEED plays the same 1/16 of another beat of its pattern.
    And some beats play **backwards**: each beat, by chance (about a third of them at full), the sound reverses for
    that beat, as the REVERSE key, then snaps back. The sequencer never actually moves, so it is back in time the
    moment JUMP is at 0.
  - **CRUSH** bits and sample rate, as before.
  - **They spring back** when you let go of FX. **FX + OCT+ latches** them (they stay; FX + OCT+ again lets them spring
    back); **FX + OCT−** resets them all and the latch. MENU > FX LATCH ON keeps them too, as before.
  - FX > MASTER still picks any of the eleven for each knob (FILTER, THROW, DEPTH, ISOLATR, WASH, PUMP, RIDE as well).
  - **Prints (0.9.4):** with REC armed and playing, hold FX: the header says `[FX] ● PRINT` and every step that plays
    is printed with ATTRITION as it is (the four knobs) and the white-key gates held. Let go and it springs back
    between the printed steps; they replay every loop with the layer closed. Holding FX again prints over them (with
    nothing up, an empty pass erases). **FX + OCT−** with REC armed clears them (`ATTRITION CLEARED`). The prints are
    the pattern's: they go with the FREQ band, the song slots and the project, beside the FX PAD's prints (sharing its
    80). A print **remembers the FEEDs it was printed on** (since 0.9.15: all but those left out) and plays on them
    (one from 0.9.4 plays on all four, as it did on the master).
- **The white keys (0.9.6's layout, Ed's):** FX + a white key holds its effect for as long as the key is down:

  | | 1 | 2 | 3 | 4 |
  |---|---|---|---|---|
  | **F3 G3 A3 B3** | REPEAT 1/4 | REPEAT 1/8 | REPEAT 1/16 | REPEAT 1/32 |
  | **C4 D4 E4 F4** | FREEZE (0.10.4; REVERSE before) | ECHO (the delay send full) | OCT+ | OCT− |
  | **G4 A4 B4 C5** | HPF (opening over a bar) | LPF (closing over a bar) | REVERB (the reverb send full) | STOP (a tape stop) |
  | **D5 E5 F5 G5** | USER 1 | USER 2 | USER 3 | USER 4 |

  The repeats record the targeted FEEDs into one mono loop of 0.74 s (it was stereo, 0.37 s), long enough for a 1/4
  down to about 81 BPM; below that a 1/4 is dark (a 1/8 holds to 41 BPM). Of the buffer effects (the repeats,
  STOP, FREEZE, OCT+ / OCT−) the last pressed plays. FLANGER and PHASER are on no key but can be put on any (below).
  REVERSE is gone from the keys and the lists (0.10.4, Ed); ATTRITION's JUMP still reverses a beat by chance.
- **USER 1–4: two effects on one key.** By default USER 1 is **1/16 + HPF** (a thin stutter), USER 2 **FREEZE +
  REVERB** (a frozen swell; REVERSE + REVERB until 0.10.3), USER 3 **STOP + ECHO** (a tape stop into its echo), USER 4 **OCT+ + REVERB** (a shimmer). To
  change one, hold FX and the USER key: **ALGORITHM** steps its first effect, **SELECT** its second (`USER 1: 1/32 +
  LPF`), **EDIT** puts its default pair back. A pair is best one buffer effect with a filter, ECHO or REVERB: of two
  buffer effects only the second plays. The pairs are kept with the settings.
- **Any key, any effect:** hold FX and a white key, then turn **PRESETS** to step what that key holds (NONE, the
  effects, USER 1–4); **EDIT** puts the key's default back. Kept with the settings, as before.
- **The jitter:** the screen breaks up for a moment, rows torn sideways, when ATTRITION moves, a gate is pressed, INGEST
  stamps a knob move, or PHANTOM starts a phrase (not on a legato slide). It lasts under 150 ms. MENU > ANIM OFF turns
  it off.
- **The starfield:** HOME's panel is debris tumbling towards you out of the dark, faster as HBT rises while playing,
  a slow drift stopped, the telemetry under it. A PHANTOM phrase or ATTRITION from rest sends a shockwave out from the
  centre and shakes the field. MENU > DISPLAY > HOME: STARS (the default), TRACKS, LEVELS or SCOPE. ANIM OFF: it holds
  still.

## Five FEEDs, B sides and per-step pan (0.10)

**Old saves are gone.** 0.10 saves projects, song slots and FREQ patterns in a new format; anything saved before 0.10 is
refused (EMPTY SLOT, or a pattern left blank), never half loaded. Everything you save on 0.10 loads on 0.10.

- **Five FEEDs, in this order on screen:** **1 DRUMS** (the kit, four voices), **2 PERC** (new), **3–5 the synths**
  (any engine). FEED numbers, ALGORITHM, the SEQ picker, the SONG lanes, MENU > MIDI (FEED 1–5 CH) and the black keys
  all follow that order. PERC sends MIDI on channel 5, PHANTOM CH is 6 by default.
- **The strip, two pages of four.** MIXER, HOME > TRACKS, HOME LEVELS and GLO's knobs show **DRUMS PERC SYNTH 1 SYNTH
  2**, or, with SYNTH 3 or PHANTOM selected, **SYNTH 3, PHANTOM, MASTER L, MASTER R**. PHANTOM's column is its LEVEL
  (turn it there or on its EDIT page); MASTER's two columns are the output's left and right peaks, red near full scale.
- **PERC:** two drum sounds, **PERC 1** and **PERC 2**, each picked from **any of the 40 kits** and **16 sounds** (KICK,
  SNARE, RIM, CLAP, CL / PD / OP HAT, CRASH, RIDE, BELL, LO / HI TOM, COWBELL, CLAVE, CONGA, SHAKER). EDIT > **SOURCE**:
  KNOB 1 / 2 the first's kit and sound, 3 / 4 the second's; the sound's values come with it, then shape them on **PERC
  1**, **PERC 2** (PITCH DECAY COLOR SWEEP), **PERC MIX** (SHAPE, LEVEL) and **PERC MDL** (MODEL). PRESETS on PERC
  takes both sounds from another kit. On the GRID and the tracker PERC has two lanes named by their sounds (`CP`,
  `SH` …); the keys play PERC 1 on the lower half, PERC 2 on the upper. PERC plays kits only. Its SLICER and REPT share
  DRUMS' recording: they work while DRUMS' SLICER is OFF and its INSERT isn't REPT.
- **Per-step pan:** lock PAN on a step — on the GRID, the **FX** column (KNOB 2), **PAN** on KNOB 4, the value on KNOB
  3 for the cursor's row — and **each hit that step plays keeps that pan while it rings**: a kick left, the snare after
  it right, each staying where it started. On every FEED.
- **B sides (the three synths):** a second sound on the same notes, both playing in full. **EDIT held + B SIDE** (the
  white key after CLEAN) turns it on: the first time it is a copy of your sound an octave up. **EDIT + EDIT A / B** (the
  next key) flips which side you are editing: everything — the engine keys, PRESETS, HOME's knobs, the EDIT pages —
  then changes **B**, and the header shows `/B` (`+B` while B is on and you edit A). **EDIT > B SIDE**: ON, **B TRN**
  (its transpose over the FEED's notes, ±24), **B LEVEL** (104 is as loud as A), **B PAN**. The FEED's LEVEL, PAN,
  sends, INSERT, SLICER, FX PAD, ARP and scale are shared. A and B share the FEED's voices: up to four each, all inside
  the six the synths share. No kit on either side while B is on.
- **RANDOM SONG** (MENU > SYSTEM) rolls a style — **GLITCH HOP, CHILLWAVE, ELECTRO or LO-FI** (its tempo and kits) —
  then: DRUMS with the off-beat hats panned left and right, PERC from two other kits (a clap / rim / clave / conga and a
  shaker / hat / cowbell ping-ponging, a fill swept across in the break), CHORDS always with a B side an octave up
  panned apart, BASS and LEAD sometimes with theirs.

## No roles: every sound anywhere, its own voices (0.10.5)

Ed: "assign voice # values to all the presets then each track can have any of the presets ... the onus is on the user
to figure out the voice composition on their own. It's less hand holdy and more straight forward."

- **The synth FEEDs have no roles any more.** PRESETS on HOME (and SAVE > PRESETS) lists every synth sound of every
  engine on each of the three synth FEEDs, and every kit on DRUMS and PERC (kits stay on those two; synths on the
  other three). **LIST** (SAVE > PRESETS, KNOB 4) starts on **ALL**; FAV and the categories (BASS LEAD PAD PLUCK KEYS
  DRUM FX OTHER) are still there to narrow it. ROLE is gone.
- **Each sound brings its voice count**, from its category: **BASS, LEAD, PLUCK, FX: 1** (mono), **PAD, KEYS: 4**,
  **OTHER** (and a sound of yours with no category): **2**. That is VOICES' **AUTO**; EDIT > VOICE > VOICES sets
  **MONO, 2, 3 or 4** by hand (4 is new). A FEED with CHORD KEYS still asks 4. An engine's own limit still applies
  (VULT plays 3 at most).
- **The three synth FEEDs share six voices** (the kits keep theirs). While their counts add up to six or less, each
  gets what it asks. Past six they are shared in proportion, the spare one going to whichever FEED is furthest short:
  two pads and a lead (4 + 4 + 1) play 3, 2 and 1. A B side plays inside its FEED's voices, as before.
- The FEEDs keep their names (SYNTH 1–3) and RANDOM SONG still deals them chords, a bass and a lead.

## FREEZE for REVERSE, PRESETS through every sound (0.10.4)

- **PRESETS on HOME steps whole sounds on every engine** (Ed: "it definitely needs to scroll through all possible
  presets"). On MORPH, DEBRIS and FM it used to step the SIGNAL and stop at SIG 16. Now it steps the same list as SAVE
  > PRESETS on every engine, switching engine as it goes (0.10.4 filtered it by the FEED's role; 0.10.5 lists them
  all, above). The SIGNAL is on the engine's first EDIT page (SIGNAL, KNOB 1).
- **FREEZE replaces REVERSE on the glitch grid** (C4), and USER 2 is FREEZE + REVERB. FREEZE grabs the moment and holds
  it, a frozen sustained slice, while held. REVERSE is off the keys and the assign lists. ATTRITION's JUMP still
  reverses a beat now and then. (A unit with REVERSE put on a key by hand keeps it until that key is set again.)

## The echo fixed (0.10.3)

0.10.2 had a bug of its own: the echo's headroom change made its return overflow whenever a loud echo came round with
the delay's MIX above about half, a full-scale pop each time. Effects stacked (the FX PAD's echoes, ECHO thrown, the
FX layer's THROW knob) made that likely. 0.10.3 fixes it, and the echo line's ceiling is now a soft knee instead of a
wall: at full feedback with the throws the repeats round off instead of crackling. Below three quarters of full scale
the echo is exactly as before. Found by stacking every effect at random on loaded songs on the host with an overflow
checker (the FX PAD on every FEED, three glitch keys, ATTRITION and the layer's knobs at random): no other overflow
in the effects. The CRUSH knob's stair-steps remain (that is the effect).

## A lighter glitch layer (0.10.2)

Ed heard clicks and pops on busy songs when holding the glitch layer's **ECHO** and **REVERB** keys, also with the
second core on. Those two throw every FEED's echo and reverb send to full, so any moment where the audio misses its
deadline gets repeated by the echo and smeared by the reverb into a train of pops. 0.10.2 gives the audio more room:

- **The second core runs the buses while the glitch layer works.** With **MENU > SYSTEM > 2ND CORE** on and the FX
  layer at work, the chorus, echo and reverb (the send buses) run on the second core at the same time as the glitch
  effects run on the first, instead of one after the other. The sound is the same, sample for sample.
- **Voices are thinned sooner while effects run.** When the audio is overloaded, the firmware fades out a released
  voice (then the least important held one). It now does this above 75 % of the time a buffer has while the glitch
  layer or an FX PAD echo is running (85 % otherwise), so the effects don't push it past 100 %.
- **More headroom in ROOM, SPRING and the echo.** Their lines take the signal 6 dB lower and give it back 6 dB higher:
  the same level, but much less clipping with every FEED's REV or DLY turned right up (ROOM clipped up to ~1 % of what
  it stored at full sends; now ~0.01 %). HALL had the room already.
- **The black keys in the glitch layer are switches.** Tap one to keep its FEED dry (`DRY`), tap again to put it
  back: no more holding a black key while reaching for the white ones.

Turn on **2ND CORE** (MENU > SYSTEM > 2ND CORE ON (BETA), then power off and on) if you haven't: it is still off by
default until it has proven itself on more units.

## Fewer presses (0.10.1)

Four shortcuts for what took three or more presses or menu dives in 0.10.

- **The FX column's list starts with what you lock most.** On the GRID's FX column (KNOB 2), KNOB 4 now steps through
  the commands (SPEED, LOOP, PITCH, MORPH), then **PAN**, **LEVEL**, then the four values HOME's knobs turn on that
  FEED's engine, then everything else as before. PAN is one click past the commands, not a long scroll. The list
  remembers what you last picked on each FEED, so the next lock on that FEED starts there.
- **PERC's HOME knobs are its two sounds:** KNOB 1 **1 KIT**, KNOB 2 **1 SOUND**, KNOB 3 **2 KIT**, KNOB 4 **2 SOUND**
  (the same as EDIT > SOURCE, without going there). Turning them picks the sound with its values, as SOURCE does; they
  are not recorded as knob motion. PRESETS on HOME still takes both sounds from the next kit.
- **A B side is its own ALGORITHM stop.** With B on, ALGORITHM goes 3, **3B**, 4, **4B**, 5, **5B**, then PHANTOM
  (a FEED without a B side has one stop). Landing on a B stop flips the FEED to B, as **EDIT A / B** does: HOME's knobs,
  PRESETS, the engine keys and the EDIT pages change B, the header shows `/B` and the screen says `B SIDE` and its
  engine. Moving off it (either way) flips the FEED back to A. **EDIT A / B** still works where it did.
  - **On the MIXER** a B stop's KNOB 1 and KNOB 2 are **B LEVEL** and **B PAN** (the B SIDE page's values, not
    recorded as motion; KNOB 3 / 4 are the FEED's REV and MUTE as ever). Each FEED card with a B side shows `+B`
    (`/B` on its B stop) and a thin second bar beside its meter: B's level, or A's while you stand on B.
- **OCT+ / OCT− turn the strip's page.** On the MIXER, HOME TRACKS and HOME LEVELS, **OCT+** shows the second page
  (SYNTH 3, PHANTOM, MASTER L / R) and **OCT−** the first (DRUMS, PERC, SYNTH 1, SYNTH 2), without changing the
  selected FEED — so on HOME LEVELS you can reach SYNTH 3's level with DRUMS still selected. The screen says
  `STRIP PAGE 1` / `2`. The page goes back to following the selection as soon as the selection moves. There the octave
  doesn't change (HOME's other views, and every other page, keep OCT+ / OCT− as the octave).

## The finishing pieces (M7, v0.9)

- **The default transmission.** At power-on the four FEEDs play: the NOISE kit's beat, and two bars in D minor at HBT
  124 — FEED 2's GRAIN PAD holding Dm, B♭, C; FEED 3's FOLD BASS pulsing on D, then B♭ and C; FEED 4's CRYSTAL BELL
  blipping A C D, F E D far apart. Press PLAY and jam PHANTOM over it. NEW (SAVE > TOOLS) clears it; RANDOM SONG rolls
  another; your last session (RESTORE LAST) comes back over it as before.
- **VOICES** (EDIT > VOICE, its first knob): how many notes a FEED plays at once. **AUTO** was its role's until 0.10.5 (now the sound's
  own, above); **MONO**, **2**, **3** (and since 0.10.5 **4**) set it. FEEDs 2–4 share eight voices (twelve less the kit's four):
  ask for more than are left and the FEEDs share what there is. FEED 1, the kit, always has its four lanes. GLIDE moved
  to KNOB 3 on that page; PRIORITY is on VOICE 3.
- **SCRAMBLE: hold EDIT, press OCT+.** SCAN, WARP, FOLD and CLARITY jump to somewhere random on the FEED's engine
  (CLARITY between -40 and +40, so it stays a sound), the screen tearing. Press OCT+ again for another; **EDIT + OCT−**
  takes it back to the sound you had, as for any load from the EDIT layer.
- **RELAY LOCK: tap LFO.** The header's play mark turns to an amber lock and the sequencer keeps playing every FEED's
  steps exactly as they were, while you change them: the GRID, step recording, the keys on a stopped GRID, the editor.
  You see your edits; you hear the locked bar. **Tap LFO again** and the edits play from the next step on. Only the
  steps are held: LEN, the knobs and automation stay live. Loading a project lets go of the lock.
  **LFO's pages** are now **LFO held** (0.6 s, then let go); hold it again for the next one. LFO held while you turn a
  knob is still MOMENTARY, as in Dungeon Tracker.
- **PHANTOM on MIDI:** MENU > MIDI > **PHANTOM CH** sends PHANTOM's keys out over USB MIDI on their own channel, as you
  play them (channel 5 by default, after FEED 1–4's; OFF sends nothing). A note's note-off goes out on the channel it
  started on, even if you change PHANTOM CH while holding it.
- **MENU > AUDIO > USB LEVEL is gone** (there is no USB audio). The MIDI tab has nine rows now (ten in 0.10: FEED 5 CH).
- **The GRID speaks plain words:** the line over the footer reads the cursor row's FX cell, so `L2x3` is
  `07 > LOOP: BACK 2 ROWS, 3 TIMES`, `HALF` is `SPEED: HALF TIME`, a lock `12 > SCAN: 58 %`.

## PHANTOM, the jam (M3)

The FEEDs loop what you built; **PHANTOM is what you play over them**, live, on the 27 keys. It is a voice of its own,
not one of the FEEDs, and **nothing it plays is ever recorded**: no notes into a FEED, a FREQ band or ORBIT.

**The keys play PHANTOM only while it is focused:** turn ALGORITHM past FEED 05 (below). Everywhere else they play the
selected FEED, as in Dungeon Tracker, so you can audition a FEED's sounds from HOME or any page, and with REC armed
record it live from HOME too (v0.9.1; in v0.9 they played PHANTOM almost everywhere).

- **A theremin by default:** one note at a time and legato. Hold a key over another and it slides there, with no new
  attack; let it go and it slides back to the key still held; let the last one go and it fades. A new phrase glides
  in from wherever the last one ended. Soft attack, long release, and a vibrato that creeps in after a moment.
- **It follows the focused FEED's scale keyboard** (SCL: QNT, ROOT, SCALE, TRN) and the octave buttons, so a jam stays
  in key with what you are looping.
- **It never takes a FEED's voice and never stops the loop:** its voice is outside the FEEDs' twelve.
- **Its own sound:** turn ALGORITHM past FEED 05 (and its B side, if it has one) and PHANTOM is focused on HOME (`PHANTOM > SIG 01 CARRIER`,
  `[OPEN CHANNEL]` or `[LIVE]` while a key is down, and the cycle it plays). Then:
  - **KNOB 1–4:** SCAN, WARP, FOLD and CLARITY on its own SIGNAL (as on MORPH).
  - **EDIT** flips the knobs to **GLIDE** (the slide's time, from a jump to about half a second), **VIBRATO** (its
    depth), **SUB** and **LEVEL** (0.9.15, in SIG's place: PRESETS still steps the SIGNAL), and back. **LEVEL** is
    how loud it plays: -13 dB by default, which sits it just under the loop (0.9.14 had no control, and it played
    about 14 dB louder: over everything; Ed: "I end up not being able to actually record it with the backing track").
  - **PRESETS:** its SIGNAL (01–16); GLIDE and VIBRATO stay as you set them.
  - ALGORITHM back, or opening any page, returns the focus to FEED 05.
  PHANTOM's knob moves are never recorded either, even with REC armed.
- **Its sound is saved with the project** (its SIGNAL, the four contort knobs, GLIDE, VIBRATO and SUB, in coarser
  steps, and its LEVEL; a project from before 0.9.15 loads the default LEVEL). A project from before PHANTOM keeps the one playing; a 0.9.3 or 0.9.4 project whose PHANTOM sang or spoke
  loads it on SIG 01 CARRIER, its GLIDE and VIBRATO kept.
- **REC armed on a FEED:** the keys play and record that FEED, as in Dungeon Tracker. On a stopped GRID, the keys enter
  steps as before.

## Changes from Dungeon Tracker 1.5 (M0)

- **No dungeon, no party, no DICE.** The tracks' roles are fixed: FEED 1 drums (4 voices), FEED 2 chords and pads (3),
  FEED 3 bass (1), FEED 4 lead (3); EDIT > VOICE > VOICES changes a FEED's (below). PRESETS' first list, once CLASS, was ROLE until 0.10.5 (now ALL).
  EDIT opens on EDIT 1; GEN KIT's voice pages are back. SEQ > VARY, the sequence dice, went too (Ed, Nov 2026): SEQ is
  the GRID and AUTOMATION.
- **RANDOM SONG** is MENU > SYSTEM's last row.
- **WAVE 32 is gone:** MORPH took its place (engine 20). A Dungeon Tracker project that used WAVE 32 loads MORPH with
  those numbers read as MORPH's, so it sounds different.
- **RANDOM SONG** still rolls a Dungeon Tracker-style verse (any of the sounds by role); the web editor's pages are
  Dungeon Tracker's, in Phantom Cosmonaut's colours.

## Trimmed (after M3)

Room for what comes next: the firmware went from 502 KB to 408 KB, and the memory the sounds share grew by 49 KB.

- **No USB audio.** The FM-1 no longer shows up as a USB sound card; USB MIDI and the web editor work as before.
  Record from the headphone / line out.
- **WAVES, MOD and MOD KIT are gone** (then, at M4, the rest of Dungeon Tracker's: see The engines).
- **No power-on fanfare:** the splash shows for under half a second and the FM-1 is ready.

## Building

See [CLAUDE.md](CLAUDE.md) for the commands (host tests, the browser emulator, the firmware package) and
[BUILDING.md](BUILDING.md) for the JieLi toolchain. A release build is `tools/build.py --release X.Y`; it writes
`build/phantom-cosmonaut-X.Y.fwsc`. The signals are generated by `python3 tools/gen_signals.py` (numpy).

## Flashing (at your own risk)

As Dungeon Tracker and Felucca: the `.fwsc` goes on with the web installer or M-VAVE's updater, and M-VAVE's updater
takes you back to the official firmware. Back up your songs with the web editor first.

## Licence

GPL-3.0-only, as Felucca and Dungeon Tracker; the parts under other licences are listed in
[LICENSING.md](LICENSING.md) and [CREDITS.md](CREDITS.md). The DotGothic16 font is SIL OFL 1.1
([LICENSES/OFL-DotGothic16.txt](LICENSES/OFL-DotGothic16.txt)).
