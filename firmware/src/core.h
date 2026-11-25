/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* FELUCCA core types: tracks, voices, engines, parameters.
 * Four tracks, each a synth part: its own engine, preset, parameters, voices and 64-step
 * pattern. The parts share one budget of NVOICE sounding voices (voice.c). Drums are the DRUM
 * engine (General MIDI map) on any part; the SAMPLE engine's PERC set is retired (drum_from_perc).
 * Sections: sizes, parameters, voices and engines, tracks and the song, system. */
#include <stdint.h>

/* ------------------------------------------------------------ sizes --- */
#define NVOICE 8                 /* voices per part (each part's array) */
/* RONDO: a fixed voice layout. Each track has its own cap and the budget is their sum, so no track ever takes a voice from
 * another: by default T1 DRUMS 4 (KICK SNARE HAT PERC), T2 CHORDS / PADS 4, T3 BASS 1 (mono), T4 LEAD 3 (TRK_CAP);
 * Dungeon Tracker: each track's class sets its cap (trk_cap, THE PARTY below), the sum still at most the budget */
#ifndef RONDO_LAYOUT
#define RONDO_LAYOUT 1           /* 0: Felucca's shared budget of 8 and its power-on sounds (the upstream tests) */
#endif
/* RONDO: Program Change in is ignored by default. A sequencer such as the QY70 sends its own (GM / XG) program numbers
 * at the start of a song, which would load other sounds onto the tracks; RONDO_PC_IN=1 builds Felucca's behaviour */
#ifndef RONDO_PC_IN
#define RONDO_PC_IN (!RONDO_LAYOUT)
#endif
#if RONDO_LAYOUT
#define NBUDGET 12               /* voices sounding at once, all tracks */
static const uint8_t TRK_CAP[5] = {4, 3, 1, 2, 2};
#else
#define NBUDGET NVOICE
static const uint8_t TRK_CAP[5] = {NVOICE, NVOICE, NVOICE, NVOICE, NVOICE};
#endif
/* the keys' first note: key 0 is F3 on the FM-1 (5: F, 53), C3 in the Spelunkr groovebox (0: C, 48; the page draws
 * C3 .. D5, the FM-1's 16 white and 11 black keys from C) */
#ifndef KEY_OFF
#define KEY_OFF 0u
#endif
#define NPART 5                  /* parts: 0.10: DRUMS PERC SYNTH 1..3 */
#define NTRK NPART               /* tracks (the formats and the protocol count these): every track is a part */
#define TRK_ALL ((1u << NTRK) - 1u)  /* a bit a FEED: all of them */
#define NSTEP 64
#define HALF_FRAMES 128          /* I2S half buffer: 2.9 ms at 44.1 kHz (a key waits 0..1 half, then plays 1 half later) */
#ifndef FELUCCA_SLICE
#define FELUCCA_SLICE (!RONDO_LAYOUT)   /* the SLICE engine (eng_slice.c), engine 13; FELUCCA_SLICE=0 builds without it
                                         * (Dungeon Tracker: without, engine 13 reserved: engines.c ENG_SLICE_GONE) */
#endif
#ifndef FELUCCA_FM4
#define FELUCCA_FM4 0            /* the DIGITAL engine (eng_digital.c, four-operator FM): kept in the tree, not built
                                  * by default; replaced by FM6, its sounds convert (fm4_convert.c) */
#endif
#ifndef FELUCCA_CPU2
#define FELUCCA_CPU2 1           /* the second core (src/felucca.c): 1 = built in, MENU > 2ND CORE ON lets it render
                                  * some parts; 0 = none of it */
#endif
#define C1_NP (FELUCCA_CPU2 ? NPART : 1)   /* the copies of the state a part's render writes that parts shared before the
                                            * second core (mod.c, voice.c SPREAD, eng_formant.c): one each with two cores,
                                            * one for all with one, as up to 1.6 */
#define C1_PI(t) ((uint32_t)((t) - trk) % C1_NP)   /* a part's copy */
#define ENGI_SLICE 13u           /* SLICE; Dungeon Tracker builds without it: 13 stays reserved (its sounds load as
                                  * SAMPLE, project.c proj_slice), so 14.. keep their numbers */
#define NENGINES 30              /* Phantom Tracker 0.9.10: VULT (29); 0.9.8: GLORP (28). Phantom Cosmonaut: FOLDER GRANULAR FM (22..24), DEBRIS VOX (25, 26), NUMBERS (27); RONDO: SLICE (13), then CYCLE KIT (14) and CYCLE TONE (15), eng_cycle.c; 1.1: WAVES (16);
                                  * 1.3: SID (17) */
#define ENGI_DIGITAL 1u          /* reserved without FELUCCA_FM4: never selectable (eng_ok), its sounds load as FM6 */
/* the engines one can pick (PRESETS, the EDIT layer, the editor), in the display order of engines.c ENGINE_ORDER.
 * RONDO: all but two, so the EDIT layer's 16 white keys still hold every engine, INIT and CLEAN (ui_layer.c LY_INIT):
 * SLICE (13) is not offered (its sounds in projects and user presets still play), and in FELUCCA_FM4 builds (tests only)
 * DIGITAL is offered instead of NOISE */
#define DT_RETIRE (RONDO_LAYOUT && !FELUCCA_FM4)   /* Dungeon Tracker 1.1: eight engines kept, the rest retired (retire.c);
                                                    * 1.5: the four families, FM WAVE PHYS SUB, two engines each */
#if DT_RETIRE
#define NENG_SHOWN 8             /* Spelunkr: the kits, SAMPLE, GENESIS SID, ANALOG MORPH, GLORP FM (engines.c ENGINE_ORDER; the rest retired) */
#define NENG_KEYS ((uint32_t)NENG_SHOWN)   /* the EDIT layer's white keys: the engines, INIT, CLEAN */
#else
#define NENG_SHOWN (NENGINES - 2)
#define NENG_KEYS 14u            /* 1.1: the EDIT layer's 16 white keys: the first 14 engines, INIT, CLEAN (NOISE: PRESETS only) */
#endif
#define UP_SLOTS 32u             /* user presets (upreset.c) */

/* ------------------------------------------------------- parameters --- */
/* the engine interface (Felucca-Engines, MIT: engines/, a git submodule): param_desc_t and F_*, voice_t,
 * vmod_t, preset_t with FX() / PAT(), engine_t, NELEM */
#include "../engines/src/felucca_engine.h"
/* POLISH (the Spelunkr page, Nov 25 2026): its compressor on one track's block, after the FX PAD and before the level
 * (fx.c mix_part / mix_spread, phantom.c for LEAD: k NTRK); d the SPREAD side or 0. Nothing elsewhere */
#ifndef POLISH_TRACK
#define POLISH_TRACK(k, b, d, n) ((void)0)
#endif
/* .. and on one drum of a kit (Ed, Nov 25: "any kind of kick and snare as well as either option on the two note Perc
 * track"): voice.c renders that voice on its own (POLISH_VOICE_ON: nonzero), squeezes it (POLISH_VOICE), adds it in */
#ifndef POLISH_VOICE
#define POLISH_VOICE_ON(t, v) 0
#define POLISH_VOICE(t, v, b, n) ((void)0)
#endif

enum {                          /* per-track parameters */
    P_LEVEL,
    P_ATK, P_DEC, P_SUS, P_REL,
    P_ED_FLT, P_ED_PIT, P_ED_SHP, P_ED_FX,     /* P_ED_FX: Felucca's unused ENV > FX; Dungeon Tracker 1.0: BEND (P_BEND) */
    P_LRATE, P_LWAVE, P_LPHASE, P_LFADE,
    P_LD_PIT, P_LD_FLT, P_LD_SHP, P_LD_AMP,
    P_AMODE, P_ARATE, P_AOCT, P_AGATE,
    P_ASWING, P_APROB, P_AHOLD, P_AORDER,
    P_ROOT, P_SCALE, P_QUANT, P_TRANS,
    P_SLEN, P_SDIV, P_SSWING, P_SGATE,
    P_DIST, P_CHOR, P_DLY, P_REV,
    P_VOICE, P_GLIDE, P_PAN, P_MUTE,
    P_GLMODE, P_PRIO, P_ALLOC, P_DETUNE,
    P_SLCR, P_SLPAT, P_SLRATE, P_SLDEPTH,      /* SLICER insert (slicer.c) */
    P_M1SRC, P_M1DST, P_M1AMT,                 /* modulation matrix (mod.c): 4 slots of SRC, DST, AMT; */
    P_M2SRC, P_M2DST, P_M2AMT,                 /* new common parameters go just before P_E0 (user presets */
    P_M3SRC, P_M3DST, P_M3AMT,                 /* and projects map by count) */
    P_M4SRC, P_M4DST, P_M4AMT,
    P_FM1_ATK, P_FM1_DEC, P_FM1_SUS, P_FM1_REL, P_FM1_LEVEL,
    P_FM2_ATK, P_FM2_DEC, P_FM2_SUS, P_FM2_REL, P_FM2_LEVEL,
    P_FM3_ATK, P_FM3_DEC, P_FM3_SUS, P_FM3_REL, P_FM3_LEVEL,
    P_FM4_ATK, P_FM4_DEC, P_FM4_SUS, P_FM4_REL, P_FM4_LEVEL,
    P_CHRD, P_VOIC,                            /* chord keys (chord.c): one key plays a chord; its voicing */
    P_LN0, P_LN1, P_LN2, P_LN3, P_LN4, P_LN5, P_LN6, P_LN7,   /* DRUM lane levels (eng_drum.c, #97): KICK .. BELL */
    P_LSYNC, P_LTRIG, P_LPOL,                  /* 1.2 (#154): LFO SYNC (OFF, a note value), TRIG (NOTE, FREE), POL (BI, UNI) */
    P_SQNT,                                    /* 1.2 (#162): QUANTIZE, the steps' recorded / nudged timing (ON: none) */
    P_SPRD,                                    /* (#148): SPREAD, the voices of a POLY / UNISON track around PAN (fx.c) */
    P_ITYPE, P_IA, P_IB, P_IC, P_IMIX,         /* 1.5 (#78, #177): the INSERT, its TYPE, three values and MIX (fx.c) */
    P_FTYPE,                                   /* 1.5 (#104): ANALOG's filter TYPE, LP (as before) / BP / HP (eng_analog.c) */
    P_ESYNC,                                   /* 1.5 (#175): ENV SYNC: ATK DEC REL as note values of the tempo (voice.c) */
    P_E0, P_E1, P_E2, P_E3, P_E4, P_E5, P_E6, P_E7,
    P_COUNT
};

/* Dungeon Tracker 1.0: the pitch wheel as a track value, so it records as automation (REC, playing: a MIDI wheel moves
 * it as a learned knob would, midi_control.c) and plays back: -64..63 of the channel's bend range, 0 the centre
 * (voice.c track_render). A performance value: sound loads keep it (ui.c param_kept), a project load zeroes it */
#define P_BEND P_ED_FX
enum {                          /* global parameters */
    G_BPM, G_SWING, G_CLOCK, G_TUNE,
    G_DTIME, G_DFDBK, G_DCOLOR, G_DMIX,
    G_RSIZE, G_RDAMP, G_CRATE, G_CDEPTH,
    G_MIDI, G_SYNC, G_ROUTE, G_INFO,   /* G_ROUTE: MIDI IN, 0 CH1-4 (channels 1..4 -> parts 1..4, 5..16 ignored), 1 SEL;
                                        * 1.2: 2..4 CH5-8 CH9-12 CH13-16 (seq.c midi_base); 1.5.5: 5 TRACK (song.mch) */
    G_SLOT, G_NAME, G_LOAD, G_SAVE,
    G_ENGSEL, G_ENGGO,          /* no page: the editor switches the engine with a SET of G_ENGSEL; G_ENGGO is
                                 * unused (ids are fixed by the formats and the protocol) */
    G_CLRSEQ, G_INITSND,
    G_RTYPE,                    /* REVERB TYPE: 0 ROOM, 1 SPRING, 2 HALL (1.2, the power-on default) (fx.c). Was
                                 * G_DRCH, the GM drum part's MIDI channel (inert since 1.0, never read); projects
                                 * of formats before FUN7 load it as ROOM (project.c proj_rtype_room) */
    G_DRLVL, G_DRREV,           /* inert (label "-", on no page): the GM drum part they set is gone; kept
                                 * because the ids and G_COUNT are fixed by the formats and the protocol (only
                                 * the import of an old project reads them: proj_drums_to_part) */
    G_COUNT
};
#define NPROJ (RONDO_LAYOUT ? 11u : 4u)         /* SAVE > PROJECT's slots (Dungeon Tracker: A..H; 1.3: A..K, project.c song_store.c) */

/* stored parameters of an older layout -> today's P_* order. A store keeps np = the P_COUNT it was
 * written with; common parameters are only ever added just before P_E0, so the first np - 8 are
 * P_LEVEL.. in order and the last 8 are P_E0..P_E7; the parameters a store does not have take def[]
 * (user presets, projects of formats 1 and 2) */
static void params_by_count(int16_t *out, const int16_t *in, uint32_t np, const int16_t *def)
{
    uint32_t i, nc = np - 8u;
    for (i = 0; i < P_E0; i++)
        out[i] = i < nc ? in[i] : def[i];
    for (i = 0; i < 8u; i++)
        out[P_E0 + i] = in[nc + i];
}

/* engine indices the stores name (engines.c ENGINES[]: append-only) */
#define ENGI_LOFI 3u                     /* RE-1: CHIP (Felucca's LOFI) */
#define ENGI_PHYS 9u
#define ENGI_DRUM 10u
#define ENGI_CKIT 14u                    /* RONDO: CYCLE KIT (eng_cycle.c) */
#define ENGI_CTONE 15u                   /* RONDO: CYCLE TONE */
#define ENGI_WAVES 16u                   /* 1.1: WAVES (retired in Phantom Cosmonaut: -> MORPH) */
#define ENGI_SID 17u                     /* 1.3: SID (the C64's chip, eng_sid.c) */
#define ENGI_MORPH 20u                   /* Phantom Cosmonaut: MORPH, the SIGNALs (eng_morph.c); Dungeon Tracker 1.5's WAVE 32 before */
#define ENGI_FOLDER 22u                  /* Phantom Cosmonaut: FOLDER, the SIGNALs through a wavefolder (retired: -> DEBRIS) */
#define ENGI_GRAN 23u                    /* .. GRANULAR, a grain scanner over the SIGNALs (retired: -> DEBRIS) */
#define ENGI_PFM 24u                     /* .. FM, one SIGNAL frame phase-modulating another */
#define ENGI_DEBRIS 25u                  /* .. DEBRIS, folded granular: FOLDER and GRANULAR as one (eng_morph.c) */
#define ENGI_VOX 26u                     /* .. VOX, a voice (0.9.3; retired in 0.9.4: -> NUMBERS) */
#define ENGI_NUMBERS 27u                 /* .. NUMBERS, a numbers-station voice: a word a note (eng_numbers.c, 0.9.4) */
#define ENGI_VULT 29u                    /* Phantom Tracker 0.9.10: VULT, Vult's PD oscillator, resonant comb and ladder (eng_vult.c) */
#define ENGI_GLORP 28u                   /* Phantom Tracker 0.9.8: GLORP, the frog synth: a vowel wah opened by the velocity (eng_glorp.c) */
#define ENG_SIG(e) ((e) == ENGI_MORPH || (e) == ENGI_PFM || (e) == ENGI_DEBRIS)   /* the SIGNAL engines */
#define ENGI_MKIT 19u                    /* Dungeon Tracker 1.5: MOD KIT (retired in Phantom Cosmonaut: -> GEN KIT) */
#define ENGI_MOD 21u                     /* 1.5: MOD (retired in Phantom Cosmonaut: -> MORPH) */
#define ENG_KIT(e) ((e) == ENGI_CKIT)   /* the kit engine (GEN KIT's lanes, GM map, values; 1.5's MOD KIT retired) */
#define ENGI_GENESIS 18u                 /* Dungeon Tracker 1.2: GENESIS (after the YM2612, eng_genesis.c) */
/* PHYS MODEL DRUM (MODEL 4, before 1.0) -> the DRUM engine, its E values in place: {MODEL, TUNE, TONE, DECY,
 * SNAP, ACC, KICK 0..127, PERC 0..127} -> {KIT, TUNE, TONE, DECY, SNAP, ACC, KICK 0..1, DRV 0}. 1 = it was one
 * (its engine is ENGI_DRUM now); projects (project.c) and user presets (upreset.c) */
static int drum_from_phys(uint32_t engine, int16_t *e)
{
    if (engine != ENGI_PHYS || e[0] != 4)
        return 0;
    e[0] = (int16_t)((e[7] < 0 ? 0 : e[7] > 127 ? 127 : e[7]) >> 5);   /* PERC -> KIT: STD HAND CYM H+CYM */
    e[6] = (int16_t)(e[6] >= 64);                                       /* KICK: PUNCH, ROUND */
    e[7] = 0;
    return 1;
}

/* SAMPLE SET 4 was PERC, the GM-mapped drum kit (tools/gen_samples.py); retired after 1.0.2: its index stays
 * (SET / GRAIN SRC 4 is an alias of PIANO, USR1..3 keep 5..7). A sound that selected it is the DRUM engine with
 * its default kit (eng_drum.c DRUM_PRESETS[0]: the same GM key map, so its patterns still play as drums): its E
 * values become the kit's, the rest of the sound (mix, sends, matrix, ..) stays. 1 = it was one (its engine is
 * ENGI_DRUM now); projects (project.c proj_perc), user presets (upreset.c up_migrate), factory preset 4 and
 * favourites (ui.c, settings_persist.c). Idempotent */
#define ENGI_SAMPLE 4u
#define SPK_SAMPLE_SETS 1        /* Spelunkr: SAMPLE has its own sets (tools/gen_spk_samples.py) */
/* Spelunkr (Ed, Nov 22 2026: "upload user samples, slice them and assign them to a synth or drum track from inside the
 * same page"): SPK_NU user samples, the web build's (felucca_web.c SPK_USER 8; 0 elsewhere: the FM-1's flash slots as
 * they were). Each is a whole sample and up to SPK_NSLICE slices (IMA ADPCM, 22.05 kHz, each slice from a fresh
 * state), played three ways: SAMPLE's SET "name" (the whole, pitched from its root across the keys), SAMPLE's SET
 * "name CHOP" (a slice a key from C3), the KIT's lane MODEL "name" (slices 1..8 on the GRID's rows BD .. CB) */
#ifdef SPK_USER
#define SPK_NU SPK_USER
#else
#define SPK_NU 0
#endif
#define SPK_NSLICE 24u
#define SPK_CHOP_KEY 48u                 /* the first slice's key (C3: the page's lowest) */
#if SPK_NU
static char spk_name[SPK_NU][10];        /* "USER 1" .. until named; shown as the set's / model's name */
static char spk_chop[SPK_NU][16];        /* "<name> CHOP" */
static const uint8_t *spk_hit_d[SPK_NU * 8];   /* the KIT's rows: slice data, length (0: silent) */
static uint32_t spk_hit_n[SPK_NU * 8];
#endif
#define SMP_SET_PERC 4u
#define DRUM_KIT_E {0, 64, 70, 64, 64, 100, 0, 0}   /* {KIT STD, TUNE, TONE, DECY, SNAP, ACC, KICK PUNCH, DRV} */
static int drum_from_perc(uint32_t engine, int16_t *e)
{
    static const int16_t KIT[8] = DRUM_KIT_E;
    uint32_t i;
    if (engine != ENGI_SAMPLE || e[0] != (int16_t)SMP_SET_PERC || SPK_SAMPLE_SETS)
        return 0;                    /* (Spelunkr: SAMPLE's set 4 is GLOCK, no PERC) */
    for (i = 0; i < 8u; i++)
        e[i] = KIT[i];
    return 1;
}

/* ------------------------------------------------- voices, engines --- */
enum { V_POLY, V_MONO, V_LEGATO, V_UNISON };   /* P_VOICE */
/* voice_t, vmod_t, preset_t, engine_t: felucca_engine.h (above) */

/* ------------------------------------------------- tracks, the song --- */
enum { ST_NOTE, ST_TIE, ST_REST };
#define SF_ACCENT 1u
#define SF_SLIDE 2u
#define SF_RATCH_SH 3u                   /* RATCH: the hits of a NOTE step - 1 (0..3: x1..x4, seq.c seq_ratchet); */
#define SF_RATCH (3u << SF_RATCH_SH)     /* bits 3..4, so a user preset's pattern (flag 4 = tie) carries it too */
#define NLANE 8                  /* drum lanes of a step (the DRUM engine's: eng_drum.c DRUM_LANE_NOTE) */
typedef struct {                 /* acid-style step: up to 4 notes (POLY), time, accent, slide; drum hits */
    uint8_t note[4];
    uint8_t n;                   /* notes in use, 0 = empty */
    uint8_t time;                /* ST_NOTE / ST_TIE / ST_REST */
    uint8_t flags;               /* SF_ACCENT | SF_SLIDE | SF_RATCH */
    uint8_t vel;
    uint8_t hit;                 /* bit l: lane l hits (its GM note, on any engine): the DRUM grid */
    uint8_t acc;                 /* bit l: that hit is accented (velocity 127) */
    uint8_t probability;         /* 0 = legacy 100%; 1..100 = percent, 101 = silent; 1.5.5: 102..112 a condition */
} step_t;
typedef struct {                 /* a step as formats 1..4 (projects to FUN4) stored it: no hits */
    uint8_t note[4];
    uint8_t n, time, flags, vel;
} step8_t;

/* Probability keeps zero-initialized and legacy patterns at 100%. */
/* 1.5.5 (#209): CONDITIONS, in the chance byte's free values (no format change): COND_FIRST + c - 1 is condition c
 * (1..NCOND): a:b (1:2 2:2 1:3 2:3 3:3 1:4 2:4 3:4 4:4) plays on loop a of every b of its track (seq.c seq_cond), FILL
 * while FILL is held (GLO + E4), !FILL while not. A step has a chance or a condition (one byte): a condition plays at
 * 100 %. Firmware before 1.5.5 refuses a project holding one (its proj_unpack wants 0..101): the slot reads empty there.
 * The knobs and the lists show the two as one value (step_chance_v): 0..100 %, then 101..100 + NCOND the conditions */
#define COND_FIRST 102u
#define NCOND 11u
#define COND_FILL 10u                    /* FILL, NOT FILL: the last two */
#define COND_NFILL 11u
#define CHANCE_V_MAX (100u + NCOND)
static uint32_t step_cond(const step_t *s)   /* its condition 1..NCOND, 0 none */
{
    return s->probability >= COND_FIRST && s->probability < COND_FIRST + NCOND ? s->probability - COND_FIRST + 1u : 0u;
}
static uint32_t step_chance(const step_t *s)
{
    return !s->probability ? 100u : s->probability <= 100u ? s->probability : s->probability == 101u ? 0u : 100u;
}
static void step_set_chance(step_t *s, uint32_t chance) { s->probability = (uint8_t)(chance >= 100u ? 0u : chance ? chance : 101u); }
static uint32_t step_chance_v(const step_t *s) { uint32_t c = step_cond(s); return c ? 100u + c : step_chance(s); }
static void step_set_chance_v(step_t *s, uint32_t v)
{
    if (v > 100u)
        s->probability = (uint8_t)(COND_FIRST - 101u + (v < CHANCE_V_MAX ? v : CHANCE_V_MAX));
    else
        step_set_chance(s, v);
}
/* RATCH: a NOTE step plays its notes and hits this many times (1..4), in equal parts of the step; every older
 * pattern holds 0 there, x1 */
static uint32_t step_ratchet(const step_t *s) { return ((s->flags & SF_RATCH) >> SF_RATCH_SH) + 1u; }
static void step_set_ratchet(step_t *s, uint32_t hits)
{
    s->flags = (uint8_t)((s->flags & ~SF_RATCH) | ((hits < 1u ? 0u : hits > 4u ? 3u : hits - 1u) << SF_RATCH_SH));
}
/* NUDGE (1.2, #162, D98): a step's timing, in 1/NUDGE_DIV of the track's DIV, -8..+7 (half a step early .. 7/16 late);
 * live recording keeps it, SEQ > AUTOMATION's NUDGE rows and STEP (steps held + PRESETS) set it, the track's QUANTIZE
 * OFF (P_SQNT, AUTOMATION's QUANTIZE row) plays it (seq.c seq_tick).
 * Four bits of the flags no other field uses: 4 (bit 3 of the nudge, its sign) and 32 64 128 (bits 0..2). 0 = on the
 * step: every older pattern. A user preset's pattern keeps none (upreset.c up_pat_norm) */
#define NUDGE_DIV 16
#define SF_NUDGE (4u | 0xE0u)
static int32_t step_nudge(const step_t *s)
{
    uint32_t u = (uint32_t)(s->flags >> 5) | (uint32_t)(s->flags & 4u) << 1;
    return (int32_t)(u ^ 8u) - 8;
}
static void step_set_nudge(step_t *s, int32_t v)
{
    uint32_t u = (uint32_t)(v < -8 ? -8 : v > 7 ? 7 : v) & 15u;
    s->flags = (uint8_t)((s->flags & ~SF_NUDGE) | (u & 7u) << 5 | (u >> 1 & 4u));
}
#define MOTION_MAX 128u          /* 1.2 (FUN10): 64 before */
/* 0.10: place is the track << 6 | the step in 16 bits (five FEEDs; a byte held four). Values retain their signed parameter range (-64..127: an int8_t, 1.2).
 * param: the P_* id (< 128: P_COUNT is at most 127, project.c), bit 7 (MOTION_LOCK, 1.1) a parameter lock: the value
 * sounds on that step only and goes back after it (motion.c motion_step); without it an automation event, the
 * value holds until another one changes it. One record per (track, step, id), of either kind */
#define MOTION_LOCK 0x80u
#define MOTION_ID(e) ((uint32_t)(e)->param & 0x7Fu)
/* RONDO: the TRACKER's commands (ui_tracker.c, seq.c ph_*): step records (MOTION_LOCK) on ids past the parameters, so
 * they are saved and undone as the FX column's locks are; never a parameter (motion.c skips them as it plays):
 *   CMD_SPD   SPEED x1 / 1/2 / x2 from this row on (until another, or the pattern's loop comes round)
 *   CMD_LOOP  after this row, back N rows (1 2 4 8), M times (1..8): N rows played M + 1 times (value N index x 8 + M - 1)
 *   CMD_LPIT  the LOOP's pitch step a pass, -12..+12 semitones (notes only: a kit's hits stay)
 *   CMD_MORPH the track's SOUND MORPH amount from this step on, 0..100 % (morph.c) */
#define CMD_SPD (P_COUNT + 0u)
#define CMD_LOOP (P_COUNT + 1u)
#define CMD_LPIT (P_COUNT + 2u)
#define CMD_MORPH (P_COUNT + 3u)
#define NCMD 4u
static inline int cmd_id(uint32_t id) { return RONDO_LAYOUT && id >= P_COUNT && id < P_COUNT + NCMD; }
typedef struct { uint16_t place; uint8_t param; int8_t value; } motion_event_t;
typedef struct { uint8_t count, on, rsv[2]; motion_event_t event[MOTION_MAX]; } motion_store_t;
/* Phantom Cosmonaut M5: the FX PAD's prints (fx.c / fxpad.c). A printed step of a FEED: its two X/Y effects as the pad
 * had them when REC printed it. A pattern's own (as its motion is: the bank, a song, a project carry it), at most
 * FXP_MAX steps over the FEEDs. place: the step; ty: bit 0 crosshair 1, bit 4 crosshair 2, bits 1..3 the FEED (0.10;
 * fxpad.c); x1 y1 x2 y2: 0..127 */
#define FXP_MAX 80u
typedef struct { uint8_t place, ty, x1, y1, x2, y2; } fxp_ev_t;
typedef struct { uint8_t count, rsv; fxp_ev_t ev[FXP_MAX]; } fxp_store_t;
_Static_assert(sizeof(motion_store_t) == 4u + 4u * MOTION_MAX, "motion layout (0.10)");
#define CHAIN_ROWS 16u
/* A song section (1.2, FUN10): the PROJECT slot each track plays (0..3, A..D; CHAIN_SILENT "-": that track plays
 * nothing there) and its repeats (1..16). Up to 1.1 a row was {slot, repeat}, all four tracks on that slot
 * (chain_v9_t, FUN6..FUN9): it loads as a section with the four on it (song_chain.c chain_from_v9) */
#define CHAIN_SILENT 4u
/* RONDO: a section plays the PATTERN BANK's A..F (song_bank.c), not the four project slots. Its byte keeps FUN10's
 * meaning for A..D and "-" (0..3, 4) and takes 5, 6 for E, F (a Felucca build reads those as not valid: no song) */
#define CHAIN_PATS (RONDO_LAYOUT ? 6u : 4u)       /* the patterns a section can name */
#define CHAIN_OFF 0xFFu
#define CHAIN_MAXV (RONDO_LAYOUT ? 6u : CHAIN_SILENT)   /* the largest byte a section's track holds */
static inline uint32_t chain_pat(uint32_t v) { return v < 4u ? v : RONDO_LAYOUT && v >= 5u && v <= 6u ? v - 1u : CHAIN_OFF; }
static inline uint32_t chain_val(uint32_t p) { return p < 4u ? p : p < CHAIN_PATS ? p + 1u : CHAIN_SILENT; }
/* the order SONG's KNOB 3 turns through: A .. (D / F), then "-" */
static inline uint32_t chain_ord(uint32_t v) { return chain_pat(v) != CHAIN_OFF ? chain_pat(v) : CHAIN_PATS; }
typedef struct { uint8_t slot[NTRK], repeat; } chain_row_t;
typedef struct {
    uint8_t count, rsv[3];
    chain_row_t row[CHAIN_ROWS];
} chain_config_t;
typedef struct { uint8_t slot, repeat; } chain_row_v9_t;   /* frozen: the rows of FUN6..FUN9 */
typedef struct {
    uint8_t count, rsv[3];
    chain_row_v9_t row[CHAIN_ROWS];
} chain_v9_t;
_Static_assert(sizeof(chain_v9_t) == 36u, "song chain layout (FUN6..FUN9)");

typedef struct track {
    int16_t p[P_COUNT];
    uint8_t engine, preset;      /* engine: what the audio ISR renders */
    uint8_t eng_req;             /* engine the UI asked for (the ISR switches at a block start) */
    uint8_t user;                /* user preset slot + 1 the sound came from (UI), 0 = none */
    voice_t v[NVOICE];
    /* LFO */
    uint32_t lfo_ph;
    int32_t lfo_val;             /* Q15 */
    int32_t lfo_fade;            /* Q15 ramp after note-on */
    uint32_t lfo_rnd;
    int16_t lfo_prev;            /* the S&H value of the cycle before (mod.c SLEW glides from it to lfo_rnd's) */
    /* keyboard / arp input: held notes in press order */
    uint8_t held[16];
    uint8_t nheld;
    uint8_t arp_phys;            /* keys physically held for the arp */
    uint8_t latched;             /* HOLD: keep notes after release */
    /* arp runtime */
    uint32_t arp_pos;            /* q8 samples into the current arp step */
    uint32_t arp_idx;
    uint8_t arp_note;            /* sounding arp note, 0 = none */
    uint8_t arp_walk;            /* WALK: the place in the note list it stands on */
    uint8_t arp_ch[3];           /* CHORD: the other notes sounding with arp_note, 0 = none */
    uint32_t arp_off;            /* q8 sample time of its note-off */
    /* sequencer */
    step_t step[NSTEP];
    uint32_t seq_pos;            /* q8 samples into the current step */
    uint16_t seq_idx;
    uint8_t seq_notes[4 + NLANE];   /* sounding seq notes (the step's notes, then its hits) */
    uint8_t seq_n;
    uint8_t seq_hold;            /* last step slides: keep the notes until the next step */
    uint8_t slide_glide;         /* next legato note glides (slide) */
    uint8_t rat_left;            /* RATCH: repeats of the playing step still to come (seq.c seq_ratchet) */
    uint32_t seq_off;
    uint8_t seq_active;          /* any step programmed */
    uint8_t rskip_idx;           /* live recording put notes into the step about to play: */
    uint8_t rskip_n, rskip[NLANE];   /* do not trigger them again there (they sound already) */
    /* NUDGE (QUANTIZE OFF, seq.c seq_tick): a step plays at its start + its nudge. Latched as a step starts: */
    uint32_t nd_late;            /* the step playing now plays at this seq_pos + 1 (0: it played) */
    uint32_t nd_early;           /* the next step plays early, at this seq_pos + 1 of this one (0: on its start) */
    uint8_t nd_fired;            /* that step + 1 once it played early (its start then plays nothing), 0 = none */
    /* live recording of held notes (seq.c rec_hold): the steps they are held into become TIEs */
    uint8_t rh_n, rh_note[4];    /* recorded notes still held, 0 = none */
    uint8_t rh_start;            /* the step they were recorded into */
    uint8_t rh_ties;             /* TIE steps written after it */
    uint8_t rh_last;             /* the last of them; rh_bak: what it held (an early release puts it back) */
    step_t rh_bak;
    /* mono */
    uint8_t mono_stack[8];
    uint8_t nmono;
    uint8_t mono_note;           /* note the MONO / LEGATO / UNISON voice(s) play, 0 = none */
    uint8_t rr;                  /* POLY ROTATE: next voice to try */
    /* mix runtime */
    int32_t peak;
    int32_t dist_hp, dist_lp1, dist_lp2;   /* DIST insert state (fx.c) */
    uint8_t tail;                /* blocks to mix after the last voice (the DIST / INSERT tail) */
    uint8_t ins_run;             /* the INSERT still sounds (fx.c track_insert: its wet share above 0) */
    uint8_t cond_loop;           /* 1.5.5 (#209): the loop of its LEN playing (0..COND_LOOPS - 1, seq.c seq_tick) */
#if FELUCCA_CPU2
    /* the second core: a part's render state, its own, so the two cores' parts share nothing (fx.c mix_block); in
     * the pad that keeps sizeof(track_t) a multiple of 16 (14 bytes before) */
    uint8_t lfo_wrap;            /* the LFO's phase wrapped this block: its S&H is drawn after the parts (voice.c) */
    uint32_t esync_k[3];         /* ATK's step, DEC's and REL's coefficient (Q16) this block (voice.c esync_coef) */
    uint8_t spread_on;           /* SPREAD this block (fx.c mix_part, voice.c track_render) */
    uint8_t rsv_sz[1];           /* (sizeof(track_t) a multiple of 16: below) */
#else
    uint8_t rsv_sz[14];          /* (sizeof(track_t) a multiple of 16: below) */
#endif
    int16_t armp, aholdp;        /* P_AMODE / P_AHOLD as last seen by the ISR */
    /* engine switch (voice.c engine_block): the old engine's voices fade out, then it switches */
    uint8_t xf_on, xf;           /* fading; blocks of the fade still to render */
    int16_t pe_old[8];           /* P_E0..P_E7 of the sounding engine: the fade renders with these */
    uint8_t xp_n, xp_note[4], xp_vel[4];   /* note-ons during the fade, played on the new engine */
    /* modulation matrix (mod.c): MIDI performance controllers of the track's channel, 0..127 */
    uint8_t mw, at;              /* CC1 mod wheel, channel aftertouch */
    uint8_t ex_off;              /* 127 - CC11 expression (0: full, the MIDI default) */
    uint8_t m_vel, m_key, m_vi;  /* the latest note-on: velocity, note, voice index (per-block destinations) */
    uint8_t sp_alt;              /* SPREAD: the side the next voice starts on (alternating, voice.c voice_start) */
    int16_t m_rnd;               /* .. its RAND */
    int32_t m_env;               /* the amp envelope of voice m_vi, last block (Q15) */
    /* RONDO: the TRACKER's commands (seq.c ph_*): the row playing (the step seq_idx keeps the time), SPEED (0 x1,
     * 1 1/2, 2 x2), the second half of a 1/2 row, the LOOP's passes and pitch, x2's second row due at pr_mid_at. 16
     * bytes after an int32 (the size stays a multiple of 16) */
    uint8_t pr_on, pr_row, pr_spd, pr_half, pr_lp, pr_mid;
    int8_t pr_semi;
    uint8_t pr_rsv;
    uint32_t pr_mid_at;
    uint8_t pr_rsv2[4];
} track_t;

/* the audio ISR indexes trk[] with a multiply by a small immediate and a shift while the size is a multiple of 16; 1.5's
 * new track parameters (the INSERT, the filter TYPE, ENV SYNC) and state would make it no multiple (the constant
 * loaded first: +3 % on the ISR's static cost, target_budget.py): one pad, rsv_sz, keeps it */
_Static_assert(sizeof(track_t) % 16u == 0, "track_t: a multiple of 16 bytes (a cheap trk[] index on the target)");

typedef struct {
    int16_t g[G_COUNT];
    uint8_t playing, seq_mode;
    uint8_t grid;                /* 1: the keys are the DRUM grid (ui.c grid_on): only the lane keys play (seq.c);
                                  * 2: NAME (ui_name.c): no key plays; 3: SONG, A..D are its (seq.c song_key) */
    uint8_t rec;                 /* live recording armed: bit per track */
    uint8_t sel;                 /* selected track 0..NTRK-1: keys, pages, editor */
    int8_t octave;
    uint32_t tick;               /* sub-blocks since play */
    uint32_t cpu_q8;             /* audio ISR load, 1/256 */
    uint32_t master_q12;
    int32_t batt_raw;            /* smoothed ADC ch3 (battery divider), 0 = not read yet */
    uint8_t mch[NTRK];           /* 1.5.5 (#202): MIDI IN TRACK (G_ROUTE ROUTE_TRACK), each track's channel as stored: 0..15
                                  * the channel's distance up from the track's own (track i: channel (i + v) % 16, so 0, the
                                  * zeroed default, is T1..T4 on channels 1..4), MCH_OMNI (any channel no track has), MCH_OFF
                                  * (none); mch_get / mch_set. The project's, in FUN10's spare bytes (project.c PROJ_MIDI_OFF);
                                  * MENU > MIDI > T1..T4 CHANNEL */
    uint8_t clsx[NTRK];          /* Dungeon Tracker's party byte, kept so its projects load byte for byte: always 0 here
                                  * (Phantom Cosmonaut has no party; the tracks' roles are fixed: trk_cls) */
} song_t;
#define ROUTE_SEL 1
#define ROUTE_TRACK 5                /* G_ROUTE: TRACK, each track on its own channel (song.mch) */
#define MCH_OMNI 16u
#define MCH_OFF 17u
#define MCH_N 18u                    /* channels 1..16, OMNI, OFF */

static track_t trk[NTRK];        /* the instrument: five parts (0.10: DRUMS, SYNTH 1..3, PERC; shown DRUMS PERC SYNTH 1..3) */

/* 0.10: the B sides of the synth FEEDs (trk[1..3]): a second sound on the same notes (voice.c). s: the sound not in the
 * track now (its engine, preset and own values, b_own: the envelope, the engine's values, the voice mode ..; the rest
 * of the strip is the FEED's, shared); on, tr (its transpose over the FEED's notes, semitones), lv (its LEVEL, 104 as
 * loud as A), pn (its PAN) are B's whichever side is in the track. flip: B is the track's now (the UI edits it), A in
 * s. The voices: A's 0..3, B's 4..7 while on (voice.c vb_base); a voice's side in pfix bit 1 */
#define B_N 3u
typedef struct { int16_t on, tr, lv, pn; uint8_t engine, preset; int16_t p[P_COUNT]; } bside_t;
typedef struct { bside_t s; uint8_t eng_req, flip, nmono, mono_note, rr, mono_stack[8]; } bsr_t;
static bsr_t bsr[B_N];
static inline bsr_t *bs_of(const track_t *t)
{
    uintptr_t a = (uintptr_t)t;
    return a >= (uintptr_t)&trk[1] && a <= (uintptr_t)&trk[B_N] ? &bsr[(t - trk) - 1] : 0;
}
static inline int b_on(const track_t *t) { const bsr_t *b = bs_of(t); return b && b->s.on == 1; }   /* (on: 0 never set, 1 on, 2 off: its sound kept) */
static song_t song;
#if RONDO_LAYOUT
/* Phantom Cosmonaut M0: each track's fixed role and its voices (Dungeon Tracker's default party, the class choice
 * removed): T1 drums (4), T2 chords and pads (4), T3 bass (1: mono), T4 lead (3); 12 in all, the FM-1's budget
 * (NBUDGET). PRESETS > LIST ROLE offers each track its own kind of sound (ui.c). The SNS channels' voice layout is
 * decided in M2 (spec section 4, open question 1) */
/* 0.10 (Ed): DRUMS (the kit, 4 voices), PERC (two lanes from any kit, 2 voices), three synths (chords, bass, lead by
 * default) with the other 6 between them. Stored and indexed DRUMS, the three synths, PERC (trk[4]: every FEED 1..4
 * kept its index, its sound and its tests); shown and stepped DRUMS PERC SYNTH 1..3 (FD_AT / fd_pos: the FEED numbers on
 * screen are the shown ones) */
enum { CLS_ROGUE, CLS_CLERIC, CLS_FIGHTER, CLS_MAGE, CLS_PERC, NCLS };   /* (the old names, internal: a track's role index) */
static const uint8_t CLS_VOICES[NCLS] = {4, 3, 1, 2, 2};   /* (AUTO: the synths' 3 + 1 + 2 = the 6 left) */
static const char *const CLS_NAME[NCLS] = {"DRUMS", "CHORDS", "BASS", "LEAD", "PERC"};
enum { ROLE_DRUMS, ROLE_CHORDS, ROLE_BASS, ROLE_LEAD };
static const uint8_t CLS_ROLE[NCLS] = {ROLE_DRUMS, ROLE_CHORDS, ROLE_BASS, ROLE_LEAD, ROLE_DRUMS};
#define PERC_TRK 4u
static uint8_t pan_held;   /* 0.10: bit k: FEED k's PAN is a step's lock now (motion.c motion_step): the voices it starts keep
                            * that pan (voice.c voice_start, the per-step pan) */                       /* PERC's index */
#define KIT_VOICES (4u + 2u)              /* DRUMS' and PERC's: their own */
static inline int trk_is_kit(uint32_t k) { return k == 0u || k == PERC_TRK; }
static const uint8_t FD_AT[NTRK] = {0, PERC_TRK, 1, 2, 3};   /* shown position -> index */
static inline uint32_t fd_pos(uint32_t k) { return k == 0u ? 0u : k == PERC_TRK ? 1u : k + 1u; }   /* index -> position */
static inline uint32_t trk_cls(uint32_t k) { return k % NTRK; }
/* Phantom Cosmonaut: VOICES (Ed, Nov 2026; spec section 4), a FEED's notes at once, on EDIT > VOICE: AUTO (its role's,
 * above), MONO, 2, 3. In P_FTYPE's place (retired ANALOG's filter TYPE: the stored layout unchanged; retire.c clears an
 * old ANALOG sound's). FEED 1, the kit, keeps its four lanes. The budget (NBUDGET) less the kit's four is the most FEEDs
 * 2..4 have between them: a FEED asking more than the others leave it plays fewer notes (never PHANTOM's: its voice is
 * its own) */
#define P_VOICES P_FTYPE
#define CHRD_KEYS 10u                    /* SCL > CHORD's KEYS (chord.c CH_KEYS; Phantom Cosmonaut 0.9.4) */
/* 0.10.5 (Ed: "assign voice # values to all the presets ... the onus is on the user to figure out the voice
 * composition"): AUTO is the loaded sound's own count, by its category (ui.c auto_vox_sync, the UI's each pass:
 * BASS LEAD PLUCK FX 1, PAD KEYS 4, OTHER and an untagged sound of yours 2); until the UI has run, the roles' of
 * before. VOICES on EDIT > VOICE: AUTO MONO 2 3 4 */
static volatile uint8_t trk_avox[NTRK] = {4, 3, 1, 2, 2};
static inline uint32_t vox_req(uint32_t k)
{
    uint32_t v = (uint32_t)trk[k % NTRK].p[P_VOICES] & 7u;
    if (v > 4u)
        v = 4u;
    if (!v && (uint32_t)trk[k % NTRK].p[P_CHRD] == CHRD_KEYS)
        return 4u;                                /* (KEYS' chords are four notes: AUTO is 4 then) */
    return v ? v : trk_avox[k % NTRK];
}
/* a synth FEED's cap: its request while the synths' requests fit the room the kits leave (6); past it, the room shared
 * in proportion to the requests (at least 1 each), so two four-voice pads get 2 and 2 beside a mono lead, not 1 and 1 */
static inline uint32_t trk_cap(uint32_t k)
{
    uint32_t j, all = 0, room = NBUDGET - KIT_VOICES, left, r[NTRK], c[NTRK];
    k %= NTRK;
    if (trk_is_kit(k))
        return CLS_VOICES[k];
    for (j = 0; j < NTRK; j++) {
        r[j] = trk_is_kit(j) ? 0u : vox_req(j);
        all += r[j];
    }
    if (all <= room)
        return r[k];
    for (j = 0, left = room; j < NTRK; j++) {          /* (floors, at least 1; then the rest one at a time to the FEED */
        c[j] = r[j] ? (r[j] * room / all ? r[j] * room / all : 1u) : 0u;   /* furthest short of its request, the first */
        left = left > c[j] ? left - c[j] : 0u;          /* of equals) */
    }
    while (left--) {
        uint32_t b = NTRK;
        for (j = 0; j < NTRK; j++)
            if (r[j] > c[j] && (b == NTRK || r[j] - c[j] > r[b] - c[b]))
                b = j;
        if (b == NTRK)
            break;
        c[b]++;
    }
    return c[k];
}
typedef char roles_fit_budget[4 + 3 + 1 + 2 + 2 <= NBUDGET ? 1 : -1];
#else
static inline uint32_t trk_cap(uint32_t k) { (void)k; return NVOICE; }
#endif
#define TSEL (&trk[song.sel])    /* the selected track */
/* MIDI IN TRACK: track i's channel 0..15, MCH_OMNI or MCH_OFF (song.mch), and set */
static inline uint32_t mch_get(uint32_t i)
{
    uint32_t v = song.mch[i % NTRK];
    return v < 16u ? (v + i) & 15u : v < MCH_N ? v : (i & 15u);
}
static inline void mch_set(uint32_t i, uint32_t ch) { song.mch[i % NTRK] = (uint8_t)(ch < 16u ? (ch - i) & 15u : ch < MCH_N ? ch : 0u); }

/* SWING of a track's step clock: the track's own plus the global one, at most 100 (#31). The sequencer
 * (seq.c step_samples) and the SLICER (slicer.c sl_enter) both time steps with it. At 100 the even
 * steps are 1.4 x the straight length and the odd ones 0.6 x. */
#define SWING_MAX 100
static inline int32_t track_swing(const track_t *t)
{
    int32_t sw = t->p[P_SSWING] + song.g[G_SWING];
    return sw < 0 ? 0 : sw > SWING_MAX ? SWING_MAX : sw;
}
/* the length of step idx of a straight length `base`, swung: even steps longer, odd ones shorter */
static inline uint32_t swing_step_len(const track_t *t, uint32_t base, uint32_t idx)
{
    int32_t sw = track_swing(t) * (int32_t)base / 250;
    return base + (uint32_t)((idx & 1u) ? -sw : sw);
}

/* RONDO: the INSERT's DUCK (fx.c ins_duck). rondo_clock counts the mix's samples (fx.c mix_block, after the block's
 * events); a kick of track 1 (eng_cycle.c ck_note_on's KICK lane, voice.c for other engines) stamps duck_at with it, so
 * every DUCK insert knows how long ago the last kick was, sounding or not since */
static volatile uint32_t rondo_clock, duck_at;
/* RONDO: the FX layer's four new master effects' values (perform.c MK_ISO ..): ISOLATOR -100..100, WASH PUMP RIDE 0..100;
 * RIDE (mk_x[3]) is the FM engines' (eng_cycle.c: COLOR and FDBK up) */
static volatile int8_t mk_x[4];
/* Phantom Cosmonaut M6: ATTRITION, the FX layer's three new knobs (perform.c MK_STUT..): STUTTER, TUNNEL, JUMP 0..100
 * (JUMP the sequencer's: seq.c seq_jump); attr_latch: FX + OCT+, they stay when FX is let go (else they spring back) */
static volatile int8_t mk_a[3];
static volatile uint8_t attr_latch;
/* .. the jitter (gfx.c cv_jitter: asked, 1 weak 2 strong, or'd) and HOME's starfield's shockwave (ui_graph.c
 * graph_stars), asked from anywhere (the ISR too: PHANTOM, the FX keys; INGEST; the FX layer's knobs) */
static volatile uint8_t jit_req, star_req;
static inline void jit_kick(uint32_t amp) { jit_req |= (uint8_t)(amp > 1u ? 2u : 1u); }
static inline void star_kick(void) { star_req = 1; }
static volatile uint8_t duck_seen;                       /* a kick since power-on (before it: nothing ducks) */
static inline void duck_kick(void)
{
    duck_at = rondo_clock;
    duck_seen = 1;
}

/* ----------------------------------------------------------- system --- */
#define RING_PUBLISH() __asm__ volatile("" ::: "memory")   /* slot store before the index update */
static volatile uint32_t fm1_ms;  /* milliseconds since boot (TIMER4-based, TIMER5 ISR in main.c) */
/* boot-loop guard (main.c): two boots in a row that die in the first 30 s -> UBOOT */
#define BOOTGUARD_MAGIC 0x42475244u
struct { uint32_t magic, failed, pending; } bootguard __attribute__((section(".noinit")));
