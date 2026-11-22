# Next build: Ed's hardware notes (0.9.3 on the FM-1)

Banked while Ed tests; nothing built until he says. Newest last.

**All nine built into 0.9.4** (Nov 10; Ed: "go ahead and apply those changes and spit out the latest CFW"). Checks:
`tests/notes094_test.c` (1, 4, 5, 6, 9), `tests/bank_test.c` (2: the PATTERNS page), `tests/engines_test.c`,
`tests/phantom_test.c`, `tests/finish_test.c` (3, 7, 8: NUMBERS). Not done of item 3 / 7: **custom phrases typed in
the editor** (they need a stored format and a protocol message; next).

1. **EDIT layer's "No." skips sounds** (Nov 10). On the EDIT layer (EDIT held: ENG, No., FAV) KNOB 2 steps through
   the sounds with SAVE > PRESETS's LIST filter applied (`ui.c eng_list_step`, LIST CLASS), but the layer has no LIST
   knob, so on FEED 4 DEBRIS it went 3, 5, 11, 13 with no way to see why. Fix: the EDIT layer steps every sound, one
   by one; the LIST filter stays a browse-page option (maybe start that page on ALL too).
2. **The pattern page moves from ENV to GLO** (Nov 10; Ed: "I never liked it being on ENV"). Today: GLO tapped opens
   ORBIT (its only page), GLO held the GLO layer (FEED levels, mutes, tap tempo); ENV held the patterns (FREQ A-F on
   the first six white keys, OCT+ copy, OCT- clear). Wanted: GLO tapped cycles two pages; GLO double-tapped or held
   opens the patterns. ENV keeps only its envelope pages.
   **Decided (Ed):** ENV is the envelope pages only. GLO tapped: first the **PATTERNS** page, tapped again **ORBIT**
   (the song), cycling. GLO held: the FEED levels quick layer, as today (levels, mutes, tap tempo stay there). No
   double tap. PATTERNS becomes a page, not a held layer: while it's open, the first six white keys pick FREQ A-F
   (the next one queued while playing, as today), OCT+ copy, OCT- clear; the page shows NOW / NEXT / the stored ones
   as the ENV layer did. Check: what the white keys do on other pages vs this one, the editor and manual, the
   QUICKSTART's "hold ENV" lines.
3. **NUMBERS replaces VOX** (Nov 10; Ed: VOX "not landing": one phoneme a note, so "I'm in outer space" can't be
   sequenced in the tracker; he picked NUMBERS from the workshop: NUMBERS, BEACON, TUNER, HULL, CHOIR). A numbers-
   station / Speak & Spell voice: **a step plays a whole word**. Sketch:
   - A word bank built in (~100-150 words, compressed speech (LPC-style), ~10-15 KB): the digits and NATO-style
     callsigns (ZERO .. NINER, ALPHA ..), the theme (ORBIT, CAPSULE, SIGNAL, CONTACT, LOST, COME IN, REPEAT, STATION,
     STANDBY, OVER, ..), enough glue words for sentences (I'M, IN, THE, IS, NO, ..).
   - The word chosen per step in the GRID (a column or a lock), PRESETS on HOME steps the word; the note sets the
     pitch (monotone radio read by default, sung if you like); the knobs: radio band-limit / static, speed (formant
     kept), pitch glide, robot vs human.
   - Custom phrases: typed in the web editor, turned into the same speech data (by the editor, in the browser),
     stored with the song; "I'M IN OUTER SPACE" as a row of steps.
   - Reuse VOX's synthesis (its resonators drive the speech frames) and its slot (engine 26, white key 4, PHANTOM's
     singing becomes speaking?). VOX's code goes; the prototype (`tools/vox_proto.py`) stays.
   - First: an audio prototype (as DEBRIS), Ed listens, then the firmware.
4. **The FX PAD's crosshairs don't move with KNOB 1-4** (Nov 10, a bug). `ui_graph.c graph_signature` (the "redraw
   only when something changed" hash) never looks at `xy_live` (the pad's X / Y, kept outside the track's p[]), so a
   knob move sounds but isn't drawn until PRESETS or ALGORITHM changes something hashed. Fix: hash `xy_live[0..1]`
   (type, x, y, on) and the print state on GR_XY.
5. **The FX PAD alive with the tempo** (Nov 10; Ed: "drawn in real time with the BPM"). On GR_XY, redraw every other
   frame while playing and move with the beat: a ring pulsing out from each active crosshair on every beat (the
   downbeat stronger), the time-based effects showing their rate (GATE flickers on its grid, ECHO's repeats as fading
   echoes of the crosshair at its time, REPT strobes, TAPE / PHASR / FLANG / CHOR a slow wobble), the grid's lines
   breathing on the bar; still when stopped. (MENU > ANIM OFF: the still pad.) Cost: the UI core only, never the audio.
6. **Chord KEYS, FEED 2's default** (Nov 10; Ed: white keys play chords, black keys pick a variant). Chords are a
   FEED setting (SCL > CHORD, `chord.c`), not an engine's. A new CHRD mode **KEYS**: the white keys play the
   diatonic chords of the FEED's scale (C major: C Dm Em F G Am Bdim); the black keys are held modifiers (pressing
   one alone plays nothing), combinable: **C# 7** (the in-key seventh), **D# sus** (sus4; the upper octave's D# sus2),
   **F# add9**, **G# flip** (major <-> minor), **A# spread** (open voicing, the root an octave down). E.g. F# + A =
   Am add9; C# + F# = a 9th. CHORD_MAX grows to 5 if add9 on a seventh needs it (check the voice budget: FEED 2's
   AUTO is 3 voices; KEYS wants 4-5 on FEED 2). REC records the chord into the step, as now; MONO plays the root.
   **FEED 2 powers on with KEYS**; the others OFF. The CHORD page names the chord as it plays (chord_name: add9, 9).
7. **NUMBERS, decided further** (Nov 10; Ed: "I trust your judgement"; adds to item 3):
   - Two modes. **WORD**: each step its own word (a per-step value, shown on the GRID by name: `C-4 ORBIT`), for
     numbers-station patterns. **PHRASE**: the FEED holds a phrase (built in, or typed in the editor); each note
     speaks the next word, back to the first at the pattern's start (and live on the keys: a key, the next word).
   - The note is the pitch (flat: the monotone read; moving: sung). A word starts on its step and runs on until the
     next word cuts it (a radio keyed); SPEED stretches / squeezes it, the pitch kept.
   - **The 100 words** (Ed to amend): numbers ZERO ONE TWO THREE FOUR FIVE SIX SEVEN EIGHT NINER TEN HUNDRED;
     callsigns ALPHA DELTA ECHO VICTOR; radio CALLING COME OVER OUT REPEAT CONFIRM NEGATIVE AFFIRMATIVE ROGER STANDBY
     COPY MAYDAY ATTENTION RECEIVING; space ORBIT CAPSULE STATION SIGNAL CONTACT SPACE OUTER EARTH MOON STARS SATELLITE
     ROCKET LAUNCH GROUND CONTROL TRANSMISSION; status SYSTEM POWER OXYGEN FAILURE WARNING ERROR ONLINE LOST DRIFTING
     SILENCE STATIC DAMAGE; who COSMONAUT PHANTOM GHOST CREW VOICE HEART; glue I I'M YOU WE IT IS ARE THE A THIS IN ON
     TO FROM NO YES NOT HERE THERE STILL CAN HEAR ME MY WHO ANYONE; mood COLD DARK FAR ALONE HOME HELLO GOODBYE NEVER
     FOREVER WAITING. Long words (TRANSMISSION AFFIRMATIVE SATELLITE) cut first if the bank runs over ~10 KB.
   - The audio prototype first: a WORD pattern (numbers station), a PHRASE line ("I'M IN OUTER SPACE" sung over the
     transmission), the knobs; it makes the idea concrete before the firmware.
8. **NUMBERS approved** (Nov 10; Ed on the prototype: "YES, MAN! ... that creepy vocal vibe I wanted").
   `tools/numbers_proto.py`: Flite's rms voice -> 8 kHz LPC-10, 25 ms frames (energy 5 bits in 3 dB steps, voiced,
   pitch movement 4 bits, K1..K10 6 6 5 5 4 4 4 4 3 3 bits in the arcsine domain; repeat frames; silence 45 dB under
   the word's peak) = **10.3 KB for the 100 words** (1.74 kbit/s, 48 s of speech). Synthesis: the frames interpolated,
   a ten-stage lattice at 8 kHz, the excitation unit-power (robot: the TMS5220-like chirp; human: a Rosenberg pulse +
   a breath + the word's own pitch movement + jitter) x the frame's loudness x sqrt(prod(1 - k^2)); de-emphasis;
   RADIO (300 Hz .. 3 kHz band, drive, static under a squelch, crackle, a 60 Hz hum). Demos: build/numbers
   (station, phrase over the transmission, the knobs, all 100 words). Firmware: the tables generated by this script,
   the lattice in integer (Q15 k's, ten multiplies a sample at 8 kHz, upsampled), engine 26 in VOX's place.
9. **ATTRITION prints** (Nov 10; Ed tried REC with FX held: "I don't think it works yet"; it never did: ATTRITION and
   the buffer gates are a performance that springs back). New: with REC armed and playing, hold FX and play: every
   step that plays is printed with ATTRITION as it is (STUTTER TUNNEL JUMP CRUSH, `mk_a`) and the white-key gates
   held (REPEAT REVERSE TAPE STOP FREEZE ..), as the FX PAD prints; they replay every loop, the spring-back still
   between them. ATTRITION is the master's, so its prints are the pattern's, not a FEED's: a lane of their own per
   FREQ pattern (per step: 4 values + a gate mask, ~5 bytes; 64 steps x 6 patterns ~2 KB RAM), saved with the
   pattern, the song slots and the project (formats bumped as fxp's were); FX + OCT- (held, REC armed) clears them;
   the steps printed lit on the FX layer. Check: how a print and a live hold mix (live wins while held), JUMP's
   playhead skips while printing (print the step that sounded), the editor (shows / clears them).
