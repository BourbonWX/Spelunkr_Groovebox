/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Projects: four slots. The slots live in .noinit RAM: they
 * survive resets and UBOOT entry. With FELUCCA_FLASH (default) every save
 * also goes to flash through storage.c, and an
 * empty RAM slot is filled from flash on load.
 *
 * Format 7 ("FUN7", written) stores P_COUNT (byte 66) and maps an older count as user presets do: FUN7 of
 * 89 parameters (before the chord keys P_CHRD / P_VOIC) loads its engine values at today's P_E0..P_E7,
 * the chord keys OFF / CLOSE, and its motion events' ids from its P_E0 on move up with them (proj_motion_ids).
 * 68 + 4 x (91 + 2 + 64 x 9) + chain + motion = 3040 of the 3372 bytes before the name: 332 spare, room for
 * 83 more track parameters (4 bytes each).
 * Format 6 ("FUN6") adds a 36-byte song chain before the checksum.
 * Format 5 ("FUN5", read only) = format 4 with the drum grid: a step is 10 bytes (step_t: its lane hits and
 * their accents after the 8 bytes it was), and each track has 40 reserved bytes (lane[8][5], written 0: room
 * for per-lane sounds of the DRUM engine). 4 ("FUN4": PROJ_NP_V4 parameters, the modulation matrix), 3
 * ("FUN3": PROJ_NP_V3, the SLICER), 2 ("FUN2") and 1 ("FUN1": PROJ_NP_V2) are read and converted, mapped by
 * count as user presets are (the first np - 8 are P_LEVEL.. in order, the last 8 P_E0..P_E7; the
 * parameters added since take their defaults: the SLICER OFF, every matrix slot OFF). Their steps get no
 * hits; on a DRUM track their notes that are a lane's note become its hits (proj_grid: the same notes and
 * velocities play, the grid shows them as its own).
 * Their engine bytes are kept: formats 1 and 2 had engines 0..7 (ANALOG .. WHEEL), and the engines
 * added since (SLICE 8, ..) were appended, no index moved.
 *
 * Track 4 was the GM drum part until 1.0 (no engine: its byte 0; its level and reverb send in the
 * globals G_DRLVL / G_DRREV, which are inert now). A project says which it has in `parts`: NPART
 * when written since, 0 before (a reserved byte, always written 0: the format and its size did not
 * change). proj_drums_to_part turns such a track 4 into a DRUM part with its default kit (the GM map, so
 * its drum steps still play drums; until 1.0.2 it was the SAMPLE engine's PERC set), keeping its steps, its
 * pattern and mix parameters (LEN DIV SWING GATE, PAN MUTE), its SLICER, and the drum level and reverb send
 * as LEVEL and REV.
 *
 * The byte `phys` (reserved, always 0, before 1.0) says what a PHYS track's MODEL means: 0 MODEL 2 was
 * DUST (dropped: it loads as MODAL bowed, eng_phys.c phys_legacy); 1 MODEL 4 was DRUM (the kit is the DRUM
 * engine since: such a track loads as DRUM, core.h drum_from_phys); 2 (PROJ_PHYS) as today. proj_phys.
 *
 * Format 8 ("FUN8", written since 1.0) = FUN7 with each track's FM6 patch (eng_fm6.c, the 128-byte packed
 * record, 4 x 128 bytes just before the name): a project is self-contained. On load an FM6 track's SLOT shows F n
 * when its patch is that factory one, else OWN (eng_fm6.c fm6_adopt; a stored 9..40, the B slots of the patch bank
 * before 1.0.3 or of 1.4.1's voice bank, is OWN too: the project has the patch).
 * It is 3584 bytes (FUN7: 3388); FUN7 is read (its tracks get the init patch). The retained cache (proj_slot,
 * .noinit) grew with it: after an update its slot 1 still starts with a FUN7 record, which is read; the other
 * slots fail their hash and come back from flash (persist_boot).
 *
 * Format 9 ("FUN9", written since 1.1) = FUN8 with 64 more bytes: the DRUM lane levels (P_LN0..P_LN7, #97) made
 * P_COUNT 99 (P_E0 91), and 68 + 4 x (99 + 2 + 576) + chain + motion = 3072 no longer fit before FUN8's patches
 * (3056). 3648 bytes, the patches at PROJ_FM6_OFF (3120): 48 spare, room for 12 more track parameters. FUN8 (3584,
 * its 91 parameters mapped by count: the lane levels 100 %) and FUN7 are read; the retained cache grew again (its
 * slot 1 still starts with the older record, read as above).
 *
 * Format 10 ("FUNA", written since 1.2; FUN10 in the docs) = FUN9 laid out the same way, 3840 bytes (a whole storage
 * object, storage.c ST_PAYLOAD_MAX), for four changes made together:
 *   - P_COUNT 104 (P_E0 96): LFO 2's SYNC TRIG POL, QUANTIZE and SPREAD (P_LSYNC .. P_SPRD) before the engine
 *     values. SPREAD (#148) came before FUN10's release, into the spare bytes below: a FUN10 of a 1.2 build before
 *     it holds 103 (byte 66) and loads by count (SPREAD 0, its engine values and their motion moved up by one);
 *   - 128 motion records (core.h MOTION_MAX, 64 before), 3 bytes each (place, id, the value as an int8_t: the values
 *     were always -64..127): the block is 4 + 384 = 388 bytes (FUN9's 260 held 64 of 4 bytes);
 *   - each step's NUDGE (core.h step_nudge, -8..+7) in bit 7 of its four note bytes (bit j in note j), bits every
 *     earlier format wrote 0: the step stays 9 bytes;
 *   - the song's sections (core.h chain_row_t): a slot per track (A..D or "-") and the repeats, 5 bytes a row: the
 *     chain is 4 + 16 x 5 = 84 bytes (FUN6..FUN9: 36, rows {slot, repeat}, chain_v9_t; they load as sections with the
 *     four tracks on the row's slot, song_chain.c chain_from_v9: the same song).
 * 68 + 4 x (104 + 2 + 576) + chain + motion = 3268; the patches at PROJ_FM6_OFF (3312): 44 spare, room for 11 more
 * track parameters. 1.5 took seven of them (no format change, FUN10 as it was): the INSERT (P_ITYPE .. P_IMIX, 96..100),
 * ANALOG's filter TYPE (P_FTYPE, 101) and ENV SYNC (P_ESYNC, 102), before the engine values (P_COUNT 111, P_E0 103; 16
 * spare, room for 4). A FUN10 of 1.2 .. 1.4 (104) loads by count: the INSERT OFF, TYPE LP, ENV SYNC OFF (as they
 * played), its engine values and their motion moved up by seven.
 * Firmware 1.2 .. 1.4 refuses a FUN10 of more parameters than its own (np > P_COUNT): a 1.5 project shows EMPTY there. FUN9 / FUN8 / FUN7 load as before (their 64 records of 4 bytes read by proj_motion_v9, every
 * step's nudge 0: the timing they always had; parameters mapped by count, the four new ones at their defaults:
 * SYNC OFF, TRIG NOTE, POL BI, QUANTIZE ON, SPREAD 0, which play as before 1.2), and so do FUN6..FUN1. Firmware before 1.2
 * refuses a FUN10 everywhere (its size and magic are no format it knows): a slot holding one shows EMPTY there (the
 * flash copy stays until that slot is saved over), a backup PUT of one is refused (rc 1), the retained RAM copy of
 * one fails its checks.
 *
 * Parameter locks (1.1, core.h MOTION_LOCK) are motion records with bit 7 of their id byte set (P_COUNT 99 < 128:
 * the bit is free): no byte moved for them, and every project before 1.1 has none. FUN9 holds them. A FUN8 / FUN7
 * record of a 1.1 development build may hold some: on load their ids move to today's positions as the others do,
 * the lock bit kept (proj_motion_ids). Firmware before 1.1 refuses a project that holds a lock (an id out of its
 * range), as with RATCH below; FUN9 it refuses anyway (a newer format).
 *
 * A step's RATCH (core.h SF_RATCH, the hits - 1, 0..3) is in bit 7 of its velocity byte (bit 0) and of its chance
 * byte (bit 1) of FUN7 / FUN8: bits every firmware before wrote 0, so every older project loads with its steps x1,
 * and no format or size changed. Firmware before RATCH refuses a project that holds a ratchet (those bytes out
 * of their range), as it would a newer format. FUN6..FUN1 have none (x1).
 *
 * DIGITAL (engine 1) was retired in 1.0 (fm4_convert.c): a track of it, in any format, loads as FM6 with the
 * patch converted from its values as the track's own (proj_fm4, on every import; the stored record keeps what it
 * holds until saved again). Its motion events on the EDIT or OP ENV values are dropped.
 *
 * SAMPLE's SET 4, PERC (the GM drum kit), was retired after 1.0.2: a SAMPLE track that selects it, in any
 * format, loads as the DRUM engine with its default kit (core.h drum_from_perc: the same GM key map, its steps
 * as they are, the rest of its sound kept; proj_perc, on every import as proj_fm4). Its motion events on the
 * EDIT values are dropped (SAMPLE's meanings, not DRUM's).
 *
 * Built on the Mac too (tests/project_test.c, -DPROJ_HOST): the part above the #ifndef
 * PROJ_HOST needs core.h, params.c (TP) and engines.c. */
#define PROJ_MAGIC 0x46554E42u                 /* "FUNB" (Phantom Tracker 0.10): five FEEDs, run-length encoded (see proj_pack);
                                                * every format before is refused (Ed: "Scrap all of our old songs") */
#define PROJ_MAGIC_V9 0x46554E39u              /* "FUN9": FUN8, 64 bytes longer (the DRUM lane levels, 99 parameters) */
#define PROJ_MAGIC_V8 0x46554E38u              /* "FUN8": FUN7 + the tracks' FM6 patches */
#define PROJ_MAGIC_V7 0x46554E37u              /* "FUN7": serialized (byte params, packed steps), chain, motion */
#define PROJ_MAGIC_V6 0x46554E36u              /* FUN6: 69 parameters, drum grid, chain */
#define PROJ_MAGIC_V5 0x46554E35u              /* "FUN5": the grid, without the chain; read only */
#define PROJ_MAGIC_V4 0x46554E34u              /* "FUN4": four tracks, PROJ_NP_V4 parameters, 8-byte steps; read only */
#define PROJ_MAGIC_V3 0x46554E33u              /* "FUN3": four tracks, PROJ_NP_V3 parameters; read only */
#define PROJ_MAGIC_V2 0x46554E32u              /* "FUN2": four tracks, PROJ_NP_V2 parameters; read only */
#define PROJ_MAGIC_V1 0x46554E31u              /* "FUN1": one instrument; loads into track 1 */
#define PROJ_NP_V2 53u                         /* P_COUNT of formats 1 and 2 (P_E0 was 45) */
#define PROJ_NG_V2 27u                         /* G_COUNT of formats 1 and 2 */
#define PROJ_NP_V3 57u                         /* P_COUNT of format 3 (P_E0 was 49) */
#define PROJ_NP_V4 69u                         /* P_COUNT of format 4 (P_E0 61) */
#define NBANK_V 6u                             /* RONDO: byte 65's largest (song_bank.c NBANK) */
#define PROJ_PHYS 2u                           /* project_t.phys: PHYS without DUST and DRUM (see the top) */
#define PROJ_NAME_LEN 12u                      /* the name: FUN7 bytes PROJ_NAME_OFF.. (the reserved tail's end) */
typedef struct {                               /* one track */
    int16_t p[P_COUNT];
    uint8_t engine, preset;
    step_t step[NSTEP];
} proj_trk_t;
typedef struct {
    uint32_t magic, size;
    int16_t g[G_COUNT];
    uint8_t sel;                               /* the selected track */
    uint8_t parts;                             /* NPART; 0: track 4 is the old GM drum part (see the top) */
    uint8_t phys;                              /* PROJ_PHYS: PHYS MODEL values as today; 1: MODEL 4 was DRUM;
                                                * 0 (a reserved byte before 1.0): MODEL 2 was DUST */
    uint8_t rsv;                               /* RONDO: the PATTERN BANK's current pattern + 1 (byte 65, FUN10; 0 none:
                                                * every project before, a Felucca's), what the autosave puts back */
    proj_trk_t t[NTRK];
    chain_config_t chain;
    motion_store_t motion;
    uint8_t fm6[NTRK][FM6_PACKED];             /* each track's FM6 patch, packed (eng_fm6.c) */
    uint8_t mch[NTRK];                         /* 1.5.5: MIDI IN TRACK's channels, as song.mch (PROJ_MIDI_OFF) */
    uint8_t mo_amt[NTRK], mo_tgt[NTRK];        /* RONDO: SOUND MORPH's AMOUNT and TARGET (PROJ_MORPH_OFF; 0 0 none) */
    uint8_t ph[5];                             /* Phantom Cosmonaut: PHANTOM's sound (phantom.c phantom_pack; PROJ_PH_OFF) */
    uint8_t ph_lvl;                            /* 0.9.15: its LEVEL + 1 (0: none saved; PROJ_PHLVL_OFF) */
    fxp_store_t fxp;                           /* Phantom Cosmonaut M5: the FX PAD's prints (PROJ_FXP_OFF, in FM6's place) */
    char name[PROJ_NAME_LEN];                  /* the project's name: upper-case ASCII 32..126, 0-padded; "" = none */
    bside_t bs[B_N];                           /* 0.10: the synth FEEDs' B sides (A's sound in t[], B's here, unflipped) */
    uint32_t sum;
} project_t;
/* Historical FUN5/6 types are frozen, independent of today's P_COUNT/step_t. */
typedef struct { uint8_t note[4], n, time, flags, vel, hit, acc; } step10_t;
typedef struct { int16_t p[69]; uint8_t engine, preset; step10_t step[NSTEP]; uint8_t lane[NLANE][5]; } proj_trk_v5_t;
typedef struct {                               /* format 5, before the song chain */
    uint32_t magic, size;
    int16_t g[G_COUNT];
    uint8_t sel, parts, phys, rsv;
    proj_trk_v5_t t[NTRK];
    uint32_t sum;
} project_v5_t;
typedef struct { uint32_t magic, size; int16_t g[G_COUNT]; uint8_t sel, parts, phys, rsv;
    proj_trk_v5_t t[NTRK]; chain_v9_t chain; uint32_t sum; } project_v6_t;
/* Serialized FUN7 keeps the retained cache's exact extent. Params are biased
 * bytes, steps pack n/time/flags. Reserved tail is zero and covered by hash.
 * The tail's last 12 bytes (PROJ_NAME_OFF, just before the hash) are the project's name since 1.0:
 * ASCII 32..126 (upper case), 0-padded, all 0 = no name ("PROJECT A"). Firmware before wrote them 0 and
 * never reads them, so every FUN7 file stays valid both ways; FUN6..FUN1 imports get no name.
 * FUN8: the same, 3584 bytes, the four packed FM6 patches at PROJ_FM6_OFF (before the name); 16 bytes of the
 * reserved tail were left for parameters added later. FUN9: the same, 3648 bytes (see the top). */
#define PROJ_STORE_SIZE 3840u                  /* FUN10: a whole storage object (storage.c ST_PAYLOAD_MAX) */
#define PROJ_STORE_V9 3648u                    /* FUN9 */
#define PROJ_MOTION_V9 260u                    /* the motion block of FUN7..FUN9: 64 x (place, id, i16 value) */
#define PROJ_STORE_V8 3584u                    /* FUN8 */
#define PROJ_STORE_V7 3388u                    /* FUN7 */
typedef union { uint32_t align; uint8_t raw[PROJ_STORE_SIZE]; } project_store_t;
_Static_assert(G_COUNT == 27u, "FUN7 globals retain original IDs");
_Static_assert(sizeof(project_store_t) == 3840u, "FUNB: a whole storage object");
typedef struct {                               /* a track of format 4, read only */
    int16_t p[PROJ_NP_V4];
    uint8_t engine, preset;
    step8_t step[NSTEP];
} proj_trk_v4_t;
typedef struct {                               /* format 4 (1.0 development builds), read only */
    uint32_t magic, size;
    int16_t g[G_COUNT];
    uint8_t sel, parts, phys, rsv;
    proj_trk_v4_t t[NTRK];
    uint32_t sum;
} project_v4_t;
typedef struct {                               /* a track of format 3, read only */
    int16_t p[PROJ_NP_V3];
    uint8_t engine, preset;
    step8_t step[NSTEP];
} proj_trk_v3_t;
typedef struct {                               /* format 3 (0.9 .. 1.0), read only */
    uint32_t magic, size;
    int16_t g[G_COUNT];
    uint8_t sel, parts, rsv[2];
    proj_trk_v3_t t[NTRK];
    uint32_t sum;
} project_v3_t;
#define PROJ_DEF_SOUND 0xFFu                   /* preset byte: the track's power-on sound, no steps (format 1) */
#define PROJ_DEF_KEEP 0xFEu                    /* .. DRUM's kit (once SAMPLE PERC), steps and the rest kept (old drums) */
typedef struct {                               /* a track of formats 1 and 2, read only */
    int16_t p[PROJ_NP_V2];
    uint8_t engine, preset;
    step8_t step[NSTEP];
} proj_trk_v2_t;
typedef struct {                               /* format 2 (until 0.9), read only */
    uint32_t magic, size;
    int16_t g[PROJ_NG_V2];
    uint8_t sel, rsv[3];
    proj_trk_v2_t t[NTRK];
    uint32_t sum;
} project_v2_t;
typedef struct {                               /* format 1 (until 0.5 beta), read only */
    uint32_t magic, size;
    int16_t g[PROJ_NG_V2];
    proj_trk_v2_t t;
    uint32_t sum;
} project_v1_t;
project_store_t proj_slot[4] __attribute__((section(".noinit")));
#if RONDO_LAYOUT                                 /* (NPROJ: core.h) */
#if FELUCCA_FLASH                                /* E..K (Dungeon Tracker/1.3): one RAM copy, of the slot last asked for, read
                                                 * from the song store when another is (A..D: proj_slot, as always) */
static project_store_t proj_hi_cache __attribute__((section(".pool")));
static uint8_t proj_hi_slot = 0xFFu;
static project_store_t *pslot_hi(uint32_t s);
static project_store_t *pslot(uint32_t s) { s %= NPROJ; return s < 4u ? &proj_slot[s] : pslot_hi(s); }
#else                                            /* (the host's RAM-only builds: a copy each) */
static project_store_t proj_slot_hi[NPROJ - 4u];
static project_store_t *pslot(uint32_t s) { s %= NPROJ; return s < 4u ? &proj_slot[s] : &proj_slot_hi[s - 4u]; }
#endif
#else
static project_store_t *pslot(uint32_t s) { return &proj_slot[s & 3u]; }
#endif

static uint32_t proj_hash(const void *p, uint32_t n)   /* FNV-1a over n bytes */
{
    const uint8_t *b = (const uint8_t *)p;
    uint32_t i, s = 0x811C9DC5u;
    for (i = 0; i < n; i++)
        s = (s ^ b[i]) * 16777619u;
    return s;
}
static uint32_t proj_sum(const project_t *p) { return proj_hash(p, sizeof *p - 4u); }
static int proj_ok(const project_t *q) { return q->magic == PROJ_MAGIC && q->size == sizeof *q &&
    q->sum == proj_sum(q) && chain_valid(&q->chain) && motion_valid(&q->motion); }

/* run-length coding (0.10: projects and FREQ patterns, as the song store's sg_enc): a byte c < 128 is c + 1 literal
 * bytes after it; c >= 128 is a run of c - 125 (3..130) of the byte after it. rle_enc: the encoded length, 0 if it
 * does not fit cap; rle_dec: the decoded length, 0 if it is not one or does not fit cap */
static uint32_t rle_enc(const uint8_t *s, uint32_t n, uint8_t *d, uint32_t cap)
{
    uint32_t i = 0, w = 0;
    while (i < n) {
        uint32_t r = 1, st, l = 0;
        while (i + r < n && r < 130u && s[i + r] == s[i])
            r++;
        if (r >= 3u) {
            if (w + 2u > cap) return 0;
            d[w++] = (uint8_t)(125u + r);
            d[w++] = s[i];
            i += r;
            continue;
        }
        st = i;
        while (i < n && l < 128u && !(i + 2u < n && s[i + 1u] == s[i] && s[i + 2u] == s[i])) {
            i++;
            l++;
        }
        if (w + 1u + l > cap) return 0;
        d[w++] = (uint8_t)(l - 1u);
        memcpy(d + w, s + st, l);
        w += l;
    }
    return w;
}
static uint32_t rle_dec(const uint8_t *s, uint32_t n, uint8_t *d, uint32_t cap)
{
    uint32_t i = 0, w = 0;
    while (i < n) {
        uint32_t c = s[i++];
        if (c >= 128u) {
            uint32_t r = c - 125u;
            if (i >= n || w + r > cap) return 0;
            memset(d + w, s[i++], r);
            w += r;
        } else {
            uint32_t l = c + 1u;
            if (i + l > n || w + l > cap) return 0;
            memcpy(d + w, s + i, l);
            i += l;
            w += l;
        }
    }
    return w;
}

static int proj_unpack(project_t *q, const uint8_t *b, uint32_t size);
/* every track's engine one this firmware has */
static int proj_engines_ok(const project_t *q)
{
    uint32_t t;
    for (t = 0; t < NTRK; t++)
        if (q->t[t].engine >= NENGINES)
            return 0;
    return 1;
}
/* n bytes of a stored project -> q; 0 = not one (0.10: FUNB only; every project before is refused) */
static void proj_retired(project_t *q);
static int proj_import(project_t *q, const void *b, int n)
{
    if (n != (int)PROJ_STORE_SIZE || ((const uint32_t *)b)[0] != PROJ_MAGIC || !proj_unpack(q, b, PROJ_STORE_SIZE) ||
        !proj_engines_ok(q))
        return 0;
    proj_retired(q);
    return 1;
}

/* 12 stored name bytes -> d (PROJ_NAME_LEN + 1): up to the first 0, upper case; a byte outside 32..126 makes it
 * no name (a damaged tail never refuses the project) */
static uint32_t proj_name_get(char *d, const uint8_t *s)   /* its length */
{
    uint32_t i;
    for (i = 0; i < PROJ_NAME_LEN && s[i]; i++) {
        if (s[i] < 32u || s[i] > 126u) { i = 0; break; }
        d[i] = (char)(s[i] >= 'a' && s[i] <= 'z' ? s[i] - 32u : s[i]);
    }
    d[i] = 0;
    return i;
}

/* FUNB (Phantom Tracker 0.10): the project laid out as below in proj_lbuf, then run-length encoded (rle_enc) into the
 * storage object: magic, PROJ_STORE_SIZE, the encoded length (2 bytes), 2 reserved, the encoded bytes, the hash of
 * everything before it in the last 4. A project too busy to fit (five FEEDs of random steps) is refused: nothing
 * written ("PROJECT TOO BIG"). The layout (each step field by field: all 64 of a field together, so the runs are long):
 *   the globals (G_COUNT x int16 LE), sel, the bank's pattern + 1, P_COUNT, NTRK
 *   NTRK x (P_COUNT values + 64, engine, preset, the steps: note 0..3 (bit 7 bit j of the nudge), n | time << 3 |
 *           flags << 5, vel | ratch bit 0 << 7, hit, acc, chance | ratch bit 1 << 7, 64 each)
 *   the chain, the motion (their structs as they are), the MIDI IN channels (NTRK), SOUND MORPH's AMOUNT and TARGET
 *   (NTRK each), PHANTOM's 5 bytes and its LEVEL + 1, the prints (rsv, count, count x 6), the name (12) */
#define PROJ_ENC_OFF 12u
#define PROJ_ENC_MAX (PROJ_STORE_SIZE - PROJ_ENC_OFF - 4u)
/* the run-length coding as a stream (no buffer of the whole layout: 6 KB of the pool kept): rw_put a byte at a time into
 * d (runs of 3..130 as rle_enc writes them, literals up to 128), rw_end the encoded length (0: over cap); rr_get a byte
 * at a time (rr.bad: past the end, or a byte that is not one) */
static struct {
    uint8_t *d, lit[128], rb;
    uint32_t w, cap, nl, nr, bad;
} rw;
static void rw_lit(void)
{
    if (!rw.nl)
        return;
    if (rw.w + 1u + rw.nl > rw.cap) { rw.bad = 1; rw.nl = 0; return; }
    rw.d[rw.w++] = (uint8_t)(rw.nl - 1u);
    memcpy(rw.d + rw.w, rw.lit, rw.nl);
    rw.w += rw.nl;
    rw.nl = 0;
}
static void rw_run(void)
{
    if (rw.nr >= 3u) {
        rw_lit();
        if (rw.w + 2u > rw.cap) { rw.bad = 1; return; }
        rw.d[rw.w++] = (uint8_t)(125u + rw.nr);
        rw.d[rw.w++] = rw.rb;
    } else {
        while (rw.nr--) {
            rw.lit[rw.nl++] = rw.rb;
            if (rw.nl == 128u)
                rw_lit();
        }
    }
    rw.nr = 0;
}
static void rw_put(uint32_t x)
{
    if (rw.nr && (uint8_t)x == rw.rb && rw.nr < 130u) { rw.nr++; return; }
    rw_run();
    rw.rb = (uint8_t)x;
    rw.nr = 1;
}
static void rw_bytes(const void *p, uint32_t n) { const uint8_t *b = p; while (n--) rw_put(*b++); }
static uint32_t rw_end(void) { rw_run(); rw_lit(); return rw.bad ? 0u : rw.w; }
static struct {
    const uint8_t *s;
    uint32_t i, n, run, lit, bad;
    uint8_t rb;
} rr;
static uint32_t rr_get(void)
{
    if (rr.run) { rr.run--; return rr.rb; }
    if (rr.lit) { rr.lit--; return rr.i < rr.n ? rr.s[rr.i++] : (rr.bad = 1, 0u); }
    if (rr.i >= rr.n) { rr.bad = 1; return 0; }
    {
        uint32_t c = rr.s[rr.i++];
        if (c >= 128u) {
            if (rr.i >= rr.n) { rr.bad = 1; return 0; }
            rr.run = c - 126u;                          /* (this byte, then the rest) */
            rr.rb = rr.s[rr.i++];
            return rr.rb;
        }
        rr.lit = c;
        return rr.i < rr.n ? rr.s[rr.i++] : (rr.bad = 1, 0u);
    }
}
static void rr_bytes(void *p, uint32_t n) { uint8_t *b = p; while (n--) *b++ = (uint8_t)rr_get(); }

/* FUNB (Phantom Tracker 0.10): the project as the bytes below, run-length coded as they are written (rw_put) into the
 * storage object: magic, PROJ_STORE_SIZE, the encoded length (2 bytes), 2 reserved, the encoded bytes, the hash of
 * everything before it in the last 4. A project too busy to fit (five FEEDs of random steps) is refused: nothing
 * written ("PROJECT TOO BIG"). The bytes (each step field by field: all 64 of a field together, so the runs are long):
 *   the globals (G_COUNT x int16 LE), sel, the bank's pattern + 1, P_COUNT, NTRK
 *   NTRK x (P_COUNT values + 64, engine, preset, the steps: note 0..3 (bit 7 bit j of the nudge), n | time << 3 |
 *           flags << 5, vel | ratch bit 0 << 7, hit, acc, chance | ratch bit 1 << 7, 64 each)
 *   the chain, the motion (their structs as they are), the MIDI IN channels (NTRK), SOUND MORPH's AMOUNT and TARGET
 *   (NTRK each), PHANTOM's 5 bytes and its LEVEL + 1, the prints (rsv, count, count x 6), the name (12),
 *   the B sides (0.10: B_N x (on, engine, preset, P_COUNT values + 64)) */
static int proj_pack(project_store_t *out, const project_t *q)
{
    uint8_t *b = out->raw;
    uint32_t t, i, j, f, e, magic = PROJ_MAGIC, size = PROJ_STORE_SIZE, sum;
    if (!chain_valid(&q->chain) || !motion_valid(&q->motion) || q->fxp.count > FXP_MAX)
        return 0;
    for (t = 0; t < NTRK; t++) {                        /* (every value in range first: nothing half written) */
        for (i = 0; i < P_COUNT; i++)
            if (q->t[t].p[i] < -64 || q->t[t].p[i] > 127) return 0;
        for (i = 0; i < NSTEP; i++) {
            const step_t *st = &q->t[t].step[i];
            if (st->n > 4u || st->time > ST_REST || (st->flags & ~(3u | SF_RATCH | SF_NUDGE)) ||
                st->probability >= COND_FIRST + NCOND)
                return 0;
        }
    }
    for (t = 0; t < B_N; t++)
        for (i = 0; i < P_COUNT; i++)
            if (q->bs[t].p[i] < -64 || q->bs[t].p[i] > 127) return 0;
    memset(out, 0, sizeof *out);
    memset(&rw, 0, sizeof rw);
    rw.d = b + PROJ_ENC_OFF;
    rw.cap = PROJ_ENC_MAX;
    for (i = 0; i < G_COUNT; i++) {
        rw_put((uint8_t)q->g[i]);
        rw_put((uint8_t)((uint16_t)q->g[i] >> 8));
    }
    rw_put(q->sel); rw_put(q->rsv <= NBANK_V ? q->rsv : 0u); rw_put(P_COUNT); rw_put(NTRK);
    for (t = 0; t < NTRK; t++) {
        for (i = 0; i < P_COUNT; i++)
            rw_put((uint8_t)(q->t[t].p[i] + 64));
        rw_put(q->t[t].engine); rw_put(q->t[t].preset);
        for (f = 0; f < 9u; f++)
            for (i = 0; i < NSTEP; i++) {
                const step_t *st = &q->t[t].step[i];
                uint32_t r = step_ratchet(st) - 1u, nd = (uint32_t)step_nudge(st) & 15u, x;
                j = f;
                x = j < 4u ? (uint32_t)((st->note[j] > 127u ? 127u : st->note[j]) | (nd >> j & 1u) << 7) :
                    j == 4u ? (uint32_t)(st->n | st->time << 3 | (st->flags & 3u) << 5) :
                    j == 5u ? (uint32_t)((st->vel > 127u ? 127u : st->vel) | (r & 1u) << 7) :
                    j == 6u ? st->hit : j == 7u ? (uint32_t)(st->acc & st->hit) :
                    (uint32_t)(st->probability | (r >> 1) << 7);
                rw_put(x);
            }
    }
    rw_bytes(&q->chain, sizeof q->chain);
    rw_bytes(&q->motion, sizeof q->motion);
    for (t = 0; t < NTRK; t++) rw_put(q->mch[t] < MCH_N ? q->mch[t] : 0u);
    for (t = 0; t < NTRK; t++) rw_put(q->mo_amt[t] <= 100u ? q->mo_amt[t] : 0u);
    for (t = 0; t < NTRK; t++) rw_put(q->mo_tgt[t]);
    rw_bytes(q->ph, sizeof q->ph);
    rw_put(q->ph_lvl);
    rw_put(q->fxp.rsv); rw_put(q->fxp.count);
    rw_bytes(q->fxp.ev, q->fxp.count * (uint32_t)sizeof q->fxp.ev[0]);
    {
        char n[PROJ_NAME_LEN + 1u];
        uint8_t nb[PROJ_NAME_LEN];
        memset(nb, 0, sizeof nb);
        memcpy(nb, n, proj_name_get(n, (const uint8_t *)q->name));
        rw_bytes(nb, PROJ_NAME_LEN);
    }
    for (t = 0; t < B_N; t++) {
        const bside_t *bs = &q->bs[t];
        rw_put((uint32_t)clamp(bs->on, 0, 2)); rw_put(bs->engine); rw_put(bs->preset);
        rw_put((uint32_t)(clamp(bs->tr, -24, 24) + 64)); rw_put((uint32_t)clamp(bs->lv, 0, 127));
        rw_put((uint32_t)(clamp(bs->pn, -64, 63) + 64));
        for (i = 0; i < P_COUNT; i++)
            rw_put((uint8_t)(bs->p[i] + 64));
    }
    if (!(e = rw_end()))
        return 0;                                       /* (too big: PROJECT TOO BIG) */
    memcpy(b, &magic, 4); memcpy(b + 4, &size, 4);
    b[8] = (uint8_t)e; b[9] = (uint8_t)(e >> 8);
    sum = proj_hash(b, PROJ_STORE_SIZE - 4u); memcpy(b + PROJ_STORE_SIZE - 4u, &sum, 4);
    return 1;
}
/* a stored FUNB -> q (0: not one, or a value out of its range) */
static int proj_unpack(project_t *q, const uint8_t *src, uint32_t st)
{
    uint32_t t, i, j, f, magic, size, sum, e;
    if (st != PROJ_STORE_SIZE)
        return 0;
    memcpy(&magic, src, 4); memcpy(&size, src + 4, 4); memcpy(&sum, src + st - 4u, 4);
    e = src[8] | (uint32_t)src[9] << 8;
    if (magic != PROJ_MAGIC || size != st || sum != proj_hash(src, st - 4u) || e > PROJ_ENC_MAX)
        return 0;
    memset(&rr, 0, sizeof rr);
    rr.s = src + PROJ_ENC_OFF;
    rr.n = e;
    memset(q, 0, sizeof *q); q->magic = PROJ_MAGIC; q->size = sizeof *q;
    for (i = 0; i < G_COUNT; i++) {
        uint32_t lo = rr_get();
        q->g[i] = (int16_t)(lo | rr_get() << 8);
    }
    q->sel = (uint8_t)rr_get(); q->rsv = (uint8_t)rr_get(); q->parts = NPART; q->phys = PROJ_PHYS;
    if (q->rsv > NBANK_V) q->rsv = 0;
    if (rr_get() != P_COUNT || rr_get() != NTRK || rr.bad)
        return 0;
    for (t = 0; t < NTRK; t++) {
        uint8_t nd[NSTEP], r0[NSTEP];
        for (i = 0; i < P_COUNT; i++) { uint32_t x = rr_get(); if (x > 191u) return 0; q->t[t].p[i] = (int16_t)x - 64; }
        q->t[t].engine = (uint8_t)rr_get(); q->t[t].preset = (uint8_t)rr_get();
        if (q->t[t].engine >= NENGINES) return 0;
        memset(nd, 0, sizeof nd);
        for (f = 0; f < 9u; f++)
            for (i = 0; i < NSTEP; i++) {
                step_t *s = &q->t[t].step[i];
                uint32_t x = rr_get();
                j = f;
                if (j < 4u) {
                    nd[i] |= (uint8_t)((x >> 7) << j);
                    s->note[j] = (uint8_t)(x & 127u);
                } else if (j == 4u) {
                    if (x > 127u) return 0;
                    s->n = x & 7u; s->time = (x >> 3) & 3u; s->flags = (x >> 5) & 3u;
                } else if (j == 5u) {
                    s->vel = x & 127u; r0[i] = (uint8_t)(x >> 7);
                } else if (j == 6u) {
                    s->hit = (uint8_t)x;
                } else if (j == 7u) {
                    s->acc = (uint8_t)x;
                } else {
                    s->probability = x & 127u;
                    if (s->n > 4u || s->time > ST_REST || s->probability >= COND_FIRST + NCOND || (s->acc & ~s->hit))
                        return 0;
                    step_set_ratchet(s, (r0[i] | (x >> 7) << 1) + 1u);
                    step_set_nudge(s, (int32_t)(nd[i] ^ 8u) - 8);
                }
            }
    }
    rr_bytes(&q->chain, sizeof q->chain);
    rr_bytes(&q->motion, sizeof q->motion);
    if (rr.bad || !chain_valid(&q->chain) || !motion_valid(&q->motion)) return 0;
    for (t = 0; t < NTRK; t++) { uint32_t x = rr_get(); q->mch[t] = x < MCH_N ? (uint8_t)x : 0u; }
    for (t = 0; t < NTRK; t++) { uint32_t x = rr_get(); q->mo_amt[t] = x <= 100u ? (uint8_t)x : 0u; }
    for (t = 0; t < NTRK; t++) q->mo_tgt[t] = (uint8_t)rr_get();
    rr_bytes(q->ph, sizeof q->ph);
    { uint32_t x = rr_get(); q->ph_lvl = x <= 128u ? (uint8_t)x : 0u; }
    q->fxp.rsv = (uint8_t)rr_get(); q->fxp.count = (uint8_t)rr_get();
    if (rr.bad || q->fxp.count > FXP_MAX) return 0;
    rr_bytes(q->fxp.ev, q->fxp.count * (uint32_t)sizeof q->fxp.ev[0]);
    fxp_bound(&q->fxp);
    {
        uint8_t nb[PROJ_NAME_LEN];
        char nm[PROJ_NAME_LEN + 1u];
        rr_bytes(nb, PROJ_NAME_LEN);
        memcpy(q->name, nm, proj_name_get(nm, nb));
    }
    for (t = 0; t < B_N; t++) {
        bside_t *bs = &q->bs[t];
        uint32_t on = rr_get(), tr, lv, pn;
        bs->engine = (uint8_t)rr_get(); bs->preset = (uint8_t)rr_get();
        tr = rr_get(); lv = rr_get(); pn = rr_get();
        if (on > 2u || tr < 40u || tr > 88u || lv > 127u || pn > 127u || bs->engine >= NENGINES) return 0;
        bs->on = (int16_t)on; bs->tr = (int16_t)tr - 64; bs->lv = (int16_t)lv; bs->pn = (int16_t)pn - 64;
        for (i = 0; i < P_COUNT; i++) { uint32_t x = rr_get(); if (x > 191u) return 0; bs->p[i] = (int16_t)x - 64; }
    }
    if (rr.bad || rr.i != rr.n || rr.run || rr.lit)
        return 0;
    for (t = 0; t < NTRK; t++)
        memcpy(q->fm6[t], FM6_INIT, FM6_PACKED);        /* (FM6 retired: never plays) */
    q->sum = proj_sum(q);
    return 1;
}

/* Dungeon Tracker 1.1: tracks of a retired engine (engines.c ENG_SUBST: DRUM ANALOG FM6 ..) -> its substitute, the
 * sound its preset of the old sound's category (retire.c; an old drum part's kit, PROJ_DEF_KEEP: the first kit), the
 * rest of the track as it was; their automation of the E values dropped (they meant the old engine's). Last of the
 * conversions: the ones before it may make DRUM tracks. Idempotent */
static void proj_retired(project_t *q)
{
#if DT_RETIRE && !defined(PROJ_HOST)
    uint32_t k, i, n, hit = 0;
    for (k = 0; k < NTRK; k++) {
        proj_trk_t *d = &q->t[k];
        uint32_t pi;
        if (!eng_retired(d->engine))
            continue;
        {
            uint32_t kp = d->preset < PROJ_DEF_KEEP ? d->preset : 0u, c = preset_cat(eng_idx(d->engine), kp);
            pi = eng_subst_preset(d->engine, kp);
            d->engine = (uint8_t)eng_retire_values(d->engine, c, pi, d->p);
        }
        if (d->preset < PROJ_DEF_KEEP)
            d->preset = (uint8_t)pi;
        hit |= 1u << k;
    }
    if (!hit)
        return;
    for (i = n = 0; i < q->motion.count && i < MOTION_MAX; i++) {
        const motion_event_t *e = &q->motion.event[i];
        if (((hit >> (e->place >> 6)) & 1u) && !cmd_id(MOTION_ID(e)) && MOTION_ID(e) >= P_E0)
            continue;
        q->motion.event[n++] = *e;
    }
    for (i = n; i < q->motion.count && i < MOTION_MAX; i++)
        memset(&q->motion.event[i], 0, sizeof q->motion.event[i]);
    q->motion.count = (uint8_t)n;
    q->sum = proj_sum(q);
#else
    (void)q;
#endif
}

#ifndef PROJ_HOST
static project_t proj_scratch               /* decoded main-loop work, never audio ISR */
    __attribute__((section(".pool")));
static char proj_name[PROJ_NAME_LEN + 1u]    /* the name of the music as it is now (loaded, saved, the editor's */
    __attribute__((section(".pool")));       /* runtime restore); "" = none. A save takes it unless one is given */
#define PROJ_NO_SLOT 0xFFu
static uint8_t proj_cur = PROJ_NO_SLOT;      /* the slot the music was loaded from or last saved to (a rename of it
                                              * renames the music too); PROJ_NO_SLOT none (the editor's restore) */
static union {                               /* serialized main-loop work; no retained expansion */
    project_store_t s;
    uint8_t raw[3840];                         /* (the staging of a backup object, up to a storage object: editor_backup.c) */
} proj_wire_u __attribute__((section(".pool")));
#define proj_wire (proj_wire_u.s)
static uint8_t proj_wire_gen;                /* +1 whenever proj_wire is rewritten (a backup's runtime copy lives there) */

static void proj_steps(step_t *s)            /* a loaded sequence stays inside its fixed fields */
{
    uint32_t i, j;
    for (i = 0; i < NSTEP; i++) {
        if (s[i].n > 4u) s[i].n = 4;
        if (s[i].time > ST_REST) s[i].time = ST_REST;
        for (j = 0; j < 4u; j++) s[i].note[j] &= 127u;
        s[i].acc &= s[i].hit;
    }
}

/* an imported older format may hold values FUN7 cannot pack: keep them inside the fields and ranges */
static void proj_bound(project_t *q)
{
    uint32_t t, i;
    for (t = 0; t < NTRK; t++) {
        proj_steps(q->t[t].step);
        for (i = 0; i < NSTEP; i++) {
            step_t *s = &q->t[t].step[i];
            s->flags &= 3u | SF_RATCH | SF_NUDGE;
            s->vel &= 127u;
            if (s->probability >= COND_FIRST + NCOND) s->probability = 0;
        }
        for (i = 0; i < P_COUNT; i++) {
            const param_desc_t *d = param_desc_of(q->t[t].engine % NENGINES, i);
            q->t[t].p[i] = (int16_t)param_fit(d, q->t[t].p[i]);   /* (a retired KIT: the kit it plays) */
        }
    }
    q->sum = proj_sum(q);
}

#if RONDO_LAYOUT && FELUCCA_FLASH
#define PROJ_SONGS 1                                   /* Dungeon Tracker: whole songs in the slots */
static void bank_bound(chain_pattern_t *c);
#include "song_store.c"
/* slot s's project (E..K) in the one RAM copy: read straight from the song store (its records are FUN10 projects:
 * sg_boot packs 1.1's, the editor's restore packs what it gets), so neither proj_wire nor proj_scratch is touched */
static project_store_t *pslot_hi(uint32_t s)
{
    if (proj_hi_slot != s) {
        proj_hi_slot = (uint8_t)s;
        if (!flash_ok || sg_get_project(s, &proj_hi_cache, sizeof proj_hi_cache) != (int)sizeof proj_hi_cache)
            memset(&proj_hi_cache, 0, sizeof proj_hi_cache);
    }
    return &proj_hi_cache;
}
#else
#define PROJ_SONGS 0
#endif
#if FELUCCA_FLASH
/* slot's stored project -> proj_wire: its length, -1 none */
static int proj_flash_load(uint32_t slot)
{
#if PROJ_SONGS
    return sg_get_project(slot, &proj_wire, sizeof proj_wire);
#else
    return st_load(OBJ_PROJECT0 + (slot & 3u), &proj_wire, sizeof proj_wire);
#endif
}
/* proj_wire's first len bytes -> slot (len 0: erased); songs: with the patterns as `pats` says (song_store.c
 * SG_PATS_*). 0 done, -9 too big, else failed */
static int proj_flash_save(uint32_t slot, uint32_t len, uint32_t pats)
{
#if PROJ_SONGS
    return sg_put(slot, &proj_wire, len, pats);
#else
    (void)pats;
    return st_save(OBJ_PROJECT0 + (slot & 3u), &proj_wire, len);
#endif
}
#ifndef SG_PATS_NONE
#define SG_PATS_NONE 0u
#define SG_PATS_LIVE 1u
#define SG_PATS_KEEP 2u
#endif
/* slot from flash into RAM (format 7, or format 6 / 5 / 4 / 3 / 2 / 1 converted) */
static void proj_fetch(uint32_t slot)
{
    project_store_t *q = pslot(slot);
    int n;
    proj_wire_gen++;
    n = proj_flash_load(slot);
    if (!proj_import(&proj_scratch, &proj_wire, n))
        memset(q->raw, 0, 4);
    else {
        proj_bound(&proj_scratch);
        if (!proj_pack(q, &proj_scratch))
            memset(q->raw, 0, 4);
    }
}
#endif

static void project_capture(project_t *p)
{
    uint32_t i;
    uint32_t f = motion_guard();
    memset(p, 0, sizeof *p);
    p->magic = PROJ_MAGIC;
    p->size = sizeof *p;
    for (i = 0; i < G_COUNT; i++)
        p->g[i] = song.g[i];
    p->sel = song.sel;
    p->parts = NPART;
    p->phys = PROJ_PHYS;
    p->chain = chain_config;
    for (i = 0; i < NTRK; i++) {
#if RONDO_LAYOUT
        for (uint32_t j = 0; j < P_COUNT; j++) p->t[i].p[j] = morph_base(&trk[i], j);   /* (a morph: the sound A) */
#else
        for (uint32_t j = 0; j < P_COUNT; j++) p->t[i].p[j] = motion_base_value(&trk[i], j);
#endif
        p->t[i].engine = trk[i].eng_req;
        p->t[i].preset = trk[i].preset;
        memcpy(p->t[i].step, trk[i].step, sizeof trk[i].step);
        fm6_pack(fm6_patch[i], p->fm6[i]);
    }
    p->motion = motion;
    p->fxp = fxp;
    memcpy(p->mch, song.mch, sizeof p->mch);
#if RONDO_LAYOUT
    memcpy(p->mo_amt, mo.amt, sizeof p->mo_amt);
    memcpy(p->mo_tgt, mo.tgt, sizeof p->mo_tgt);
    phantom_pack(p->ph);
    p->ph_lvl = phantom_pack_level();
#endif
    for (i = 0; i < B_N; i++) {                         /* 0.10: the B sides, A's sound in t[] (a flipped one put back) */
        p->bs[i] = bsr[i].s;
        if (bsr[i].flip) {
            proj_trk_t *a = &p->t[i + 1u];
            bside_t *b = &p->bs[i];
            uint32_t j;
            uint8_t x;
            for (j = 0; j < P_COUNT; j++)
                if (b_own(j)) { int16_t v = a->p[j]; a->p[j] = b->p[j]; b->p[j] = v; }
            x = a->engine; a->engine = b->engine; b->engine = x;
            x = a->preset; a->preset = b->preset; b->preset = x;
        }
    }
    motion_unguard(f);
    memcpy(p->name, proj_name, str_len(proj_name));
    p->sum = proj_sum(p);
}

/* the music as it is now -> slot, named `name` (0: the current name; "" none); 0 saved, nonzero refused or failed.
 * The current name becomes the saved one */
static int project_save_as(uint32_t slot, const char *name)
{
    project_t *p = &proj_scratch;
    if (transport_busy()) {                            /* a flash erase silences the audio and stalls the */
        ui_message("STOP TO SAVE");                     /* sequencer (storage_hw.c): only while stopped */
        return 1;
    }
#if PROJ_SONGS
    fm1_irq_off();
    bank_keep();                                        /* (1.2: the current pattern as it is now, with the others) */
    fm1_irq_on();
#endif
    project_capture(p);
    if (name) {
        memset(p->name, 0, sizeof p->name);
        memcpy(p->name, name, str_len(name) < PROJ_NAME_LEN ? str_len(name) : PROJ_NAME_LEN);
    }
    proj_wire_gen++;
    if (!proj_pack(&proj_wire, p)) { ui_message("SAVE FORMAT ERROR"); return 2; }

#if FELUCCA_FLASH
    if (flash_ok) {
        int rc = proj_flash_save(slot, sizeof proj_wire, SG_PATS_LIVE);
        if (rc) {
            ui_message(rc == -9 ? "SONG TOO BIG" : "SAVE ERROR");
            return 2;
        }
        memcpy(pslot(slot), &proj_wire, sizeof proj_wire);
        proj_name_get(proj_name, (const uint8_t *)p->name);
        proj_cur = (uint8_t)(slot % NPROJ);
        pat_from_set((slot % NPROJ) + 1u);
        ui_message("SAVED");
        return 0;
    }
#endif
    memcpy(pslot(slot), &proj_wire, sizeof proj_wire);
    proj_name_get(proj_name, (const uint8_t *)p->name);
    proj_cur = (uint8_t)(slot % NPROJ);
    pat_from_set((slot % NPROJ) + 1u);
    ui_message("SAVED (RAM)");
    return 0;
}
static int project_save(uint32_t slot) { return project_save_as(slot, 0); }
static void project_cur_name(char *b) { str_cpy(b, proj_name, PROJ_NAME_LEN + 1u); }   /* b: 13 bytes */

/* slot's name -> b (PROJ_NAME_LEN + 1 bytes); 0 = an empty slot (b ""). Uses proj_scratch */
static int project_name(uint32_t slot, char *b) { return project_info(slot, b, 0, 0, 0); }
/* .. and, len48 not 0, the length of each track's pattern as SONG plays it (song_chain.c chain_tick: the clock track's
 * LEN steps of its DIV, clamped as chain_prepare does) in 1/48 quarter notes (a 4/4 bar: 192; 0 = an empty slot):
 * len48[0..NTRK-1] */
static uint32_t len48_of(int32_t len, int32_t div)
{
    uint32_t n = (uint32_t)clamp(len, TP[P_SLEN].min, TP[P_SLEN].max), d = (uint32_t)clamp(div, TP[P_SDIV].min, TP[P_SDIV].max);
    return n * (d < 6u ? 48u / DIV_DEN[d] : d < 10u ? 48u << (d - 5u) : 48u / DIV_DEN[d % 6u]);   /* (fx.c div_samples) */
}
/* SONG's picture of a pattern (ui_graph.c graph_song): its LEN steps in n = min(LEN, 16) cells (a cell: steps
 * c * LEN / n .. (c + 1) * LEN / n - 1), 2 bits a cell, cell c at bits 2c: a step weighs 0 (rest, empty, CHANCE 0),
 * 1 (a tie), 2 (a note or hit), 3 (accented); a cell's level is its steps' mean, rounded up (0..3) */
static uint32_t pat_density(const step_t *st, int32_t slen, uint8_t *n)
{
    uint32_t len = (uint32_t)clamp(slen, 1, NSTEP), nc = len < 16u ? len : 16u, c, i, d = 0;
    for (c = 0; c < nc; c++) {
        uint32_t i0 = c * len / nc, i1 = (c + 1u) * len / nc, w = 0;
        for (i = i0; i < i1; i++) {
            const step_t *s = &st[i];
            if (s->time == ST_TIE)
                w += 1u;
            else if (s->time == ST_NOTE && (s->n || s->hit) && step_chance(s))
                w += (s->flags & SF_ACCENT) || s->acc ? 3u : 2u;
        }
        w = (w + (i1 - i0) - 1u) / (i1 - i0);
        d |= (w < 3u ? w : 3u) << (2u * c);
    }
    *n = (uint8_t)nc;
    return d;
}
static uint32_t proj_density(const proj_trk_t *t, uint8_t *n) { return pat_density(t->step, t->p[P_SLEN], n); }
/* .. and, den not 0, each track's picture (proj_density: den[k], its cells n[k]; 0 for an empty slot) */
static int project_info(uint32_t slot, char *b, uint16_t *len48, uint32_t *den, uint8_t *n)
{
    uint32_t k;
    b[0] = 0;
    if (len48)
        memset(len48, 0, NTRK * sizeof *len48);
    if (den) {
        memset(den, 0, NTRK * sizeof *den);
        memset(n, 0, NTRK);
    }
    if (!proj_import(&proj_scratch, pslot(slot), sizeof(project_store_t)))
        return 0;
    proj_name_get(b, (const uint8_t *)proj_scratch.name);
    for (k = 0; len48 && k < NTRK; k++)
        len48[k] = (uint16_t)len48_of(proj_scratch.t[k].p[P_SLEN], proj_scratch.t[k].p[P_SDIV]);
    for (k = 0; den && k < NTRK; k++)
        den[k] = proj_density(&proj_scratch.t[k], &n[k]);
    return 1;
}

/* a stored project renamed in place (nothing else of it changes; the music playing is not touched, but the slot
 * it was loaded from / saved to: its name is the new one, the next SAVE's prefill): 0 done, 1 refused (playing,
 * an empty slot), 2 failed (the slot as it was) */
static int project_rename(uint32_t slot, const char *name)
{
    project_t *p = &proj_scratch;
    if (transport_busy()) {
        ui_message("STOP TO SAVE");
        return 1;
    }
    if (!proj_import(p, pslot(slot), sizeof(project_store_t))) {
        ui_message("EMPTY SLOT");
        return 1;
    }
    memset(p->name, 0, sizeof p->name);
    memcpy(p->name, name, str_len(name) < PROJ_NAME_LEN ? str_len(name) : PROJ_NAME_LEN);
    proj_wire_gen++;
    if (!proj_pack(&proj_wire, p)) { ui_message("SAVE FORMAT ERROR"); return 2; }
#if FELUCCA_FLASH
    if (flash_ok && proj_flash_save(slot, sizeof proj_wire, SG_PATS_KEEP)) {   /* (its patterns kept) */
        ui_message("SAVE ERROR");
        return 2;
    }
#endif
    memcpy(pslot(slot), &proj_wire, sizeof proj_wire);
    if (proj_cur == (slot % NPROJ))
        proj_name_get(proj_name, (const uint8_t *)p->name);
#if FELUCCA_FLASH
    if (flash_ok) { ui_message("RENAMED"); return 0; }
#endif
    ui_message("RENAMED (RAM)");
    return 0;
}

/* 1.5.5 (#199): a stored project erased (SAVE > PROJECT, KNOB 2 ERASE, the dialog): the slot empty. Written as a save
 * is (st_save: the other copy of the A/B pair erased and an empty record committed last, as a full backup's restore
 * writes an empty slot), so a power cut leaves the project there or the slot empty, never half of either. The music
 * playing is not touched: it keeps its name; were it loaded from or last saved to this slot it now has none (the
 * header's slot letter goes). 0 erased, 1 refused (playing, an empty slot), 2 failed (the slot as it was) */
static int project_erase(uint32_t slot)
{
    uint32_t k;
    slot %= NPROJ;
    if (transport_busy()) {                            /* (a flash erase silences the audio: project_save_as) */
        ui_message("STOP TO ERASE");
        return 1;
    }
    if (!project_used(slot)) {
        ui_message("EMPTY SLOT");
        return 1;
    }
#if FELUCCA_FLASH
    if (flash_ok && proj_flash_save(slot, 0, SG_PATS_NONE)) {
        ui_message("ERASE ERROR");
        return 2;
    }
#endif
    memset(pslot(slot), 0, sizeof(project_store_t));
    if (proj_cur == slot)
        proj_cur = PROJ_NO_SLOT;
    for (k = 0; k < NTRK; k++)
        if (pat_from[k] == slot + 1u)
            pat_from[k] = 0;
#if FELUCCA_FLASH
    if (flash_ok) {
        char b[20] = "PROJECT A ERASED";
        b[8] = (char)('A' + slot);
        ui_message(b);
        return 0;
    }
#endif
    ui_message("ERASED (RAM)");
    return 0;
}

static int project_restore_runtime(const project_t *input)
{
    project_t *p = &proj_scratch;
    uint32_t i, k;
    if (!proj_ok(input) || !proj_engines_ok(input)) return 1;
    mom_back(0);                                  /* (ui_input.c MOMENTARY: the values as they were, then the load) */
    memset(drum_mute, 0, sizeof drum_mute);       /* (1.4.1: the DRUM lanes' mutes, performance state) */
    if (p != input) memcpy(p, input, sizeof *p);
    proj_retired(p);                                    /* .. or tracks of a retired engine (1.1: last) */
    transport_req = 2;
    panic_req = (1u << NTRK) - 1u;
    fm1_irq_off();                                      /* the audio ISR must not see half a project */
    seq_stop();
    transport_req = 0;
    chain_config = p->chain;
    motion = p->motion;
    fxp = p->fxp;
    fxp_stop();
    memset(motion_active, 0, sizeof motion_active);
    motion_base_valid = 0;
    ui.song_row = 0;
    for (i = 0; i < G_COUNT; i++)
        if (i != G_SLOT && i != G_LOAD && i != G_SAVE)
            song.g[i] = (int16_t)clamp(p->g[i], GP[i].min, GP[i].max);
    for (k = 0; k < NTRK; k++)                          /* MIDI IN TRACK's channels (1.5.5; 0, channels 1..4, before) */
        song.mch[k] = (uint8_t)(p->mch[k] < MCH_N ? p->mch[k] : 0u);
    midi_route = MIDI_ROUTE_AGAIN;                      /* (events_block: what the channels no longer hear let go) */
    for (k = 0; k < NTRK; k++) {
        track_t *t = &trk[k];
        const proj_trk_t *s = &p->t[k];
        uint32_t e = s->engine;                         /* (< NENGINES: proj_engines_ok) */
        t->eng_req = (uint8_t)e;
        t->user = 0;                                    /* (no user preset slot is saved) */
        for (i = 0; i < P_COUNT; i++) {                 /* every value back inside its range (param_fit) */
            const param_desc_t *d = param_desc_of(e, i);
            t->p[i] = (int16_t)param_fit(d, s->p[i]);
        }
        t->p[P_BEND] = 0;                               /* (1.0: the wheel starts centred; Felucca's unused value gone) */
        t->preset = (uint8_t)(ENGINES[e]->npresets ? (s->preset >= PROJ_DEF_KEEP ? 0u : s->preset) % ENGINES[e]->npresets : 0u);
        memcpy(t->step, s->step, sizeof t->step);
        proj_steps(t->step);
        {   /* the project's own FM6 patch, never reloaded from SLOT: F n if it is that factory patch, else OWN */
            uint8_t v[FP_SIZE + 1u];
            fm6_unpack(p->fm6[k], v);
            fm6_set_patch(k, v);
            fm6_adopt(k);
        }
    }
    for (k = 0; k < B_N; k++) {                         /* 0.10: the B sides, unflipped, their voices gone with the panic */
        bsr_t *b = &bsr[k];
        memset(b, 0, sizeof *b);
        b->s = p->bs[k];
        if (b->s.engine >= NENGINES || ENG_KIT(b->s.engine) || ENG_KIT(trk[k + 1u].eng_req % NENGINES))
            b->s.on = b->s.on ? 2 : 0;                  /* (a kit on either side: off) */
        b->s.engine = (uint8_t)(b->s.engine % NENGINES);
        b->eng_req = b->s.engine;
        for (i = 0; i < P_COUNT; i++)
            b->s.p[i] = (int16_t)param_fit(param_desc_of(b->s.engine, i), b->s.p[i]);
        b->s.preset = (uint8_t)(ENGINES[b->s.engine]->npresets ? b->s.preset % ENGINES[b->s.engine]->npresets : 0u);
    }
    for (k = 0; k < NTRK; k++)
        for (i = 0; i < NVOICE; i++)
            if (trk[k].v[i].pfix & 2u) {                /* (a B side's voice: its sound is gone) */
                trk[k].v[i].active = trk[k].v[i].gate = trk[k].v[i].stage = 0;
                trk[k].v[i].pfix = 0;
            }
    song.sel = (uint8_t)(p->sel < NTRK ? p->sel : 0u);
#if RONDO_LAYOUT
    morph_load(p->mo_amt, p->mo_tgt);                   /* (RONDO: SOUND MORPH from the sounds just set: morph_poll) */
    memset(song.clsx, 0, sizeof song.clsx);              /* (no party: the roles are fixed, core.h trk_cls) */
    if (phantom_unpack(p->ph))                           /* (PHANTOM's sound, if the project has one: else as it was) */
        phantom_unpack_level(p->ph_lvl);                 /* (0.9.15: its LEVEL; a project before: the default) */
#endif
    fm1_irq_on();
    proj_name_get(proj_name, (const uint8_t *)p->name);
    proj_cur = PROJ_NO_SLOT;                            /* (project_load: its slot) */
    pat_from_set(0);                                    /* (project_load: its slot; the header's letter) */
    undo.trk = 0;                                       /* (ui.c) the undo copy belongs to the old project */
    undo_depth++;                                       /* and these loads take none */
    for (k = 0; k < NTRK; k++)
        pat_sig[k] = ~steps_sig(&trk[k]);               /* a project's steps are the user's */
    undo_depth--;
    sync_reload = 1;
    ui.force = 1;
    ui_message("LOADED");
    memset(snd_said, 0, sizeof snd_said);               /* a missing sample is said again, after LOADED */
    return 0;
}
static void project_load(uint32_t slot)
{
#if FELUCCA_FLASH
    if (flash_ok && !proj_import(&proj_scratch, pslot(slot), sizeof(project_store_t))) proj_fetch(slot);
#endif
    if (!proj_import(&proj_scratch, pslot(slot), sizeof(project_store_t))) { ui_message("EMPTY SLOT"); return; }
    proj_bound(&proj_scratch);                          /* a retained RAM slot of an older format: as from flash */
    if (!project_restore_runtime(&proj_scratch)) {
        relay_set(0);                                   /* (Phantom Cosmonaut: RELAY LOCK let go, the music is new) */
        proj_cur = (uint8_t)(slot % NPROJ);
        pat_from_set((slot % NPROJ) + 1u);
#if PROJ_SONGS
        if (flash_ok && sg_get_patterns(slot) < 0)     /* (1.2: the song's six patterns, the current one the tracks') */
            ui_message("PATTERNS UNREADABLE");
#endif
    }
}

/* Dungeon Tracker: SAVE > TOOLS, KNOB 1 turned on past CLEAR (ui_input.c): NEW, a blank song to start from. Every
 * track's steps and automation, the song order, the other patterns of the bank (B..F; the current one is the cleared
 * tracks), the project's name and slot; the tempo, the globals, MIDI stay. sounds: NEW + DEFAULT SOUNDS, the power-on
 * sounds too (TRK_DEF, the mix with them; SOUND MORPH off). Its undo: SAVE held while nothing changed since (new_undo):
 * the song before it as a project (new_copy, packed: 3.8 KB of the pool), the bank's patterns never wiped (their keys
 * only emptied, so the flash forgets them; given back if still empty) */
static uint32_t autosave_sig(void);
static project_store_t new_copy __attribute__((section(".pool")));
static struct {
    uint8_t ok, cur, used, from[NTRK];
    uint32_t from_h[NTRK], after;
} nw;
static void (*new_undo_hook)(void);                     /* (1.0: RANDOM SONG's: its patterns away, boot_song.c) */
static void new_song(uint32_t sounds)
{
    uint32_t k;
    if (chain_busy()) {
        ui_message("STOP TO EDIT");
        return;
    }
    project_capture(&proj_scratch);
    nw.ok = (uint8_t)(proj_pack(&new_copy, &proj_scratch) != 0);
    nw.cur = proj_cur;
    memcpy(nw.from, pat_from, sizeof nw.from);
    memcpy(nw.from_h, pat_from_h, sizeof nw.from_h);
    mom_back(0);
    undo_depth++;                                       /* (no single-track undo copy of these) */
    if (sounds)
        for (k = 1; k <= B_N; k++) {                    /* 0.10: NEW + SOUNDS: no B sides */
            b_set(&trk[k], 0);
            memset(&bsr[k - 1u], 0, sizeof bsr[k - 1u]);
        }
    for (k = 0; k < NTRK; k++) {
        track_t *t = &trk[k];
        seq_release(t);
        if (sounds) {
            drum_mute[k] = 0;
            track_defaults(t);                          /* (the mix, LEN DIV SWING GATE, ARP, SCALE) */
#if RONDO_LAYOUT
#endif
            set_engine_of(t, TRK_DEF[k][0]);
            apply_preset_to(t, TRK_DEF[k][1]);
        }
        track_defaults_steps(t);
        t->nheld = 0;
        t->arp_phys = 0;
        pat_sig[k] = steps_sig(t);
        pat_last[k] = 0;
    }
    undo_depth--;
    new_undo_hook = 0;                                  /* (a NEW of its own; RANDOM SONG sets it after) */
#if RONDO_LAYOUT
    if (sounds)
        morph_reset();
    {
        uint32_t f = motion_guard();
        uint8_t others = (uint8_t)(bank.used & ~(1u << bank.cur));
        nw.used = others;
        bank.used = (uint8_t)(bank.used & ~others);
        bank.dirty |= others;                           /* (bank_poll: an empty record each) */
        bank.queue = CHAIN_OFF;
        motion_unguard(f);
    }
#endif
    chain_defaults(&chain_config);
    ui.song_row = 0;
    proj_name[0] = 0;
    proj_cur = PROJ_NO_SLOT;
    pat_from_set(0);
    undo.trk = 0;                                       /* (SAVE held: this NEW, not a load before it) */
    sync_reload = 1;
    ui.tl_new = 0;
    ui.act = 0;
    ui.force = 1;
    nw.after = autosave_sig();
    ui_message(sounds ? "NEW SONG + SOUNDS" : "NEW SONG");
    str_cpy(ui.msg2, "[SAVE] HOLD TO UNDO", sizeof ui.msg2);
}
/* SAVE held: the NEW undone when nothing changed since (a note, a sound, the tempo: the held SAVE is the usual undo,
 * of nothing). 1 = done */
static int new_undo(void)
{
    if (!nw.ok || undo.trk || chain_busy() || autosave_sig() != nw.after)
        return 0;
    nw.ok = 0;
    if (!proj_import(&proj_scratch, &new_copy, sizeof new_copy))
        return 0;
    proj_bound(&proj_scratch);
    if (project_restore_runtime(&proj_scratch))
        return 0;
    proj_cur = nw.cur;
    memcpy(pat_from, nw.from, sizeof pat_from);
    memcpy(pat_from_h, nw.from_h, sizeof pat_from_h);
    if (new_undo_hook) {
        new_undo_hook();
        new_undo_hook = 0;
    }
#if RONDO_LAYOUT
    {
        uint8_t back = (uint8_t)(nw.used & ~bank.used);   /* (a key filled since keeps what it has now) */
        bank.used |= back;
        bank.dirty |= back;
    }
#endif
    ui_message("NEW UNDONE");
    return 1;
}

/* settings + learned panel table: one flash object. The flash copy wins at
 * boot (the .noinit copies are garbage after a power-off). */
#include "settings_persist.c"
#if FELUCCA_FLASH && FELUCCA_SLICE
#include "slice_store.c"                          /* SLICE's MAN slices, kept in the user slots */
#endif
#if FELUCCA_FLASH
static persist_t persist_saved __attribute__((section(".pool")));
static uint8_t persist_pending;                 /* 1 requested, 2 waiting after a flash error */
static uint32_t persist_retry_ms;
#endif

/* the FM-1's flash parts (JEDEC id): the 1 MiB part of the first units, and the UC25HQ80 of newer ones (0xB36014,
 * Felucca 1.8.2). Any other: RAM-only (nothing saved, and no update: main.c ota_session wants flash_ok, so a unit whose
 * part is missing here could not leave this firmware but through the official one) */
static int flash_part_ok(uint32_t id) { return id == 0x856014u || id == 0xB36014u; }

static void persist_boot(void)                    /* before settings_init / panel_init */
{
#if FELUCCA_FLASH
    persist_t p;
    uint32_t f = irq_save();
    flash_ok = (uint8_t)flash_part_ok(FL_FAR(fl_jedec_ram)());   /* a known 1 MiB part, else stay RAM-only */
    irq_restore(f);
    if (!flash_ok)
        return;
    fl_plain_window_init();                        /* flash above 0x93000 reads as plaintext through XIP
                                                    * (user sample sets are played from there) */
#if FELUCCA_SLICE
    slc_store_boot();                              /* (the scans read each slot's stored slices) */
#endif
    {
        uint32_t k;
        for (k = 0; k < SMP_USER_SLOTS; k++)
            smp_user_scan(k);
    }
    {
        int n = st_load(OBJ_SETTINGS, &p, sizeof p);
        if (settings_import(&p, n))
            persist_saved = p;
    }
#if PROJ_SONGS
    sg_boot();                                     /* 1.2: the song slots (the projects of 1.1 moved in, once) */
#endif
    {   /* projects: fill empty RAM slots from flash, so the slot list is right after power-on */
        uint32_t i;
        for (i = 0; i < NPROJ; i++)
            if (!proj_import(&proj_scratch, pslot(i), sizeof(project_store_t)))
                proj_fetch(i);
    }
    up_boot();                                     /* user presets */
#endif
}

static int project_used(uint32_t slot) { return proj_import(&proj_scratch, pslot(slot), sizeof(project_store_t)); }
static uint32_t project_pats(uint32_t slot)
{
#if PROJ_SONGS
    if (!flash_ok)
        return 0;
    sg_on();
    return sg.used[slot % SG_NSLOT];
#else
    (void)slot;
    return 0;
#endif
}

/* Main loop only: no flash access or copies when the ISR changes rows. */
static uint32_t chain_prepare(void)
{
    uint32_t i, k, j, used = 0;
    if (transport_busy())
        return 2;
    if (!chain_valid(&chain_config) || !chain_config.count)
        return 1;
#if RONDO_LAYOUT
    (void)j;
    for (i = 0; i < chain_config.count; i++)            /* RONDO: the bank's patterns the lanes play */
        for (k = 0; k < NTRK; k++) {
            uint32_t s = chain_pat(chain_config.row[i].slot[k]);
            if (s < NBANK && !bank_has(s))
                return 3u + s;
        }
    (void)used;
    fm1_irq_off();
    bank_keep();                                        /* (the current one as it is now) */
    bank.queue = CHAIN_OFF;
    fm1_irq_on();
    chain.config = chain_config;
    RING_PUBLISH();
    chain.armed = 1;
    transport_req = 1;
    return 0;
#else
    for (i = 0; i < chain_config.count; i++)            /* the slots the lanes play (not the silent ones) */
        for (k = 0; k < NTRK; k++) {
            uint32_t s = chain_config.row[i].slot[k];
            if (s >= 4u || ((used >> s) & 1u))
                continue;
            if (!project_used(s))
                return 3u + s;
            used |= 1u << s;
        }
    chain.config = chain_config;
    for (i = 0; i < 4u; i++)
        if ((used >> i) & 1u) {
            project_t *p = &proj_scratch;
            if (!proj_import(p, &proj_slot[i], sizeof(project_store_t))) return 3u + i;
            chain.source[i].motion = p->motion;
            chain.source[i].fxp = p->fxp;
            /* A song keeps its current instruments. Engine-specific motion from
             * another instrument would change a kit/wave/algorithm unexpectedly. */
            {
                motion_store_t *m = &chain.source[i].motion; uint32_t n = 0;
                for (uint32_t e = 0; e < m->count; e++) {
                    const motion_event_t *v = &m->event[e]; uint32_t owner = v->place >> 6;
                    if (MOTION_ID(v) >= P_FM1_ATK && !cmd_id(MOTION_ID(v)) && p->t[owner].engine != trk[owner].eng_req) continue;
                    m->event[n++] = *v;
                }
                m->count = (uint8_t)n;
            }
            for (k = 0; k < NTRK; k++) {
                memcpy(chain.source[i].step[k], p->t[k].step, sizeof p->t[k].step);
                proj_steps(chain.source[i].step[k]);
                chain.source[i].qnt[k] = p->t[k].p[P_SQNT] != 0;
                for (j = 0; j < 4u; j++)
                    chain.source[i].timing[k][j] = (int16_t)clamp(p->t[k].p[P_SLEN + j],
                        TP[P_SLEN + j].min, TP[P_SLEN + j].max);
            }
        }
    RING_PUBLISH();
    chain.armed = 1;
    transport_req = 1;
    return 0;
#endif
}

static void settings_poll(void)
{
#if FELUCCA_FLASH
    persist_t p;
#if FELUCCA_SLICE
    slc_store_poll();                              /* SLICE's slices edited on the SLICES page */
#endif
    if (!persist_pending || !flash_ok || transport_busy() ||
        (persist_pending == 2u && (uint32_t)(fm1_ms - persist_retry_ms) < 1000u))
        return;
    p = persist_saved;
    settings_export(&p);
    if (!memcmp(&p, &persist_saved, sizeof p)) {
        persist_pending = 0;
        return;                                    /* unchanged: no erase cycle */
    }
    if (st_save(OBJ_SETTINGS, &p, sizeof p) == 0) {
        persist_saved = p;
        persist_pending = 0;
    } else {
        persist_pending = 2;
        persist_retry_ms = fm1_ms;
    }
#endif
}

static void settings_save(void)
{
#if FELUCCA_FLASH
    persist_pending = 1;
#endif
    settings_poll();
}

/* The autosave (1.2, Discussion #130): the music as it is (what a project holds: every track's sound, steps, LEN DIV
 * SWING GATE, mix, the song, the automation and locks, the FM6 patches, the name) kept in its own storage object
 * (OBJ_AUTOSAVE, A/B at 0xE5000 / 0xE6000, never one of the four project slots), and put back at power-on
 * (autosave_boot) with MENU > SYSTEM > RESTORE LAST ON (the default). Flash wear: a 4 KiB erase per write, the two
 * sectors taking turns, so a write only when
 *   - something a project holds changed since the last one (autosave_sig: a hash of those values, no copy; not the
 *     selected track, nor the PROJECT page's own controls),
 *   - and it has stayed the same for AS_IDLE_MS (3 s; 10 s before 1.0.1) with the transport stopped, no key or button down, no voice
 *     sounding and no editor transfer (a backup or a sample: autosave_hold); an erase stops the audio for its
 *     ~40 ms, as every save does, so it never happens while anything plays,
 *   - and at least AS_GAP_MS (20 s; 60 s before 1.0.1) after the one before (AS_RETRY_MS after a failed write).
 * Dungeon Tracker: a session built while playing was lost when the FM-1 went off before 10 s of quiet (nothing
 * said so). Now: 3 s after STOP, a mark in the header (as.pend: an asterisk after the pattern letter) while the music
 * is not in the autosave yet, and the autosave is SAVE > PROJECT's QUICK slot (above A-D): LOAD brings it back, SAVE
 * writes it at once (quick_save, as any save: stopped). Wear: at most 3 writes a minute while stopping and starting,
 * 200,000 over the pair: over 1,000 hours of that, far more with real pauses.
 * So at most one write a minute while editing, none while playing or idle: about 100,000 erase cycles per sector
 * give over 200,000 writes, a write a minute for 8 hours a day for over a year, far more with real pauses.
 * RESTORE LAST OFF writes none (and restores none). Never after a crash, a hang or a failed boot (main.c): the music
 * that was playing then could be what crashed. Checked every AS_POLL_MS; the main loop only (never the audio ISR) */
static uint8_t bank_boot_cur;                  /* RONDO: the autosave's PATTERN BANK pattern + 1 (autosave_boot; 0 none) */
#define AS_POLL_MS 250u
#define AS_IDLE_MS 3000u
#define AS_GAP_MS 20000u
#define BANK_GAP_MS 60000u               /* (the bank's current pattern copy: as before 1.0.1) */
#define AS_RETRY_MS 30000u
static struct {
    uint32_t saved, seen;                        /* the signature written last (or loaded at power-on), seen last */
    uint32_t t, last, poll;                      /* fm1_ms: seen changed / busy, the last write, the last check */
    uint32_t writes;                             /* (written since power-on: the host tests count them) */
    uint8_t wrote, err;                          /* a write since power-on (last is valid); the last one failed */
    uint8_t pend;                                /* 1.0.1: the music is not the autosave's (the header's mark) */
    uint8_t have;                                /* 1.0.1: an autosave is in the flash (the QUICK slot used) */
} as __attribute__((section(".pool")));

static uint32_t as_mix(uint32_t h, const void *p, uint32_t n)   /* FNV-1a, continued */
{
    const uint8_t *b = p;
    while (n--) {
        h ^= *b++;
        h *= 16777619u;
    }
    return h;
}

/* what an autosave would hold, hashed (no copy, no RAM): project_capture's values */
static uint32_t autosave_sig(void)
{
    uint32_t h = 2166136261u, i, j;
    for (i = 0; i < NTRK; i++) {
        const track_t *t = &trk[i];
        for (j = 0; j < P_COUNT; j++) {
#if RONDO_LAYOUT
            int16_t v = morph_base(t, j);
#else
            int16_t v = motion_base_value(t, j);
#endif
            h = as_mix(h, &v, sizeof v);
        }
        h = as_mix(h, &t->eng_req, 1);
        h = as_mix(h, &t->preset, 1);
        h = as_mix(h, t->step, sizeof t->step);
        h = as_mix(h, fm6_patch[i], sizeof fm6_patch[i]);
    }
    for (i = 0; i < G_COUNT; i++)
        if (i != G_SLOT && i != G_NAME && i != G_LOAD && i != G_SAVE)
            h = as_mix(h, &song.g[i], sizeof song.g[i]);
    h = as_mix(h, song.mch, sizeof song.mch);           /* (1.5.5: MIDI IN TRACK's channels, the project's) */
    h = as_mix(h, &motion, sizeof motion);
    h = as_mix(h, &fxp, sizeof fxp);
    h = as_mix(h, &chain_config, sizeof chain_config);
#if RONDO_LAYOUT
    h = as_mix(h, (const void *)&bank.cur, 1);
    h = as_mix(h, mo.amt, sizeof mo.amt);
    h = as_mix(h, mo.tgt, sizeof mo.tgt);
#endif
    return as_mix(h, proj_name, sizeof proj_name);
}

/* nothing going on that an erase (the audio stopped ~40 ms) would be heard in, or would race */
static int autosave_quiet(void)
{
    uint32_t k, i;
    if (transport_busy() || fm1_in.notes || fm1_in.buttons)
        return 0;
    for (k = 0; k < NTRK; k++)
        for (i = 0; i < NVOICE; i++)
            if (trk[k].v[i].active)
                return 0;
    return 1;
}

static void autosave_hold(void) { as.t = fm1_ms; }   /* the editor's transfers: their staging RAM is proj_wire */

/* main loop, every pass (after settings_poll) */
static void autosave_poll(void)
{
#if FELUCCA_FLASH
    uint32_t sig;
    if (!flash_ok || fm1_ms - as.poll < AS_POLL_MS)
        return;
    as.poll = fm1_ms;
    sig = autosave_sig();
    as.pend = (uint8_t)(sig != as.saved && !(ui_prefs & PREF_RESTORE_OFF));
    if (sig != as.seen || !autosave_quiet()) {
        as.seen = sig;
        as.t = fm1_ms;
        return;
    }
    if (sig == as.saved || (ui_prefs & PREF_RESTORE_OFF) || fm1_ms - as.t < AS_IDLE_MS ||
        (as.wrote && fm1_ms - as.last < (as.err ? AS_RETRY_MS : AS_GAP_MS)))
        return;
    as.last = fm1_ms;
    as.wrote = 1;
    project_capture(&proj_scratch);
#if RONDO_LAYOUT
    proj_scratch.rsv = (uint8_t)(bank.cur + 1u);         /* (RONDO: which pattern of the bank the music is) */
    proj_scratch.sum = proj_sum(&proj_scratch);
#endif
    proj_wire_gen++;
    as.err = !proj_pack(&proj_wire, &proj_scratch) || st_save(OBJ_AUTOSAVE, &proj_wire, sizeof proj_wire) != 0;
    if (!as.err) {
        as.saved = sig;
        as.writes++;
        as.pend = 0;
        as.have = 1;
    }
#endif
}

/* 1.0.1: SAVE > PROJECT's QUICK slot. SAVE: the music into the autosave now (stopped, as any save; any RESTORE LAST):
 * 0 saved, 1 refused (playing), 2 failed */
static int quick_save(void)
{
    if (transport_busy()) {
        ui_message("STOP TO SAVE");
        return 1;
    }
#if FELUCCA_FLASH
    if (flash_ok) {
        uint32_t sig = autosave_sig();
        project_capture(&proj_scratch);
#if RONDO_LAYOUT
        proj_scratch.rsv = (uint8_t)(bank.cur + 1u);
        proj_scratch.sum = proj_sum(&proj_scratch);
#endif
        proj_wire_gen++;
        if (!proj_pack(&proj_wire, &proj_scratch) || st_save(OBJ_AUTOSAVE, &proj_wire, sizeof proj_wire) != 0) {
            ui_message("SAVE ERROR");
            return 2;
        }
        as.saved = as.seen = sig;
        as.last = fm1_ms;
        as.wrote = 1;
        as.err = 0;
        as.writes++;
        as.pend = 0;
        as.have = 1;
        ui_message("QUICK SAVED");
        return 0;
    }
#endif
    ui_message("NO FLASH HERE");
    return 2;
}
/* .. LOAD: the autosave becomes the music (as a project load: SAVE held undoes it) */
static void quick_load(void)
{
#if FELUCCA_FLASH
    int n = -1;
    if (flash_ok) {
        proj_wire_gen++;
        n = st_load(OBJ_AUTOSAVE, &proj_wire, sizeof proj_wire);
    }
    if (n <= 0 || !proj_import(&proj_scratch, &proj_wire, n)) {
        ui_message("EMPTY SLOT");
        return;
    }
    proj_bound(&proj_scratch);
    if (!project_restore_runtime(&proj_scratch)) {
        as.saved = as.seen = autosave_sig();
        as.pend = 0;
    }
#else
    ui_message("EMPTY SLOT");
#endif
}
static int quick_used(void) { return as.have; }
static int autosave_pending(void) { return as.pend; }

/* power-on, after the defaults (main.c): the autosave becomes the music when `allowed` (no crash, hang or failed boot
 * before) and RESTORE LAST is ON; an autosave that does not load (none, torn, damaged, another format) is ignored.
 * The music now (restored or not) is what is saved: nothing is written until it changes. 1 = restored */
static int autosave_boot(int allowed)
{
    int rc = 0;
#if FELUCCA_FLASH
    if (flash_ok) {
        int n;
        proj_wire_gen++;
        n = st_load(OBJ_AUTOSAVE, &proj_wire, sizeof proj_wire);
        as.have = (uint8_t)(n > 0 && proj_import(&proj_scratch, &proj_wire, n));   /* (1.0.1: the QUICK slot) */
        if (as.have && allowed && !(ui_prefs & PREF_RESTORE_OFF)) {
            proj_bound(&proj_scratch);
#if RONDO_LAYOUT
            bank_boot_cur = proj_scratch.rsv;           /* (RONDO: the bank's current pattern then) */
#endif
            if (!project_restore_runtime(&proj_scratch)) {
                ui_message("RESTORED");
                rc = 1;
            }
        }
    }
#else
    (void)allowed;
#endif
    as.saved = as.seen = autosave_sig();
    as.t = fm1_ms;
    as.pend = 0;
    return rc;
}

#if RONDO_LAYOUT
/* RONDO: the PATTERN BANK in the flash (song_bank.c; storage.c OBJ_BANK0 + p): a pattern as {magic, its letter, 1,
 * the pattern}; cleared: the 8 bytes with 0 for 1. Written by bank_poll, one a pass, only when an erase would not be
 * heard (autosave_quiet: stopped, no key, no voice) for BANK_IDLE_MS; the current pattern's own changes stored into
 * its copy then too, at most once every BANK_GAP_MS (its latest is in the autosave meanwhile, with which one it is) */
#define BANK_MAGIC 0x4B4E4252u                 /* "RBNK" */
#define BANK_IDLE_MS 3000u
typedef struct { uint32_t magic; uint8_t pat, ver; uint16_t len; uint8_t enc[PROJ_STORE_SIZE - 8u]; } bank_store_t;
/* 0.10: ver 3, the pattern (chain_pattern_t as it is) run-length encoded (rle_enc), len its encoded length; every
 * version before is refused (five FEEDs: it no longer fits a storage object as it was) */
#define BANK_VER 3u
#if FELUCCA_FLASH
_Static_assert(sizeof(bank_store_t) <= sizeof(project_store_t) && sizeof(bank_store_t) <= ST_PAYLOAD_MAX,
               "a bank pattern fits one storage object");
#endif
static struct { uint32_t poll, t, cap, retry; uint8_t err, seen; uint32_t writes; } bk __attribute__((section(".pool")));
/* a pattern read back: inside its fields and ranges (as a project's steps are, proj_bound) */
static void bank_bound(chain_pattern_t *c)
{
    uint32_t k, i, j;
    for (k = 0; k < NTRK; k++) {
        proj_steps(c->step[k]);
        for (i = 0; i < NSTEP; i++) {
            step_t *s = &c->step[k][i];
            s->flags &= 3u | SF_RATCH | SF_NUDGE;
            s->vel &= 127u;
            if (s->probability >= COND_FIRST + NCOND) s->probability = 0;
        }
        for (j = 0; j < 4u; j++)
            c->timing[k][j] = (int16_t)clamp(c->timing[k][j], TP[P_SLEN + j].min, TP[P_SLEN + j].max);
        c->qnt[k] = c->qnt[k] != 0;
    }
    if (!motion_valid(&c->motion))
        memset(&c->motion, 0, sizeof c->motion);
    fxp_bound(&c->fxp);
}
/* power-on, after autosave_boot: the six from the flash; the current one the autosave's (its music is that one's
 * latest), else the first stored one put on the tracks, else A (the music as it is) */
static uint8_t bank_flash_used;                /* the patterns the flash held at power-on (bank_boot; boot_song: none) */
static void bank_boot(void)
{
    uint32_t p;
    bank_reset();
    bank.used = 0;
#if FELUCCA_FLASH
    for (p = 0; flash_ok && p < NBANK; p++) {
        const bank_store_t *b = (const bank_store_t *)(const void *)&proj_wire;
        int n;
        proj_wire_gen++;
        n = st_load(OBJ_BANK0 + p, &proj_wire, sizeof proj_wire);
        if (n < 8 || b->magic != BANK_MAGIC || b->pat != p || b->ver != BANK_VER || b->len > n - 8 ||
            rle_dec(b->enc, b->len, (uint8_t *)&bank_pat[p], sizeof bank_pat[p]) != sizeof bank_pat[p]) {
            memset(&bank_pat[p], 0, sizeof bank_pat[p]);
            continue;
        }
        bank_bound(&bank_pat[p]);
        bank.used |= (uint8_t)(1u << p);
    }
#endif
    bank_flash_used = bank.used;
    if (bank_boot_cur >= 1u && bank_boot_cur <= NBANK) {
        bank.cur = (uint8_t)(bank_boot_cur - 1u);
    } else {
        for (p = 0; p < NBANK && !bank_has(p); p++)
            ;
        bank.cur = (uint8_t)(p < NBANK ? p : 0u);
        if (p < NBANK) {
            fm1_irq_off();
            bank_give(p);
            fm1_irq_on();
        }
    }
    bank.used |= (uint8_t)(1u << bank.cur);
    bank.dirty = 0;
    bk.seen = bank.gen;
    bk.t = fm1_ms;
}
static void bank_dropped(void);                /* ui.c: a switch: the undo copy of the steps it took over goes */
/* main loop, every pass: a switch seen (the UI told), a pattern queued when the transport stopped (at once), the
 * flash written when nothing would hear it */
static void bank_poll(void)
{
    uint32_t p;
    if (bk.seen != bank.gen) {
        bk.seen = bank.gen;
        bank_dropped();
    }
    arr_drain();                                        /* (the arrangement recorded: SONG's sections) */
    if (arr.on)
        bank.clk = (uint8_t)bank_clock();
    if (bank_pc) {                                      /* (MIDI: a Program Change on channel 16, as ENV + its key) */
        uint32_t p = bank_pc - 1u;
        bank_pc = 0;
        if (bank_has(p) && !chain_busy())
            bank_key(p);
    }
    if (bank.queue < NBANK) {
        if (!song.playing && !transport_busy()) {
            fm1_irq_off();
            bank_switch(bank.queue);
            fm1_irq_on();
        } else
            bank.clk = (uint8_t)bank_clock();
    }
#if FELUCCA_FLASH
    if (!flash_ok || fm1_ms - bk.poll < AS_POLL_MS)
        return;
    bk.poll = fm1_ms;
    if (!autosave_quiet() || fm1_ms - bk.t < BANK_IDLE_MS || (bk.err && fm1_ms - bk.retry < AS_RETRY_MS)) {
        if (!autosave_quiet())
            bk.t = fm1_ms;
        return;
    }
    if (!bank_same(bank.cur) && (!bk.cap || fm1_ms - bk.cap >= BANK_GAP_MS)) {
        bk.cap = fm1_ms ? fm1_ms : 1u;
        fm1_irq_off();
        bank_keep();
        fm1_irq_on();
    }
    for (p = 0; p < NBANK && !((bank.dirty >> p) & 1u); p++)
        ;
    if (p == NBANK)
        return;
    {
        bank_store_t *b = (bank_store_t *)(void *)&proj_wire;
        uint32_t len = 8u;
        proj_wire_gen++;
        memset(b, 0, sizeof *b);
        b->magic = BANK_MAGIC;
        b->pat = (uint8_t)p;
        b->ver = (uint8_t)(bank_has(p) ? BANK_VER : 0u);
        if (bank_has(p)) {
            uint32_t e = rle_enc((const uint8_t *)&bank_pat[p], sizeof bank_pat[p], b->enc, sizeof b->enc);
            if (!e) {                                    /* (too busy to store: kept in RAM, said once) */
                bank.dirty &= (uint8_t)~(1u << p);
                bk.err = 1;
                bk.retry = fm1_ms;
                return;
            }
            b->len = (uint16_t)e;
            len = 8u + e;
        }
        if (st_save(OBJ_BANK0 + p, b, len) == 0) {
            bank.dirty &= (uint8_t)~(1u << p);
            bk.writes++;
            bk.err = 0;
        } else {
            bk.err = 1;                                   /* (a failed write: again AS_RETRY_MS later) */
            bk.retry = fm1_ms;
        }
    }
#endif
}
#endif

#if FELUCCA_FLASH
_Static_assert(sizeof(project_store_t) <= ST_PAYLOAD_MAX, "project does not fit one flash sector");
_Static_assert(sizeof(persist_t) <= ST_PAYLOAD_MAX, "settings do not fit one flash sector");
#endif
#endif /* PROJ_HOST */
