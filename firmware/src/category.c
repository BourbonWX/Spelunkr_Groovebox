/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Sound categories (1.2, Discussion #90): every sound in the PRESETS list belongs to one, so KNOB 4 (LIST) can show
 * only the basses, the pads, .. (ui.c list_mode). Factory presets: a static table here (a letter per preset, in the
 * engine's preset order); user presets: a byte of their record (upreset.c up_cat, set on the NAME screen when saving
 * or renaming, ui_name.c). Included by ui.c and upreset.c (the host build of the user presets needs the numbers only).
 * The numbers are stored (a user preset's record, the editor's UP_LIST / UP_GET / UP_PUT): append-only. CAT_NONE is a
 * record of before (or the editor's without one): its category follows from its engine (cat_of_engine) */
#ifndef FELUCCA_CATEGORY
#define FELUCCA_CATEGORY 1
enum { CAT_NONE, CAT_BASS, CAT_LEAD, CAT_PAD, CAT_PLUCK, CAT_KEYS, CAT_DRUM, CAT_FX, CAT_OTHER, CAT_N };
static const char *const CAT_NAME[CAT_N] = {"-", "BASS", "LEAD", "PAD", "PLUCK", "KEYS", "DRUM", "FX", "OTHER"};
/* the category a sound without one is listed under: a kit (DRUM, SLICE: engines 10, 13) DRUM, any other OTHER */
static uint32_t cat_of_engine(uint32_t e) { return e == 10u || e == 13u || e == 14u || e == 19u ? CAT_DRUM : CAT_OTHER; }   /* (14: RONDO's FMKIT) */

#ifndef UP_HOST
/* the factory presets' categories: per engine number (engines.c ENGINES[]), a letter per preset in its order (B BASS,
 * L LEAD, P PAD, U PLUCK, K KEYS, D DRUM, F FX, O OTHER); a preset past its string (DIGITAL's, FELUCCA_FM4 builds
 * only; SAMPLE's retired PERC, past SMP_NPRESETS): its engine's (cat_of_engine). tests/ui_test.c checks every string
 * against its engine's preset count. A preset's category is a hint for browsing: nothing else reads it */
static const char *const PRESET_CAT[NENGINES] = {
    "LPBPBKLBULLPBLBBLKUPPBFFL", /* 0 ANALOG: SAW LEAD, SOFT PAD, SQR BASS, PWM STR, ACID, SINE KEY, RAVE, SUB BASS,
                                  * PLUCK, BRASS, WIND, STRINGS; 1.5: MOOG BASS .. SOLO SQUARE */
    "",                          /* 1 DIGITAL (retired) */
    "LKPLUU",                    /* 2 PHASE: BRASS, ORGAN, STRING, RESO, BELL, WIRE */
    "LBLLLLBBULFDDDKLP",         /* 3 CHIP (Felucca's LOFI): PULSE LD, WAVE BASS, ARP 8BIT, WAVE LEAD, STEP LEAD; RE-1: SQUARE 50,
                                  * TRI BASS, PULSE BASS, CHIP PLUCK, FAST ARP, LASER ZAP, CHIP KICK, NOISE HAT,
                                  * NOISE SNR, WAVE ORGAN, 8BIT SAW, VOX CHOIR */
    SMP_PRESET_CATS,             /* 4 SAMPLE (Spelunkr: tools/gen_spk_samples.py PRESETS, LOFI PIANO .. VHS VIBES) */
    "PLBF",                      /* 5 VOICE: CHOIR AAH, VOX LEAD, WOW BASS, WHISPER */
    "BLLUP",                     /* 6 TRIO: FAT BASS, ARP LEAD, SYNC LEAD, RING BELL, CHIP CHOIR */
    "KKKKK",                     /* 7 WHEEL: the organs */
    "PFPP",                      /* 8 GRAIN: CLOUD PAD, GLITCH, FROZEN, SHIMMER */
    "UUUPUDDPU",                 /* 9 PHYS: BELL TREE, MARIMBA, PLUCK, BOWED METAL, KALIMBA, HAND DRUM, TOMS, DRONE
                                  * STRING, HARP */
    "DDDD",                      /* 10 DRUM: DRUM KIT; 1.5.6: DEEP KIT, TIGHT KIT, PERC KIT */
    "FFFF",                      /* 11 NOISE: WIND, RAIN, ARCADE, METAL */
    "KUBLPUKU",                  /* 12 FM6: TINE EP, BELL, FM BASS, BRASS, PAD, MARIMBA, ORGAN, PLUCK */
    "DD",                        /* 13 SLICE: CHOP, STUTTER */
    "DDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDD",   /* 14 (Spelunkr: + CRATE .. SNES, sampled) the NOISE kit (0.9.12: + FM ROCK .. SID BREAKS; 0.9.11: + SIXTEEN BIT .. TIN CAN, on the engines' voices): CAPSULE .. DISCO; 0.9.7: GLITCH HOP CHILLWAVE TAPE DECK WONK (the sets'
                                  * places, synthesized) */
    "PKKPKBBBLLLUBBBBBBBBPPPPPKKKKKUUUULLLLLLLF",
                                 /* 15 FMTONE (RONDO): FM PAD, CHORD EP, STAB, GLASS PAD, ORGAN, FM BASS, SUB BASS,
                                  * PLUCK BASS, FM LEAD, SYNC LEAD, BRASS, BELL */
    "PLKBLBPKPUUBLPBFUPBBBBBBBPPPPPPKKKUUULLLLLLFFF",
                                 /* 16 WAVES: WT PAD, WT SWEEP, WT KEYS, WAVEQ BASS, 8BIT LEAD, DIRTY BASS, QUAD PAD,
                                  * GRIT KEYS, TORCHLIGHT, CRYSTAL CAVE, COIN GET, ACID WAVE, BOSS FIGHT, GHOST CHOIR,
                                  * DEEP DELVE, LASER ZAP, ARP RUNES, MANA POOL */
    "BBBLLLUPULDDDFFFLPDDDDBBBBBBBPPPKKUULLLLLLFFFF",
                                 /* 17 SID : MONTY BASS, GROWL BASS, SAW BASS, SYNC LEAD, PWM LEAD, TRI FLUTE,
                                  * RING BELL, SID STRINGS, C64 ARP, FILTER SWEEP, SID KICK, SID SNARE, SID HAT, ZAP,
                                  * POWER UP, NOISE WIND, TITLE SCREEN, SPELLBOOK; 1.0 appends more of every engine's (eng_*.c) */
    "BBBBKKKKUUUULLLLLLLPPPPF",  /* 18 GENESIS (1.2): GEN BASS .. ROCK BASS, GEN PIANO CLAV ORGAN MUSIC BOX, HARP MARIMBA
                                  * SITAR GUITAR, SQUARE .. BRASS, STRINGS CHOIR WARM PAD BELL PAD, LASER */
    "DDDDDDDD",                  /* 19 MOD KIT (1.5): every one a kit */
    "LBLPKLPPUFBUPUBB",          /* 20 MORPH (Phantom Cosmonaut): the 16 SIGNALs, CARRIER .. DARK SIDE */
    "BBBBKKKKUUUULLLLLLLPPPPF",  /* 21 MOD (1.5): TRACKER BASS .., MOD PIANO .., MOD PLUCK .., SQUARE LEAD .., MOD STRINGS ..,
                                  * NOISE SWEEP */
    "LUBPBLKFUP",                /* 22 FOLDER (Phantom Cosmonaut): HULL SCRAPE .. SOLAR FOLD */
    "FPFPPBLKUF",                /* 23 GRANULAR: RADIO DUST .. ION STORM */
    "UKPBLUFKUK",                /* 24 FM: CRYSTAL BELL .. GHOST ORGAN */
    "PBUPLPPBPKFP",              /* 25 DEBRIS: GLASS PAD, ROUND BASS, MUSIC BOX, SLOW DRONE, HALO LEAD, GHOST CHOIR, WRECK PAD,
                                  * FOLD BASS, GRAIN PAD, TIDE KEYS, VENT HAZE, SKYWAVE */
    "LLLLLPPLLLPLLLLLLFFFFFFUUUUUUUU",
                                 /* 26 VOX (retired: its sounds NUMBERS'): the vowels AH .. ER (OO, OH a choir's), the nasals
                                  * M (a hum) N NG, L R W Y, the breaths H S SH F Z V, the syllables PA BA TA DA KA GA CHA JA */
    "LLLUPPLF",                  /* 27 NUMBERS: STATION, OUTER SPACE, GROUND CTRL, HEAR ME, MAYDAY, GHOST, SPEAK SPELL, DEAD AIR */
    "BBBLKKPL",                  /* 28 GLORP: GLORP, FROG BASS, TOAD, GLOOP LEAD, BUBBLE KEYS, WAH CHORDS, OOZE, SWAMP TALK */
    "BULFLPKL",                  /* 29 VULT: LATERAL BASS, RESO PLUCK, ACID VULT, METAL COMB, CZ SWEEP, HOLLOW PAD, DUST KEYS,
                                  * SQUELCH LEAD */
};
static uint32_t preset_cat(uint32_t e, uint32_t k)    /* factory preset k of engine e */
{
    static const char L[] = "?BLPUKDFO";               /* (a letter's index is its CAT_*) */
    const char *s = e < NELEM(PRESET_CAT) && PRESET_CAT[e] ? PRESET_CAT[e] : "";
    uint32_t i, c;
    for (i = 0; i < k && s[i]; i++)
        ;
    for (c = 1; s[i] && L[c] && L[c] != s[i]; c++)
        ;
    return s[i] && L[c] ? c : cat_of_engine(e);
}
#endif
#endif
