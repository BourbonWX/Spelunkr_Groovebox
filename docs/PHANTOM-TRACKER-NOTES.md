# Phantom Tracker (FM-1 firmware; Phantom Cosmonaut until 0.9.4) — notes for Claude

Phantom Tracker (Phantom Cosmonaut until 0.9.4; the repo, the files and the code still say Cosmonaut) is a fork of
Dungeon Tracker 1.5 (itself a fork of Felucca 1.6.1, GPL-3.0) for the M-VAVE FM-1: a groovebox tracker, Phantom
Cosmonaut's look, workflow and FX with Dungeon Tracker's GENESIS ANALOG SID back and the speech gone. The owner is Ed. **The spec and plan is `docs/PHANTOM-COSMONAUT-SPEC.md`: read it
whole before writing code, and keep it up to date as decisions change.** The manual is `PHANTOM-COSMONAUT.md`: keep it
in step with every user-visible change. Dungeon Tracker's manual (`docs/DUNGEON-TRACKER-1.5.md`) still describes the
parts carried over.

## Where things stand (v0.10.5 beta: Phantom Tracker, Nov 2026)

Naming (Ed, Nov 2026; the spec uses these): the four tracks are **FEED 01–04**, the wavetables **SIGNAL 01–16** (`SIG 07`
where space is short), KNOB 1–4 **SCAN WARP FOLD CLARITY**. SNS and LOG are gone.

- **M0** (fork and rename): the dungeon, the party (fixed roles, `core.h` trk_cls: FEED 1 drums 4, 2 chords 4, 3 bass 1,
  4 lead 3) and DICE are removed; RANDOM SONG is MENU > SYSTEM's last row (row indices before it unchanged).
- **M1** (the look), done as a re-skin of Dungeon Tracker's pages rather than every page from scratch: the PHANTOM
  palette (default; `tools/gen_ui_palettes.py`), DotGothic16 as the UI font (`tools/gen_aa_font.py --preset
  phantom`: S 12 px, M 14, L 28, its outlines moved 28 units onto the pixel grid; `assets/fonts/DotGothic16-Latin1.ttf`),
  the names on screen (ORBIT, GRID, HBT, FREQ, FEED), HOME's terminal (`ui_graph.c graph_home`). Not done of the
  spec's M1: the SEQ grid's FX codes in plain words, a from-scratch SAVE / GLO grammar, the jitter (M5 anyway).
- **M2**: MORPH (`eng_morph.c`, engine 20 in WAVE 32's place; WAVE 32 removed) with the 16 SIGNALs
  (`tools/gen_signals.py` -> `signals_tables.h`, 8 frames each, 8-bit, 81.8 KB); FEED 2..4 power on with MORPH SOLAR
  WIND, HULL GROAN, CARRIER (`engines.c TRK_DEF`); EDIT's oscilloscope (`ui_graph.c graph_signal`, `morph_cycle`).
  Tests: `tests/signal_test.c` (levels, aliasing, every knob, cost), `tests/contort_test.c` (INGEST records the four
  knobs, a lock per step, saved and loaded).
- **M3**: PHANTOM (`phantom.c`, Ed's decisions: its own voice, a mono legato theremin, never recorded): `ph_trk`, a
  track_t outside `trk[]` (no FEED array sees it), its own envelope, glide and vibrato over MORPH's render, mixed after
  the FEEDs in `fx.c mix_block`. The keys go to it in `seq.c keyboard_block` while `ph_keys` (the UI
  clears it with REC armed, where the keys play and record the FEED, and on a stopped GRID, where they enter steps).
  ALGORITHM's fifth stop `ph_focus` (HOME only): `ui_input.c` (KNOB 1..4, PRESETS, EDIT flips `ph_page`), `ui_draw.c
  draw_columns` / `draw_foot`, `ui_graph.c graph_phantom`. Saved in 5 project bytes (`project.c PROJ_PH_OFF`: the 3
  spare after the motion and Dungeon Tracker's 2 party bytes; bit 7 of the first marks a saved PHANTOM). Tests:
  `tests/phantom_test.c`; demo: `tests/phantom_demo.c` (by hand). PHANTOM's MIDI OUT came at M7 (PHANTOM CH).
- **Cleanup after M3** (Ed: "remove usb audio", old projects may load differently): USB audio off (`felucca.c
  FELUCCA_UAC 0`; USB MIDI stays). WAVES (16), MOD KIT (19), MOD (21) retired like Dungeon Tracker's others
  (`engines.c` ENGINES -> `ENG_GONE`, `ENG_SUBST`: WAVES MOD PHASE CHIP VOICE PHYS GRAIN -> MORPH, MOD KIT SAMPLE SLICE
  DRUM -> GEN KIT; `retire.c eng_subst_c`: a drum whose substitute has none -> GEN KIT); `mutable/`, `eng_mod.c`,
  `mod_tables.h`, `tools/gen_mod.py`, `jingle.c` (the fanfare) and their tests deleted. Shown engines (`core.h
  NENG_SHOWN` 5, `NENG_KEYS` follows): GENESIS GEN KIT MORPH ANALOG SID. The retired engines that nothing kept shares
  are no longer compiled (`engines.c #if !DT_RETIRE`; the files stay for Felucca's RONDO_LAYOUT=0 / FELUCCA_FM4=1
  builds); SAMPLE, DRUM, FM6 still are (their sets, lane notes and mutes, patches), with `ENG_MEM_FITS_RETIRED`.
  `ENG_MEM_PART` 512 (GENESIS's notes), was 12,888 (PHYS). `eng_own_values` / `kit_param_desc` moved to `engines.c`.
  Level references in `signal_test`, `sid_test`, `genesis_test` are WAVES' measured rms (0.499 / 0.496); cost
  yardsticks ANALOG (GEN KIT) and GENESIS (MORPH). The web editor's `RETIRED` map comes from the firmware
  (a dump of `eng_subst_k` / `eng_subst_preset` per retired preset).
- **M4** (Ed: "delete all the engines except these four", the focus "pikocore-styled glitch-looped playback with live
  jamming on top"): the engines are MORPH (20), FOLDER (22), GRANULAR (23), FM (24), all in `eng_morph.c` on the
  SIGNALs, sharing `sig_tail` (SUB, CLARITY's low-pass / static, DC, amplitude), and the NOISE kit (14, `eng_cycle.c`:
  GEN KIT's controls, GM map, lanes, mutes kept, `ck_render`'s voices new: sine-sweep kick, two-sine + LCG-noise snare,
  sample-and-hold noise hats, ring-mod perc; no engine memory). `NENGINES` 25, `ENG_SIG(e)`, `ENGINE_ORDER` {20, 22,
  23, 24, 14}, `NENG_SHOWN` 5. GENESIS, SID, ANALOG retired and no longer compiled (`#if !DT_RETIRE`); `ENG_SUBST`
  maps every retired engine onto the five (`retire.c eng_subst_c`: drums -> the kit). `ENG_MEM_PART` 16. TRK_DEF:
  NOISE CAPSULE, GRANULAR GRAIN PAD, FOLDER FOLD BASS, FM CRYSTAL BELL; PHANTOM stays MORPH. RIDE (perform.c mk_x[3])
  brightens the SIGNAL engines too. Favourites: `favorites.c fav_row` gives the kept engines (all >= 14) rows 2, 3, 5,
  6, 7 (row 14 is MIDI LEARN's: the old `factory[ENGI_CKIT]` write was a bug). Tests: `tests/engines_test.c` (levels
  against MORPH, every knob, the scope, cost); `sid_test.c`, `genesis_test.c` deleted; the kit checks in
  `rondo_test.c`; the UI suite's known FAILs are 214 now (two fixed by the favourites). Demos: `build/rondo_demo`
  folder.wav granular.wav fm.wav. The editor's `ENG` entries for 14, 22..24 are generated from `build/host/desc.json`.
- **M5: the FX PAD** (`fxpad.c`, included by `fx.c`; Ed: per FEED, the 11 INSERT effects + RING GATE ECHO TAPE,
  crosshairs reset on entering / leaving, REC prints): FX's first page `GR_XY` ("FX PAD", params.c). Two slots per
  FEED (`xys[NTRK][2]`, ins_t) after the INSERT in `mix_part` / `mix_spread` (`xy_want`, `xy_busy`); the INSERT's DSP
  takes its three values as an argument now (`ins_drive(s, v3, ..)` ..; `ins_rep_own` shares REPT's sl_buf between the
  INSERT and the pad). Live state `xy_live[2]`, `xy_trk` (the FEED the pad is open on, 0xFF closed; `ui_input.c
  xy_open` each pass), `xy_print`. Prints: `fxp_store_t fxp` (core.h; 80 x 6 bytes), the current pattern's, mirrored
  as motion is: `chain_pattern_t.fxp` (bank: object ver 2, ver 1 read as none), song slots (`SG_VER` 2, `SG_PRAW_V1`),
  projects (`PROJ_FXP_OFF` = FM6's 512 bytes, marker 0xF0), chain sources, `motion_reset` clears a FEED's; played by
  `seq.c seq_play -> fxp_step` (`chain_fxp`), `fxp_stop` at stop. UI: `ui_input.c` (knobs, PRESETS, ALGORITHM, REC in
  `rec_tap`, OCT-), `ui_draw.c` cards, `ui_graph.c graph_xy`. Tests: `tests/fxpad_test.c`; demo `tests/fxpad_demo.c`
  (SOLO=1: the drums alone). Host cost: eight effects (ECHO + FLANG on every FEED) ~25–65 % on top of the mix:
  check on the FM-1, the second core is the plan B (Ed).
- **The sequence dice removed** (Ed): SEQ > VARY (`ui_vary.c`, `GR_VARY`, `tests/vary_test.c`) gone; SEQ is the GRID and
  AUTOMATION.
- **M6** (Ed away, "assume yes"): ATTRITION, jitter, starfield (spec section 8, M6). ATTRITION = `perform.c` MK_STUT
  MK_TUNNEL MK_JUMP in the master-knob pool (`MK_N` 11; `mk_eff` / `mk_assign` 4 bits a slot as the difference from
  `MK_DEF` {STUT TUNNEL JUMP CRUSH}; `G_ENGGO` -32768..32767); their values `mk_a[3]` (core.h); STUTTER is a virtual
  REPEAT key (`mk_stut_bits` into perf_begin's held), TUNNEL the LPF macro and ECHO THROW's `ew1` in proportion, JUMP
  `seq.c seq_jump` in `seq_play` and its reverse (Ed: "a reverse probability", the sound): `mk_jrev_bits`, REVERSE held
  for a beat by chance (35 % at 100). Spring-back on FX let go (`ui_layer.c`, unless `attr_latch`: FX + OCT+, or FX
  LATCH); FX + OCT- resets. Jitter: `gfx.c cv_jitter` in `cv_blit` / `cv_blit_from`, `ui_draw.c jit_frame`, kicked by
  `jit_kick` (core.h: motion_capture, ph_key_on's first note, the FX layer's ATTRITION knobs, perf_press). Starfield:
  `ui_graph.c graph_stars` in `graph_home` (`HV_STARS` 0, `HV_SCOPE` 3), `star_kick` (core.h). Tests:
  `tests/m6_test.c` (jitter torn and clean again, its triggers, the field's motion, ANIM OFF, the shockwave, cost
  vs the scope), ATTRITION in `tests/livefx_test.c`; `ui_test.c ui_power_on` gives the inherited checks Felucca's four
  knobs (FILTER CRUSH THROW DEPTH) — the spring-back fixed 7 of them: 206 known FAILs now. Demos:
  `tests/attrition_demo.c` (by hand), `build/host/m6_test DIR` (HOME's frames as PPMs).
- **M7** (Ed away, "you have the con"; v0.9 beta, `FELUCCA_VERSION`, package identity FM-1_909):
  - **VOICES** (`core.h`: `P_VOICES` = `P_FTYPE`, ANALOG's old filter TYPE byte; `vox_req`, `trk_cap` share NBUDGET
    less the kit's 4; `CLS_VOICES` {4,3,1,3}: FEED 2's AUTO is 3 now). EDIT > VOICE {VOICES VOICE GLIDE GLMODE}, VOICE 3
    {SPRD PRIO}; `ui.c page_visible` tells VOICE from ANALOG's FILTER page by `id[1]` (it was hiding VOICE everywhere);
    `retire.c` clears the byte for old ANALOG sounds. The editor's entry 101 is VOICES.
  - **SCRAMBLE** (`ui_layer.c scramble`): EDIT + OCT+ throws the engine's knob[0..3] (CLARITY -40..40) inside
    `load_begin` / `load_end`, so EDIT + OCT- undoes it.
  - **PRESETS on HOME** (`ui_input.c presets_turn`): a SIGNAL engine's P_E0 (SIG 01..16, motion_capture'd); the kit
    still loads kits; whole sounds on SAVE > PRESETS (`GR_BROWSE`). Two inherited undo checks browse there now.
  - **RELAY LOCK** (`seq.c relay_st` / `relay_on` / `play_steps` / `relay_set`; the audio reads `play_steps`, the screen
    `seq_steps`): `ui_input.c page_tap` — LFO tapped toggles it, held `LFO_HOLD_MS` 600 then let go opens LFO; the
    header's play icon is `ICON_X_LOCK` in ACCENT; `project_load` lets go. +2.8 KB RAM.
  - **PHANTOM CH** (`menu_items.c MI_PHCH` after `MI_TCH4`, editor id 33; stored in `favorites.factory[15][13]` as
    value + 1, 0 = channel 5; `phantom.c ph_och`, `seq.c ph_midi_on/off`, `ph_kch` keeps a note's channel). MIDI tab 9
    rows (`MTAB_ROWS` 9, `ui_menu.c mr_h` 16 px). **USB LEVEL hidden**: `MI_USB` moved between MIDI and SYSTEM with
    `MTAB_HIDDEN` (a tab's rows must be one run); `MTAB_OF` gives the editor its old tab; editor id 10 kept.
  - **The GRID's FX in plain words** (`ui_tracker.c tk_plain`, the footer's first row in `ui_draw.c draw_foot`).
  - **The default transmission** (`boot_song.c transmission`, `main.c felucca_init`): FEED 2..4 two bars in D minor,
    LEN 32, at power-on only (NEW still clears). The host tests' `ui_power_on` does not call it.
  - Docs: the manual's M7 section, `docs/QUICKSTART.md`; the editor in the PHANTOM palette and DotGothic16
    (`web/make_site.py` copies the font and, when built, the emulator to `webapp/emulator`); `fmhub.json` +
    `docs/card.png` (a starfield frame from `m6_test DIR`). The repo URL in `fmhub.json` is assumed.
  - Tests: `tests/finish_test.c` (VOICES, SCRAMBLE, PRESETS, RELAY LOCK, PHANTOM CH, USB LEVEL, the GRID's words, the
    transmission; `build/host/finish_test OUT.wav` renders eight bars of it), in `run_rondo.sh`. UI suite: **205 known
    FAILs** (the MENU tabs check passes again). Not done: a from-scratch SAVE / GLO / MENU (kept as the re-skin).
- Image (`--release 0.9`): 402,520 B of 581,564 (~179 KB free); RAM .data+.bss 41,592 B of 57,344; pool 352,600 B of
  385,024 (32 KB free).
- **Nothing has run on the FM-1 yet** (Ed has not flashed 0.1 or 0.2): check the font's look and MORPH's CPU on the
  hardware before going much further.

- **v0.9.1** (Ed's first note after v0.9): **the keys play PHANTOM only while it is focused** (`ui_input.c`: `ph_keys =
  ph_focus`; `phantom.c ph_keys` starts 0); otherwise the selected FEED (audition sounds anywhere, record live from
  HOME). `tests/phantom_test.c focus_phantom()`, `finish_test.c` 5b.

- **v0.9.2** (M8, in part: Ed, "bake Derelict and Shortwave into the firmware"; Balanced budget, a SET knob): the
  factory sets as `firmware/src/factory_tables.h` (generated: `python3 tools/gen_factory_kits.py --header
  firmware/src/factory_tables.h`; the WAV references `tools/gen_factory_kits.py build/factory/wav`, each sound's noise
  seeded by its name so references and tables match). Kit SET = `P_LN7` (`eng_cycle.c fk_set`, `fk_note_on`,
  `fk_render`, `FK_MARK` in s[4] marks a sampled voice; presets 16..19 DERELICT SCRAP SHORTWAVE DEAD AIR via
  `ck_kit_set`; `category.c` row 14 has 20). GRANULAR SIG 17..20 = `FK_TAPE` (`eng_morph.c sig_max` / `sig_name`,
  used by `ui_graph.c` and `ui_input.c presets_turn`). The editor mirrors it (ENG 14 presets, CK_KITS, `CK_SET` lane
  90, GRANULAR SIG 0..19). `tests/factory_render.c` (OUTDIR [sets|tapes|demo]: the NOISE references, each hit through
  the kit, the tapes, the transmission on each set); `finish_test.c` section 8. UI suite: **203 known FAILs**. Image
  537,088 B (44 KB free, 24 KB above the margin); RAM unchanged.

- **v0.9.3** (Ed: "combine folder and granular into one", "less harsh", "leave the harsh sounds entirely out", "FOLD
  this new engine and the vox engine into the firmware"): **DEBRIS** (engine 25, `eng_morph.c`: two Hann grains in
  phase with the note, a frame each, dithered between neighbours; each through `deb_shape`'s sine fold x1..x4, faded
  in; the mip level drops one / two with the fold (`xf`): no aliasing, `engines_test` checks -60 dB between harmonics
  at full FOLD; CLARITY left the low-pass, right SHIMMER, never static; E5 SCATTER E6 FENV E7 DRIFT; the window a line
  a block, `deb_wline`; the tapes on SIG 17..20) and **VOX** (engine 26, `eng_vox.c`: Felucca VOICE's Klatt
  resonators, copied under `#if DT_RETIRE`; the prototype's mouth -> formants, nose, hiss band-pass, ONSETs; per-voice
  state past voice_t in `vox_x[(NPART + 1) * NVOICE]` (.pool, PHANTOM's the last row); `.poly` 3; `vox_response` /
  `ui_graph.c graph_vox` draw the mouth's response on EDIT). FOLDER (22) and GRANULAR (23) retired onto DEBRIS
  (`ENG_SUBST`, by category; PHASE CHIP TRIO GRAIN NOISE SID too; Felucca's VOICE -> VOX). `NENGINES` 27, `ENGINE_ORDER`
  {20, 25, 24, 26, 14}, `ENG_SIG` MORPH FM DEBRIS; favourites rows DEBRIS 10, VOX 11; `ENG_VCOST` 490 / 520 (host:
  DEBRIS 1.9x MORPH, VOX 1.9..2.4x: **check the CPU on the FM-1** with a three-note DEBRIS pad and VOX). TRK_DEF:
  DEBRIS GLASS PAD, DEBRIS ROUND BASS. PRESETS on HOME on VOX steps the phonemes (E values only, motion_capture'd).
  PHANTOM sings: PRESETS past SIG 16 -> VOX's first 16 phonemes (`phantom.c ph_voice`, `PH_VOX_N`; the ISR switches
  the engine; the 5 bytes: SUB's field 7 marks VOX, `ph_q2` packs NASAL / TIP by compares: a shift-and-mask compiled
  to 0xFFC00000, which build.py's ROM check flags). Editor: ENG 25, 26 from desc.json, 22, 23 retired, RETIRED map
  regenerated (`tests/retired_dump.c`; SAMPLE's fifth kept by hand),
  mock power-on and demo presets. Tests: `engines_test` (DEBRIS, VOX), `phantom_test` (VOX), `finish_test` (VOX on
  HOME), the others moved off FOLDER / GRANULAR. `factory_render OUT sing`: the transmission with PHANTOM singing.
  UI suite: **203 known FAILs**. Image 545,076 B (36 KB free); RAM unchanged; pool 354,800 of 385,024.

- **v0.9.4** (Ed's notes on 0.9.3 on the FM-1, `docs/NEXT-BUILD.md` 1..9, all built): **NUMBERS** (engine 27,
  `eng_numbers.c`; VOX (26) retired onto it, `ENG_SUBST`, `eng_vox.c` no longer compiled): a numbers-station voice, a
  word a note: 100 words of Flite's rms voice as LPC-10 (`tools/gen_numbers.py` -> `numbers_tables.h`, 10,496 B; the
  prototype `tools/numbers_proto.py`); per-voice state `nm_x[(NPART + 1) * 2]` (.pool, PHANTOM's the last row),
  `nm_p[]` the PHRASE cursor (reset when the FEED's pattern wraps or the phrase changes) and the word said last; a
  lattice at 8 kHz (int64 products: **watch int32 overflow** in its filters, the first draft had four), a DC blocker,
  the radio; `.poly` 1; legato notes speak too (`voice.c mono_play`, not on a fall-back: `mono_back`). E0 WORD E1 RADIO
  E2 SPEED E3 HUMAN E4 MODE E5 PHRASE E6 DROP E7 TAIL; HOME PRESETS steps WORD or PHRASE; `ui_graph.c graph_numbers` /
  `graph_nm_curve`. `ENG_VCOST` 700 (host: 2.7x MORPH, one voice). PHANTOM speaks (PRESETS past SIG 16: the 16
  phrases; each key the next word, legato too: `phantom_note`; packed in SUB's field 7, the phrase in SIG's).
  **CHORD KEYS** (`chord.c CH_KEYS`, `CHRD_KEYS` 10, FEED 2's default: `ui.c track_defaults`; `ch_mods` from the black
  keys in `seq.c keyboard_block`; FEED 2's AUTO 4 on KEYS: `vox_req`). **GLO > PATTERNS** (a page, `GR_BANK`, before
  ORBIT; `song.grid` 4 owns the keys: `ui_layer.c bank_page_keys` / `bank_page_leds`, OCT- tapped unqueues; ENV held
  opens no layer: `LY_GONE`). **The EDIT layer's No.** steps every sound (`eng_list_step`: no LIST ROLE filter).
  **The FX PAD** redraws on KNOB 1-4 (`graph_signature` hashes `xy_live`) and moves with the tempo while playing
  (`xy_pulse`, ANIM OFF still). **ATTRITION prints** (`fxpad.c atp_*`: fxp events of type 0, the pattern's, no format
  change; `perform.c atp_gate`). Tests: `tests/notes094_test.c` (new, in `run_rondo.sh`), `bank_test` on the page,
  `engines_test` / `phantom_test` / `finish_test` on NUMBERS, four `ui_test` GLO checks updated (still **203 known
  FAILs**). Demos: `factory_render OUT speak` (transmission-spoken.wav, numbers-station.wav), `OUT keys`
  (chord-keys.wav). Editor: ENG 27, 26 retired, RETIRED map regenerated, CHRD KEYS. Image 560,544 B (21 KB free: at
  the margin; the next feature needs room made first); pool 354,240.
  **After Ed's first listen** ("can't hear the voice synth at all", car speakers): (1) a bug: PHANTOM speaking said only
  a phrase's first word when a key came during the last word's release (`phantom_note`'s fresh-phrase path skipped
  note_on unless the envelope was at 0; now always on NUMBERS; `phantom_test` checks it); (2) the level: speech's
  peaks reached the master limiter first, so a part-level rms match sat 16 dB under in the mix; now an AGC after the
  radio's band (`NM_AGC_T` / `_A` / `_G`, held between words, reset at each word to x4), the knee, x5 (`NM_OUT`).
  **Check levels where they are heard:** `notes094_test` section 5 (PHANTOM speaking vs the transmission and vs
  PHANTOM on MORPH; FEED 4 NUMBERS vs FEED 4's FM lead). Demos add `numbers-phrases.wav` (the 16, solo).
  **Clarity** (Ed: "can't really hear what the voice is saying ... muffled"): measured with a speech recogniser
  (`tools/numbers_clarity.py` + `tests/numbers_say.c`: faster-whisper base.en, word accuracy over the 16 phrases; Flite
  at 8 kHz scores 0.96, so the band was never the problem). Fixes, each scored: the bank (`gen_numbers.py`) smooths the
  voicing (median of 5) and keeps all ten K's on unvoiced frames (0.63 -> 0.80 in the prototype), its K indices
  Huffman-coded per coefficient (`NW_HN` / `NW_HO` / `NW_HS`, `nm_k`; 13.2 -> 10.0 KB, the audio bit-identical);
  the AGC slow (93 / 370 ms) and under the knee (driven hard it was loud but muffled); RADIO's static ducks under the
  voice (`NM_HISS_*`; it rose with the voice, the biggest RADIO loss); HUMAN's pulse de-tilted, its breath halved.
  PHANTOM's default voice ~0.2 -> 0.70, clean A2 0.60 -> 0.76. Loudness traded: FEED 4 NUMBERS sits ~3 dB under the
  FM lead (`notes094_test` allows 3.5). `fx.c mix_part` pans with a pre-shift and `phantom.c` clamps its sends (a
  part to ~2^31 / 1024: NUMBERS peaks ~290k). Image 560,608 B (20.9 KB free).
  **KIT** (Ed: "more like a drum kit where we can have all possible words in 1 preset ... I have to manually flip
  between presets to make it say sentences"; he picked a PITCH knob, words grouped by type, PHRASE kept): MODE 2
  (`NM_KIT`, the default; STATION and SPEAK SPELL are kits). `nm_keys` (engine_t.keys): key k on page `nm_page()` =
  song.octave mod 4 plays note `NM_KBASE` 20 + page x 27 + k (the bank's order is the grouping: `NW_PAGE_NAME`), past
  the 100 silent (255); `nm_note_on` takes the word from the note; PITCH = E0 via `nm_desc` (engine_t.desc; F_NOTE with
  max > 11 shows the octave: params.c), `nm_kit_inc` (E0 x 16 + the voice's pitch16 less pitch_cur: bend / LFO / env,
  not glide). **A word is a hit in every mode** (`ownenv` + `nm_done`: the word, its tail, the lattice's ring; a 16th's
  gate cut words before). UI: OCT names the page (`ui_input.c`), HOME PRESETS steps PITCH, `ui_graph.c graph_nm_kit`
  (the page's words), the GRID's `tk_cell` three letters (`nm_abbr`: the first three but `NW_ABX`'s 8) and `tk_plain`
  (`SAYS OUTER`). Room: `NW_OFF` (uint16 byte offsets) + `NW_NF` for the old padded index, and the energy and pitch
  Huffman-coded too (`NM_HE` / `NM_HP`: the bank 9,013 B; `NW_FSUM`, a weighted sum of every field, checks the decode).
  Tests: `engines_test` (KIT keys / pages / pitch, a word a hit, codes unique, the bank's sum), `notes094_test` 6 (keys,
  OCT's page, the GRID), `finish_test` (PRESETS -> PITCH; COLOR's check now the grit, not the rms a hit's variety
  swung). FEED 4 NUMBERS now -14 dB vs the FM lead's -17 (no ADSR, no velocity). `factory_render OUT speak`: the
  station is KIT steps. Image 560,544 B (21.0 KB free).
  **A phrase per pattern** (Ed: "once I select a pre-set phrase it plays across all the patterns"): the motion was
  already per FREQ pattern, so an automation event of P_E5 on step 0 does it (a lock would spring back each step and
  restart the phrase); `ui_input.c presets_turn`: REC armed (not during an ORBIT chain), PRESETS in PHRASE mode writes
  that event (`FREQ B: MAYDAY`) instead of motion_capture's timed one. `notes094_test` 7.
  (All of NUMBERS above is history since 0.9.5: kept for the record and for the files, which stay in the tree.)

- **v0.9.5: PHANTOM TRACKER** (Ed: "scrapping the gimmicks of each to create one unified excellent groove box tracker";
  option 1: the speech out, GENESIS ANALOG SID back, 0.9.4's look kept, the new name). **The speech removed:** NUMBERS
  (27) retired onto MORPH (`ENG_SUBST[ENGI_NUMBERS]`, VOX's too), `eng_numbers.c` no longer included (it,
  `numbers_tables.h`, `tools/gen_numbers.py`, `numbers_clarity.py`, `tests/numbers_say.c` left in the tree, unbuilt);
  PHANTOM MORPH-only again (`phantom.c`: no `ph_vox`, PRESETS stops at SIG 16; a 0.9.4 save packed speaking, SUB's
  field 7, loads on SIG 1 with GLIDE VIBRATO); the KIT GRID codes, OCT's page message, HOME PRESETS' WORD / PHRASE /
  PITCH and the phrase per pattern gone; `voice.c`'s legato hook and `mono_back`, `params.c`'s F_NOTE octave gone.
  **The engines back:** `eng_analog.c`, `eng_genesis.c`, `eng_sid.c` included again (no `DT_RETIRE` gate);
  ENGINES[0] / [17] / [18] real; `ENG_SUBST` without them, and CHIP TRIO -> SID, FMTONE -> GENESIS (as 1.5);
  `ENGINE_ORDER` {20, 25, 24, 18, 0, 17, 14}, `NENG_SHOWN` 7 (the EDIT layer's keys 1-7, INIT CLEAN after);
  `ENG_MEM_PART` 416 (GENESIS's notes); `N_ENGNAME` names them; favourites rows GENESIS 1, SID 13 (ANALOG its own 0).
  Note: `ENG_SUBST` 0 means kept, so nothing can be substituted onto ANALOG. **The name:** the splash PHANTOM / TRACKER
  (glyphs R K E; TRACKER in 4 px cells like PHANTOM), MENU's about, `FELUCCA_VERSION` v0.9.5, the package
  `phantom-tracker-VER.fwsc` (build.py, make_site.py), the editor, the installer, the emulator page, fmhub.json.
  **Tests:** `sid_test` and `genesis_test` back in `run_rondo.sh` (from DT 1.5, their reference MORPH now: WAVES gone;
  GENESIS within 5 dB, it sits ~4 dB over MORPH's sustained voice; the LOFI / DUNGEON KIT checks dropped with MOD KIT);
  `engines_test` checks the seven keys and the substitutions; `notes094_test` 5 now PHANTOM over the bed and FEED 4 on
  GENESIS / ANALOG / SID within 6 dB of its FM lead (6 and 7 removed); `phantom_test`'s speech section now "no speech";
  `finish_test` PRESETS steps the three engines' sounds; `rondo_test` FMTONE -> GENESIS; one `ui_test` browse check
  turns on until the engine changes (ANALOG's 25 sounds); **203 known FAILs** still. Editor: ENG 0 / 17 / 18 from
  desc.json, 27 null, RETIRED regenerated (`tests/retired_dump.c`), ENGINE_ORDER, the mock's user preset 5 on GENESIS.
  Demos: `factory_render OUT engines` (tracker-genesis / -analog / -sid / -all.wav: the transmission's FEEDs 2-4 on
  each engine by category). Image 553,216 B (28.3 KB free); pool 354,200.

- **v0.9.6: the FX flipped** (Ed: "our X/Y Kaosspad emulator should be for the sum of the tracks while the pikocore
  glitch effects should be on a per track basis"; he kept FILTR GATE ECHO FLANG, picked "option B" (two effects a
  crosshair, one an axis), and the glitch on the selected FEED with black keys adding). **The FX PAD on the mix**
  (`fxpad.c` rewritten): `xy_master` in `fx.c perf_master` (MASTER, the glitch bus added, then the pad, then
  master_out; `perf_master` now runs when `perf || xy_want()`): `xm_filter` (perform's PF_SVF, left LP to index 10,
  right HP to 52, sides cross through the centre), `xm_gate` (pf.ph's clock; up 1/16 / 1/32, down 1/8 / 1/4, depth
  by 24 from the centre), `xm_flange` (its own line `xyf`, two bars, feedback +-0.74), `xm_echo` (`xyb[2][8192]` at
  11 kHz: the per-FEED pads' 32 KB; left 1/8 straight, right dotted-1/8 ping-pong, feedback to 0.85, the time gliding,
  `XM_TAIL` 3 s ringing after it is let go). `XM_DEAD` 2 around 64 is off; `xy_val_t.type` unused; `xm_card` the cards'
  text; `xy_trk` 0 open, 0xFF closed. PRESETS on the pad says the four; ALGORITHM picks the FEED as anywhere. Prints:
  `fxp_step` on FEED 1's steps only, place = step, ty bit 0 / bit 4 the crosshairs; an ATTRITION print is one with
  x1's bit 7 (`FXP_ATP`), its ty now the FEEDs it was printed on (`pf_aim`; 0 = all, 0.9.4's); `fxp_bound` turns
  0.9.4's per-FEED prints into the mix's (a step's first); `fxp_count(steps)`, `fxp_clear_pad` (OCT-; ATTRITION's
  kept); `motion_reset` no longer clears prints. **The glitch layer per FEED** (`perform.c`): `pf_aim` (the selected
  FEED | the black keys held or latched; a playing ATTRITION print's `atp_tgt` instead), `pf_tgt` (= pf_aim while
  something runs, else 0), `pf.tgg[k]` each FEED's share of the glitch bus gliding at SL_SLOPE (`fx.c mix_part` /
  `mix_spread` split the FEED's L R into `glb_l` / `glb_r`, mixsum_t `gl` `gr` on the second core); `perf_master`
  runs `perf_block` on the bus only, `perf_pre` (THROW WASH) takes it, `perf_throw` only for FEEDs with a share; JUMP
  only on `pf_aim`'s FEEDs (`seq.c seq_play`). `pf.live` vs `pf.busy` (busy also while the shares glide back). The
  black keys are targets now (PF_M1 + k, never a mute; the GLO layer's solo still mutes through them); their cells
  read FX, lit when targeted; LEDs likewise. **The keys:** `PF_R4` (REPEAT 1/4) and `PF_U1..U4` appended (stored
  numbers); `PF_REPEAT` includes R4; `pf_looped` / `pf_den`; the loop **mono** (`PB_FRAMES` 16384, `pb_put` the mean,
  `PB_AT`; 743 ms: R4 to ~81 BPM); `PF_DEF` Ed's layout (1/4 1/8 1/16 1/32, REV ECHO OCT+ OCT-, HPF LPF REVERB STOP,
  USER 1..4); `pf_user[4][2]` / `PF_UDEF` (1/16+HPF, REV+REVERB, STOP+ECHO, OCT++REVERB), expanded in `perf_begin`
  (`pf_user_bits`), their order set on the press; stored in `favorites.factory[12][0..7]` (+1, 0 default, 0xFF none)
  with `[12][8]` 0x5A (`settings_persist.c` zeroes the row without it); `ui_layer.c fx_user_of / fx_user_put /
  fx_user_turn / fx_user_held`, `FX_USER_FOOT`; `ui_input.c`: ALGORITHM / SELECT with a USER key held. PF_NAME:
  REVERB ECHO STOP (the throws and TAPE STOP renamed). **Tests:** `fxpad_test` rewritten (the mix's four, cards,
  ringing out, prints, 0.9.4's prints, ends, cost 13 %); `glitch_test` new (CRUSH on the selected FEED only, black keys,
  REPEAT 1/16 leaves the other live, 1/4, USER, JUMP, ATTRITION print targets, the throws); `livefx_test` selects T2;
  `ui_test`'s inherited map checks moved to the new layout plus a USER check (still **203 known FAILs**);
  `ui_render`'s pad scenes. Demo: `factory_render OUT fx` (fx-tour.wav, the cues on stdout). Image 555,376 B (26.2 KB
  free: 5.7 KB over the margin); RAM .data+.bss 42,232; pool 350,664.

- **v0.9.7: the sampled drum sets scrapped** (Ed: "scrapping our four sampled drum sets in order to keep FM and the
  Signal knobs"; room for kits built on the engines and the "glorpy" frog synth). `tools/gen_factory_kits.py --header`
  writes only DEBRIS's tapes now (`--sets` writes the hits too, as 0.9.2; the tapes regenerate byte-identical);
  `eng_cycle.c`'s SET path (`fk_*`, `FK_IS`, `CK_SET_DESC`) under `#ifdef FK_NSET`, unbuilt; `ck_kit_set` 0, so
  `P_LN7` is 0 on every kit and a sound with SET 1 / 2 plays the synthesis. Kits 16..19 kept their numbers (songs,
  favourites, the RETIRED map) as new synthesized kits: GLITCH HOP, CHILLWAVE (kit DECAY +4: +8 rang past
  `rondo_test`'s 10 s at DECAY 127), TAPE DECK, WONK (`CK_KITS`, `CK_PRESETS`, the editor's mirror). Tests:
  `finish_test` 8 (the 20 kits synthesized and within 0.4–2.5x CAPSULE's kick, a SET plays the synthesis, the choke),
  `factory_render OUT sets` / `demo` on the new kits, `test_web` (lane 90 is BELL again). Image 487,016 B (94.5 KB free,
  74.5 KB over the margin).

- **v0.9.8: GLORP** (engine 28, `eng_glorp.c`; Ed: "the viral glorpy frog synth", "an almost automatic wah effect that
  opens and closes based on how hard the keys are pressed"; of six sketches, `tools/glorp_proto.py` A..F, he picked F,
  the vowel wah). A glottal pulse (sin^2 over 40 % of the period) + a saw (BUZZ), three Chamberlin band-passes in
  parallel (1, 0.6, 0.25; Q 10 / 12 / 14, +6 at velocity 127 for F1 F2) on Peterson & Barney's OO OH AH EH EE, a tanh
  (GLOOP: Q and drive x 0.5..1.5); the mouth rests on OO and opens towards VOWEL by (1 - e^-t/A) e^-t/D x min(1, 1.2 v)
  x WAH, D = (70 + 200 v) ms x SPEED's 0.25..2.26, A = ATTACK's 1..81 ms; velocity over 0.75 opens past VOWEL (to one
  vowel more); DIP: the pitch from up to +70 % x v down in 25 ms; SENS blends the velocity with a fixed 0.8 (the FM-1's
  keys have one velocity). Per control block; Q12 filters clamped at +-250000 (no 64-bit in the sample loop); a DC
  blocker after the tanh. **State slots:** the opening's two envelopes and the dip in `s[2] s[3] s[6]` (voice.c puts
  ph[] and the kept s[] back on a retrigger: a mono retrigger must open again), the filters in `s[0] s[1] s[4] s[5]
  ph[1] ph[2]`, `.keep` 0xB3. `glo_formants` (the formants from the state) is what the test reads. Registered:
  `NENGINES` 29, `ENGI_GLORP`, ENGINES[28], `ENGINE_ORDER` {20, 25, 24, 18, 0, 17, 28, 14}, `NENG_SHOWN` 8 (EDIT layer key
  7, NOISE 8), N_ENGNAME, PRESET_CAT "BBBLKKPL", ENG_VCOST 140 (host 0.5 x MORPH), MIDI_CC_ENG 0x14 (CC74 VOWEL, CC71
  GLOOP), favourites row 11 (0.9.3 VOX's). 8 presets (GLORP = sketch F, FROG BASS, TOAD, GLOOP LEAD, BUBBLE KEYS, WAH
  CHORDS, OOZE, SWAMP TALK). It ignores `m->cutoff` / `m->shape`: they carry the FEED's ENV / LFO -> FILTER / SHAPE
  (voice.c), and the presets' FENV pushed VOWEL to EE in the first draft. Tests: `engines_test` (the presets' level, the wah opens with velocity and
  closes, WAH 0, SENS 0, VOWEL, DIP, heard; cost; `glorp.wav` sketch F's riff on each sound), `sid_test`'s table check,
  `ui_render` glorp_home / glorp_edit1 / glorp_edit2 / glorp_edit_layer, `test_web` (eight engines; the editor's ENG 28
  from desc.json). Demos: `factory_render OUT glorp` (glorp-bass.wav, glorp-bass-lead.wav: the transmission on GLITCH
  HOP with GLORP on FEED 3, then FEED 4 too). Image 489,120 B (92.4 KB free).

- **v0.9.9: two fixes from upstream Felucca 1.8.2** (a scan of the other FM-1 firmwares, Ed: "knock out 1"). (1) **The
  newer FM-1 flash part**: `project.c flash_part_ok` takes 0x856014 and the UC25HQ80's 0xB36014. Before, such a unit
  ran RAM-only: nothing saved, and `main.c ota_session` (gated by `flash_ok`) refused updates, so it could not leave
  the firmware but through the official one. `persistence_test` checks both ids and a few others. The emulator
  (`web/emu/felucca_web.c`) still says 0x856014. (2) `fm6_core.c`'s AMS: the Q30 mantissa shifted right by
  30 - exponent (Felucca #304; it was shifted left and overflowed). Only FM6 (retired) reaches it: no Phantom Tracker
  sound changes. Not taken from 1.8.2 yet: its signed-arithmetic (UBSan) fixes, FM6's 32-bit patch generation.

- **v0.9.10: VULT** (engine 29, `eng_vult.c`; Ed: "add the Vult engine, then with whatever space is left over ... premade
  drum kits"; he meant Vult, written "Vault DSP"). Vult's own code is MIT (github.com/vult-dsp/vult `examples/`; the
  VCV modules' source is not public): a hand port to Q16 of Phd (the phase-distortion oscillator), Rescomb (the
  resonant comb: a half-period delay of note + TUNE in `vlt_buf` int16 Q14, `VLT_N` 512, `VLT_POLY` 3, 12.3 KB of
  .pool; the DC blocker and Saturate_soft in the feedback; TONE sqrt-curved), Ladder.process_euler (`vlt_ladder`, four
  Euler steps of four cubic clippers, 32-bit products) and Saturate_soft (`vlt_sat`: softclip at x / 32 << 5); then a
  DC blocker (the PD wave's) and x1.5. Cutoff = E0 + `m->cutoff` (the FEED's ENV / LFO -> FILTER) + KEYTRK, as ANALOG;
  `vlt_fh` maps it to Vult's CV (note 24 + 120 cv: x 1935 >> 15, not 15 >> 8, half a semitone low before). HOME:
  CUTOFF RESO COMB DRIVE; EDIT 2: SHAPE TONE TUNE KEYTRK; `.keep` 0xF3. **Checked against Vult itself:**
  `tools/vult/make_ref.sh` compiles Vult's ladder and comb with the Vult compiler (npm vult 0.4.15) to float C++ and
  writes their harmonic levels to `tests/vult_ref.h`; `tests/vult_test.c` (in `run_rondo.sh`) runs the port on the same
  five cases: ladder within 0.12–0.21 dB median, 0.53 worst; comb 0.06 / 0.14 (a TONE bug, x 67108 for x 67108864,
  was caught here). Registered: `NENGINES` 30, `ENGI_VULT`, ENGINES[29], `ENGINE_ORDER` {20, 25, 24, 18, 0, 17, 28, 29,
  14}, `NENG_SHOWN` 9 (EDIT layer key 8, NOISE 9), N_ENGNAME, PRESET_CAT "BULFLPKL", ENG_VCOST 300 (host 2.3 x MORPH),
  MIDI_CC_ENG 0x12 (CC74 CUTOFF, CC71 RESO), favourites row 9 (PHYS's). Tests: `engines_test` (presets' level within
  0.1 dB of MORPH, CUTOFF, COMB, SHAPE, RESO bounded, cost; `parts_clear` now zeroes `vage`: DEBRIS seeds its grains by
  the voice's age, and its SHIMMER check moved with every new test before it), `sid_test`'s table check, `ui_render`
  vult_home / edit1 / edit2, `test_web` (nine engines). Demos: `engines_test DIR` vult.wav (the riff on each sound),
  `factory_render OUT vult` (vult-bass.wav, vult-bass-lead.wav). Licences: `LICENSES/MIT-Vult.txt`, CREDITS, LICENSING.
  `tools/build.py --release` takes two-digit Z / W now (0.9.10). Image 491,632 B (89.9 KB free); pool 362,952 of
  385,024 (22 KB free).

- **v0.9.11: the kit's MODELs and ten kits on them** (Ed: "engine flavoured voices plus kits that use them ... target
  10 usable but also unique bespoke kits"). `eng_cycle.c`: each lane's MODEL in `P_LN4 + lane` (the DRUM lane levels
  the kit never used; M8's SET was P_LN7): `ck_model` (1 FM, 2 CHIP, 3 COMB, 4 BLOOP; 0 and anything else NOISE),
  `params.c param_fit` maps an out-of-range MODEL to 0 (older sounds hold 127 there; every load path fits), so old
  songs play as before (a 0.9.2–0.9.6 SET 1 / 2 in P_LN7 is now PERC's FM / CHIP). `ck_model_render` (FM: two
  operators, SHAPE the ratio from `CK_HAT_RATIO` / `CK_PERC_RATIO`, COLOR the index on its own faster envelope in s[3];
  CHIP: noise frames (441 samples) then a stepped tone, triangle or band-limited pulse, 32 volume steps, 7 bits, the
  hat an LCG or a 127-step loop; COMB: Vult's Rescomb struck, its line `CKC_N` 384 samples of the part's `vlt_buf`
  (moved here from eng_vult.c with `vlt_sat` / `vlt_isqrt`: a part plays one engine, so a kit's four lanes share
  VULT's three lines; zeroed at each COMB hit), the kick struck by one sine period; BLOOP: GLORP's glottal pulse
  through one Chamberlin band-pass opening from 250 Hz, the low mixed in for body). `ck_kit_t` has a 7th value, the
  MODEL (`ck_kit_load`, `engines.c eng_own_values`); `CK_MODEL_DESC` ("BD MODL" ..); page **MODEL** (`params.c`, after
  LANES 2; `ui.c page_visible`: the kit's). Kits 20..29: SIXTEEN BIT, BREADBIN, RESONATOR, POND, GLITCHWAVE, VAPOR FM,
  ARCADE, FROGBEAT, FOUNDRY, TIN CAN (levels matched by hit to CAPSULE's, `/tmp` harness; PRESET_CAT 30 D's).
  Tests: `finish_test` 8b (the ten named, each with a model, each model in 3+ kits, kicks 0.4–2.5x CAPSULE's, every
  model on every lane sounds / bounded / not NOISE's, 127 -> NOISE, labels); `ui_test`'s inherited DRUM lane check
  accepts the kit's MODEL 0 (still 203 known FAILs); `ui_render` kit_model; `test_web` (the editor's CK_KITS with
  the 7th value, lanes 87..90 `CK_MODEL`, the presets from desc.json). Demos: `factory_render OUT kits` (kits-solo.wav:
  each kit 4 bars of a glitch-hop groove alone; kits-mix.wav: each under the transmission). Image 494,788 B (86.8 KB
  free); pool unchanged.

- **v0.9.12: ten real kits** (Ed: "five more genesis kits that try to emulate actual drum kits and five more SID kits
  that do the same"): kits 30..39 FM ROCK, FM JAZZ, FM FUNK, FM EIGHT, FM NINE (all lanes FM), SID ROCK, SID EIGHT, SID
  NINE, SID JAZZ, SID BREAKS (all CHIP); tuned with a host harness against real-kit targets (kick 50–75 Hz, snare ~190 Hz,
  closed hat 20–60 ms, the open hat / cymbals long, toms 130–230 Hz; VARI and VELO up for a played feel). The FM model's
  hat: its index falls with the hit (not 3x faster) and a quarter of high-passed noise (sizzle): a crash was rings of
  pure partials before; this changes the 0.9.11 kits' FM hats too. The jazz kits keep the kit DECAY at 0 and OPEN 100
  (`rondo_test`'s 10 s corner check hung at +4 / 110, as CHILLWAVE's did). Tests: `finish_test` 8c (named, all FM / all
  CHIP, the kick's strongest frequency 40..90 Hz over its first 120 ms (`out_peak_hz`), the open hat longer than the
  closed, kicks 0.4–2.5x CAPSULE's), 8b's count 40. Editor: CK_KITS / presets mirrored. Demos: `factory_render OUT
  real` (real-solo.wav: each kit 3 bars of a rock / funk groove with ghost notes and a tom fill, then a crash;
  real-mix.wav: each under the transmission). Image ~495 KB (86 KB free).

- **v0.9.13: the FM cymbals** (Ed: "The SID kits are perfection ... the rhythmic dings ... sound so similar across each
  of those kits ... variety of high hats, ride cymbals, and crashes"). `ck_model_render` takes the voice's note; the FM
  model's HAT lane is its own case: a self-fed-back modulator (`flt` its last sample; feedback 2.5–3 radians, where a
  one-sample feedback FM turns chaotic: below ~2.4 it is a periodic saw, the "ding"), the carrier at `CK_HAT_RATIO`
  bent by the modulator (index on the hit's envelope) and by the noise itself (`kn`), plus first-difference noise
  (`w - w0`, w0 the LCG's previous state >> 16: no filter state) as the sizzle, all scaled by COLOR. By the note: a hat
  (42 44 46), a ride (51 59: feedback ~2, a ping, its index falling at half the hit's speed), a crash (49 52 55 57: all
  noise, the index held). Measured with a host harness (`tonal`: the share of the 8 strongest of 1024 bins; SID's
  hats ~0.05): FM hats 0.03–0.33, crashes 0.04–0.07, rides 0.5–0.85 (the ping); centroids 4.4–8.7 kHz by kit. The FM
  kits' hat lanes made distinct (ROCK bright 112 / COLOR 110, JAZZ dark 76 / 50 / ratio 1.73, FUNK 124 / 120 / 5.19,
  EIGHT 104 / 30 / 1.41, NINE 127 / 127 / 4.60) and FUNK / EIGHT / NINE's OPEN raised (90 / 95 / 100) for crashes that
  ring. Demo: `factory_render OUT cymbals` (cymbals.wav: each FM kit, then each SID kit: a bar of closed hats, one of
  open hats, one on the ride, one of crashes).

- **v0.9.14: GENESIS fixed** (Ed, after the first flash of 0.9.13 on the FM-1: "everything seems to run smoothly however
  ... a large chunk of the presets just return static"). Not the cymbals: GENESIS's presets never loaded their operators
  (GEN_OPS) since 0.9.5. Dungeon Tracker 1.5 called `gen_ops_load` from `ui.c apply_preset_to` / `set_engine_of` and
  `morph.c`; M4 retired GENESIS and dropped those calls, 0.9.5 brought the engine back without them, so every
  operator sat at the defaults (LVL 127, SUS 127) and the chained algorithms (ALG 1, 3, 4, 5 with feedback) were noise:
  12 of 24 (GEN BASS SLAP BASS ROCK BASS GEN PIANO MUSIC BOX MARIMBA SITAR GUITAR SAW LEAD SYNC SCREAM HARMONICA
  LASER). The host tests missed it: `hostsim.c host_preset` skipped them too, and the level checks passed on the
  noise. Now `engines.c eng_own_values` sets GENESIS's operators (so `retire.c`'s conversions get them too), called
  from `ui.c apply_preset_to` / `set_engine_of`, `morph.c morph_make` and `hostsim.c`; the editor's mock mirrors
  `GEN_OPS` (`descdump` dumps them, `test_web` compares). Tests: `finish_test` (GENESIS chosen and every preset: the
  operators are GEN_OPS, each sounds, each a tone by first-difference brightness < 0.7: LASER 0.09–0.48, the static
  1.2–2.0; fails without the fix); `notes094_test` 5 compares each engine's first LEAD now (GENESIS's first sound, GEN
  BASS, is a pluck 21 dB down at its sustain: it passed only as static). **A GENESIS sound saved on 0.9.5–0.9.13 keeps
  the default operators**: step PRESETS off it and back. Demos: `genesis_test DIR` (each preset;
  phantom-tracker-0.9.14-genesis-presets.wav, all 24 in a row), `factory_render OUT engines` tracker-genesis.wav.
  Image 496,680 B (84.9 KB free).

- **v0.9.15: the FX flipped back, the levels** (Ed after flashing 0.9.14: "flip the per track - master effects back to how
  it was with the X/Y being per track and the glitch effects being over all"; "all of our drums need to be increased in
  volume"; "the phantom theremin track is way too loud ... it doesn't have a volume control"; he chose: 0.9.6's four
  effects on each FEED's pad, black keys leave a FEED out, PHANTOM's LEVEL on its EDIT page).
  **The FX PAD per FEED** (`fxpad.c`): `xy_trk` the FEED it is open on (`ui_input.c xy_open(song.sel)`: ALGORITHM moves
  it, re-centred); `xy_pr[NTRK][2]`; per-FEED state `xpf[NTRK]` (xs_t: FILTR's SVF, GATE, FLANG with its own line
  `xyf[NTRK][FL_LEN]` mono, the ECHO send `es`); `xy_feed(t, b, d, n)` in `fx.c mix_part` / `mix_spread` after the
  SLICER and the mute, before the level (a SPREAD side `d` narrows with FILTR / FLANG, gated with GATE). ECHO: one
  echo (`xyb`, 32 KB) fed by each FEED's send (`xy_send_to`, gliding in the mix loops) into the bus `xeb`
  (`mixsum_t.e` on the second core), run in `perf_master` (`xy_echo`), its time / feedback from `xy_echo_lead` (the
  strongest crosshair). `xy_feed_want(k)`, `xy_want()` (the echo). Prints: `place` = FEED << 6 | step in a store
  marked `rsv` `FXP_FEEDS` (project mark `PROJ_FXP_MARK2` 0xF1); an unmarked store (0.9.6–0.9.14's mix prints) plays on
  every FEED, `fxp_feeds` splits it four ways when printed into; `fxp_find(st, k, place)`; `fxp_count` / `fxp_clear_pad`
  for the open FEED (closed: all). `ui_graph.c graph_xy` draws the open FEED's steps.
  **The glitch on the mix** (`perform.c perf_begin`): `pf_aim` = 15 less the black keys held / latched (an ATTRITION
  print's `atp_tgt` as before); the glitch bus machinery of 0.9.6 kept (it now carries every FEED not left out). The
  black keys' cells read `FX` / `DRY` (lit) (`ui_layer.c`), their LEDs lit while held.
  **Levels:** `eng_cycle.c CK_GAIN` 32 (x2, the kit's output), `fx.c feed_lvl` (every FEED not on the kit, and
  PHANTOM, x `MIX_TRIM` 2584 = -4 dB). Measured (host, the transmission, `/tmp` harness with a limiter probe): the
  pad FEED drove the master limiter ~8 dB (mean) into reduction, drums alone 2 dB under the bass / lead alone; now
  2.3+ dB over them (three kits), the limiter ~5.5 dB, the mix's loudness within 0.3 dB. **PHANTOM's LEVEL**: page 1
  KNOB 4 (`phantom.c ph_param`, SIG's place; PRESETS still steps SIG), `PH_LEVEL_DEF` 86 (-13 dB; 106 before, no trim:
  ~14 dB louder), saved at `project.c PROJ_PHLVL_OFF` (the FM6 block's last byte: LEVEL + 1, 0 = the default).
  Tests: `fxpad_test` (per FEED: FILTR on FEED 2 only, FEED 3 untouched and its own pad, GATE, ECHO only its FEED's,
  ALGORITHM moves the pad, prints per FEED, the mix's prints on every FEED and split, the marks), `glitch_test` (every
  FEED, black keys DRY, JUMP, ATTRITION targets, throws), `phantom_test` (LEVEL: the knob, heard, saved, a project
  before), `finish_test` (the drums over the bass / lead in the transmission, PHANTOM under it), `rondo_test`'s kick
  peak bound x CK_GAIN, `ui_test`'s inherited map-LED check (the black keys breathe). Demos: `factory_render OUT fx`
  (fx-tour.wav rewritten), a before / after of the levels (`/tmp` harness built on both trees). Image 496,556 B
  (84.9 KB free); RAM .data+.bss 42,696; pool 364,376 of 385,024 (20.6 KB free: the FLANG lines).

- **The second core** (Ed: "worth preemptively enabling the second core by default?"; answered: not before the first flash).
  Built in, OFF by default (MENU > SYSTEM > 2ND CORE ON (BETA), from the next power-on); a misread job or one that times
  out (`fm1_cpu1.h fm1_c1_gave_up` 1 / 2) turns it off for good with "2ND CORE MISREAD: OFF"; a crash in the last boot's
  first 30 s never starts it. `tests/dualcore_test.c` (Felucca's) had stopped compiling (PHYS's PM_COUNT): fixed and
  now in `run_rondo.sh`: 54 scenes, every engine's presets (GLORP, VULT, all 40 kits) the same bit for bit on two cores
  at every split. Not yet taken from JIANT / Felucca 1.8.2: a check word per job, the effects' output range check.

- **Planned for 0.10 (the plan as agreed; built below)**: DRUMS (the kit, 4 voices), PERC (new, sequenced: two lanes,
  each any drum sound of any factory kit, 2 voices), three synth FEEDs with an optional B side (a second full sound on
  the same notes, own LEVEL / PAN / transpose, the rest of the strip shared; 6 voices between them), PHANTOM with its
  LEVEL, MASTER as two columns (L / R meter): two pages of four. **Per-step pan** on every FEED, each hit keeping its
  pan (SPREAD's side signal per voice). **Old saves scrapped** (Ed: no loading of 4-FEED projects / songs / patterns);
  **RANDOM SONG rewritten** to show the new features. RAM, not flash, is the limit (~6 KB of ~35 KB free). Waiting on Ed's CPU check
  of 0.9.15 on the FM-1.

- **LAB tree (Nov 17 2026): `/home/claude/phantom-lab`** (Ed: "make all those changes into an alternative cfw so I
  can test them side by side"): 0.10.5 + DEBRIS at half rate (`eng_morph.c DEB_HALF`, `deb_prev`; the low-pass only
  when closing), three new engines in `eng_lab.c` (STRINGS 30, PLUCK 31 on VULT's comb lines, ORGAN 32 on a per-FEED
  drawbar table), FM retired onto GENESIS (WHEEL -> ORGAN, PHYS -> PLUCK), 8 synth voices while the second core runs
  (`core.h vox_more`, set by `main.c`), 2ND CORE ON when no settings are stored. Not built: the paraphonic filter;
  the web editor not updated (excluded from the LAB package). Tests: `tests/lab_test.c` (in its `run_rondo.sh`),
  `tests/run_all_lab.sh` (every test, no stop). Costs measured with callgrind (instructions a voice-sample: MORPH 101,
  DEBRIS 238 -> 152, STRINGS 94, PLUCK 73, ORGAN 80, VULT 408). Package `phantom-tracker-0.10.5-lab.fwsc`; delivery
  `/home/claude/out/2026-11-17/lab/` (LAB-NOTES.md). Merge into the main tree only if Ed keeps it.
- **The web studio (Nov 17 2026; Ed: "host the emulator on my own website ... let people export a song")**: the emulator
  page has a studio bar: **Record** (the worklet's output as it plays, 24-bit WAV), **Export song** (`web/emu/bounce.js`:
  the worklet's whole wasm memory copied into a worker's instance, then `felucca_web.c web_bounce_stop` / `web_bounce_go`
  play the song (GLO > SONG once: the chain stops itself) or the pattern N times from the top, headless, ~100x real
  time; stems = one pass per FEED with the others' P_MUTE; zip, WAV writer in JS), **Save / Open project file** (a
  `.phantom` JSON of the flash's stored sectors after `web_save_now`: the autosave and FREQ A-F written at once;
  refused while playing), **Reset**. `tools/make_emu_site.sh` -> `build/phantom-tracker-web/` + `.zip` (the page,
  guide.html / manual.html from the markdown, `source/` the tree's tarball without CLAUDE.md (GPL), licences,
  .htaccess / _headers, `web/emu/HOSTING.md` as README.md). Tests: `node web/emu/studio_test.mjs` (deterministic
  bounce, stems, song length, the project round trip). The claude.ai preview artifact saves through the `downloads`
  capability (WAV inside a zip, the project as .json there).
  **Share link** (Ed: "a sharable link"): `#song=` + base64url(deflate-raw("PT" 1, then id / u16 length / bytes)):
  id 0 the autosave object, 1..6 FREQ A..F as stored (absent ones as the bank's cleared record), `worklet.js
  getSong / putSong / packSong / unpackSong`; ~1.1–1.8 K characters a song. Opening one: `putSong` before `web_boot`
  (`felucca_web.c web_obj_put`, music objects only; `web_shared` restores whatever RESTORE LAST says), then PLAY
  (the song if GLO > SONG has sections); the hash is cleared. `studio_test` 4.
- **v0.10.5: no roles** (Ed: any sound on any synth FEED, voices per sound). AUTO voices = the loaded sound's category
  (`ui.c auto_vox_sync` -> `core.h trk_avox`; `CAT_VOX` BASS LEAD PLUCK FX 1, PAD KEYS 4, else 2); VOICES 0..4; `trk_cap`
  shares the synths' 6 in proportion past it; LIST ALL by default, ROLE gone, `list_keep` keeps kits to DRUMS / PERC.
  Tests: `finish_test` 1 (AUTO by sound, sharing, the lists), `ui_test` categories / browse moved to a synth FEED.
- **v0.10.4: FREEZE for REVERSE, PRESETS through every sound** (Ed: "scrap reverse"; HOME PRESETS "stops at SIG 16
  Dark Side"; he chose FREEZE, whole sounds by role, JUMP's reverse kept). `PF_DEF[4]` / `PF_UDEF[1][0]` = PF_FRZ, the
  assign lists skip PF_REV; `presets_turn`'s SIGNAL branch off (SIG on EDIT page 1). `finish_test` 3 rewritten.
- **v0.10.3: the echo's return** (Ed: pops with several effects stacked, CPU 69 %): 0.10.2's headroom made
  `fx_buses`' `mulq15(x << 2, dmix)` overflow past half scale (now `(x * dmix) >> 13`); the delay line writes through
  `dly_knee` (a soft ceiling). `glitch_test` 10. **Check new fixed-point scaling with -fsanitize=signed-integer-overflow
  on a stacked-effects run** (the harness: every FEED's pad, three glitch keys, ATTRITION at random on a RANDOM SONG).
- **v0.10.2: the glitch layer lighter** (Ed: pops under the FX layer's ECHO / REVERB on busy songs, second core on,
  ~75 % playing; spec "As built (0.10.2)"). With the FX layer at work the second core runs `fx_buses` (`fx.c c1_buses`)
  while core 0 runs the glitch bus (`perf_glb`); shedding at 75 % while FX run (`audio.c SHED_TICKS_FX`); ROOM /
  SPRING / the delay line 6 dB more headroom (in / 2, out x 2); the FX layer's black keys toggle DRY (`perform.c
  perf_press`, kept through a menu, FX + OCT- clears). Tests: `dualcore_test` GLITCH scenes (bit for bit, TSAN clean),
  `glitch_test` 9. The 2ND CORE is still OFF by default (Ed's unit: ~20 % less CPU with it on).
- **v0.10.1: fewer presses** (Ed's picks 1 2 3 6 of a workflow review; spec "As built (0.10.1)"). The FX column's
  list: commands, PAN, LEVEL, HOME's four, the rest (`ui_tracker.c tk_fx_list` over `tk_fx_list0`). PERC's HOME knobs
  1 KIT 1 SOUND 2 KIT 2 SOUND (`ui.c home_param`, turned via `pc_source`, unrecorded). ALGORITHM stops at B sides
  (`ui_input.c algo_stops` / `algo_step`: 3 3B 4 4B 5 5B PHANTOM; arrive = flip, leave = unflip); the MIXER on a B stop
  turns / shows B's LEVEL / PAN (`tracks_edit`, `ui_draw.c` SC_TRK, momentary refused), cards show `+B` / `/B` and a
  thin bar (`ui_graph.c draw_tracks`). OCT+ / OCT− page the strip on MIXER, HOME TRACKS / LEVELS (`ui.c strip_flip`,
  `ui.spg_*`, held while the selection is unchanged; no octave change there). Tests: `v010_test` section 4;
  `ui_render` scenes mixer_b, mixer_bstop(_large), perc_home, strip_oct.
- **v0.10: five FEEDs, B sides, per-step pan, the strip** (Ed cleared the build Nov 16; spec section 8 "0.10", "As
  built"). `NPART` 5; PERC is **index 4** (`PERC_TRK`), **shown second** (`core.h FD_AT` / `fd_pos`: FEED numbers,
  ALGORITHM, the SEQ picker, SONG lanes, MENU > MIDI FEED n CH, the black keys, `layer_pick` all go DRUMS PERC SYNTH
  1–3); roles `CLS_*` DRUMS CHORDS BASS LEAD PERC, `CLS_VOICES` {4,3,1,2,2}, `trk_is_kit`, `KIT_VOICES` 6; `TRK_ALL`
  masks; motion `place` uint16. **Saves:** FUNB projects run-length coded as written (`project.c rw_put` / `rr_get`, no
  layout buffer), B sides at the end (unflipped); FREQ patterns rle (`BANK_VER` 3); song slots `SG_VER` 3; everything
  older refused. **The strip** (`ui.c strip_page` / `strip_at`): MIXER, HOME TRACKS / LEVELS, GLO's knobs: DRUMS PERC
  S1 S2 | S3 PHANTOM MASTER-L MASTER-R (`ui_graph.c strip_extra`, `strip_extra_row`; `fx.c mst_pk`, `ui.c
  mst_meter`); GLO black keys mute the five, white F G A B solo shown 1–4, G4 the fifth (`ui_layer.c glo_solo_pos`);
  layer cells of five (`L5_X`, `lc_w`). **Per-step pan:** `voice_t.pan` / `pfix` bit 0 (a step's PAN lock: `core.h
  pan_held`, set by `motion.c motion_step`), `voice.c vpan_render` (each voice alone, its mid and half side by PAN's
  law), `fx.c mix_spread` (L = b + d, R = b − d; SPREAD the same path). Made on the GRID's FX column (PAN). **PERC:**
  the kit engine, two slots (`eng_cycle.c` PC_*: source kit + one of 16 sounds in lanes 2 / 3's PITCH / DECAY,
  `pc_source`, `pc_slot` by the roll lane's parity, `pc_lane_name`, `pc_param_desc`; s[0] = slot | voice^slot << 2 |
  GM note << 8); pages SOURCE / PERC 1 / PERC 2 / PERC MIX / PERC MDL; kits only (`set_engine_of`); `drum_nlanes`;
  the tracker's `TK_PERC`; its COMB lines `pc_comb` (vlt_buf rows 4); its SLICER / REPT row borrowed from DRUMS
  (`fx.c sl_row`, `SL_ROWS` 4). **B sides** (`core.h bsr_t`, `trk[1..3]`): `voice.c b_own` / `b_swap` / `b_enter` /
  `b_leave` (the B sound swapped into the track per note and per block; voices A 0–3, B 4–7 via `vb_base`, side in
  `pfix` bit 1; `track_render` = `track_render1` twice; `engine_block` per side), `ui.c b_set` / `b_flip` (FLIP: the UI
  edits B; completes a pending engine switch), EDIT layer `LY_BSIDE` / `LY_FLIP`, EDIT > B SIDE (`GR_BSIDE`,
  `params.c page_desc`), header `+B` / `/B`. **RANDOM SONG** rewritten (`boot_song.c`: four styles, PERC, pans, B sides);
  the transmission has a PERC shaker. Tests: `tests/v010_test.c` (per-step pan, PERC, B sides), `boot_song_test` 0.10
  checks; UI suite **200 known FAILs** (201 lines with the summary; baseline /tmp/claude-0/b1 at the start). Image
  509,772 B; RAM 47,304 of 57,344; pool 375,792 of 385,024. Release id `FM-1_910` (`build.py`: Y two digits after 0).
  Not done: the web editor's B sides / PERC sources; the CPU on the FM-1 with B sides on.
  **Never `git checkout` a file here: HEAD is Dungeon Tracker's a1b9de8, everything since is uncommitted** (restore
  from the latest out/ source tarball).

Open questions (spec section 10): confirm RELAY LOCK's gestures on the device; the GitHub repo for the hub listing.

Still Dungeon Tracker's: RANDOM SONG (`boot_song.c`), the editor's pages (in Phantom Cosmonaut's look). The project
format still carries Dungeon Tracker's party bytes (`song.clsx`): read past, written 0.

## Target constraints

JieLi AC79 (pi32v2): no float, and **no 64-bit division** (`__udivdi3` is missing: keep divisions 32-bit). The image
must stay under 581,564 B with ≥ 20 KB headroom. The firmware is a unity build: `felucca.c` includes the other sources.
Never draw from the audio path.

## Build, test, run

```sh
# generated tables (once, and after changing tools/gen_*.py)
python3 -c "import sys; sys.path.insert(0, 'tools'); import build; build.generate()"
# firmware (needs the JieLi toolchain and AC79 SDK, see BUILDING.md; tools/get_toolchain.sh)
JIELI_TOOLCHAIN=~/.jieli/toolchain AC79_SDK=~/fw-AC79_AIoT_SDK python3 tools/build.py --release 0.10
# the SIGNALs (after changing tools/gen_signals.py; numpy): writes firmware/src/signals_tables.h
python3 tools/gen_signals.py
# the host tests (plain cc, no toolchain needed): must exit 0
NOCOST=1 sh tests/run_rondo.sh
# the UI suite: 200 known FAILs (201 lines with the summary) (retired-engine checks inherited from Dungeon Tracker 1.5's 223); no new ones
cc -O1 -w -Ibuild/gen -Ifirmware/src -Itests -o build/host/ui_test tests/ui_test.c -lm && build/host/ui_test | grep -c FAIL
# every screen in every palette rendered + layout lint (must report 0 lint findings; LINT_ALL=1 lists past 600)
cc -O1 -w -Ibuild/gen -Ifirmware/src -Itests -o build/host/ui_render tests/ui_render.c -lm && mkdir -p build/ui_new/ppm && build/host/ui_render build/ui_new
# web tests
(cd web && node test_web.mjs)
# browser emulator (clang + the WASI sysroot in ~/wasi: wasi-sdk 25's wasi-sysroot and libclang_rt.builtins), then serve it
sh tools/build_emu_clang.sh && python3 -m http.server -d build/emu 8790
```

`tests/run_tests.sh` (Felucca's suite) needs a firmware build first.

## Working with Ed

Casual and direct. He tests on the real FM-1 and likes audio demos and screenshots. **Ed commits and pushes: never
commit, push or release unless he asks.** Credits stay: Felucca by Leo Kuroshita (Hügelton Instruments), Dungeon
Tracker by Bourbon Weather.
