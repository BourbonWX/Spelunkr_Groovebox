/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Dungeon Tracker contributors. Built on Felucca (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* MENU > SYSTEM > RANDOM SONG (from Dungeon Tracker 1.0; 0.10 rewritten for the five FEEDs), a song to start from. From
 * one seed: a style, a key, a mode (minor, dorian, major) and two chords of a progression, then for each FEED's fixed
 * role (core.h trk_cls) a sound and a part. Spelunkr (Nov 26 2026): seven genres (the four below and VAPORWAVE (another genre's song, VAPOR on),
 * CITY POP 96..120, FUTURE FUNK 110..128; the web page's GENRE picks one: rs_genre), chords with their own qualities
 * (PROGS: 7ths, 9ths, borrowed chords, slash bass) voiced to move as little as they can, written grooves, bass lines with
 * approach notes and chord rhythms beside the generated ones; patterns A..F INTRO, A SECTION, B SECTION, BRIDGE, OUTRO,
 * BUILD. Nov 22: the style is a grammar, not only a tempo and kits:
 *   CHIPTUNE 116..150, straight: 16th hats, rolls, four on the floor or a driving generated kick; octave-jumping bass;
 *            chords as a fast arpeggio; a busy lead an octave up; SID / GENESIS sounds
 *   CHILLWAVE 70..90, a little swing: mostly half-time (the snare on 3), off-beat or soft hats, open hats; long or
 *            pushed 7th / 9th chords; a bass on the kicks or held (808); sax, glass, vibes; often slowed and warped
 *   ELECTRO / 808 96..124: syncopated kicks, clap backbeat, hat rolls (ratchets); 808 bass held to the next kick and
 *            gliding; stabs or off-beat chords; a short riff lead; ANALOG / GLORP / MORPH / FM sounds
 *   LO-FI 72..92, swung hard: boom-bap kicks, ghost snares; walking or kick-following bass; pushed 7ths and 9ths on
 *            piano and EPs; sax, flute, mallets; often dusty (VINYL)
 * The kicks are drawn from the style's weights per step, PERC's two lanes are euclidean rhythms; within a style the
 * hats, the open hat, the voicing, the chord rhythm, the bass's job and the lead's rhythms are rolled too. Before it:
 *   DRUMS a beat on one of the style's kits (kick, backbeat snare, hats 8ths or 16ths, an open hat or a tom), the
 *         hats alone on the off-beats panned left and right, each its own (the per-step pan: PAN locks);
 *   PERC  two sounds from two other kits of the style (a clap, rim, clave or conga; a shaker, hat or cowbell): a
 *         syncopation and a shaker line ping-ponging (PAN locks), a fill swept across in the break;
 *   CHORDS the chords, held or stabbed, with a B side always: bells or keys an octave up, the two panned apart;
 *   BASS  a bass line on the roots from a few grooves; half the time a B side, a sub an octave down or a pluck up;
 *   LEAD  a phrase on the scale, chord tones on the beats; sometimes a B side, another lead an octave up, panned.
 * random_song (the menu's row) does it as NEW + DEFAULT SOUNDS would (the arrangement, the other patterns and the name
 * cleared), so SAVE held undoes it. Nothing here runs in the audio ISR; the sounds are switched as any load */

#ifndef BOOT_SONG_C
#define BOOT_SONG_C 1
static uint32_t bs_rng;
static uint32_t bs_rand(void)
{
    uint32_t s = bs_rng ? bs_rng : 0x2545F491u;
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return bs_rng = s;
}
static uint32_t bs_in(uint32_t n) { return n ? bs_rand() % n : 0u; }   /* 0..n-1 */
static int bs_chance(uint32_t pct) { return bs_in(100) < pct; }
static int32_t bs_abs(int32_t v) { return v < 0 ? -v : v; }   /* (no abs() in the firmware's libc) */

static void bs_note(step_t *s, const uint8_t *n, uint32_t k, uint32_t vel, uint32_t acc)
{
    uint32_t i;
    step_clear(s);
    s->time = ST_NOTE;
    s->n = (uint8_t)k;
    for (i = 0; i < k && i < 4u; i++)
        s->note[i] = n[i];
    s->vel = (uint8_t)vel;
    if (acc)
        s->flags |= SF_ACCENT;
}
static void bs_tie(step_t *s) { step_clear(s); s->time = ST_TIE; }

/* the scale's degree d (any, wrapping by octaves) above the tonic t */
static int32_t bs_deg(const int8_t *sc, int32_t t, int32_t d)
{
    int32_t o = d >= 0 ? d / 7 : -((-d + 6) / 7);
    return t + 12 * o + sc[d - 7 * o];
}


/* Phantom Cosmonaut's default "transmission" (spec 1): what the FEEDs carry at power-on, under FEED 1's beat (TRK_DEF:
 * the kit's BEAT): two bars in D minor at HBT 120, LEN 32 each.
 *   FEED 2 GRAIN PAD  the carrier: Dm held a bar, Bb, then C (the third voice leading down)
 *   FEED 3 FOLD BASS  the pulse: D on the offbeats of bar 1, an octave jump on 7; Bb and C under bar 2
 *   FEED 4 CRYSTAL BELL  telemetry: a few high blips, A C D, F E D, far apart
 *   PERC (0.10)  GLITCH HOP's SHAKER on the off-beats, panned left and right in turn; its CLAP closing bar 2
 * PHANTOM is free over it. NEW clears it as ever (a blank song); RANDOM SONG rolls another. The music's, not the user's
 * (pat_sig: autosave_boot's, main.c) until it is edited */
static void transmission(void)
{
    static const uint8_t PAD[3][3] = {{50, 53, 57}, {46, 50, 53}, {48, 52, 55}};   /* Dm, Bb, C */
    static const uint8_t PAD_AT[3] = {0, 16, 24}, PAD_LEN[3] = {16, 8, 8};
    static const uint8_t BASS[][3] = {{0, 38, 1}, {3, 38, 0}, {6, 50, 1}, {8, 38, 0}, {10, 38, 0}, {12, 41, 0}, {14, 45, 0},
                                      {16, 34, 1}, {19, 34, 0}, {22, 46, 1}, {24, 36, 1}, {27, 36, 0}, {30, 48, 0}};
    static const uint8_t BELL[][2] = {{4, 69}, {10, 72}, {14, 74}, {20, 77}, {22, 76}, {28, 74}};
    uint32_t i, k;
    for (k = 1; k < NTRK; k++) {
        track_defaults_steps(&trk[k]);
        trk[k].p[P_SLEN] = 32;
    }
    for (i = 0; i < 3u; i++) {
        bs_note(&trk[1].step[PAD_AT[i]], PAD[i], 3, 84, 0);
        for (k = 1; k < PAD_LEN[i]; k++)
            bs_tie(&trk[1].step[PAD_AT[i] + k]);
    }
    for (i = 0; i < NELEM(BASS); i++)
        bs_note(&trk[2].step[BASS[i][0]], &BASS[i][1], 1, BASS[i][2] ? 112 : 92, BASS[i][2]);
    for (i = 0; i < NELEM(BELL); i++)
        bs_note(&trk[3].step[BELL[i][0]], &BELL[i][1], 1, 88, 0);
    for (i = 2; i < 32u; i += 4u) {                   /* 0.10: PERC's SHAKER on the off-beats, left and right in turn (each
                                                       * hit keeps its pan: PAN locks); its CLAP once, closing bar 2 */
        step_t *st = &trk[PERC_TRK].step[i];
        step_clear(st);
        st->time = ST_NOTE;
        st->hit = 2u;
        motion_set_lock(&trk[PERC_TRK], i, P_PAN, (int16_t)(i & 4u ? 40 : -40));
    }
    trk[PERC_TRK].step[28].time = ST_NOTE;
    trk[PERC_TRK].step[28].hit = 1u;
    motion_set_enabled(&trk[PERC_TRK], 1);
    for (k = 1; k < NTRK; k++)
        pat_sig[k] = steps_sig(&trk[k]);              /* (a default pattern, not the user's) */
}

/* (tests, the screens: a rolled song as the power-on state, nothing to write until it is edited: the autosave and the
 * PATTERN BANK take it as theirs) */
static void boot_song_settle(void)
{
#if RONDO_LAYOUT
    fm1_irq_off();
    (void)bank_keep();                                    /* (pattern A's copy: the song) */
    fm1_irq_on();
    bank.dirty = 0;
#endif
    as.saved = as.seen = autosave_sig();
    as.t = fm1_ms;
}
/* a random factory sound of category cat, any engine (CAT_DRUM: a kit, GEN KIT's) */
static void rs_sound(track_t *t, uint32_t cat)
{
    uint32_t e, k, n = 0, pick, kit = cat == CAT_DRUM ? ENGI_CKIT : 0u;
    for (pick = 0; pick < 2u; pick++) {                   /* (count them, then take the one picked) */
        uint32_t want = pick ? bs_in(n) : 0u, i = 0;
        for (e = 0; e < NENGINES; e++) {
            const engine_t *en = ENGINES[e];
            if (!en->npresets || e == ENGI_SLICE || !eng_ok(e) || (kit && e != kit))   /* (Spelunkr: not the hidden DEBRIS, VULT; a kit: MOD KIT's 3 in 10, the LOFI
                                                                         * and the DUNGEON among them; else GEN KIT's) */
                continue;
            for (k = 0; k < en->npresets; k++) {
                if (preset_orig(en, k) != k || preset_cat(e, k) != cat)
                    continue;
                if (pick && i == want) {
                    set_engine_of(t, e);
                    apply_preset_to(t, k);
                    return;
                }
                i++;
            }
        }
        n = i;
    }
}
/* drum hits: lanes (1 kick, 2 snare, 4 clap, 8 closed hat, 16 open hat, 32 tom, 64 rim, 128 bell) as FMKIT's lanes or
 * as a SAMPLE kit's General MIDI notes */
static void rs_hit(track_t *t, step_t *s, uint32_t hit, uint32_t acc)
{
    static const uint8_t GM[8] = {36, 38, 39, 42, 46, 45, 37, 70};
    uint32_t l, n = 0;
    if (!hit)
        return;
    step_clear(s);
    s->time = ST_NOTE;
    if (t->eng_req % NENGINES != ENGI_SAMPLE) {
        s->hit = (uint8_t)hit;
        s->acc = (uint8_t)(acc ? hit & 3u : 0u);
        return;
    }
    for (l = 0; l < 8u && n < 4u; l++)
        if ((hit >> l) & 1u)
            s->note[n++] = GM[l];
    s->n = (uint8_t)n;
    s->vel = (uint8_t)(acc ? 110 : 92);
}
/* the six sections, PATTERN BANK A..F (Ed, Nov 26 2026: "Pattern 1 = Intro / Pattern 2 = A Section / Pattern 3 = B
 * Section / .. Bridge / .. Outro", and a build): A INTRO, B the A SECTION (the verse), C the B SECTION (the chorus),
 * D the BRIDGE (its own chords, the drums lighter, a fill out), E the OUTRO (the verse thinning out, home on the
 * tonic), F the BUILD (the drums rising into the chorus). The sections 2 or 4 bars (the chords every half bar or every
 * bar); the arrangement from a few templates with tracks left out of some passes (the lead coming in late, the drums
 * dropping out) */
enum { RS_INTRO, RS_VERSE, RS_CHORUS, RS_BRIDGE, RS_OUTRO, RS_BUILD, RS_N };
/* the genres (Ed, Nov 22 2026: "they all sound like the same pattern just with different instruments"; Nov 26: the
 * RANDOM SONG pop-up's GENRE): each its own grammar (the drums' feel, the bass's job, the chords' voicing and rhythm,
 * the lead's). The progressions, the written grooves, bass lines and chord rhythms after JAMTRACK FM-1's (Philip Bos,
 * MIT, Copyright (c) 2026 Philip Bos: app/dsp/gen.h, styles.h; LICENSES/MIT-JAMTRACK.txt, CREDITS.md) */
enum { ST_CHIP, ST_CHILL, ST_ELECTRO, ST_LOFI, ST_VAPOR, ST_CITY, ST_FUNK, ST_N };
#define RS_ANY 0xFFu
static uint8_t rs_genre = RS_ANY;                         /* the next roll's genre (ST_*), RS_ANY: any (felucca_web.c) */
#ifndef ENGI_ANALOG
#define ENGI_ANALOG 0u                                    /* (ANALOG: engine 0, engines.c ENG_TO_ANALOG) */
#endif
enum { BM_GROOVE, BM_KICK, BM_OCTAVE, BM_WALK, BM_808, BM_PAT = 16 };   /* the bass's job (BM_PAT + k: BPAT[k]) */
enum { CR_HELD, CR_STAB, CR_OFF, CR_ARP, CR_PUSH, CR_PAT = 16 };        /* the chords' rhythm (CR_PAT + k: CPAT[k]) */
/* the chord qualities: up to four notes over the root (semitones); the 9ths rootless (the bass has the root) */
enum { Q_DIA, Q_MAJ, Q_MIN, Q_7, Q_MAJ7, Q_M7, Q_MAJ9, Q_M9, Q_9, Q_13, Q_SUS4, Q_7SUS4, Q_SUS2, Q_M6, Q_6, Q_M7B5,
       Q_ADD9, Q_M11, Q_DIM, Q_N };
static const struct { const char *s; uint8_t n; int8_t iv[4]; } QUAL[Q_N] = {
    {"", 0, {0}}, {"", 3, {0, 4, 7}}, {"m", 3, {0, 3, 7}}, {"7", 4, {0, 4, 7, 10}}, {"maj7", 4, {0, 4, 7, 11}},
    {"m7", 4, {0, 3, 7, 10}}, {"maj9", 4, {4, 7, 11, 14}}, {"m9", 4, {3, 7, 10, 14}}, {"9", 4, {4, 7, 10, 14}},
    {"13", 4, {4, 10, 14, 21}}, {"sus4", 3, {0, 5, 7}}, {"7sus4", 4, {0, 5, 7, 10}}, {"sus2", 3, {0, 2, 7}},
    {"m6", 4, {0, 3, 7, 9}}, {"6", 4, {0, 4, 7, 9}}, {"m7b5", 4, {0, 3, 6, 10}}, {"add9", 4, {0, 4, 7, 14}},
    {"m11", 4, {3, 7, 10, 17}}, {"dim", 3, {0, 3, 6}},
};
typedef struct { int8_t deg, acc; uint8_t q, inv; } rs_ch_t;   /* a scale degree (0..6), a flat / sharp, Q_*, the bass:
                                                                 * 0 the root, 1 the third, 2 the fifth */
enum { RM_MIN, RM_DOR, RM_MAJ, RM_MIX, RM_LYD, RM_N };
static const int8_t MODE[RM_N][7] = {{0, 2, 3, 5, 7, 8, 10}, {0, 2, 3, 5, 7, 9, 10}, {0, 2, 4, 5, 7, 9, 11},
                                     {0, 2, 4, 5, 7, 9, 10}, {0, 2, 4, 6, 7, 9, 11}};
static const uint8_t MODE_SCL[RM_N] = {2, 3, 1, 4, 9};    /* (params.c N_SCALE: MIN DOR MAJ MIX LYD) */
typedef struct { uint8_t r[16]; int8_t c[16]; } rs_motif_t;  /* a bar of lead: 1 a note, 2 held, 0 rest; its degrees */
static struct {
    const int8_t *sc;
    int32_t tonic;
    rs_ch_t prog[3][4];                                   /* the verse's four chords, the chorus's, the bridge's */
    uint8_t kicks, perc, extra, groove[2], rhythm[2], stab[NTRK], sixteen, rich;
    uint8_t sh, ppan, hpan;                               /* 0.10: PERC 2's density, its pan; the hats' pan */
    uint8_t genre, sty, mode, half,   /* (genre: the one asked for; sty: the one whose song it is) */
            snare, hat, oh, crhy, bmode, lead_up, appr;
    uint8_t grv;                                          /* a written groove (GROOVES), 0xFF: the beats generated */
    uint16_t kick[2][2];                                  /* [verse, chorus][odd bar, even bar]: bit i a kick on step i */
    uint16_t ghost, roll[2], p1[2], p2;                   /* ghost snares; hat rolls [verse, chorus]; PERC's lanes */
    uint8_t gl[2][2][16], gv[2][2][16], gp[2][2];         /* a written groove resolved: lanes, levels; PERC 2 from it */
    uint16_t gpm[2][2];                                   /* (its P row: PERC 2's hits) */
    uint8_t hr, len;                                      /* steps a chord (8 / 16), a section (4 chords) */
    rs_motif_t mot[2];                                    /* the hooks: [verse, chorus] */
} rsg;

/* ---------------------------------------------------------------- the genres' material */
/* the progressions: a mode (M major, m minor, d dorian, x mixolydian, l lydian), then four chords as numbers of the
 * mode's scale (b / # a semitone off), a quality, and /3 or /5 for the bass on the chord's third or fifth; two chords
 * play twice. A plain number is a major triad; "~" a chord of the scale (a triad, or a 7th when the genre is rich) */
static const char *const PROGS[ST_N][16] = {
    /* CHIPTUNE: triads, minor first */
    {"m:1m 6 3 7", "m:1m 7 6 7", "M:1 5 6m 4", "M:6m 4 1 5", "m:1m 4m 6 7", "M:1 4 5 4", "m:1m 6 4m 5", "M:4 5 3m 6m",
     "x:1 7 4 1", "m:1m 3 7 6", 0},
    /* CHILLWAVE: long 7ths and 9ths, the minor iv, lydian */
    {"M:1maj7 4maj7", "M:1maj7 3m7 4maj7 4m6", "m:1m9 6maj7 3maj7 7", "d:1m7 47", "M:4maj7 3m7 2m7 1maj7",
     "M:1maj7 1m7 4maj7 4m7", "l:1maj7 2", "M:6m9 4maj7 1maj7 5sus4", "m:1m9 7 6maj7 5m7", "M:1maj9 4maj9", 0},
    /* ELECTRO / 808: minor, dark */
    {"m:1m 6 3 7", "m:1m 7 6 7", "m:1m7 4m7", "d:1m7 47", "m:1m 6 7 7", "M:6m 4 1 5", "m:1m 3 7 6",
     "m:1m9 6maj7 7 5m7", "m:1m 6 4m 5", 0},
    /* LO-FI: jazz 7ths, ii-V */
    {"M:2m7 57 1maj7 6m7", "M:1maj7 2m7 3m7 4maj7", "M:4maj7 3m7 6m7 2m7", "M:1maj7 6m7 2m9 513", "m:1m7 4m7 7 3maj7",
     "M:1maj7 b7 4maj7 4m6", "M:6m7 2m7 5sus4 1maj7", "M:1maj9 4maj9", "m:2m7b5 57 1m7 1m7", "M:4maj7 57 3m7 6m7", 0},
    /* VAPORWAVE: none of its own (Nov 26, Ed: "pull from the other genres and just apply the Vapor effect": rs_vapor) */
    {0},
    /* CITY POP: the IV-V-iii-vi and its cousins, secondary dominants */
    {"M:4maj7 57 3m7 6m7", "M:4maj7 3m7 2m7 1maj7", "M:2m7 57 3m7 6m7", "M:1maj7 6m7 2m7 57", "M:4maj7 57 3m7 67",
     "M:2m9 57 1maj9 1maj9", "M:4maj9 3m7 2m9 1maj9", "M:4maj7 57sus4 3m7 6m7", "M:1maj7 37 6m7 5m7",
     /* (Nov 26: six more: "Just the Two of Us" (IV III7 vi, the borrowed v), I VI7 ii V, vi ii V I, ii V iii VI7,
      * the backdoor bVII9, IV's borrowed iv) */
     "M:4maj7 37 6m7 5m7", "M:1maj7 67 2m7 57", "M:6m7 2m7 57 1maj7", "M:2m7 57 3m7 67", "M:4maj9 b79 1maj9 1maj9",
     "M:4maj7 4m7 3m7 6m7", 0},
    /* FUTURE FUNK: dorian vamps, disco 7ths */
    {"d:1m7 47", "M:1maj7 6m7 2m7 57", "m:1m7 4m7 7 3maj7", "M:2m7 3m7 4maj7 57", "d:1m7 2m7 3maj7 47",
     "M:4maj7 3m7 2m7 1maj7", "m:1m7 7 6maj7 57", "d:1m9 49", "M:4maj7 57 3m7 6m7",
     /* (Nov 26: five more: a ii-V-I-vi, the city-pop III7 into vi, VI VII i, a disco walk up, a dorian vamp with ii) */
     "M:2m7 57 1maj7 6m7", "M:4maj9 37 6m9 59", "m:6maj7 7 1m9 1m9", "M:1maj7 2m7 3m7 2m7", "d:1m9 2m7 1m9 47", 0},
};
/* the written grooves (JAMTRACK's, 4/4): rows K kick, S snare, R clap / rim, H closed hat, O open hat, P percussion
 * (PERC's second lane); X accent, x a hit, o soft, g a ghost, ? a hit some of the time (more in the chorus), , a ghost
 * some of the time. The first bar the verse's, the second the chorus's (busier) */
enum { GR_K, GR_S, GR_R, GR_H, GR_O, GR_P, GR_N };
typedef struct { const char *name; const char *v[GR_N], *c[GR_N]; } rs_groove_t;
#define G6(k, s, r, h, o, p) {k, s, r, h, o, p}
static const rs_groove_t GROOVES[] = {
    {"STRAIGHT 8", G6("x.....x.x.....?.", "....x.......x...", 0, "X.x.x.x.X.x.x.x.", 0, 0),
                   G6("x.....x.x.x...x.", "....x.......x.g.", 0, "X.x.x.x.X.x.x...", "..............x.", 0)},
    {"DREAMY", G6("x.........x.....", 0, "........x.......", "..o...o...o...o.", 0, "ogogogogogogogog"),
               G6("x.....?...x.....", 0, "........x.....,.", "o.o.o.o.o.o.o.o.", 0, "xgogxgogxgogxgog")},
    {"DISCO", G6("x...x...x...x...", "....x.......x...", 0, "xg.gxg.gxg.gxg.g", "..x...x...x...x.", "....o.......o..."),
              G6("x...x...x...x...", "....x.......x.g.", "....x.......x...", "xgxgxgxgxgxgxgxg", "..x...x...x...x.", "o.o.o.o.o.o.o.o.")},
    {"HALF TIME", G6("x......x..x.....", "........x.......", 0, "x.x.x.x.x.x.x.x.", 0, 0),
                  G6("x.....x...x..x..", "........x.....g.", 0, "x.x.x.x.x.x.x...", "..............x.", 0)},
    {"BOSSA", G6("o..oo..oo..oo..o", 0, "x..x..x...x..x..", "o.o.o.o.o.o.o.o.", 0, "gogogogogogogogo"),
              G6("o..oo..oo..oo..o", 0, "x..x...x..x.x...", "o.o.o.o.o.o.o.o.", 0, "gogogogogogogogo")},
    {"FUNK 16", G6("x..x..x...x..x..", "....x..g.g..x..g", 0, "Xgxgxgxgxgxgxgxg", 0, 0),
                G6("x..x..x...x?.x..", "....x..g.g..x.gg", 0, "Xgxgxgx.Xgxgxgx.", ".......x.......x", 0)},
    {"MOTORIK", G6("x.x.....x.x.....", "....x.......x...", 0, "xgxgxgxgxgxgxgxg", 0, 0),
                G6("x.x.....x.x.....", "....x.......x...", 0, "xgxgxgxgxgxgxgxg", "......x.......x.", "o.o.o.o.o.o.o.o.")},
    {"BOOM BAP", G6("x......x..x.....", "....x.......x...", 0, "x.x.x.x.x.x.x.x.", 0, 0),
                 G6("x......x..x..?..", "....x.......x..,", 0, "x.x.x.x.x.x.x.x.", "......x.........", 0)},
    {"LO-FI", G6("x.....x...x.....", "....o.......o...", 0, "o.o.o.o.o.o.o.o.", 0, "....g.......g..."),
              G6("x.....x...x..?..", "....o..,....o...", 0, "o.ogo.o.o.ogo.o.", 0, "....g.......g...")},
    {"MACHINE", G6("x.....x.x.......", 0, "....x.......x...", "xgxgxgxgxgxgxgxg", 0, "..x.......x....."),
                G6("x.....x.x.x.....", 0, "....x.......x...", "xgxgxgxgxgxgxgxg", "..............x.", "..x...x...x.....")},
    {"SHAKER", G6("x.......x.x.....", "....x.......x...", 0, 0, 0, "xgogxgogxgogxgog"),
               G6("x.......x.x...?.", "....x.......x..,", 0, "..o...o...o...o.", 0, "xgogxgogxgogxgox")},
    {"BREAKBEAT", G6("x.x.......x.....", "....x..g.g..x..g", 0, "x.x.x.x.x.x.x.x.", 0, 0),
                  G6("x.x.......xx....", "....x..g.g..x.g.", 0, "x.x.x.x.x.x.x...", "............o...", 0)},
    {"TRAP", G6("x......x..x.....", 0, "........x.......", "xgxgxgxgxgxgxgxg", 0, 0),
             G6("x......x..x...x.", 0, "........x.......", "xgxgxgxgxgxxxgxx", 0, 0)},
    {"NEW WAVE", G6("x.......x.x.....", "....x.......x...", 0, "XgxgXgxgXgxgXgxg", 0, 0),
                 G6("x.......x.x.x...", "....x.......x.x.", 0, "XgxgXgxgXgxgXgx.", "..............x.", 0)},
    {"AFROBEAT", G6("x.....x...x.....", "....x..g..x.,...", 0, "x.x.x.x.x.x.x.x.", 0, "x.x.xx.x.x.xx.x."),
                 G6("x.....x.x.x.....", "....x..g..x.,..g", 0, "x.x.x.x.x.x.x...", "......x.......x.", "x.x.xx.x.x.xx.x.")},
    {"MOTOWN", G6("x.....x.x.....x.", "o...X...o...X...", 0, "x.x.x.x.x.x.x.x.", 0, "....x.......x..."),
               G6("x.....x.x.x...x.", "o...X...o...X.g.", 0, "x.x.x.x.x.x.x.x.", 0, "..x.x...x.x.x...")},
};
enum { GV_STR8, GV_DREAMY, GV_DISCO, GV_HALF, GV_BOSSA, GV_FUNK, GV_MOTORIK, GV_BOOMBAP, GV_LOFI, GV_MACHINE, GV_SHAKER,
       GV_BREAK, GV_TRAP, GV_NEWWAVE, GV_AFRO, GV_MOTOWN };
/* the written bass lines: R the root (the chord's bass note), 5 its fifth, 8 the octave up, 3 the chord's third, 7 its
 * seventh, L the root an octave down, A an approach to the next chord, P a passing note towards it, d a dead note,
 * - hold, . rest (a bar; the chord under each step) */
static const char *const BPAT[] = {
    "R-----5-8---3-A-",   /* 0 MELODIC */
    "R-----R-----5-A-",   /* 1 TRESILLO */
    "R.dR..8dR..R.5A.",   /* 2 FUNK 16 */
    "R-.8.5R-.R.8..A.",   /* 3 MOTOWN */
    "R.8.R.8.5.8.A.P.",   /* 4 DISCO RUN */
    "R..R..R...5..8A.",   /* 5 SYNCOPATED */
    "R..R..R.d.R..8A.",   /* 6 LOCKED */
    "R-----5-5-----A-",   /* 7 BOSSA */
    "R------.R-R-----",   /* 8 HIP HOP */
    "R.3.5.8.5.3.R.A.",   /* 9 ARPEGGIO */
    "..R...R...R...R.",   /* 10 OFFBEAT */
    "R-----------.-5-",   /* 11 SPARSE */
    "R.8.R.8.R.8.R.8.",   /* 12 OCTAVES */
};
/* the written chord rhythms: X x a chord (accented), u a soft upstroke, s a short stab, # a gated hit, 1..5 one voice
 * (an arpeggio), - hold, . rest (a bar) */
static const char *const CPAT[] = {
    "x..x..x...x..x..",   /* 0 BOSSA */
    "x-----x-----x---",   /* 1 TRESILLO */
    "x-....x-........",   /* 2 CHARLESTON */
    "x-------..x-x---",   /* 3 PIANO POP */
    "..ss..s...ss..s.",   /* 4 SKANK */
    "X.uux.u.x.uux.u.",   /* 5 16TH STRUM */
    "#.##.##.#.##.##.",   /* 6 GATED */
    "##.#.##.##.#.##.",   /* 7 GATED 2 */
    "#.#.##.#.#.###.#",   /* 8 GATED 3 */
    ".s.ss.s..s.ss.s.",   /* 9 AFRO */
    "1234543212345432",   /* 10 RIPPLE */
    "4.3.2.1.2.3.4.3.",   /* 11 ARP DOWN */
    "1.3.2.4.1.3.2.4.",   /* 12 FINGERPICK */
    "s..s..s...s..s..",   /* 13 STABS */
};
#define CP(k) (CR_PAT + (k))
#define BP(k) (BM_PAT + (k))
/* each genre: a tempo (lo + 2 x 0..n-1), a swing (lo + 0..sw-1), DRUMS' kits and PERC's (CK_PRESETS' numbers), how
 * often half time, slow chords (a bar each), a written groove, the bass leaning into the next chord; its grooves,
 * hats, chord rhythms, bass jobs and lead rhythms to roll from; rich: the scale's chords as 7ths */
static const struct {
    uint8_t bpm, n, swing, sw, kit[4], pkit[4];
    uint8_t half, slow, written, appr, rich;
    uint8_t grooves[6], hat[4], crhy[6], bmode[6], rhy[4];
    uint8_t kn[2], pk[2], sk[2], w[16];
} STYLE[ST_N] = {
    /* (Spelunkr: the drums the sampled kits, CK_PRESETS 40.. CRATE BEDROOM VAPOR 808 LINN GATED NES GAMEBOY SNES) */
    {116, 18, 0, 1, {45, 46, 47, 16}, {32, 26, 13, 27}, 0, 30, 25, 20, 0,       /* CHIPTUNE 116..150 */
     {GV_STR8, GV_MOTORIK, GV_DISCO, GV_STR8, GV_MOTORIK, GV_DISCO}, {1, 1, 3, 0},
     {CR_ARP, CR_ARP, CR_STAB, CR_HELD, CP(10), CP(11)}, {BM_OCTAVE, BM_OCTAVE, BM_GROOVE, BM_KICK, BP(12), BP(4)}, {4, 5, 4, 2},
     {2, 3}, {3, 3}, {8, 9}, {0, 0, 2, 2, 0, 0, 3, 2, 4, 0, 4, 2, 0, 1, 3, 1}},
    {70, 11, 8, 18, {42, 41, 42, 17}, {31, 22, 5, 1}, 75, 75, 40, 30, 1,        /* CHILLWAVE 70..90 */
     {GV_HALF, GV_DREAMY, GV_SHAKER, GV_MACHINE, GV_HALF, GV_DREAMY}, {0, 2, 3, 0},
     {CR_HELD, CR_HELD, CR_PUSH, CP(6), CP(10), CP(3)}, {BM_KICK, BM_808, BM_OCTAVE, BM_GROOVE, BP(11), BP(0)}, {0, 1, 2, 3},
     {1, 2}, {2, 3}, {4, 5}, {0, 0, 1, 2, 0, 1, 2, 3, 0, 1, 4, 3, 1, 0, 2, 1}},
    {96, 15, 0, 14, {43, 44, 42, 8}, {26, 15, 34, 7}, 30, 50, 35, 15, 0,        /* ELECTRO / 808 96..124 */
     {GV_MACHINE, GV_TRAP, GV_BREAK, GV_NEWWAVE, GV_MACHINE, GV_TRAP}, {1, 3, 1, 0},
     {CR_STAB, CR_OFF, CR_OFF, CR_HELD, CP(6), CP(13)}, {BM_808, BM_808, BM_KICK, BM_GROOVE, BP(5), BP(8)}, {6, 7, 6, 3},
     {3, 3}, {3, 3}, {6, 7}, {0, 0, 2, 4, 0, 0, 4, 3, 0, 2, 5, 4, 0, 2, 3, 1}},
    {72, 11, 28, 30, {40, 41, 40, 6}, {38, 10, 29, 21}, 10, 60, 40, 55, 1,      /* LO-FI 72..92, swung */
     {GV_BOOMBAP, GV_LOFI, GV_BOSSA, GV_BOOMBAP, GV_LOFI, GV_BOOMBAP}, {0, 0, 3, 2},
     {CR_PUSH, CR_HELD, CR_PUSH, CR_STAB, CP(2), CP(3)}, {BM_KICK, BM_WALK, BM_WALK, BM_GROOVE, BP(8), BP(0)}, {0, 1, 2, 3},
     {2, 3}, {3, 2}, {5, 4}, {0, 0, 1, 3, 0, 1, 2, 4, 2, 3, 6, 3, 0, 1, 3, 2}},
    {62, 12, 4, 12, {42, 41, 43, 42}, {25, 17, 24, 18}, 70, 80, 65, 35, 1,      /* VAPORWAVE: (never its own song now:
                                                                                  * another genre's, VAPOR on; kept for the
                                                                                  * ids) */
     {GV_HALF, GV_DREAMY, GV_LOFI, GV_BOOMBAP, GV_BOSSA, GV_MACHINE}, {0, 2, 0, 3},
     {CR_HELD, CR_HELD, CP(6), CP(7), CP(3), CP(10)}, {BM_808, BM_KICK, BP(11), BP(0), BP(7), BP(8)}, {0, 1, 2, 3},
     {1, 2}, {2, 3}, {4, 5}, {0, 0, 1, 2, 0, 1, 2, 3, 0, 1, 4, 3, 1, 0, 2, 1}},
    {96, 13, 0, 8, {43, 44, 15, 40}, {15, 32, 31, 30}, 5, 40, 85, 65, 1,        /* CITY POP 96..120 */
     {GV_DISCO, GV_MOTOWN, GV_FUNK, GV_STR8, GV_BOSSA, GV_NEWWAVE}, {1, 0, 3, 1},
     {CP(13), CP(3), CP(5), CR_PUSH, CP(0), CP(9)}, {BP(3), BP(12), BP(5), BP(0), BP(6), BP(4)}, {6, 0, 7, 2},
     {2, 3}, {3, 3}, {6, 7}, {0, 0, 2, 2, 0, 0, 3, 2, 2, 0, 3, 2, 0, 1, 2, 1}},
    {110, 10, 0, 6, {15, 43, 44, 15}, {15, 32, 8, 26}, 0, 20, 85, 60, 1,        /* FUTURE FUNK 110..128 */
     {GV_DISCO, GV_DISCO, GV_FUNK, GV_MOTORIK, GV_BREAK, GV_AFRO}, {1, 1, 3, 0},
     {CP(6), CP(7), CP(8), CP(13), CP(5), CR_OFF}, {BP(4), BP(12), BP(2), BP(6), BP(3), BM_OCTAVE}, {4, 6, 7, 5},
     {3, 2}, {3, 3}, {8, 7}, {0, 0, 1, 1, 2, 0, 1, 2, 3, 0, 1, 2, 2, 0, 1, 2}},
};
const char *const RS_GENRE_NAME[ST_N] = {"CHIPTUNE", "CHILLWAVE", "ELECTRO", "LO-FI", "VAPORWAVE", "CITY POP", "FUTURE FUNK"};

/* ---------------------------------------------------------------- the chords */
static int32_t rs_root(const rs_ch_t *c) { return rsg.sc[c->deg % 7] + c->acc; }   /* semitones over the tonic */
/* the chord's notes as semitones over its root (n of them) */
static uint32_t rs_iv(const rs_ch_t *c, int8_t *iv)
{
    uint32_t k, n;
    if (c->q != Q_DIA) {
        n = QUAL[c->q].n;
        for (k = 0; k < n; k++)
            iv[k] = QUAL[c->q].iv[k];
        return n;
    }
    n = rsg.rich ? 4u : 3u;                               /* (the scale's own: stacked thirds) */
    for (k = 0; k < n; k++) {
        int32_t d = c->deg + 2 * (int32_t)k, s = rsg.sc[d % 7] + 12 * (d / 7);
        iv[k] = (int8_t)(s - rsg.sc[c->deg % 7]);
    }
    return n;
}
static int32_t rs_third(const rs_ch_t *c)                 /* the chord's third (or its sus), 7th: over its root */
{
    int8_t iv[4];
    uint32_t n = rs_iv(c, iv), k;
    for (k = 0; k < n; k++)
        if (iv[k] % 12 >= 2 && iv[k] % 12 <= 5) return iv[k] % 12;
    return 4;
}
static int32_t rs_fifth(const rs_ch_t *c)                 /* (its own: a diminished chord's, the scale's own) */
{
    int8_t iv[4];
    uint32_t n = rs_iv(c, iv), k;
    for (k = 0; k < n; k++)
        if (iv[k] % 12 >= 6 && iv[k] % 12 <= 8) return iv[k] % 12;
    return 7;
}
static int32_t rs_seventh(const rs_ch_t *c)
{
    int8_t iv[4];
    uint32_t n = rs_iv(c, iv), k;
    for (k = 0; k < n; k++)
        if (iv[k] % 12 >= 9) return iv[k] % 12;
    return 10;
}
static int32_t rs_bassnote(const rs_ch_t *c)              /* the chord's bass (a slash chord: its third or fifth) */
{
    return rs_root(c) + (c->inv == 1u ? rs_third(c) : c->inv == 2u ? rs_fifth(c) : 0);
}
/* a progression from its text (PROGS): its mode, four chords; 0 if it doesn't read */
static int rs_parse(const char *s, rs_ch_t *out, uint32_t *mode)
{
    static const char MC[RM_N] = {'m', 'd', 'M', 'x', 'l'};
    uint32_t n = 0, m, k;
    if (!s || !s[0] || s[1] != ':')
        return 0;
    for (m = 0; m < RM_N && MC[m] != s[0]; m++)
        ;
    if (m == RM_N)
        return 0;
    *mode = m;
    s += 2;
    while (*s && n < 4u) {
        rs_ch_t c = {0, 0, Q_MAJ, 0};
        uint32_t best = 0, bl = 0;
        while (*s == ' ') s++;
        if (!*s) break;
        if (*s == '~') { c.q = Q_DIA; s++; }
        if (*s == 'b') { c.acc = -1; s++; } else if (*s == '#') { c.acc = 1; s++; }
        if (*s < '1' || *s > '7') return 0;
        c.deg = (int8_t)(*s++ - '1');
        for (k = 1; k < Q_N && c.q != Q_DIA; k++) {        /* (the longest quality that matches) */
            uint32_t l = (uint32_t)strlen(QUAL[k].s);
            if (l > bl && !strncmp(s, QUAL[k].s, l)) { best = k; bl = l; }
        }
        if (bl) { c.q = (uint8_t)best; s += bl; }
        else if (c.q != Q_DIA && *s == 'm') { c.q = Q_MIN; s++; }
        if (*s == '/') { c.inv = (uint8_t)(s[1] == '3' ? 1 : s[1] == '5' ? 2 : 0); s += 2; }
        if (*s && *s != ' ') return 0;
        out[n++] = c;
    }
    if (!n) return 0;
    for (k = n; k < 4u; k++)                              /* (two chords: twice; three: the last held on) */
        out[k] = n == 2u ? out[k - 2u] : out[n - 1u];
    return 1;
}
static const rs_ch_t *rs_chd(uint32_t sec, uint32_t i)   /* the chord under step i of section sec */
{
    static rs_ch_t home;
    uint32_t n = (i % rsg.len) / rsg.hr % 4u;
    if (sec == RS_BRIDGE) return &rsg.prog[2][n];
    if (sec == RS_BUILD) return &rsg.prog[0][2u + n / 2u];      /* (the verse's last two, leaning into the chorus) */
    if (sec == RS_OUTRO && n == 3u) {                            /* (the outro: home on the tonic) */
        home = rsg.prog[0][0];
        home.deg = 0; home.acc = 0; home.inv = 0;
        if (home.q == Q_7 || home.q == Q_9 || home.q == Q_13 || home.q == Q_SUS4 || home.q == Q_7SUS4) home.q = Q_DIA;
        return &home;
    }
    return &rsg.prog[sec == RS_CHORUS][n];
}
static int32_t rs_chord(uint32_t sec, uint32_t i) { return rs_chd(sec, i)->deg; }   /* (its scale degree: the lead's) */
static int rs_in_scale(int32_t n)
{
    int32_t pc = ((n - rsg.tonic) % 12 + 12) % 12, k;
    for (k = 0; k < 7; k++)
        if (rsg.sc[k] == pc) return 1;
    return 0;
}
/* the lead's note over a chord: on the beat onto the chord's nearest tone; off it, a note a semitone from a tone of the
 * chord that isn't the scale's (a borrowed chord) moved onto it */
static int32_t rs_fit(int32_t n, const rs_ch_t *c, int onbeat)
{
    int8_t iv[4];
    uint32_t k, nv = rs_iv(c, iv);
    int32_t r = rsg.tonic + rs_root(c), best = n, bd = 99;
    for (k = 0; k < nv; k++) {
        int32_t pc = ((r + iv[k]) % 12 + 12) % 12, d = ((pc - n) % 12 + 12) % 12;
        int32_t dd = d > 6 ? d - 12 : d;                  /* (-5..6: up or down to it) */
        if (!onbeat && (rs_in_scale(n) || (dd != 1 && dd != -1)) && !(!rs_in_scale(n) && bs_abs(dd) <= 2))
            continue;
        if (bs_abs(dd) < bs_abs(bd) || (bs_abs(dd) == bs_abs(bd) && dd < bd)) { bd = dd; best = n + dd; }
    }
    if (!onbeat && rs_in_scale(n)) {                      /* (a scale note clashing with a borrowed tone: onto it) */
        for (k = 0; k < nv; k++) {
            int32_t t = r + iv[k], d = ((t - n) % 12 + 12) % 12;
            if (!rs_in_scale(t) && (d == 1 || d == 11)) return n + (d == 1 ? 1 : -1);
        }
        return n;
    }
    return bd == 99 ? n : best;
}
/* the voicing (after JAMTRACK: the chords move as little as they can): the chord's notes stacked from each of its
 * tones as the lowest, in 50..80, the one nearest the last; v (n notes) in, the new out */
static uint32_t rs_voice(const rs_ch_t *c, uint8_t *v, uint32_t pn)
{
    int8_t iv[4];
    uint32_t nv = rs_iv(c, iv), r, k, best = 0x7FFFFFFFu;
    int32_t root = rsg.tonic + rs_root(c), lo;
    uint8_t out[4], cand[4];
    for (r = 0; r < nv; r++)
        for (lo = 50; lo < 62; lo++) {
            int32_t p0 = root + iv[r], cost = 0;
            if (((p0 - lo) % 12 + 12) % 12) continue;     /* (the lowest note: tone r at lo) */
            cand[0] = (uint8_t)lo;
            for (k = 1; k < nv; k++) {
                int32_t p = root + iv[(r + k) % nv];
                while (p <= cand[k - 1u]) p += 12;
                while (p - 12 > cand[k - 1u]) p -= 12;
                cand[k] = (uint8_t)p;
            }
            if (cand[nv - 1u] > 84) continue;
            for (k = 0; k < nv; k++) {                     /* (each note to the last voicing's nearest, and back) */
                uint32_t j, m = 99;
                for (j = 0; j < pn; j++) m = (uint32_t)bs_abs((int32_t)cand[k] - v[j]) < m ? (uint32_t)bs_abs((int32_t)cand[k] - v[j]) : m;
                cost += (int32_t)m;
            }
            for (k = 0; k < pn; k++) {
                uint32_t j, m = 99;
                for (j = 0; j < nv; j++) m = (uint32_t)bs_abs((int32_t)cand[j] - v[k]) < m ? (uint32_t)bs_abs((int32_t)cand[j] - v[k]) : m;
                cost += (int32_t)m;
            }
            if ((uint32_t)cost < best) { best = (uint32_t)cost; memcpy(out, cand, nv); }
        }
    if (best == 0x7FFFFFFFu) {                            /* (nothing fit: root position, moved into range) */
        for (k = 0; k < nv; k++) {
            int32_t p = root + iv[k];
            while (p > 80) p -= 12;
            while (p < 50) p += 12;
            out[k] = (uint8_t)p;
        }
    }
    memcpy(v, out, nv);
    return nv;
}
/* scale and chord tones of the song (tests: every melodic note is one of them) */
static uint16_t rs_song_pcs(void)
{
    uint16_t m = 0;
    uint32_t p, c, k;
    for (k = 0; k < 7u; k++)
        m |= (uint16_t)(1u << ((rsg.tonic + rsg.sc[k]) % 12));
    for (p = 0; p < 3u; p++)
        for (c = 0; c < 4u; c++) {
            int8_t iv[4];
            uint32_t n = rs_iv(&rsg.prog[p][c], iv);
            const rs_ch_t *ch = &rsg.prog[p][c];
            for (k = 0; k < n; k++)
                m |= (uint16_t)(1u << (((rsg.tonic + rs_root(ch) + iv[k]) % 12 + 12) % 12));
            m |= (uint16_t)(1u << (((rsg.tonic + rs_bassnote(ch)) % 12 + 12) % 12));
        }
    {                                                     /* (the outro's home chord) */
        int8_t iv[4];
        const rs_ch_t *h = rs_chd(RS_OUTRO, 3u * rsg.hr);
        uint32_t n = rs_iv(h, iv);
        for (k = 0; k < n; k++)
            m |= (uint16_t)(1u << (((rsg.tonic + rs_root(h) + iv[k]) % 12 + 12) % 12));
    }
    return m;
}

/* ---------------------------------------------------------------- the lead's hooks */
/* item 4, a hook: a bar's rhythm (the style's) and a contour (small steps, a repeat, one leap; the chorus's tighter
 * and more repetitive), sung as is, then answered */
static void rs_motif_make(rs_motif_t *m, uint32_t row, uint32_t chorus)
{
    static const uint8_t LEAD_RHYTHM[8][16] = {           /* 1 a note, 2 held on (a tie); 0 rest */
        {1, 0, 0, 0, 1, 2, 0, 0, 1, 0, 0, 1, 1, 2, 2, 0},   /* 0..3: sparse, a singer's (lo-fi, chillwave) */
        {1, 2, 2, 0, 0, 0, 1, 0, 1, 2, 0, 0, 0, 0, 0, 0},
        {1, 0, 0, 1, 0, 0, 1, 2, 2, 0, 0, 0, 1, 2, 2, 2},
        {0, 0, 1, 0, 1, 0, 0, 0, 1, 2, 2, 0, 0, 0, 1, 0},
        {1, 1, 1, 0, 1, 1, 0, 1, 1, 2, 1, 1, 1, 1, 2, 0},   /* 4, 5: busy 16ths, a game's (chip) */
        {1, 0, 1, 1, 1, 0, 1, 0, 1, 1, 1, 1, 1, 2, 2, 2},
        {1, 0, 1, 0, 0, 0, 1, 0, 1, 0, 1, 0, 0, 0, 0, 0},   /* 6, 7: a short riff, repeated (electro, funk) */
        {1, 0, 0, 1, 0, 0, 1, 0, 0, 0, 1, 0, 1, 0, 0, 0},
    };
    int32_t d = 0, leap = 1;
    uint32_t i;
    memcpy(m->r, LEAD_RHYTHM[row % 8u], 16);
    if (bs_chance(35)) {                                  /* (now and then a note moved a 16th: its own rhythm) */
        uint32_t a = bs_in(15);
        if (m->r[a] == 1u && !m->r[a + 1u]) { m->r[a] = 0; m->r[a + 1u] = 1; }
    }
    for (i = 0; i < 16u; i++) {
        if (m->r[i] != 1u) { m->c[i] = (int8_t)d; continue; }
        if (i) {
            uint32_t r = bs_in(100);
            if (chorus && r < 30) ;                       /* (a repeated note: hooks repeat) */
            else if (leap && r > 82) { d += bs_chance(70) ? 3 + (int32_t)bs_in(2) : -3; leap = 0; }
            else d += r % 2u ? 1 : -1;
            d = clamp(d, -3, 6);
        }
        m->c[i] = (int8_t)d;
    }
}

/* ---------------------------------------------------------------- the drums */
/* k hits spread over 16 steps (a euclidean rhythm), turned by rot */
static uint16_t rs_euclid(uint32_t k, uint32_t rot)
{
    uint32_t i, m = 0;
    for (i = 0; i < 16u; i++)
        if ((i * k) % 16u < k)
            m |= 1u << ((i + rot) % 16u);
    return (uint16_t)m;
}
/* a bar's kicks: step 1 always, then n more where the style's weights put them (never on its snare) */
static uint16_t rs_kick_bar(uint32_t n)
{
    uint16_t m = 1u, snare = rsg.half ? (uint16_t)(1u << 8) : (uint16_t)(1u << 4 | 1u << 12);
    uint32_t tries;
    for (tries = 0; n && tries < 64u; tries++) {
        uint32_t i, tot = 0, r;
        for (i = 0; i < 16u; i++)
            if (!((m | snare) >> i & 1u)) tot += STYLE[rsg.sty].w[i];
        if (!tot) break;
        r = bs_in(tot);
        for (i = 0; i < 16u; i++) {
            uint32_t w = (m | snare) >> i & 1u ? 0u : STYLE[rsg.sty].w[i];
            if (r < w) { m |= (uint16_t)(1u << i); n--; break; }
            r -= w;
        }
    }
    return m;
}
/* a written groove resolved for [verse, chorus][odd, even bar]: its ? and , rolled once, so the bass on the kicks and the
 * drums agree */
static void rs_groove_make(void)
{
    static const uint8_t LANE[GR_N] = {1, 2, 4, 8, 16, 0};
    const rs_groove_t *g = &GROOVES[rsg.grv];
    uint32_t c, bar, r, i;
    for (c = 0; c < 2u; c++)
        for (bar = 0; bar < 2u; bar++) {
            uint16_t km = 0, pm = 0;
            memset(rsg.gl[c][bar], 0, 16);
            memset(rsg.gv[c][bar], 0, 16);
            for (r = 0; r < GR_N; r++) {
                const char *row = c ? g->c[r] : g->v[r];
                if (!row) continue;
                for (i = 0; i < 16u; i++) {
                    char ch = row[i];
                    uint32_t v = ch == 'X' ? 112u : ch == 'x' ? 96u : ch == 'o' ? 70u : ch == 'g' ? 42u : 0u;
                    if (ch == '?' && bs_chance(c ? 70 : 45)) v = 88u;
                    if (ch == ',' && bs_chance(40)) v = 40u;
                    if (!v) continue;
                    if (r == GR_P) { pm |= (uint16_t)(1u << i); continue; }
                    if (r == GR_K) km |= (uint16_t)(1u << i);
                    rsg.gl[c][bar][i] |= LANE[r];
                    if (v > rsg.gv[c][bar][i]) rsg.gv[c][bar][i] = (uint8_t)v;
                }
            }
            rsg.kick[c][bar] = km ? km : 1u;
            rsg.gpm[c][bar] = pm;
        }
}
static void rs_drums_make(void)
{
    uint32_t c, n;
    if (rsg.grv != 0xFFu) {
        rs_groove_make();
    } else {
        for (c = 0; c < 2u; c++) {                        /* [verse, chorus] */
            if ((rsg.sty == ST_CHIP && rsg.kicks == 0u) || (rsg.sty == ST_FUNK && rsg.kicks < 3u)) {   /* (four on the floor) */
                rsg.kick[c][0] = rsg.kick[c][1] = (uint16_t)(0x1111u | (c ? 1u << 14 : 0u));
                continue;
            }
            n = STYLE[rsg.sty].kn[0] + bs_in(STYLE[rsg.sty].kn[1]) + c;
            rsg.kick[c][0] = rs_kick_bar(n);
            rsg.kick[c][1] = rsg.kick[c][0];              /* bar 2: bar 1 with its second half moved */
            if (bs_chance(70)) {
                uint16_t late = (uint16_t)(rsg.kick[c][0] & 0xFF00u), mv = rs_kick_bar(1 + bs_in(2)) & 0xFF00u;
                rsg.kick[c][1] = (uint16_t)((rsg.kick[c][0] & 0x00FFu) | (bs_chance(50) ? mv : (uint16_t)(late | mv)));
            }
        }
    }
    rsg.ghost = 0;                                        /* ghost snares: lo-fi always, chillwave sometimes */
    if (rsg.grv == 0xFFu && (rsg.sty == ST_LOFI || ((rsg.sty == ST_CHILL || rsg.sty == ST_VAPOR) && bs_chance(40)))) {
        static const uint8_t GP[6] = {3, 7, 9, 10, 13, 15};
        for (n = 1 + bs_in(3); n; n--)
            rsg.ghost |= (uint16_t)(1u << GP[bs_in(6)]);
        rsg.ghost &= (uint16_t)~(rsg.kick[0][0] | rsg.kick[0][1]);
    }
    for (c = 0; c < 2u; c++) {                            /* hat rolls (a ratchet): electro and chip, the chorus more */
        uint32_t k = rsg.sty == ST_ELECTRO ? 1u + bs_in(2) + c : rsg.sty == ST_CHIP ? bs_in(2) + c : 0u;
        rsg.roll[c] = 0;
        for (; k; k--)
            rsg.roll[c] |= (uint16_t)(1u << (bs_chance(60) ? 13u + bs_in(3) : 5u + 2u * bs_in(4)));
    }
    {                                                     /* PERC: euclidean, its own density by the style */
        uint32_t k1 = STYLE[rsg.sty].pk[0] + bs_in(STYLE[rsg.sty].pk[1]);
        rsg.p1[0] = rs_euclid(k1, 2u + bs_in(5));
        rsg.p1[1] = rs_euclid(k1 + 1u, 1u + bs_in(5));
        rsg.p2 = rs_euclid(STYLE[rsg.sty].sk[0] + bs_in(STYLE[rsg.sty].sk[1]), bs_in(2));
    }
}
/* the next chord's bass after step i (its change: n steps on) */
static int32_t rs_next_bass(uint32_t sec, uint32_t i, uint32_t *n)
{
    uint32_t len = rsg.hr, at = i % len;
    *n = len - at;
    return rs_bassnote(rs_chd(sec, i + len - at));
}
static void rs_part(track_t *t, uint32_t c, uint32_t nth, uint32_t sec)
{
    static const uint8_t BASS_GROOVE[4][8] = {            /* 1 the root, 2 its octave, 3 the fifth; 0 rest (a chord's 8) */
        {1, 0, 0, 1, 0, 0, 1, 0}, {1, 0, 0, 0, 1, 0, 2, 0}, {1, 0, 0, 0, 0, 0, 3, 0}, {1, 0, 1, 0, 0, 2, 0, 0},
    };
    static const uint16_t PERC[4] = {0x4848, 0x2492, 0x0A0A, 0x8888};   /* (a second ROGUE's: bit i step i of 16) */
    const uint32_t L = rsg.len, last = L - 16u;           /* the section's steps (32 / 64), its last bar's first */
    uint32_t i;
    track_defaults_steps(t);
    t->p[P_SLEN] = (int16_t)L;
    switch (c) {
    case CLS_ROGUE:
        for (i = 0; i < L; i++) {
            uint32_t b = i % 16u, bar = i / 16u, hit = 0, vel = 0, ratch = 0, grv = rsg.grv != 0xFFu;
            uint32_t ch = sec == RS_CHORUS, kick = (rsg.kick[ch][bar & 1u] >> b) & 1u, snare = rsg.half ? b == 8u : b == 4u || b == 12u;
            if (nth) {                                    /* (a second ROGUE: percussion around the beat) */
                if ((sec == RS_VERSE || sec == RS_CHORUS) && ((PERC[rsg.perc] >> b) & 1u))
                    hit = b % 4u == 3u ? 32u : (i & 4u) ? 64u : rsg.extra;
                rs_hit(t, &t->step[i], hit, b % 4u == 0u);
                continue;
            }
            if (sec == RS_INTRO) {                        /* the kick from bar 2 (chillwave, vaporwave: none), the hats */
                if (kick && i >= 16u && rsg.sty != ST_CHILL && rsg.sty != ST_VAPOR) hit |= 1u;
                if (rsg.hat == 2u ? b % 4u == 2u : !(b & 1u)) hit |= 8u;
                if (i < 8u && rsg.sty != ST_CHIP) hit &= ~8u;
            } else if (sec == RS_BRIDGE) {                /* the bridge: lighter (the kick on 1 and 3's and, a soft
                                                           * backbeat, 8th hats), the last bar's end a fill (the style's) */
                if (b == 0u || (b == 10u && !rsg.half)) hit |= 1u;
                if (rsg.half ? b == 8u : b == 4u || b == 12u) { hit |= 2u; vel = 72u; }
                if (!(b & 1u)) { hit |= 8u; if (!vel) vel = b % 4u ? 52u : 66u; }
                if (i >= L - 8u) {
                    hit = 0; vel = 0;
                    if (rsg.sty == ST_ELECTRO && !(i & 1u)) { hit = 2u | 4u; vel = 60u + (i - (L - 8u)) * 8u; }
                    else if (rsg.sty == ST_CHIP && i >= L - 4u) { hit = 2u; ratch = i == L - 1u ? 4u : 2u; }
                    else if (i == L - 4u || i == L - 2u || i == L - 3u) { hit = 2u; vel = 70u + (i - (L - 4u)) * 12u; }
                    else if (i == L - 1u) hit = 32u;
                    else if (i == L - 8u) hit = 1u | 8u;
                }
            } else if (sec == RS_OUTRO) {                 /* the outro: the verse's beat, then the kick alone on the
                                                           * bars and soft quarter hats, a last hit */
                if (i == L - 16u) hit = 1u | 16u;
                else if (i > L - 16u) hit = 0;
                else if (i >= L / 2u) {
                    if (b == 0u) hit |= 1u;
                    if (!(b & 3u)) { hit |= 8u; vel = 58u; }
                } else {
                    if (kick) hit |= 1u;
                    if (snare) hit |= rsg.snare;
                    if (!(b & 1u)) hit |= 8u;
                }
            } else if (sec == RS_BUILD) {                 /* item 5: the kick on the beats, the snare in 8ths then
                                                           * 16ths, louder and louder, the last 16th left empty */
                uint32_t q = i * 4u / L;                  /* (the quarter of the section) */
                if (!(b & 3u) && q < 3u) hit |= 1u;
                if (q >= 1u && (q == 3u || !(b & 1u)) && (q >= 2u || !(b & 3u))) hit |= 2u;
                if (hit) vel = 56u + i * 70u / L;
                if (i == L - 1u) hit = 0;
                if (i == L - 2u && rsg.sty != ST_CHILL && rsg.sty != ST_VAPOR) hit |= 16u;
            } else if (grv) {                             /* a written groove */
                hit = rsg.gl[ch][bar & 1u][b];
                if (hit & 4u) hit = (hit & ~4u) | (rsg.snare & 6u ? rsg.snare & 6u : 4u);   /* (its R: the kit's clap) */
                vel = rsg.gv[ch][bar & 1u][b];
                if (vel >= 96u) vel = 0;                  /* (a plain hit: the accent below) */
                if ((rsg.roll[ch] >> b) & 1u && (bar & 1u) && (hit & 8u)) ratch = 2u + bs_in(3);
                if (i >= last && b >= 12u && (ch || L == 64u) && bs_chance(55)) {   /* (a fill into the next) */
                    hit = (hit & 1u) | (b == 15u ? 32u : 2u);
                    vel = 70u + (b - 12u) * 14u;
                }
                if (ch && i == L - 1u) hit |= 32u;
            } else {
                if (kick) hit |= 1u;
                if (snare) hit |= rsg.snare;
                else if ((rsg.ghost >> b) & 1u) { hit |= 2u; vel = 38u + bs_in(14); }
                switch (rsg.hat) {                        /* the hats: 8ths, 16ths, off-beats, 16ths with gaps */
                case 0: if (!(b & 1u)) hit |= 8u; break;
                case 1: hit |= 8u; if (b & 1u && !vel) vel = 70u; break;
                case 2: if (b % 4u == 2u) hit |= 8u; break;
                default: if (b % 4u != 3u) hit |= 8u; if (b & 1u && !vel) vel = 74u; break;
                }
                if (ch && rsg.sixteen && !(hit & 8u)) { hit |= 8u; if (!vel) vel = 64u; }
                if ((rsg.roll[ch] >> b) & 1u && (bar & 1u)) { hit |= 8u; ratch = 2u + bs_in(3); }
                if (rsg.oh && b == (rsg.oh == 1u ? 14u : 10u) && ((bar & 1u) || rsg.oh == 2u)) hit = (hit & ~8u) | 16u;
                if (i >= last && b >= 12u && (ch || L == 64u) && bs_chance(55)) {   /* (a fill into the next) */
                    hit = (hit & 1u) | (b == 15u ? 32u : 2u);
                    vel = 70u + (b - 12u) * 14u;
                }
                if (ch && i == L - 1u) hit |= 32u;        /* (a tom into the next) */
            }
            rs_hit(t, &t->step[i], hit, b % 4u == 0u && !vel);
            if (hit && vel && !(hit & 1u && b % 4u == 0u && sec != RS_BUILD)) t->step[i].vel = (uint8_t)vel;
            if (ratch) t->step[i].flags |= (uint8_t)((ratch - 1u) << SF_RATCH_SH);
            if (hit == 8u && (b & 2u) && sec != RS_BRIDGE && bar < 2u)   /* 0.10: a hat alone off the beat: panned, kept */
                motion_set_lock(t, i, P_PAN, (int16_t)(b & 4u ? (int32_t)rsg.hpan : -(int32_t)rsg.hpan));
        }
        break;
    case CLS_CLERIC: {
        uint8_t v[4] = {57, 60, 64, 67}, pn = 3;          /* (the voicing before: around middle C) */
        const rs_ch_t *cur = 0;
        uint32_t nv = 3;
        if (sec == RS_INTRO && nth)
            break;
        for (i = 0; i < L; i++) {
            uint32_t len = sec == RS_BUILD ? 2u * rsg.hr : rsg.hr;
            uint32_t push = rsg.crhy == CR_PUSH && (sec == RS_VERSE || sec == RS_CHORUS);
            uint32_t at = (i + push) % len, b = i % 16u;
            uint32_t rhy = sec == RS_BRIDGE || sec == RS_INTRO || sec == RS_BUILD || sec == RS_OUTRO ? CR_HELD : rsg.crhy;
            const rs_ch_t *cd = rs_chd(sec, i + push);
            uint8_t chd[4];
            uint32_t k, change = cd != cur;
            if (change) {                                 /* (a new chord: voiced near the last) */
                nv = rs_voice(cd, v, pn);
                pn = (uint8_t)nv;
                cur = cd;
            }
            for (k = 0; k < nv; k++)
                chd[k] = (uint8_t)(v[k] + (nth ? 12 : 0));
            if (sec == RS_OUTRO && i >= L - rsg.hr) rhy = CR_HELD;
            if (nth) {                                    /* the arpeggio: 8ths up the chord */
                if (sec != RS_BRIDGE && sec != RS_OUTRO && !(i & 1u))
                    bs_note(&t->step[i], &chd[(at / 2u) % nv], 1, at ? 84 : 96, 0);
                continue;
            }
            if (rhy >= CR_PAT) {                          /* a written rhythm: its bar, a new chord struck where it falls */
                char pc = CPAT[(rhy - CR_PAT) % NELEM(CPAT)][b];
                if (pc >= '1' && pc <= '5') {
                    uint32_t x = (uint32_t)(pc - '1');
                    uint8_t n1 = (uint8_t)(chd[x % nv] + (x >= nv ? 12 : 0));
                    bs_note(&t->step[i], &n1, 1, x ? 80 : 96, 0);
                } else if (pc == 'x' || pc == 'X' || pc == '#' || pc == 's') {
                    bs_note(&t->step[i], chd, nv, pc == 'X' ? 104 : pc == 's' ? 86 : pc == '#' ? 90 : 92, pc == 'X');
                } else if (pc == 'u') {
                    bs_note(&t->step[i], chd + (nv > 2u ? 1 : 0), nv > 2u ? nv - 1u : nv, 74, 0);
                } else if (pc == '-' && i) {
                    if (change || t->step[i - 1u].time == ST_REST) bs_note(&t->step[i], chd, nv, 88, 0);
                    else bs_tie(&t->step[i]);
                } else if (change && pc == '.' && (b & 3u) == 0u && rhy != CP(4) && rhy != CP(9)) {
                    bs_note(&t->step[i], chd, nv, 86, 0);   /* (a chord change on the beat: always heard) */
                }
                continue;
            }
            if (rhy == CR_ARP) {                          /* chip: the chord as a fast arpeggio, up and over */
                static const uint8_t UP[8] = {0, 1, 2, 3, 2, 1, 0, 1};
                uint8_t n1 = chd[UP[at % 8u] % nv];
                if (UP[at % 8u] >= nv) n1 = (uint8_t)(chd[0] + 12);
                bs_note(&t->step[i], &n1, 1, at % 4u ? 80 : 100, 0);
            } else if (rhy == CR_STAB ? (at % 8u == 0u || at % 8u == 3u || at % 8u == 6u) : rhy == CR_OFF ? (at % 4u == 2u) : at == 0u) {
                bs_note(&t->step[i], chd, nv, rhy == CR_OFF ? 84 : 92, 0);
            } else if ((rhy == CR_HELD || rhy == CR_PUSH) && (at < len - 1u || (sec == RS_OUTRO && i >= L - rsg.hr))) {
                if (i) bs_tie(&t->step[i]);
                else bs_note(&t->step[i], chd, nv, 92, 0);   /* (pushed: the first chord still from the top) */
            }
            if (sec == RS_BUILD && !(i % 4u))             /* item 5: the build swells (LEVEL, step by step) */
                motion_set_lock(t, i, P_LEVEL, (int16_t)(64 + i * 52u / L));
            if (sec == RS_OUTRO && !(i % 4u) && i >= L / 2u)   /* (the outro fades) */
                motion_set_lock(t, i, P_LEVEL, (int16_t)(100 - (i - L / 2u) * 50u / (L / 2u)));
        }
        break;
    }
    case CLS_FIGHTER:
        for (i = 0; i < L; i++) {
            uint32_t len = sec == RS_BUILD ? 2u * rsg.hr : rsg.hr, at = i % len, b = i % 16u, g, ch = sec == RS_CHORUS, nx;
            const rs_ch_t *cd = rs_chd(sec, i);
            int32_t root = rsg.tonic - 12 + rs_bassnote(cd), next;
            uint8_t n;
            while (root > 50) root -= 12;                 /* (the bass's register: E1..D3) */
            while (root < 28) root += 12;
            if (nth && root >= 36) root -= 12;
            next = rsg.tonic - 12 + rs_next_bass(sec, i, &nx);
            while (next > root + 6) next -= 12;           /* (the next root: the nearest) */
            while (next < root - 6) next += 12;
            if (sec == RS_BUILD) {                        /* the build: the root in 8ths, then 16ths */
                n = (uint8_t)root;
                if (!(b & 1u) || i >= L / 2u) bs_note(&t->step[i], &n, 1, 80 + i * 40u / L, 0);
                continue;
            }
            if (nth || sec == RS_BRIDGE || sec == RS_INTRO || sec == RS_OUTRO) {   /* the roots held (the intro: two beats) */
                n = (uint8_t)root;
                if (!at) bs_note(&t->step[i], &n, 1, 100, 1);
                else if (at < (sec == RS_INTRO && !nth ? 4u : len - 1u) || (sec == RS_OUTRO && i >= L - rsg.hr)) bs_tie(&t->step[i]);
                if (sec == RS_BRIDGE && !nth && at == len - 2u && next != root && rsg.appr) {   /* (into the next chord) */
                    n = (uint8_t)(next + (next > root ? -1 : 1));
                    bs_note(&t->step[i], &n, 1, 88, 0);
                }
                continue;
            }
            if (rsg.bmode >= BM_PAT) {                    /* a written line: its bar, the chord under each step */
                char pc = BPAT[(rsg.bmode - BM_PAT) % NELEM(BPAT)][b];
                int32_t x = root, vel = b % 4u == 0u ? 104 : 92;
                switch (pc) {
                case 'R': break;
                case '5': x = root + rs_fifth(cd); break;
                case '8': x = root + 12; break;
                case '3': x = root + rs_third(cd); break;
                case '7': x = root + rs_seventh(cd); break;
                case 'L': x = root - 12; if (x < 24) x += 12; break;
                case 'A': x = nx <= 4u && next != root ? next + (bs_chance(65) ? (next > root ? -1 : 1) : (next > root ? 2 : -2)) : root + 7; break;
                case 'P': x = root + (next >= root ? 2 : -2); break;
                case 'd': vel = 40; break;
                case '-':
                    if (i && t->step[i - 1u].time != ST_REST && at) bs_tie(&t->step[i]);
                    else if (!at) { n = (uint8_t)root; bs_note(&t->step[i], &n, 1, 100, 1); }
                    continue;
                default:
                    if (!at && i) { n = (uint8_t)root; bs_note(&t->step[i], &n, 1, 100, 1); }   /* (a change: heard) */
                    continue;
                }
                n = (uint8_t)clamp(x, 24, 64);
                bs_note(&t->step[i], &n, 1, (uint32_t)vel, b % 4u == 0u && pc != 'd');
                continue;
            }
            switch (rsg.bmode) {
            case BM_KICK:                                 /* on the kicks, short; the fifth now and then */
            case BM_808: {                                /* .. or held to the next one, gliding to a new root */
                uint32_t on = (rsg.kick[ch][(i / 16u) & 1u] >> b) & 1u || at == 0u;
                if (on) {
                    n = (uint8_t)(b > 8u && bs_chance(20) ? root + 7 : root);
                    bs_note(&t->step[i], &n, 1, 104, b % 4u == 0u);
                    if (rsg.bmode == BM_808 && i && t->step[i - 1u].time == ST_TIE && bs_chance(45))
                        t->step[i - 1u].flags |= SF_SLIDE;
                } else if (i && (rsg.bmode == BM_808 || (t->step[i - 1u].time == ST_NOTE && bs_chance(50))) &&
                           t->step[i - 1u].time != ST_REST) {
                    bs_tie(&t->step[i]);
                }
                if (rsg.bmode == BM_KICK && rsg.appr && at == len - 2u && next != root && t->step[i].time != ST_NOTE) {
                    n = (uint8_t)(next + (next > root ? -1 : 1));   /* (a 16th into the next chord) */
                    bs_note(&t->step[i], &n, 1, 86, 0);
                }
                break;
            }
            case BM_OCTAVE:                               /* 8ths jumping the octave (the chorus: 16ths) */
                if (ch || !(b & 1u)) {
                    n = (uint8_t)(root + ((i / (ch ? 1u : 2u)) & 1u ? 12 : 0));
                    bs_note(&t->step[i], &n, 1, (i / (ch ? 1u : 2u)) & 1u ? 86 : 104, 0);
                }
                break;
            case BM_WALK:                                 /* quarters: root, a chord tone, the fifth, a step into the next */
                if (!(b & 3u)) {
                    int32_t nn = at == 0u ? root : at + 4u >= len ? next + (next > root ? -1 : 1) :
                                 bs_chance(50) ? root + rs_fifth(cd) : root + rs_third(cd);
                    if (nn > 64) nn -= 12;
                    if (nn < 24) nn += 12;
                    n = (uint8_t)nn;
                    bs_note(&t->step[i], &n, 1, at == 0u ? 100 : 86, 0);
                    if (b + 1u < 16u) bs_tie(&t->step[i + 1u]), i++;
                }
                break;
            default:
                g = BASS_GROOVE[rsg.groove[ch]][at % 8u];
                n = (uint8_t)(g == 2u ? root + 12 : g == 3u ? root + rs_fifth(cd) : root);
                if (g) bs_note(&t->step[i], &n, 1, 100, at == 0u);
                if (rsg.appr && at == len - 2u && next != root) {   /* (into the next chord) */
                    n = (uint8_t)(next + (next > root ? -1 : 1));
                    bs_note(&t->step[i], &n, 1, 86, 0);
                }
                break;
            }
        }
        break;
    case CLS_PERC: {                                      /* PERC 1 (lane 1) a euclidean syncopation, PERC 2 (lane 2) a
                                                           * shaker line ping-ponging (or the groove's own); the bridge's end
                                                           * a PERC 1 fill swept across */
        int32_t side = -1;
        for (i = 0; i < L; i++) {
            uint32_t b = i % 16u, hit = 0, ch = sec == RS_CHORUS, bar = (i / 16u) & 1u;
            if (sec == RS_BRIDGE) {
                if (i >= L - 8u && !(i & 1u))
                    hit = 1u;
                else if (i < L - 8u && !(b & 3u) && b) hit = 2u;
            } else if (sec == RS_BUILD) {                 /* the shaker in 16ths over the build's second half */
                if (i >= L / 2u) hit = 2u;
            } else if (sec == RS_OUTRO) {
                if (i < L / 2u && ((rsg.p2 >> b) & 1u)) hit = 2u;
            } else {
                uint16_t p2 = rsg.grv != 0xFFu && rsg.gpm[ch][bar] ? rsg.gpm[ch][bar] : rsg.p2;
                if (sec != RS_INTRO && ((rsg.p1[ch] >> b) & 1u))
                    hit |= 1u;
                if (sec != RS_INTRO || i >= 16u) {
                    if ((p2 >> b) & 1u || (ch && rsg.sh && !(b & 1u)))
                        hit |= 2u;
                }
            }
            rs_hit(t, &t->step[i], hit, b % 4u == 2u);
            if (sec == RS_BUILD && hit) t->step[i].vel = (uint8_t)(50 + (i - L / 2u) * 140u / L);
            if (hit == 2u && (b & 1u) && sec != RS_BUILD && i < 32u) {   /* (the shaker's off 16ths: left, right) */
                t->step[i].vel = 72;
                motion_set_lock(t, i, P_PAN, (int16_t)(side * (int32_t)rsg.ppan));
                side = -side;
            } else if (hit == 1u && sec == RS_BRIDGE) {   /* (the fill: left to right) */
                motion_set_lock(t, i, P_PAN, (int16_t)(-48 + (int32_t)(i - (L - 8u)) * 16));
            }
        }
        break;
    }
    default: {                                            /* item 4, the lead: its hook (rs_motif_make) sung over each bar,
                                                           * moved to the bar's chord, and the phrase's last bar answering;
                                                           * the bridge: the verse's hook higher, half of each bar; the
                                                           * outro: the verse's, then gone */
        const rs_motif_t *m = &rsg.mot[sec == RS_CHORUS];
        uint32_t bars = L / 16u, bar;
        int32_t anchor = 7 + (sec == RS_CHORUS ? 2 : sec == RS_BRIDGE ? 3 : 0), hi = rsg.lead_up ? 12 : 0;
        if (sec == RS_INTRO || sec == RS_BUILD || (nth && sec != RS_CHORUS))
            break;
        for (bar = 0; bar < bars; bar++) {
            uint32_t answer = bar == bars - 1u, b;
            int32_t ch0 = rs_chord(sec, bar * 16u), j, best = anchor;
            if (sec == RS_OUTRO && bar >= (bars + 1u) / 2u)
                break;
            if (sec != RS_CHORUS || bar == 0u) {           /* (the chorus keeps its anchor: a hook repeats) */
                for (j = ch0 - 7; j <= ch0 + 14; j += 2)   /* (the chord tone nearest the last anchor) */
                    if (j >= 5 && j <= 12 && bs_abs(j - anchor) < bs_abs(best - anchor) + (best == anchor ? 0 : 1))
                        best = j;
                anchor = best;
            }
            for (b = 0; b < 16u; b++) {
                step_t *s = &t->step[bar * 16u + b];
                uint32_t r = m->r[b];
                int32_t d = anchor + m->c[b], chd = rs_chord(sec, bar * 16u + b), k;
                if (nth && b < 8u) r = 0;                 /* (a second lead: the bar's second half) */
                if (sec == RS_BRIDGE && b >= 8u && !answer) r = 0;
                if (answer && b >= 8u) {                  /* the answer: the bar's end comes home, held */
                    if (r == 1u && b >= 12u) { r = b == 12u ? 1u : 2u; }
                    if (b == 12u) { r = 1u; d = ch0 % 7 + 7 + (sec == RS_CHORUS ? 0 : 2); while (d > 14) d -= 7; }
                    if (b > 12u) r = 2u;
                }
                if (r == 1u && b % 4u == 0u) {            /* on the beat: onto a chord tone (the nearest) */
                    for (k = 0; k < 7; k++) {
                        int32_t rel = ((d - chd) % 7 + 7) % 7;
                        if (rel == 0 || rel == 2 || rel == 4) break;
                        d += k & 1 ? -(k + 1) : k + 1;
                    }
                }
                d = clamp(d, 4, 18);
                if (r == 1u) {
                    int32_t n = bs_deg(rsg.sc, rsg.tonic + 12 + hi, d);
                    uint8_t u;
                    n = rs_fit(n, rs_chd(sec, bar * 16u + b), b % 4u == 0u);   /* (a borrowed chord's tones) */
                    while (n > 98) n -= 12;               /* (chip's lead an octave up: kept under D7) */
                    while (n < 60) n += 12;
                    u = (uint8_t)n;
                    bs_note(s, &u, 1, b % 4u == 0u ? 104 : 88, 0);
                } else if (r == 2u && b && t->step[bar * 16u + b - 1u].time != ST_REST) {
                    bs_tie(s);
                }
            }
        }
        break;
    }
    }
}
/* a factory sound by its engine and name (the style's choice); 0: not offered here */
static int rs_named(track_t *t, uint32_t e, const char *name)
{
    const engine_t *en = e < NENGINES ? ENGINES[e] : 0;
    uint32_t k;
    if (!en || !en->npresets || !eng_ok(e))
        return 0;
    for (k = 0; k < en->npresets; k++)
        if (preset_orig(en, k) == k && str_eq(en->presets[k].name, name)) {
            set_engine_of(t, e);
            apply_preset_to(t, k);
            return 1;
        }
    return 0;
}
/* the style's sounds for a role: one of its list, else any factory sound of the category */
typedef struct { uint8_t e; const char *name; } rs_snd_t;
enum { RR_CHORDS, RR_BASS, RR_LEAD, RR_BELL, RR_N };
static void rs_style_sound(track_t *t, uint32_t role, uint32_t cat)
{
#define S_(e, n) {ENGI_##e, n}
    static const rs_snd_t CHIP[RR_N][10] = {
        {S_(SID, "SID ORGAN"), S_(SID, "SID KEYS"), S_(SID, "HARPSI"), S_(SID, "C64 ARP"), S_(GENESIS, "CLAV"),
         S_(GENESIS, "GEN PIANO"), S_(GENESIS, "ORGAN"), S_(GENESIS, "HARP"), S_(SAMPLE, "8BIT KEYS"), S_(SID, "SID PLUCK")},
        {S_(SID, "MONTY BASS"), S_(SID, "SID PULSE"), S_(SID, "SID TRI BASS"), S_(SID, "BUZZ BASS"), S_(SID, "DEEP PULSE"),
         S_(GENESIS, "GEN BASS"), S_(GENESIS, "SLAP BASS"), S_(GENESIS, "ROCK BASS"), S_(ANALOG, "SQR BASS"), S_(SID, "FUNK BASS")},
        {S_(SID, "ARCADE LEAD"), S_(SID, "TITLE SCREEN"), S_(SID, "PWM LEAD"), S_(SID, "SID SOLO"), S_(SID, "TRI FLUTE"),
         S_(GENESIS, "SQUARE LEAD"), S_(GENESIS, "HERO LEAD"), S_(GENESIS, "FLUTE"), S_(ANALOG, "SOLO SQUARE"), S_(SID, "SYNC LEAD")},
        {S_(SID, "RING BELL"), S_(SID, "C64 ARP"), S_(GENESIS, "MUSIC BOX"), S_(GENESIS, "MARIMBA"), S_(SAMPLE, "GLOCK"),
         S_(SID, "SID PLUCK"), S_(GENESIS, "HARP"), S_(SAMPLE, "8BIT KEYS"), S_(SID, "HARPSI"), S_(GENESIS, "BELL PAD")},
    };
    static const rs_snd_t CHILL[RR_N][10] = {
        {S_(SAMPLE, "GLASS PAD"), S_(SAMPLE, "VHS VIBES"), S_(SAMPLE, "MALL EP"), S_(SAMPLE, "WORN TAPE"), S_(ANALOG, "SOFT PAD"),
         S_(ANALOG, "PWM STR"), S_(ANALOG, "SWEEP PAD"), S_(PFM, "GLASS PAD"), S_(GENESIS, "WARM PAD"), S_(SID, "DREAM PAD")},
        {S_(ANALOG, "SUB BASS"), S_(ANALOG, "MOOG BASS"), S_(ANALOG, "PWM BASS"), S_(PFM, "FM BASS"), S_(GENESIS, "SUB THUMP"),
         S_(ANALOG, "SUB BASS"), S_(ANALOG, "SQR BASS"), S_(GLORP, "TOAD"), S_(SID, "DEEP PULSE"), S_(MORPH, "PULSAR")},
        {S_(SAMPLE, "SMOOTH SAX"), S_(SAMPLE, "NIGHT SAX"), S_(ANALOG, "WIND"), S_(PFM, "SATELLITE"), S_(MORPH, "CARRIER"),
         S_(SAMPLE, "VIBES"), S_(ANALOG, "SAW LEAD"), S_(GENESIS, "FLUTE"), S_(SID, "TRI FLUTE"), S_(SAMPLE, "SMOOTH SAX")},
        {S_(SAMPLE, "GLOCK"), S_(SAMPLE, "KALIMBA"), S_(SAMPLE, "MUSIC BOX"), S_(PFM, "CRYSTAL BELL"), S_(PFM, "CHIME ARRAY"),
         S_(SAMPLE, "VIBES"), S_(PFM, "ICE TINE"), S_(SAMPLE, "DREAM VIBES"), S_(GENESIS, "BELL PAD"), S_(SAMPLE, "MARIMBA")},
    };
    static const rs_snd_t ELECTRO[RR_N][10] = {
        {S_(ANALOG, "POLY KEYS"), S_(ANALOG, "PLUCK"), S_(ANALOG, "BLIP"), S_(ANALOG, "STRINGS"), S_(PFM, "DIGITAL EP"),
         S_(PFM, "ICE TINE"), S_(GLORP, "WAH CHORDS"), S_(GLORP, "BUBBLE KEYS"), S_(MORPH, "COLD START"), S_(ANALOG, "DARK PAD")},
        {S_(ANALOG, "SUB BASS"), S_(ANALOG, "REESE"), S_(ANALOG, "ACID"), S_(ANALOG, "ACID 2"), S_(GLORP, "FROG BASS"),
         S_(GLORP, "GLORP"), S_(MORPH, "PULSAR"), S_(MORPH, "DARK SIDE"), S_(PFM, "FM BASS"), S_(ANALOG, "MOOG BASS")},
        {S_(ANALOG, "SYNC LEAD"), S_(ANALOG, "RAVE"), S_(ANALOG, "SAW LEAD"), S_(ANALOG, "SUPERSAW"), S_(GLORP, "GLOOP LEAD"),
         S_(PFM, "SATELLITE"), S_(MORPH, "TELEMETRY"), S_(SID, "SYNC SCREAM"), S_(ANALOG, "SOLO SQUARE"), S_(GLORP, "SWAMP TALK")},
        {S_(PFM, "CHIME ARRAY"), S_(ANALOG, "BLIP"), S_(PFM, "PULSE CLOCK"), S_(PFM, "CRYSTAL BELL"), S_(MORPH, "SPUTNIK"),
         S_(ANALOG, "PLUCK"), S_(MORPH, "DEAD STAR"), S_(PFM, "ICE TINE"), S_(SID, "RING BELL"), S_(MORPH, "NUMBERS")},
    };
    static const rs_snd_t LOFI[RR_N][10] = {
        {S_(SAMPLE, "LOFI PIANO"), S_(SAMPLE, "MALL EP"), S_(SAMPLE, "UPRIGHT"), S_(SAMPLE, "WORN TAPE"), S_(SAMPLE, "FM PIANO"),
         S_(SAMPLE, "DREAM VIBES"), S_(PFM, "DIGITAL EP"), S_(GENESIS, "GEN PIANO"), S_(SAMPLE, "LOFI PIANO"), S_(GLORP, "WAH CHORDS")},
        {S_(ANALOG, "SUB BASS"), S_(ANALOG, "MOOG BASS"), S_(PFM, "FM BASS"), S_(GENESIS, "SUB THUMP"), S_(GENESIS, "SLAP BASS"),
         S_(SID, "FUNK BASS"), S_(GLORP, "TOAD"), S_(ANALOG, "PWM BASS"), S_(SID, "SID TRI BASS"), S_(ANALOG, "SUB BASS")},
        {S_(SAMPLE, "SMOOTH SAX"), S_(SAMPLE, "NIGHT SAX"), S_(SAMPLE, "KALIMBA"), S_(SAMPLE, "MUSIC BOX"), S_(SAMPLE, "MARIMBA"),
         S_(SAMPLE, "VIBES"), S_(GENESIS, "FLUTE"), S_(GENESIS, "HARMONICA"), S_(SAMPLE, "GLOCK"), S_(SID, "TRI FLUTE")},
        {S_(SAMPLE, "VIBES"), S_(SAMPLE, "GLOCK"), S_(SAMPLE, "KALIMBA"), S_(SAMPLE, "MUSIC BOX"), S_(SAMPLE, "MARIMBA"),
         S_(PFM, "CRYSTAL BELL"), S_(SAMPLE, "DREAM VIBES"), S_(GENESIS, "MUSIC BOX"), S_(PFM, "ICE TINE"), S_(SAMPLE, "VIBES")},
    };
    static const rs_snd_t CITY[RR_N][10] = {    /* (EPs, clav and poly keys; slap and funk basses; brass and sax) */
        {S_(SAMPLE, "MALL EP"), S_(PFM, "DIGITAL EP"), S_(SAMPLE, "FM PIANO"), S_(GENESIS, "CLAV"), S_(ANALOG, "POLY KEYS"),
         S_(GENESIS, "GEN PIANO"), S_(PFM, "ICE TINE"), S_(SAMPLE, "MALL EP"), S_(GLORP, "WAH CHORDS"), S_(ANALOG, "STRINGS")},
        {S_(GENESIS, "SLAP BASS"), S_(SID, "FUNK BASS"), S_(PFM, "FM BASS"), S_(GENESIS, "GEN BASS"), S_(ANALOG, "MOOG BASS"),
         S_(GENESIS, "SLAP BASS"), S_(ANALOG, "PWM BASS"), S_(ANALOG, "SQR BASS"), S_(SID, "SYNC BASS"), S_(PFM, "FM BASS")},
        {S_(ANALOG, "BRASS"), S_(GENESIS, "BRASS"), S_(SAMPLE, "SMOOTH SAX"), S_(SAMPLE, "NIGHT SAX"), S_(ANALOG, "SAW LEAD"),
         S_(PFM, "SATELLITE"), S_(ANALOG, "SOLO SQUARE"), S_(GENESIS, "FLUTE"), S_(ANALOG, "SYNC LEAD"), S_(GENESIS, "BRASS")},
        {S_(PFM, "ICE TINE"), S_(PFM, "CRYSTAL BELL"), S_(SAMPLE, "VIBES"), S_(SAMPLE, "MARIMBA"), S_(SAMPLE, "GLOCK"),
         S_(PFM, "CHIME ARRAY"), S_(ANALOG, "PLUCK"), S_(SAMPLE, "DREAM VIBES"), S_(SAMPLE, "KALIMBA"), S_(PFM, "ICE TINE")},
    };
    static const rs_snd_t FUNK[RR_N][10] = {    /* (bright keys and chops, slap, brass and saws) */
        {S_(ANALOG, "POLY KEYS"), S_(GLORP, "WAH CHORDS"), S_(GENESIS, "CLAV"), S_(PFM, "DIGITAL EP"), S_(ANALOG, "STRINGS"),
         S_(ANALOG, "PLUCK"), S_(SAMPLE, "MALL EP"), S_(GLORP, "BUBBLE KEYS"), S_(GENESIS, "STRINGS"), S_(SAMPLE, "FM PIANO")},
        {S_(GENESIS, "SLAP BASS"), S_(SID, "FUNK BASS"), S_(SID, "SYNC BASS"), S_(PFM, "FM BASS"), S_(ANALOG, "MOOG BASS"),
         S_(GENESIS, "GEN BASS"), S_(ANALOG, "SQR BASS"), S_(GENESIS, "SLAP BASS"), S_(ANALOG, "PWM BASS"), S_(SID, "FUNK BASS")},
        {S_(ANALOG, "BRASS"), S_(GENESIS, "BRASS"), S_(ANALOG, "SUPERSAW"), S_(ANALOG, "SYNC LEAD"), S_(ANALOG, "SAW LEAD"),
         S_(ANALOG, "RAVE"), S_(ANALOG, "SOLO SQUARE"), S_(GENESIS, "HERO LEAD"), S_(SAMPLE, "SMOOTH SAX"), S_(PFM, "SATELLITE")},
        {S_(ANALOG, "BLIP"), S_(ANALOG, "PLUCK"), S_(PFM, "ICE TINE"), S_(PFM, "CRYSTAL BELL"), S_(PFM, "CHIME ARRAY"),
         S_(SAMPLE, "GLOCK"), S_(SAMPLE, "VIBES"), S_(SAMPLE, "MARIMBA"), S_(PFM, "PULSE CLOCK"), S_(SID, "C64 ARP")},
    };
#undef S_
    static const rs_snd_t (*const BY[ST_N])[10] = {CHIP, CHILL, ELECTRO, LOFI, CITY, CITY, FUNK};
    uint32_t tries;
    for (tries = 0; tries < 4u; tries++) {
        const rs_snd_t *s = &BY[rsg.sty % ST_N][role % RR_N][bs_in(10)];
        if (rs_named(t, s->e, s->name))
            return;
    }
    rs_sound(t, cat);
}
/* a whole song from one seed (the genre rs_genre's, else any): THE PARTY and its sounds, PATTERN BANK A..F (intro, A
 * section, B section, bridge, outro, build) and SONG's arrangement; the tracks left on the A section (B). Called over
 * NEW + DEFAULT SOUNDS (random_song) */
static void random_song_make(uint32_t seed)
{
    /* the arrangements: {pattern, 4-bar blocks, the tracks left out (bit k: T1 drums, chords, bass, lead, PERC)} */
    enum { I_ = RS_INTRO, V_ = RS_VERSE, C_ = RS_CHORUS, D_ = RS_BRIDGE, O_ = RS_OUTRO, B_ = RS_BUILD };
    static const uint8_t ARRS[5][13][3] = {
        {{I_, 1, 0}, {V_, 1, 8}, {V_, 1, 0}, {B_, 1, 0}, {C_, 2, 0}, {V_, 1, 0}, {B_, 1, 0}, {C_, 2, 0}, {D_, 1, 0},
         {C_, 2, 0}, {O_, 1, 0}},
        {{I_, 1, 0}, {V_, 2, 0}, {C_, 2, 0}, {V_, 1, 0}, {B_, 1, 0}, {C_, 2, 0}, {D_, 2, 0}, {C_, 2, 0}, {O_, 1, 0}},
        {{I_, 2, 8}, {V_, 1, 17}, {V_, 1, 0}, {C_, 1, 0}, {C_, 1, 8}, {V_, 1, 0}, {B_, 1, 0}, {C_, 2, 0}, {D_, 1, 0},
         {B_, 1, 0}, {C_, 2, 0}, {O_, 1, 0}},
        {{I_, 1, 0}, {V_, 2, 0}, {B_, 1, 0}, {C_, 2, 0}, {D_, 1, 0}, {C_, 2, 0}, {O_, 2, 0}},
        {{I_, 1, 0}, {B_, 1, 0}, {C_, 2, 0}, {V_, 2, 0}, {B_, 1, 0}, {C_, 2, 0}, {D_, 1, 0}, {C_, 2, 0}, {O_, 1, 0}},
    };
    static const uint8_t P1[4] = {3, 2, 13, 14}, P2[4] = {15, 5, 12, 4};   /* PERC's: CLAP RIM CLAVE CONGA; SHAKER PD HAT
                                                                             * COWBELL CL HAT (eng_cycle.c PC_GM) */
    uint8_t cls[NTRK], seen[NCLS];
    uint32_t mode, k, sec, f, sty, np, a, b, c, m2, tries;
    bs_rng = seed | 1u;
    undo_depth++;                                         /* (random_song keeps one undo copy: NEW's) */
    for (k = 1; k <= B_N; k++) {                          /* (no B side from the song before) */
        b_set(&trk[k], 0);
        memset(&bsr[k - 1u], 0, sizeof bsr[k - 1u]);
    }
    sty = bs_in(ST_N);                                    /* (rolled either way: the rolls after it the same) */
    if (rs_genre < ST_N)
        sty = rs_genre;
    rsg.genre = (uint8_t)sty;
    if (sty == ST_VAPOR) {                                /* VAPORWAVE (Ed, Nov 26: "just resampled audio slowed down"):
                                                           * another genre's song, then VAPOR on it (below) */
        static const uint8_t SRC[5] = {ST_CITY, ST_CITY, ST_LOFI, ST_CHILL, ST_FUNK};
        sty = SRC[bs_in(5)];
    }
    rsg.sty = (uint8_t)sty;
    song.g[G_BPM] = (int16_t)(STYLE[sty].bpm + 2 * bs_in(STYLE[sty].n));
    song.g[G_SWING] = (int16_t)(STYLE[sty].swing + bs_in(STYLE[sty].sw));
    rsg.rich = STYLE[sty].rich;
    {                                                     /* the progressions: the A section's, the B section's (another of
                                                           * the same mode), the bridge's (a third, else the scale's IV V
                                                           * iii vi) */
        rs_ch_t tmp[4];
        for (np = 0; np < 16u && PROGS[sty][np]; np++)
            ;
        a = bs_in(np);
        if (!rs_parse(PROGS[sty][a], rsg.prog[0], &mode)) { mode = RM_MAJ; rs_parse("M:1 5 6m 4", rsg.prog[0], &mode); }
        b = a;
        for (tries = 0; tries < 16u; tries++) {
            c = bs_in(np);
            if (c != a && rs_parse(PROGS[sty][c], tmp, &m2) && m2 == mode) { b = c; break; }
        }
        if (b == a) {                                     /* (no other in its mode: from its IV) */
            static const int8_t D4[4] = {3, 4, 2, 5};
            for (k = 0; k < 4u; k++) { rsg.prog[1][k] = rsg.prog[0][0]; rsg.prog[1][k].deg = D4[k]; rsg.prog[1][k].q = Q_DIA; rsg.prog[1][k].acc = 0; rsg.prog[1][k].inv = 0; }
        } else
            rs_parse(PROGS[sty][b], rsg.prog[1], &m2);
        c = a;
        for (tries = 0; tries < 16u; tries++) {
            uint32_t x = bs_in(np);
            if (x != a && x != b && rs_parse(PROGS[sty][x], tmp, &m2) && m2 == mode) { c = x; break; }
        }
        if (c == a || bs_chance(30)) {                    /* (the bridge somewhere new: the scale's own IV V iii vi, or vi
                                                           * IV V V; not the B section's) */
            static const int8_t DB[2][4] = {{3, 4, 2, 5}, {5, 3, 4, 4}};
            const int8_t *d = DB[b == a ? 1u : bs_in(2)];
            for (k = 0; k < 4u; k++) { rs_ch_t z = {d[k], 0, Q_DIA, 0}; rsg.prog[2][k] = z; }
        } else
            rs_parse(PROGS[sty][c], rsg.prog[2], &m2);
    }
    rsg.mode = (uint8_t)mode;
    rsg.sc = MODE[mode];
    rsg.tonic = 45 + (int32_t)bs_in(12);                   /* the chords' tonic: A2..G#3 (+12: their register) */
    rsg.kicks = (uint8_t)bs_in(4);
    rsg.perc = (uint8_t)bs_in(4);
    rsg.extra = (uint8_t)(bs_chance(50) ? 128u : 4u);
    rsg.sixteen = (uint8_t)bs_chance(40);
    rsg.groove[0] = (uint8_t)bs_in(4);
    rsg.groove[1] = (uint8_t)((rsg.groove[0] + 1u + bs_in(3)) % 4u);
    rsg.sh = (uint8_t)bs_in(2);
    rsg.ppan = (uint8_t)(32 + bs_in(25));
    rsg.hpan = (uint8_t)(14 + bs_in(19));
    {                                                     /* the genre's grammar (rs_part): rolled within its own choices */
        uint32_t r;
        rsg.half = (uint8_t)bs_chance(STYLE[sty].half);
        rsg.snare = (uint8_t)(sty == ST_ELECTRO || sty == ST_FUNK ? (bs_chance(60) ? 4u : 6u) :
                              (sty == ST_CHILL || sty == ST_VAPOR || sty == ST_CITY) && bs_chance(50) ? 6u : 2u);
        rsg.hat = STYLE[sty].hat[bs_in(4)];
        rsg.oh = (uint8_t)(sty == ST_CHIP ? 0u : bs_in(3));
        rsg.crhy = STYLE[sty].crhy[bs_in(6)];
        rsg.bmode = STYLE[sty].bmode[bs_in(6)];
        rsg.appr = (uint8_t)bs_chance(STYLE[sty].appr);
        rsg.lead_up = (uint8_t)(sty == ST_CHIP);
        rsg.grv = (uint8_t)(bs_chance(STYLE[sty].written) ? STYLE[sty].grooves[bs_in(6)] : 0xFFu);
        if (rsg.grv != 0xFFu && GROOVES[rsg.grv].v[GR_S] == 0) rsg.half = 1;   /* (a groove with its snare on the clap row) */
        rsg.rhythm[0] = rsg.rhythm[1] = STYLE[sty].rhy[bs_in(4)];
        for (r = 0; r < 4u && rsg.rhythm[1] == rsg.rhythm[0]; r++)
            rsg.rhythm[1] = STYLE[sty].rhy[bs_in(4)];
        if (rsg.rhythm[1] == rsg.rhythm[0]) rsg.rhythm[1] = (uint8_t)(sty == ST_CHIP ? 5u : sty == ST_ELECTRO ? 7u : (rsg.rhythm[0] + 1u) % 4u);
        rs_drums_make();
        rsg.hr = (uint8_t)(bs_chance(STYLE[sty].slow) ? 16u : 8u);   /* item 3: the chords' pace (the section 2 or 4 bars) */
        rsg.len = (uint8_t)(rsg.hr * 4u);
        rs_motif_make(&rsg.mot[0], rsg.rhythm[0], 0);    /* item 4: the hooks, the A section's and the B section's */
        rs_motif_make(&rsg.mot[1], rsg.rhythm[1], 1);
    }
#if SPK_NU                                                /* Spelunkr's web build: VAPOR off (Ed: "leave the effects off the
                                                           * random songs"; felucca_web.c vap_get: G_DRLVL, G_DRREV) */
    song.g[G_DRLVL] = song.g[G_DRREV] = 0;
    if (rsg.genre == ST_VAPOR) {                          /* (VAPORWAVE: slowed and warbly always, dusty mostly, crushed now and
                                                           * then: felucca_web.c G_DRLVL = SLOWED << 8 | WOW, G_DRREV = VINYL
                                                           * << 8 | CRUSH, 0..127; the page's VAPORIZE is 56 40 44 0) */
        uint32_t slow = 30 + bs_in(41),   /* (88..72 % speed: felucca_web.c vap_speed, to 50 %) */
                  wow = 26 + bs_in(30), vinyl = bs_chance(80) ? 20 + bs_in(40) : 0,
                 crush = bs_chance(20) ? 8 + bs_in(24) : 0;
        song.g[G_DRLVL] = (int16_t)(slow << 8 | wow);
        song.g[G_DRREV] = (int16_t)(vinyl << 8 | crush);
    }
#endif

    for (k = 0; k < NTRK; k++)                            /* Phantom Cosmonaut M0: the tracks' fixed roles (core.h) */
        cls[k] = (uint8_t)trk_cls(k);
    memset(seen, 0, sizeof seen);
    for (k = 0; k < NTRK; k++) {                          /* the sounds, the party's */
        track_t *t = &trk[k];
        uint32_t c = cls[k], nth = seen[c]++;
        if (c == CLS_ROGUE || c == CLS_PERC) {            /* 0.10: the style's kits */
            set_engine_of(t, ENGI_CKIT);
            apply_preset_to(t, c == CLS_ROGUE ? STYLE[sty].kit[bs_in(4)] : STYLE[sty].pkit[bs_in(4)]);
            if (c == CLS_PERC) {                          /* (the second sound from another kit of the style's) */
                t->p[PC_SND(0)] = (int16_t)(P1[bs_in(4)] + 1u);
                t->p[PC_SND(1)] = (int16_t)(P2[bs_in(4)] + 1u);
                t->p[PC_KIT(1)] = (int16_t)STYLE[sty].pkit[bs_in(4)];
                pc_source(t, 0);
                pc_source(t, 1);
            }
            continue;
        }
        if (CLS_ROLE[c] == ROLE_BASS) rs_style_sound(t, RR_BASS, CAT_BASS);
        else if (c == CLS_MAGE) rs_style_sound(t, RR_LEAD, CAT_LEAD);
        else rs_style_sound(t, nth ? RR_BELL : RR_CHORDS, nth || bs_chance(50) ? CAT_KEYS : CAT_PAD);
        rsg.stab[0] = c == CLS_CLERIC && !nth ? (uint8_t)(preset_cat(t->eng_req % NENGINES, t->preset) != CAT_PAD) : rsg.stab[0];
        if (c == CLS_CLERIC || (c == CLS_FIGHTER && bs_chance(50)) || (c == CLS_MAGE && bs_chance(40))) {
            bsr_t *b = bs_of(t);                          /* 0.10: a B side: its own sound, an octave off, panned apart */
            int32_t w = 18 + (int32_t)bs_in(17);
            if (!b || !b_set(t, 1))
                continue;
            b_flip(t);
            if (c == CLS_CLERIC) rs_style_sound(t, RR_BELL, CAT_PLUCK);      /* (the style's bells over its chords) */
            else if (c == CLS_FIGHTER) rs_style_sound(t, bs_chance(50) ? RR_BASS : RR_BELL, CAT_BASS);
            else rs_style_sound(t, RR_LEAD, CAT_LEAD);
            b_flip(t);
            b->s.tr = c == CLS_FIGHTER ? (preset_cat(b->s.engine, b->s.preset) == CAT_BASS ? -12 : 12) : 12;
            b->s.lv = (int16_t)(c == CLS_FIGHTER ? 84 : 80 + bs_in(17));
            t->p[P_PAN] = (int16_t)(c == CLS_FIGHTER ? 0 : -w);
            b->s.pn = (int16_t)(c == CLS_FIGHTER ? 0 : w);
        }
    }
#if RONDO_LAYOUT
    for (sec = 0; sec < RS_N; sec++) {                    /* the six patterns, A..F */
        f = motion_guard();
        memset(&motion, 0, sizeof motion);                 /* (each pattern its own locks: the pans) */
        motion_unguard(f);
        memset(seen, 0, sizeof seen);
        for (k = 0; k < NTRK; k++) {
            motion_clear(&trk[k]);
            rs_part(&trk[k], cls[k], seen[cls[k]]++, sec);
        }
        motion_set_enabled(&trk[0], 1);
        motion_set_enabled(&trk[1], 1);                   /* (the build's swell, the outro's fade: LEVEL locks) */
        motion_set_enabled(&trk[PERC_TRK], 1);
        f = motion_guard();
        bank_take(sec);
        bank.dirty |= (uint8_t)(1u << sec);
        motion_unguard(f);
    }
    f = motion_guard();
    bank.cur = RS_VERSE;                                  /* (the A section to play, to jam on) */
    bank.queue = CHAIN_OFF;
    bank_give(RS_VERSE);
    motion_unguard(f);
    chain_defaults(&chain_config);
    {                                                     /* the arrangement: a template, its lengths and drops varied */
        const uint8_t (*arr)[3] = ARRS[bs_in(5)];
        uint32_t n = 0, r, per = 64u / rsg.len;
        for (r = 0; r < 13u && arr[r][1] && n < CHAIN_ROWS; r++, n++) {
            chain_row_t *row = &chain_config.row[n];
            uint32_t p = arr[r][0], blocks = arr[r][1], drop = arr[r][2];
            if ((p == RS_VERSE || p == RS_CHORUS) && r && arr[r + 1u][1]) {   /* (a little of its own: lengths, drops) */
                if (blocks == 2u && bs_chance(30)) blocks = 1;
                else if (blocks == 1u && p == RS_CHORUS && bs_chance(25)) blocks = 2;
                if (p == RS_VERSE && !drop && bs_chance(20)) drop = 16u;   /* (an A section without PERC) */
            }
            for (k = 0; k < NTRK; k++)
                row->slot[k] = (uint8_t)((drop >> k) & 1u ? CHAIN_SILENT : chain_val(p));
            row->repeat = (uint8_t)(blocks * per);
        }
        chain_config.count = (uint8_t)n;
    }
#else
    memset(seen, 0, sizeof seen);
    for (k = 0; k < NTRK; k++)
        rs_part(&trk[k], cls[k], seen[cls[k]]++, 1u);
    (void)sec; (void)f;
#endif
    for (k = 0; k < NTRK; k++) {                          /* Spelunkr: the song's key on the melodic tracks (ROOT, SCALE:
                                                           * the page's KEY shows it, RANDOM's notes keep to it; the keys
                                                           * stay free: QNT as it was) */
        if (CLS_ROLE[cls[k]] == ROLE_DRUMS)
            continue;
        trk[k].p[P_ROOT] = (int16_t)(rsg.tonic % 12);
        trk[k].p[P_SCALE] = (int16_t)MODE_SCL[mode % RM_N];
    }
    for (k = 0; k < NTRK; k++) {
        pat_sig[k] = steps_sig(&trk[k]);
        pat_last[k] = 0;
    }
    undo_depth--;
    song.sel = 0;
    ui.force = 1;
}
#if RONDO_LAYOUT && !defined(BS_NO_MENU)
/* MENU > GAME > RANDOM SONG (OCT+): NEW + DEFAULT SOUNDS, then a roll of the four patterns and the arrangement; SAVE held
 * undoes it (as NEW: the song before, its current pattern back in its key). Patterns of the user's in the bank besides
 * the current one would be written over (no room to keep a copy of them): then the first OCT+ says so and a second
 * within RS_ARM_MS rolls; a roll nothing has changed since rolls again at once */
#define RS_ARM_MS 4000u
static uint32_t rs_count, rs_arm_t;
static uint32_t rs_salt;                                  /* (the web page's: Math.random's 32 bits each roll, felucca_web.c
                                                           * web_rs_salt; without it the seed is the time since power-on
                                                           * alone, so two visitors rolling at the same moment of theirs
                                                           * got the same song, Nov 26 2026) */
static uint8_t rs_old_cur, rs_fresh;
static void random_undo(void)                             /* (project.c new_undo, after the song before is back) */
{
    uint32_t p, f = motion_guard();
    for (p = 0; p < RS_N; p++)                            /* the rolled patterns: gone (what they wrote over: lost, as said) */
        if (p != rs_old_cur) {
            memset(&bank_pat[p], 0, sizeof bank_pat[p]);
            bank.used &= (uint8_t)~(1u << p);
            bank.dirty |= (uint8_t)(1u << p);
            nw.used &= (uint8_t)~(1u << p);
        }
    bank.cur = rs_old_cur;                                /* the song before's pattern, in its own key again */
    bank.queue = CHAIN_OFF;
    bank_take(rs_old_cur);
    bank.dirty |= (uint8_t)(1u << rs_old_cur);
    motion_unguard(f);
    rs_fresh = 0;
}
static void random_song(void)
{
    uint8_t others = (uint8_t)(bank.used & ~(1u << bank.cur));
    if (chain_busy()) {
        ui_message("STOP TO EDIT");
        return;
    }
    if (others && !(rs_fresh && autosave_sig() == nw.after) && !(rs_arm_t && fm1_ms - rs_arm_t < RS_ARM_MS)) {
        rs_arm_t = fm1_ms | 1u;                           /* (the bank holds the user's patterns: OCT+ again) */
        ui_message("REPLACES PATTERNS A-F");
        str_cpy(ui.msg2, "[OCT+] AGAIN TO ROLL", sizeof ui.msg2);
        return;
    }
    rs_arm_t = 0;
    if (!(rs_fresh && autosave_sig() == nw.after))       /* (a roll again: the undo keeps the song before the first) */
        rs_old_cur = bank.cur;
    if (rs_fresh && autosave_sig() == nw.after) {         /* (re-roll: NEW's copy stays the user's song) */
        uint32_t f = motion_guard();
        bank.cur = rs_old_cur;
        motion_unguard(f);
        undo_depth++;
        for (uint32_t k = 0; k < NTRK; k++) {
            seq_release(&trk[k]);
            drum_mute[k] = 0;
            track_defaults(&trk[k]);
        }
        undo_depth--;
    } else {
        new_song(1);
    }
    random_song_make(((fm1_ms ^ (fm1_ticks() << 8)) * 2654435761u + ++rs_count * 40503u) ^ rs_salt);
    rs_salt = 0;
    morph_reset();
    nw.after = autosave_sig();                            /* (SAVE held: this roll back to what was) */
    new_undo_hook = random_undo;
    rs_fresh = 1;
    ui_message("RANDOM SONG");
    str_cpy(ui.msg2, "[SAVE] HOLD TO UNDO", sizeof ui.msg2);
}
#endif
#endif
