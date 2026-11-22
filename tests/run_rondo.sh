#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# RONDO's own host checks: the CYCLE engines and the voice layout (tests/rondo_test.c), with demo WAVs in
# build/rondo_demo; the TRACKER and the SEQ + SELECT track picker (tests/tracker_test.c);
# the live effects: FILTR DUCK REPT, ARP held, the master knobs (tests/livefx_test.c);
# the TRACKER's commands SPEED LOOP PITCH (tests/cmd_test.c); the PATTERN BANK, SONG on it, the arrangement recorded, MIDI program changes (tests/bank_test.c); SOUND MORPH (tests/morph_test.c); 1.2's song slots (tests/songslot_test.c). Run from the repo root after the generate step (build/gen):
#   sh tests/run_rondo.sh            (NOCOST=1 skips the timing comparison)
set -e
cd "$(dirname "$0")/.."
[ -f build/gen/felucca_tables.h ] || { echo "run the generate step first (CLAUDE.md, Build)"; exit 1; }
mkdir -p build/host build/rondo_demo
# the Spelunkr groovebox's keys start on C (core.h KEY_OFF 0); the tests press keys by the FM-1's numbering from F
CC="${CC:-cc} -DKEY_OFF=5u"
${CC:-cc} -O2 -w -Ibuild/gen -Ifirmware/src -o build/host/rondo_test tests/rondo_test.c -lm
build/host/rondo_test build/rondo_demo
${CC:-cc} -O1 -w -Ibuild/gen -Ifirmware/src -Itests -o build/host/tracker_test tests/tracker_test.c -lm
build/host/tracker_test
${CC:-cc} -O1 -w -Ibuild/gen -Ifirmware/src -Itests -o build/host/livefx_test tests/livefx_test.c -lm
build/host/livefx_test
${CC:-cc} -O1 -w -Ibuild/gen -Ifirmware/src -Itests -o build/host/cmd_test tests/cmd_test.c -lm
build/host/cmd_test
${CC:-cc} -O1 -w -Ibuild/gen -Ifirmware/src -Itests -o build/host/bank_test tests/bank_test.c -lm
build/host/bank_test
# 0.9.4: Ed's notes on 0.9.3 (EDIT's No., the FX PAD drawn live, CHORD KEYS, ATTRITION prints: tests/notes094_test.c)
${CC:-cc} -O1 -w -Ibuild/gen -Ifirmware/src -Itests -o build/host/notes094_test tests/notes094_test.c -lm
build/host/notes094_test > build/host/notes094.txt || { grep FAIL build/host/notes094.txt; exit 1; }; tail -1 build/host/notes094.txt
${CC:-cc} -O1 -w -Ibuild/gen -Ifirmware/src -Itests -o build/host/morph_test tests/morph_test.c -lm
build/host/morph_test
${CC:-cc} -O2 -w -Ibuild/gen -Ifirmware/src -Itests -o build/host/signal_test tests/signal_test.c -lm   # Phantom Cosmonaut M2: MORPH and the SIGNALs
build/host/signal_test build/rondo_demo
${CC:-cc} -O2 -w -Ibuild/gen -Ifirmware/src -Itests -o build/host/engines_test tests/engines_test.c -lm   # DEBRIS FM, the engine keys
build/host/engines_test build/rondo_demo
# Phantom Tracker: the engines back from Dungeon Tracker: SID (1.3, tests/sid_test.c) and GENESIS (1.2, tests/genesis_test.c)
${CC:-cc} -O2 -w -Ibuild/gen -Ifirmware/src -Itests -o build/host/sid_test tests/sid_test.c -lm
build/host/sid_test build/rondo_demo
${CC:-cc} -O2 -w -Ibuild/gen -Ifirmware/src -Itests -o build/host/genesis_test tests/genesis_test.c -lm
build/host/genesis_test
${CC:-cc} -O1 -w -Ibuild/gen -Ifirmware/src -Itests -o build/host/m6_test tests/m6_test.c -lm   # M6: the jitter, the starfield
build/host/m6_test
${CC:-cc} -O1 -w -Ibuild/gen -Ifirmware/src -Itests -o build/host/finish_test tests/finish_test.c -lm   # VOICES, SCRAMBLE, the last pieces
build/host/finish_test
${CC:-cc} -O1 -w -Ibuild/gen -Ifirmware/src -Itests -o build/host/v010_test tests/v010_test.c -lm   # 0.10: per-step pan, PERC, B sides
build/host/v010_test
${CC:-cc} -O1 -w -Ibuild/gen -Ifirmware/src -Itests -o build/host/contort_test tests/contort_test.c -lm   # M2: the knobs record and lock
build/host/contort_test
${CC:-cc} -O1 -w -Ibuild/gen -Ifirmware/src -Itests -o build/host/phantom_test tests/phantom_test.c -lm   # M3: PHANTOM, the jam
build/host/phantom_test
${CC:-cc} -O1 -w -Ibuild/gen -Ifirmware/src -Itests -o build/host/fxpad_test tests/fxpad_test.c -lm   # M5: the FX PAD (0.9.6: the mix's)
build/host/fxpad_test
${CC:-cc} -O1 -w -Ibuild/gen -Ifirmware/src -Itests -o build/host/glitch_test tests/glitch_test.c -lm   # 0.9.6: the FX layer per FEED
build/host/glitch_test
${CC:-cc} -O1 -w -Ibuild/gen -Ifirmware/src -Itests -o build/host/vult_test tests/vult_test.c -lm   # 0.9.10: VULT against Vult's own
build/host/vult_test
${CC:-cc} -O2 -w -Ibuild/gen -Ifirmware/src -Itests -pthread -o build/host/dualcore_test tests/dualcore_test.c -lm   # the second core: bit for bit one core's
build/host/dualcore_test
${CC:-cc} -O1 -w -Ibuild/gen -Ifirmware/src -Itests -o build/host/glide_test tests/glide_test.c -lm   # 1.5: GLIDE
build/host/glide_test
# 1.0: the pitch wheel records as automation (tests/bend_test.c)
${CC:-cc} -O2 -w -Ibuild/gen -Ifirmware/src -Itests -o build/host/bend_test tests/bend_test.c -lm
build/host/bend_test
${CC:-cc} -O1 -w -Ibuild/gen -Ifirmware/src -Itests -o build/host/persistence_test tests/persistence_test.c -lm
build/host/persistence_test > build/host/persistence.txt || { grep FAIL build/host/persistence.txt; exit 1; }
tail -1 build/host/persistence.txt                    # (the flash: the PATTERN BANK's pairs too)
# 1.2: the SONG SLOTS, A..H, whole songs (tests/songslot_test.c: round trips, power cuts, 1.1's projects moved in)
${CC:-cc} -O1 -w -Ibuild/gen -Ifirmware/src -Itests -o build/host/songslot_test tests/songslot_test.c -lm
build/host/songslot_test > build/host/songslot.txt || { grep FAIL build/host/songslot.txt; exit 1; }
tail -1 build/host/songslot.txt
# the audio tools build (tests/chip_demo.c: CHIP's demo, run by hand)
${CC:-cc} -O1 -w -Ibuild/gen -Ifirmware/src -Itests -o build/host/chip_demo tests/chip_demo.c -lm
# the editor: the firmware's side of the protocol (tests/editor_test.c, both layouts) and the web editor against the
# firmware's tables (tests/descdump.c -> build/host/desc.json; web/test_web.mjs, when node is there)
${CC:-cc} -O1 -w -Ibuild/gen -Ifirmware/src -Itests -o build/host/editor_test tests/editor_test.c -lm
build/host/editor_test > build/host/editor.txt || { grep FAIL build/host/editor.txt; exit 1; }
tail -1 build/host/editor.txt
${CC:-cc} -O1 -w -Ibuild/gen -Ifirmware/src -o build/host/descdump tests/descdump.c -lm
build/host/descdump > build/host/desc.json
if command -v node >/dev/null 2>&1; then node web/test_web.mjs > build/host/web.txt || { grep FAIL build/host/web.txt; exit 1; }; tail -1 build/host/web.txt; fi
# Dungeon Tracker's power-on song (tests/boot_song_test.c; WAVs of four power-ons in build/boot_song)
mkdir -p build/boot_song
${CC:-cc} -O1 -w -Ibuild/gen -Ifirmware/src -Itests -o build/host/boot_song_test tests/boot_song_test.c -lm
build/host/boot_song_test build/boot_song
# SELECT navigates (tests/nav_test.c: the pages, the MENU's rows, the tempo's other ways, MENU > SELECT BPM)
${CC:-cc} -O1 -w -Ibuild/gen -Ifirmware/src -Itests -o build/host/nav_test tests/nav_test.c -lm
build/host/nav_test
# SAVE > TOOLS' NEW, NEW + DEFAULT SOUNDS and their undo (tests/new_song_test.c)
${CC:-cc} -O1 -w -Ibuild/gen -Ifirmware/src -Itests -o build/host/new_song_test tests/new_song_test.c -lm
build/host/new_song_test
# drums written one at a time share a step (tests/kit_entry_test.c)
${CC:-cc} -O1 -w -Ibuild/gen -Ifirmware/src -Itests -o build/host/kit_entry_test tests/kit_entry_test.c -lm
build/host/kit_entry_test
