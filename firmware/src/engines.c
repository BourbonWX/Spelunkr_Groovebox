/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Engine table (order = PRESETS browsing order and the engine numbers of the editor protocol), the
 * factory patterns (SAVE > PHRASES) and the parts' sounds at power-on. */
#include "../engines/src/dsp.c"   /* Felucca-Engines (MIT): the shared DSP blocks */

/* The parts' engine memory (1.5): a part plays one engine at a time, so the engines with large per-part state
 * (engine_t.mem: GENESIS's and GEN KIT's notes; before, PHYS's slots, GRAIN's grains, DRUM's lanes, FM6's notes,
 * SLICE's reverse windows) keep it in one region per part instead of an array each. The region holds the state of
 * the engine that last claimed it (eng_mem_owner); a switch to another such engine zeroes its engine_t.mem bytes,
 * which is the state each had at power-on, so a switch sounds as it did with the arrays (tests/union_test.c).
 * Phantom Cosmonaut (Nov 2026): sized by the engines it plays (GENESIS's notes the largest), no longer by retired
 * PHYS (12,888 B a part): 49 KB of the pool back. An engine that needs more than ENG_MEM_PART grows it for every
 * part (x NPART of the pool). Zero-initialised pool section: main.c fm1_cstart clears it. */
#if DT_RETIRE
#define ENG_MEM_PART 416u              /* (Phantom Tracker: GENESIS's notes, the largest it plays) */
#else
#define ENG_MEM_PART 12888u
#endif
static union {
    uint64_t align;
    uint8_t b[ENG_MEM_PART];
} eng_mem[NPART] __attribute__((section(".pool")));
static uint8_t eng_mem_owner[NPART];       /* engine index + 1 whose state is in the region, 0 = none (zeros) */
#define ENG_MEM(p) ((void *)eng_mem[p].b)  /* part p's region (p < NPART) */
#define ENG_MEM_FITS(name, size) _Static_assert((size) <= ENG_MEM_PART, name " state over ENG_MEM_PART")
/* a retired engine still compiled for what the kept ones share with it (DRUM's lanes and mutes, SAMPLE's sets, FM6's
 * patches): never in ENGINES (ENG_GONE), so it never claims a region or renders, and its state need not fit */
#if DT_RETIRE
#define ENG_MEM_FITS_RETIRED(name, size) _Static_assert(1, name)
#else
#define ENG_MEM_FITS_RETIRED(name, size) ENG_MEM_FITS(name, size)
#endif

#include "../engines/src/eng_analog.c"   /* Felucca-Engines (MIT): ANALOG (0; Phantom Tracker: back) */
#if !DT_RETIRE                  /* Phantom Cosmonaut: the retired engines that nothing kept shares are not compiled
                                  * (the files stay for Felucca's own builds: RONDO_LAYOUT=0, FELUCCA_FM4=1) */
#include "eng_phase.c"
#include "../engines/src/eng_lofi.c"
#include "eng_formant.c"
#include "../engines/src/eng_trio.c"
#include "../engines/src/eng_wheel.c"
#include "eng_grain.c"
#include "eng_phys.c"           /* PHYS: DaisySP physical models (phys_dsp.c, MIT) */
#include "../engines/src/eng_noise.c"
#endif
#include "eng_sample.c"         /* (retired: its sets and user slots, the editor's sample transfer) */
#include "eng_drum.c"           /* DRUM: the 8-lane kit (drum_voice.c; retired: its lane notes and mutes, GEN KIT's too) */
#include "eng_fm6.c"            /* FM6: 6-operator FM, msfa ported (fm6_core.c, Apache-2.0; retired: its patches) */
#include "fm4_convert.c"        /* DIGITAL's tables, and its sounds -> FM6 */
/* Dungeon Tracker 1.1: a retired engine's slot (retire.c): silent, no presets, never offered; a sound of it loads as its
 * substitute (ENG_SUBST below). The engines' code is no longer referenced: the build leaves it out */
static const engine_t ENG_GONE = {
    .name = "-",
    .page_title = {"-", "-"},
    .edit = FM4_EDIT("-", "-", "-", "-", "-", "-", "-", F_INT, F_INT, 0, 0),
    .note_on = fm4_gone_note_on,
    .render = fm4_gone_render,
    .knob = {P_E4, P_E5, P_E6, P_REL},
};
#if DT_RETIRE
#define ENG_KEPT(x) &ENG_GONE
#else
#define ENG_KEPT(x) &x
#endif
#if FELUCCA_FM4
#include "eng_digital.c"        /* DIGITAL: four-operator FM (retired; FELUCCA_FM4=1 builds it) */
#endif
#if FELUCCA_SLICE
#include "eng_slice.c"
#endif
#if RONDO_LAYOUT && !FELUCCA_SLICE  /* Dungeon Tracker: SLICE retired; 13 reserved as DIGITAL's 1 is (never offered:
                                     * eng_ok; a SLICE sound in a project loads as SAMPLE, project.c proj_slice) */
static const engine_t ENG_SLICE_GONE = {
    .name = "-",
    .page_title = {"-", "-"},
    .edit = FM4_EDIT("-", "-", "-", "-", "-", "-", "-", F_INT, F_INT, 0, 0),
    .note_on = fm4_gone_note_on,
    .render = fm4_gone_render,
    .knob = {P_E4, P_E5, P_E6, P_REL},
};
#endif
#include "eng_genesis.c"        /* 1.2: GENESIS (18): after the YM2612 (Phantom Tracker: back) */
#include "eng_cycle.c"          /* RONDO: CYCLE KIT (14) and CYCLE TONE (15) */
#include "eng_morph.c"          /* Phantom Cosmonaut: MORPH (20, WAVE 32's place), FM (24), DEBRIS (25) */

/* a factory sound's own values past E1..E8 (1.5, eng_mod.c until MOD's retirement): a kit's voices and lanes */
static void eng_own_values(uint32_t e, uint32_t k, int16_t *p)
{
    uint32_t l, f;
    if (ENG_KIT(e)) {
        const ck_kit_t *K = &CK_KITS[k % NELEM(CK_KITS)];
        for (l = 0; l < CK_N; l++) {
            for (f = 0; f < 5u; f++)
                p[P_FM1_ATK + 5u * l + f] = K->v[l][f];
            p[P_LN0 + l] = K->v[l][5];
            p[P_LN4 + l] = K->v[l][6];                     /* (0.9.10: the lane's MODEL) */
        }
    } else if (e == ENGI_GENESIS) {                   /* 0.9.14: its operators (GEN_OPS); DT 1.5 loaded them in ui.c,
                                                       * a call lost when M4 retired GENESIS and not put back in 0.9.5 */
        for (l = 0; l < 4u; l++)
            for (f = 0; f < 5u; f++)
                p[P_FM1_ATK + 5u * l + f] = GEN_OPS[k % NELEM(GEN_OPS)][l][f];
    }
}
/* the descriptor of id on a kit track of engine e (GEN KIT's), 0 = not one of its own */
static const param_desc_t *kit_param_desc(uint32_t e, uint32_t id)
{
    (void)e;
    return ck_param_desc(id);
}
#include "eng_sid.c"            /* 1.3: SID (17): after the MOS 6581 (Phantom Tracker: back) */
#include "eng_glorp.c"          /* Phantom Tracker 0.9.8: GLORP (28), the frog synth */
#include "eng_vult.c"           /* Phantom Tracker 0.9.10: VULT (29), on Vult's DSP (MIT) */

/* the editor protocol, user presets and projects store these indices: append, never reorder */
static const engine_t *const ENGINES[NENGINES] = {
    &ENG_ANALOG,                 /* 0 (1.5: back, the subtractive family's; Phantom Tracker: kept) */
#if FELUCCA_FM4
    &ENG_DIGITAL,                /* 1 (ENGI_DIGITAL) */
#else
    &ENG_FM4_GONE,               /* 1: reserved (DIGITAL, retired: its sounds convert to FM6, fm4_convert.c) */
#endif
    ENG_KEPT(ENG_PHASE),                  /* 2 */
    ENG_KEPT(ENG_LOFI),                   /* 3 CHIP (1.5: retired, -> WAVES) */
    &ENG_SAMPLE,                 /* 4 (Spelunkr: back, on its own sets: the pianos, vibes, sax, glass ..) */
    ENG_KEPT(ENG_FORMANT),                /* 5 VOICE (eng_formant.c: "voice" is a sounding note in voice.c) */
    ENG_KEPT(ENG_TRIO),                   /* 6 */
    ENG_KEPT(ENG_WHEEL),                  /* 7 */
    ENG_KEPT(ENG_GRAIN),                  /* 8 (1.5: retired, -> WAVE 32; now MORPH) */
    ENG_KEPT(ENG_PHYS),                   /* 9 (ENGI_PHYS; 1.5: retired, -> MOD, its drums MOD KIT) */
    ENG_KEPT(ENG_DRUM),                   /* 10 (ENGI_DRUM) */
    ENG_KEPT(ENG_NOISE),                  /* 11 */
    ENG_KEPT(ENG_FM6),                    /* 12 (ENGI_FM6) */
#if FELUCCA_SLICE
    &ENG_SLICE,                  /* 13 (FELUCCA_SLICE=0 builds without it) */
#elif RONDO_LAYOUT
    &ENG_SLICE_GONE,             /* 13: reserved (SLICE, retired in Dungeon Tracker) */
#endif
    &ENG_CKIT,                   /* 14 (ENGI_CKIT): RONDO */
    ENG_KEPT(ENG_CTONE),                  /* 15 (ENGI_CTONE): RONDO (1.5: retired, -> GENESIS) */
    &ENG_GONE,                   /* 16 (ENGI_WAVES): Dungeon Tracker's WAVES (Phantom Cosmonaut: retired, -> MORPH) */
    &ENG_SID,                    /* 17 (ENGI_SID): Dungeon Tracker 1.3 (Phantom Tracker: kept) */
    &ENG_GENESIS,                /* 18 (ENGI_GENESIS): Dungeon Tracker 1.2 (Phantom Tracker: kept) */
    &ENG_GONE,                   /* 19 (ENGI_MKIT): Dungeon Tracker 1.5's MOD KIT (Phantom Cosmonaut: retired, -> GEN KIT) */
    &ENG_MORPH,                  /* 20 (ENGI_MORPH): Phantom Cosmonaut (Dungeon Tracker 1.5's WAVE 32 before) */
    &ENG_GONE,                   /* 21 (ENGI_MOD): Dungeon Tracker 1.5's MOD (Phantom Cosmonaut: retired, -> MORPH) */
    &ENG_GONE,                   /* 22 (ENGI_FOLDER): Phantom Cosmonaut's FOLDER (retired: -> DEBRIS) */
    &ENG_GONE,                   /* 23 (ENGI_GRAN): .. GRANULAR (retired: -> DEBRIS) */
    &ENG_PFM,                    /* 24 (ENGI_PFM) */
    &ENG_DEBRIS,                 /* 25 (ENGI_DEBRIS): folded granular */
    &ENG_GONE,                   /* 26 (ENGI_VOX): Phantom Cosmonaut 0.9.3's voice (retired: -> MORPH) */
    &ENG_GONE,                   /* 27 (ENGI_NUMBERS): .. 0.9.4's numbers-station voice (Phantom Tracker: retired, -> MORPH) */
    &ENG_GLORP,                  /* 28 (ENGI_GLORP): Phantom Tracker 0.9.8, the frog synth */
    &ENG_VULT,                   /* 29 (ENGI_VULT): Phantom Tracker 0.9.10, Vult's comb and ladder */
};

/* a track's engine number as an index (the audio paths: a compare, cheaper than % NENGINES; a bad number: 0) */
static inline uint32_t eng_idx(uint32_t e) { return e < NENGINES ? e : 0u; }

/* part t's engine memory goes to its engine (t->engine; voice.c engine_block, once a block: on a switch, after
 * the old engine's fade, before any note on the new one): zeroed (its engine_t.mem bytes) when another
 * engine's state is in it. An engine without memory leaves it as it is: a switch back finds its state. */
static void eng_mem_claim(const track_t *t)
{
    uint32_t p = (uint32_t)(t - trk), e, k, n;
    uint32_t *w;
    if (p >= NPART)
        return;
    e = eng_idx(t->engine);
    n = ENGINES[e]->mem;
    if (!n || eng_mem_owner[p] == e + 1u)
        return;
    w = (uint32_t *)ENG_MEM(p);
    for (k = 0; k < (n + 3u) / 4u; k++)
        w[k] = 0;
    eng_mem_owner[p] = (uint8_t)(e + 1u);
}

/* the sample source a track's sound plays that has no data (SAMPLE's SET, GRAIN's SRC, SLICE's SRC: an empty or
 * invalid user slot, a set or PIANO this build lacks): its name (it plays a sine: eng_sample.c smp_sine), 0 = none;
 * *code a small number for it (1.., per engine and source) so the UI says it once (ui_input.c sample_notice) */
static uint8_t snd_said[NTRK];   /* per track: the code last said, 0 = none (a project load clears them: said again) */
static const char *snd_missing(const track_t *t, uint32_t *code)
{
    const engine_t *e = ENGINES[eng_idx(t->eng_req)];
    uint32_t si = (uint32_t)t->p[P_E0] % SMP_NALL;
#if FELUCCA_SLICE
    if (e == &ENG_SLICE) {
        si = slc_src_of(t->p);
        *code = 64u + si;
        return slc_get(si) ? 0 : N_SLC_SRC[si];
    }
#endif
#if DT_RETIRE
    if (e != &ENG_SAMPLE)
        return 0;
    *code = 1u + si % 32u;
#else
    if (e != &ENG_SAMPLE && e != &ENG_GRAIN)
        return 0;
    *code = (e == &ENG_GRAIN ? 32u : 1u) + si % 32u;
#endif
    return smp_set_missing(si) ? SMP_ALL_NAMES[si] : 0;
}

/* the order the engines are shown in (PRESETS browsing and its ENG knob, the EDIT layer's keys, the editor's list):
 * engine indices, never DIGITAL's reserved 1 (with FELUCCA_FM4 it follows FM6). The indices stay as they are (the
 * stores and the protocol hold them); only this table orders them */
#if DT_RETIRE
static const uint8_t ENGINE_ORDER[NENG_SHOWN] = {14, 4, 18, 17, 0, 20, 28, 24};   /* Spelunkr (Ed, Nov 18 2026): the kits,
                                                                      * SAMPLE, GENESIS SID, ANALOG MORPH, GLORP FM */
#else
static const uint8_t ENGINE_ORDER[NENG_SHOWN] = {
    14, 15,                      /* RONDO: FMKIT, FMTONE first */
    0,                           /* ANALOG */
    12,                          /* FM6 */
    16,                          /* 1.1: WAVES */
    17,                          /* 1.3: SID */
#if FELUCCA_FM4
    1,                           /* DIGITAL */
#endif
    2, 3, 4, 5, 6, 7, 9,         /* PHASE LOFI SAMPLE VOICE TRIO WHEEL PHYS */
    19, 20, 21,                  /* 1.5: MOD KIT, MORPH (WAVE 32 before), MOD */
    10,                          /* DRUM (RONDO: SLICE, 13, not offered: core.h NENG_SHOWN) */
    8,                           /* GRAIN (1.3: after the 14 on the EDIT layer's keys, with NOISE: PRESETS only) */
#if !FELUCCA_FM4
    11,                          /* NOISE (1.1: last, after the 14 on the EDIT layer's keys: PRESETS only) */
#endif
};
#endif

/* the engines one can pick (engine 1 only with FELUCCA_FM4), in ENGINE_ORDER: eng_ok(e), the n-th of them
 * eng_vis(n), e's place among them eng_rank(e), the next / previous one eng_step(e, dir) (wraps) */
#if DT_RETIRE
#define ENGI_GRAIN 8u
#define ENG_TO_ANALOG 0xFFu              /* (ANALOG is engine 0, the table's "kept") */
static const uint8_t ENG_SUBST[NENGINES] = {   /* 1.1: a retired engine's substitute (retire.c), 0 = kept. Phantom Cosmonaut
                                                * (Ed, Nov 2026): MORPH DEBRIS FM on the SIGNALs, VOX, the NOISE kit;
                                                * every other sound lands on the nearest of them, by its category (a
                                                * bass a bass, a kit a kit: retire.c) */
    [ENGI_DIGITAL] = ENGI_PFM,           /* DIGITAL -> FM */
    [2] = ENGI_DEBRIS,                   /* PHASE (phase distortion: the fold's) */
    [3] = ENGI_SID,                      /* CHIP (Phantom Tracker: the chip's own, SID) */
    [5] = ENGI_MORPH,                    /* VOICE (the formants: MORPH's VOSTOK, LAST WORDS by category) */
    [6] = ENGI_SID,                      /* TRIO (three oscillators, sync, ring: SID's, as 1.5) */
    [7] = ENGI_PFM,                      /* WHEEL (the organs: FM's GHOST ORGAN) */
    [8] = ENGI_DEBRIS,                   /* GRAIN -> DEBRIS (GRANULAR before) */
    [ENGI_PHYS] = ENGI_PFM,              /* PHYS (bells, plucks: FM's; its drums the kit's: retire.c) */
    [ENGI_DRUM] = ENGI_CKIT,             /* DRUM -> the NOISE kit (the GM map, the grid: the same) */
    [11] = ENGI_DEBRIS,                  /* NOISE (winds: DEBRIS's tapes) */
    [ENGI_FM6] = ENGI_PFM,               /* FM6 -> FM */
    [ENGI_SLICE] = ENGI_CKIT,            /* SLICE (retired in 1.0: its breaks) */
    [ENGI_CTONE] = ENGI_GENESIS,         /* FMTONE -> GENESIS (as 1.5) */
    [ENGI_WAVES] = ENGI_MORPH,           /* WAVES -> MORPH (the wavetables: the SIGNALs) */
    [ENGI_MKIT] = ENGI_CKIT,             /* MOD KIT -> the NOISE kit */
    [ENGI_MOD] = ENGI_MORPH,             /* MOD -> MORPH */
    [ENGI_FOLDER] = ENGI_DEBRIS,         /* Phantom Cosmonaut's FOLDER and GRANULAR -> DEBRIS (Ed, Nov 2026: "combine folder */
    [ENGI_GRAN] = ENGI_DEBRIS,           /* and granular into one"): a pad its GLASS PAD, a bass its ROUND BASS .. */
    [ENGI_VOX] = ENGI_MORPH,             /* Phantom Cosmonaut 0.9.3's VOX and 0.9.4's NUMBERS -> MORPH (Phantom Tracker, */
    [ENGI_NUMBERS] = ENGI_MORPH,         /* Ed: "scrapping the gimmicks"): MORPH's VOSTOK, LAST WORDS by category */
    [ENGI_DEBRIS] = ENGI_MORPH,          /* Spelunkr (Ed, Nov 18 2026: keep GENESIS + SID, ANALOG + MORPH, GLORP + FM): */
    [ENGI_VULT] = ENG_TO_ANALOG,         /* DEBRIS's sounds MORPH's, VULT's ANALOG's, by category */
};
static int eng_retired(uint32_t e) { return e < NENGINES && ENG_SUBST[e] != 0u; }
static uint32_t eng_subst(uint32_t e)               /* (followed on: FOLDER's DEBRIS is retired too, so MORPH) */
{
    uint32_t k;
    e = e < NENGINES ? e : 0u;
    for (k = 0; k < 4u && ENG_SUBST[e]; k++)
        e = ENG_SUBST[e] == ENG_TO_ANALOG ? 0u : ENG_SUBST[e];
    return e;
}
#else
static int eng_retired(uint32_t e) { (void)e; return 0; }
static uint32_t eng_subst(uint32_t e) { return e; }
#endif
/* the family an engine's card names (1.5's FM WAVE AMIGA'D SUB; Phantom Cosmonaut: SIGNAL, DRUMS) */
static const char *eng_family(uint32_t e)
{
    return ENG_KIT(e) ? "DRUMS" : "SIGNAL";
}
static int eng_ok(uint32_t e) { return e < NENGINES && !eng_retired(e) && (FELUCCA_FM4 || e != ENGI_DIGITAL) && (FELUCCA_SLICE || !RONDO_LAYOUT || e != ENGI_SLICE); }
static uint32_t eng_vis(uint32_t n) { return ENGINE_ORDER[n % NENG_SHOWN]; }
static uint32_t eng_rank(uint32_t e)
{
    uint32_t n;
    if (!eng_ok(e))
        e = DT_RETIRE ? eng_subst(e) : ENGI_FM6;   /* (DIGITAL without FELUCCA_FM4: its sounds play as FM6; 1.1: a retired
                                                * engine's as its substitute) */
    for (n = 0; n < NENG_SHOWN && ENGINE_ORDER[n] != e; n++)
        ;
    return n < NENG_SHOWN ? n : 0u;
}
static uint32_t eng_step(uint32_t e, int32_t dir)
{
    return eng_vis((eng_rank(e % NENGINES) + (dir > 0 ? 1u : NENG_SHOWN - 1u)) % NENG_SHOWN);
}

/* factory sequence patterns: SAVE > PHRASES loads one into the selected track (ui.c pat_load); a preset
 * only suggests one with PAT(n), loading a sound never touches the steps. Absolute notes, loaded as they
 * are (DRUM plays them as GM drums, SLICE as slices; SCL TRANS and OCT transpose): 0 = rest;
 * flags 1 = accent, 2 = slide, 4 = tie (holds the previous note). Names: at most 8 characters */
#define T_ 4
static const struct {
    const char *name;
    uint8_t note[16], flags[16];
} PATTERNS[] = {
    {"ACID", {45, 45, 57, 45, 0, 48, 45, 55, 45, 0, 57, 52, 45, 48, 0, 50},          /* 1 */
     {1, 0, 2, 0, 0, 0, 1, 2, 0, 0, 1, 0, 0, 2, 0, 1}},
    {"OFFBEAT", {0, 36, 0, 36, 0, 36, 0, 48, 0, 36, 0, 36, 0, 39, 0, 43},          /* 2 bass */
     {0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0}},
    {"MELODY", {60, 0, 67, 0, 72, 67, 0, 64, 62, 0, 69, 0, 74, 69, 0, 67},         /* 3 pluck */
     {1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0}},
    {"LEAD", {72, 0, 0, 74, 0, 0, 76, 0, 79, 0, 76, 0, 74, 0, 0, 0},               /* 4 */
     {1, T_, 0, 2, T_, 0, 0, 0, 1, 0, 2, 0, 0, T_, T_, 0}},
    {"PAD", {60, 0, 0, 0, 0, 0, 0, 0, 57, 0, 0, 0, 55, 0, 0, 0},                   /* 5 long notes */
     {0, T_, T_, T_, T_, T_, T_, 0, 0, T_, T_, 0, 0, T_, T_, 0}},
    {"KEYS", {0, 0, 60, 0, 0, 63, 0, 0, 0, 0, 60, 0, 0, 65, 0, 63},                /* 6 offbeat stabs */
     {0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0}},
    {"BELL", {72, 0, 0, 79, 0, 0, 84, 0, 0, 0, 76, 0, 0, 0, 0, 0},                 /* 7 sparse */
     {1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0}},
    {"SUB", {36, 0, 0, 0, 0, 0, 0, 36, 0, 0, 34, 0, 0, 0, 0, 0},                   /* 8 low, held */
     {1, T_, T_, T_, 0, 0, 0, 0, 0, 0, 0, T_, T_, T_, 0, 0}},
    /* SLICE (eng_slice.c): note = C4 + slice */
    {"CHOP", {60, 61, 62, 67, 64, 65, 60, 69, 68, 70, 62, 67, 72, 72, 74, 64},     /* 9 16 slices re-ordered */
     {1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0}},
    {"STUTTER", {60, 60, 61, 61, 62, 0, 63, 63, 64, 65, 65, 0, 66, 66, 66, 67},    /* 10 8 slices, repeats */
     {1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0}},
    {"SLICES", {60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75},   /* 11 in order */
     {1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0}},
    /* DRUM (General MIDI: 36 kick, 38 snare, 42 closed / 46 open hi-hat) */
    {"BEAT", {36, 42, 42, 42, 38, 42, 36, 42, 36, 42, 42, 36, 38, 42, 46, 42},     /* 12 */
     {1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0}},
    /* 16ths up a C minor arpeggio, twice: the line the ARP presets (RAVE, ARP 8BIT, ARP LEAD) suggest
     * now that the arpeggiator is the track's and a preset no longer switches it on */
    {"ARP", {48, 51, 55, 60, 63, 67, 72, 75, 48, 51, 55, 60, 63, 67, 72, 75},       /* 13 */
     {1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0}},
};
#undef T_
#define NPATTERNS (sizeof PATTERNS / sizeof PATTERNS[0])

/* the parts at power-on (engine, preset, PATTERNS[n - 1] in the sequencer, 0 = empty: all are): bass, pad, lead, drums */
/* RONDO: T1 drums (FMKIT, the BEAT pattern), T2 chords / pads (FMTONE FM PAD), T3 bass (FMTONE FM BASS, mono),
 * T4 lead (FMTONE FM LEAD); the sequencers empty but the drums' */
#if RONDO_LAYOUT
static const uint8_t TRK_DEF[NPART][3] = {{ENGI_CKIT, 40, 12}, {ENGI_SAMPLE, 1, 0}, {ENGI_GENESIS, 0, 0},
                                         {ENGI_SAMPLE, 2, 0}, {ENGI_CKIT, 16, 0}};   /* (Spelunkr: CRATE on the beat,
                                                           * SAMPLE's MALL EP, GENESIS' GEN BASS (mono), SAMPLE's DREAM VIBES) */   /* (0.10: PERC, trk[4], a kit) */   /* (Phantom
                                                           * Cosmonaut: FEED 1 the NOISE kit's CAPSULE on the beat, 2 DEBRIS's
                                                           * GLASS PAD, 3 its ROUND BASS, 4 FM's CRYSTAL BELL; PHANTOM is
                                                           * MORPH's) */
#else
static const uint8_t TRK_DEF[NPART][3] = {{0, 4, 0}, {ENGI_FM6, 4, 0}, {3, 0, 0}, {ENGI_DRUM, 0, 0}, {ENGI_DRUM, 0, 0}};   /* Felucca's */
#endif
static uint32_t trk_def_engine(uint32_t i) { return TRK_DEF[i % NPART][0]; }
