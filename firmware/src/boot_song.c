/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Dungeon Tracker contributors. Built on Felucca (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* MENU > SYSTEM > RANDOM SONG (from Dungeon Tracker 1.0; 0.10 rewritten for the five FEEDs), a song to start from. From
 * one seed: a style (GLITCH HOP, CHILLWAVE, ELECTRO, LO-FI: its tempo and its kits), a key, a mode (minor, dorian, major)
 * and two chords of a progression, then for each FEED's fixed role (core.h trk_cls) a sound and a part:
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
/* the four sections, PATTERN BANK A..D: what each class plays in them */
enum { RS_INTRO, RS_VERSE, RS_CHORUS, RS_BREAK, RS_N };
static struct {
    const int8_t *sc;
    int32_t tonic;
    int8_t prog[2][4];                                    /* the verse's four chords, the chorus's (scale degrees) */
    uint8_t kicks, perc, extra, groove[2], rhythm[2], stab[NTRK], sixteen;
    uint8_t sh, ppan, hpan;                               /* 0.10: PERC 2's density, its pan; the hats' pan */
} rsg;
static int32_t rs_chord(uint32_t sec, uint32_t i)        /* the chord (a degree) under step i of section sec */
{
    return sec == RS_BREAK ? rsg.prog[1][(i / 16u) * 2u] : rsg.prog[sec == RS_CHORUS][(i / 8u) % 4u];
}
static void rs_part(track_t *t, uint32_t c, uint32_t nth, uint32_t sec)
{
    static const uint8_t BASS_GROOVE[4][8] = {            /* 1 the root, 2 its octave, 3 the fifth; 0 rest (a chord's 8) */
        {1, 0, 0, 1, 0, 0, 1, 0}, {1, 0, 0, 0, 1, 0, 2, 0}, {1, 0, 0, 0, 0, 0, 3, 0}, {1, 0, 1, 0, 0, 2, 0, 0},
    };
    static const uint8_t LEAD_RHYTHM[4][16] = {           /* 1 a note, 2 held on (a tie); 0 rest: sparse on purpose */
        {1, 0, 0, 0, 1, 2, 0, 0, 1, 0, 0, 1, 1, 2, 2, 0},
        {1, 2, 2, 0, 0, 0, 1, 0, 1, 2, 0, 0, 0, 0, 0, 0},
        {1, 0, 0, 1, 0, 0, 1, 2, 2, 0, 0, 0, 1, 2, 2, 2},
        {0, 0, 1, 0, 1, 0, 0, 0, 1, 2, 2, 0, 0, 0, 1, 0},
    };
    static const uint16_t KICK[4] = {0x0101, 0x0121, 0x0105, 0x2101};   /* bit i: step i of a bar's 16 */
    static const uint16_t PERC[4] = {0x4848, 0x2492, 0x0A0A, 0x8888};
    uint32_t i;
    track_defaults_steps(t);
    t->p[P_SLEN] = 32;
    switch (c) {
    case CLS_ROGUE:
        for (i = 0; i < 32u; i++) {
            uint32_t b = i % 16u, hit = 0, kick = (KICK[rsg.kicks] >> b) & 1u;
            if (nth) {                                    /* percussion around the beat: the verse and the chorus */
                if ((sec == RS_VERSE || sec == RS_CHORUS) && ((PERC[rsg.perc] >> b) & 1u))
                    hit = b % 4u == 3u ? 32u : (i & 4u) ? 64u : rsg.extra;
            } else if (sec == RS_INTRO) {                 /* kick and hats */
                hit = (kick ? 1u : 0u) | (!(b & 1u) && i >= 8u ? 8u : 0u);
            } else if (sec == RS_BREAK) {                 /* a kick to start, a fill to end */
                hit = i == 0u ? 1u : i == 28u || i == 30u ? 2u : i == 31u ? 32u : 0u;
            } else {
                hit = (kick ? 1u : 0u) | (b == 4u || b == 12u ? 2u : 0u);
                if (!(b & 1u) || (sec == RS_CHORUS && rsg.sixteen)) hit |= 8u;
                if (sec == RS_CHORUS && (b == 14u)) hit = (hit & ~8u) | 16u;
                if (sec == RS_CHORUS && i == 31u) hit |= 32u;
                if (sec == RS_VERSE && i == 30u) hit = (hit & ~8u) | 16u;
            }
            rs_hit(t, &t->step[i], hit, b % 4u == 0u);
            if (hit == 8u && (b & 2u) && sec != RS_BREAK)   /* 0.10: a hat alone off the beat: panned, kept */
                motion_set_lock(t, i, P_PAN, (int16_t)(b & 4u ? (int32_t)rsg.hpan : -(int32_t)rsg.hpan));
        }
        break;
    case CLS_CLERIC:
        if (sec == RS_INTRO && nth)
            break;
        for (i = 0; i < 32u; i++) {
            uint32_t len = sec == RS_BREAK ? 16u : 8u, at = i % len;
            int32_t d = rs_chord(sec, i);
            uint8_t ch[3];
            uint32_t k;
            for (k = 0; k < 3u; k++) {                    /* (into 36..96 by octaves, so a tone stays in the key) */
                int32_t n = bs_deg(rsg.sc, rsg.tonic + 12 + (nth ? 12 : 0), d + 2 * (int32_t)k);
                while (n > 96) n -= 12;
                while (n < 36) n += 12;
                ch[k] = (uint8_t)n;
            }
            if (nth) {                                    /* the arpeggio: 8ths up the chord */
                if (sec != RS_BREAK && !(i & 1u))
                    bs_note(&t->step[i], &ch[(at / 2u) % 3u], 1, at ? 84 : 96, 0);
            } else if (rsg.stab[0] && sec != RS_BREAK && sec != RS_INTRO ? (at == 0u || at == 3u || at == 6u) : at == 0u) {
                bs_note(&t->step[i], ch, 3, 92, 0);
            } else if (!(rsg.stab[0] && sec != RS_BREAK && sec != RS_INTRO) && at < len - 1u) {
                bs_tie(&t->step[i]);
            }
        }
        break;
    case CLS_FIGHTER:
        for (i = 0; i < 32u; i++) {
            uint32_t len = sec == RS_BREAK ? 16u : 8u, at = i % len, g;
            int32_t root = bs_deg(rsg.sc, rsg.tonic - 12, rs_chord(sec, i));
            uint8_t n;
            while (root > 50) root -= 12;                 /* (the bass's register: E1..D3) */
            while (root < 28) root += 12;
            if (nth && root >= 36) root -= 12;
            if (nth || sec == RS_BREAK || sec == RS_INTRO) {   /* the roots held (the intro: two beats each) */
                n = (uint8_t)root;
                if (!at) bs_note(&t->step[i], &n, 1, 100, 1);
                else if (at < (sec == RS_INTRO && !nth ? 4u : len - 1u)) bs_tie(&t->step[i]);
                continue;
            }
            g = BASS_GROOVE[rsg.groove[sec == RS_CHORUS]][at];
            n = (uint8_t)(g == 2u ? root + 12 : g == 3u ? root + 7 : root);
            if (g) bs_note(&t->step[i], &n, 1, 100, at == 0u);
        }
        break;
    case CLS_PERC: {                                      /* 0.10: PERC 1 (lane 1) a syncopation, PERC 2 (lane 2) a shaker
                                                           * line ping-ponging; the break a PERC 1 fill swept across */
        static const uint16_t SH[3] = {0xAAAA, 0xEEEE, 0xFFFF};   /* the shaker: 8ths, three of four 16ths, all 16ths */
        int32_t side = -1;
        for (i = 0; i < 32u; i++) {
            uint32_t b = i % 16u, hit = 0;
            if (sec == RS_BREAK) {
                if (i >= 24u && !(i & 1u))
                    hit = 1u;
            } else {
                if (sec != RS_INTRO && ((PERC[rsg.perc] >> b) & 1u))
                    hit |= 1u;
                if ((SH[sec == RS_CHORUS ? rsg.sh + 1u > 2u ? 2u : rsg.sh + 1u : sec == RS_INTRO ? 0u : rsg.sh] >> b) & 1u)
                    hit |= 2u;
                if (sec == RS_INTRO && i < 16u)
                    hit &= 1u;
            }
            rs_hit(t, &t->step[i], hit, b % 4u == 2u);
            if (hit == 2u && (b & 1u)) {                  /* (the shaker's off 16ths: left, right, each kept) */
                motion_set_lock(t, i, P_PAN, (int16_t)(side * (int32_t)rsg.ppan));
                side = -side;
            } else if (hit == 1u && sec == RS_BREAK) {    /* (the fill: left to right) */
                motion_set_lock(t, i, P_PAN, (int16_t)(-48 + (int32_t)(i - 24u) * 16));
            }
        }
        break;
    }
    default: {                                            /* the lead: a phrase, sung twice, its end varied */
        int32_t prev = 7 + (sec == RS_CHORUS ? 3 : 0) + (nth ? 2 : 0);
        if (sec == RS_INTRO || (nth && sec != RS_CHORUS))
            break;
        for (i = 0; i < 32u; i++) {
            step_t *s = &t->step[i];
            uint32_t b = i % 16u, r = sec == RS_BREAK ? (b == 0u ? 1u : b < 12u ? 2u : 0u) :
                                       LEAD_RHYTHM[rsg.rhythm[sec == RS_CHORUS]][b];
            if (nth && b < 8u) r = 0;                     /* (the answer: the bar's second half) */
            if (i >= 16u && i < 24u && sec != RS_BREAK && !nth) {   /* (bars twice: the start again, the end new) */
                *s = t->step[i - 16u];
                continue;
            }
            if (r == 1u) {
                int32_t chord = rs_chord(sec, i), d, j, best = chord + 7;
                uint8_t n;
                if (b % 4u == 0u) {                       /* on the beat: a chord tone near the last note */
                    for (j = chord; j <= chord + 14; j += 2)
                        if (bs_abs(j - prev) < bs_abs(best - prev))
                            best = j;
                    d = best;
                } else {
                    d = prev + (int32_t)bs_in(5) - 2;
                }
                d = clamp(d, 5, 17);
                n = (uint8_t)bs_deg(rsg.sc, rsg.tonic + 12, d);
                bs_note(s, &n, 1, b % 4u == 0u ? 104 : 88, 0);
                prev = d;
            } else if (r == 2u && b) {
                bs_tie(s);
            }
        }
        break;
    }
    }
}
/* a whole song from one seed: THE PARTY and its sounds, PATTERN BANK A..D (intro, verse, chorus, break; 32 steps) and
 * SONG's arrangement; the tracks left on the verse (B). Called over NEW + DEFAULT SOUNDS (random_song) */
static void random_song_make(uint32_t seed)
{
    static const int8_t MODE[3][7] = {{0, 2, 3, 5, 7, 8, 10}, {0, 2, 3, 5, 7, 9, 10}, {0, 2, 4, 5, 7, 9, 11}};
    static const int8_t PROG[3][4][4] = {                 /* scale degrees, four chords */
        {{0, 5, 2, 6}, {0, 3, 4, 0}, {0, 6, 5, 6}, {0, 5, 3, 4}},   /* minor */
        {{0, 3, 0, 6}, {0, 6, 3, 0}, {0, 3, 6, 4}, {0, 1, 3, 0}},   /* dorian */
        {{0, 4, 5, 3}, {0, 5, 3, 4}, {0, 3, 0, 4}, {5, 3, 0, 4}},   /* major */
    };
    static const uint8_t ARR[][2] = {{RS_INTRO, 2}, {RS_VERSE, 2}, {RS_CHORUS, 2}, {RS_VERSE, 2}, {RS_CHORUS, 2},
                                     {RS_BREAK, 1}, {RS_CHORUS, 2}};   /* SONG: A B C B C D C */
    static const struct {                                 /* 0.10: the styles: a tempo range (lo + 2 x 0..n-1), DRUMS' kits,
                                                           * PERC's (CK_PRESETS' numbers) */
        uint8_t bpm, n, kit[4], pkit[4];
    } STYLE[4] = {
        /* (Spelunkr: the drums the sampled kits, CK_PRESETS 40.. CRATE BEDROOM VAPOR 808 LINN GATED NES GAMEBOY SNES) */
        {88, 9, {45, 46, 47, 16}, {32, 26, 13, 27}},     /* CHIPTUNE (was GLITCH HOP): NES GAMEBOY SNES GLITCH HOP */
        {76, 11, {42, 41, 42, 17}, {31, 22, 5, 1}},      /* CHILLWAVE: VAPOR 808 BEDROOM VAPOR 808 CHILLWAVE */
        {112, 9, {43, 44, 42, 8}, {26, 15, 34, 7}},      /* ELECTRO: LINN GATED VAPOR 808 ELECTRO */
        {84, 7, {40, 41, 40, 6}, {38, 10, 29, 21}},      /* LO-FI: CRATE BEDROOM CRATE BOOM BAP */
    };
    static const uint8_t P1[4] = {3, 2, 13, 14}, P2[4] = {15, 5, 12, 4};   /* PERC's: CLAP RIM CLAVE CONGA; SHAKER PD HAT
                                                                             * COWBELL CL HAT (eng_cycle.c PC_GM) */
    uint8_t cls[NTRK], seen[NCLS];
    uint32_t mode, pr, pc, k, sec, f, sty;
    bs_rng = seed | 1u;
    undo_depth++;                                         /* (random_song keeps one undo copy: NEW's) */
    for (k = 1; k <= B_N; k++) {                          /* (no B side from the song before) */
        b_set(&trk[k], 0);
        memset(&bsr[k - 1u], 0, sizeof bsr[k - 1u]);
    }
    sty = bs_in(4);
    song.g[G_BPM] = (int16_t)(STYLE[sty].bpm + 2 * bs_in(STYLE[sty].n));   /* 76..128 by the style */
    mode = bs_in(10) < 5u ? 0u : bs_in(2) ? 1u : 2u;      /* minor half the time, dorian or major */
    rsg.sc = MODE[mode];
    pr = bs_in(4);
    pc = (pr + 1u + bs_in(3)) % 4u;                       /* (the chorus: another progression) */
    memcpy(rsg.prog[0], PROG[mode][pr], 4);
    memcpy(rsg.prog[1], PROG[mode][pc], 4);
    rsg.tonic = 45 + (int32_t)bs_in(12);                   /* the chords' tonic: A2..G#3 (+12: their register) */
    rsg.kicks = (uint8_t)bs_in(4);
    rsg.perc = (uint8_t)bs_in(4);
    rsg.extra = (uint8_t)(bs_chance(50) ? 128u : 4u);
    rsg.sixteen = (uint8_t)bs_chance(40);
    rsg.groove[0] = (uint8_t)bs_in(4);
    rsg.groove[1] = (uint8_t)((rsg.groove[0] + 1u + bs_in(3)) % 4u);
    rsg.rhythm[0] = (uint8_t)bs_in(4);
    rsg.rhythm[1] = (uint8_t)((rsg.rhythm[0] + 1u + bs_in(3)) % 4u);
    rsg.sh = (uint8_t)bs_in(2);
    rsg.ppan = (uint8_t)(32 + bs_in(25));
    rsg.hpan = (uint8_t)(14 + bs_in(19));

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
        rs_sound(t, CLS_ROLE[c] == ROLE_BASS ? CAT_BASS : c == CLS_MAGE ? CAT_LEAD : nth || bs_chance(50) ? CAT_KEYS : CAT_PAD);
        rsg.stab[0] = c == CLS_CLERIC && !nth ? (uint8_t)(preset_cat(t->eng_req % NENGINES, t->preset) != CAT_PAD) : rsg.stab[0];
        if (c == CLS_CLERIC || (c == CLS_FIGHTER && bs_chance(50)) || (c == CLS_MAGE && bs_chance(40))) {
            bsr_t *b = bs_of(t);                          /* 0.10: a B side: its own sound, an octave off, panned apart */
            int32_t w = 18 + (int32_t)bs_in(17);
            if (!b || !b_set(t, 1))
                continue;
            b_flip(t);
            rs_sound(t, c == CLS_CLERIC ? (bs_chance(50) ? CAT_KEYS : CAT_PLUCK) : c == CLS_FIGHTER ?
                        (bs_chance(50) ? CAT_BASS : CAT_PLUCK) : CAT_LEAD);
            b_flip(t);
            b->s.tr = c == CLS_FIGHTER ? (preset_cat(b->s.engine, b->s.preset) == CAT_BASS ? -12 : 12) : 12;
            b->s.lv = (int16_t)(c == CLS_FIGHTER ? 84 : 80 + bs_in(17));
            t->p[P_PAN] = (int16_t)(c == CLS_FIGHTER ? 0 : -w);
            b->s.pn = (int16_t)(c == CLS_FIGHTER ? 0 : w);
        }
    }
#if RONDO_LAYOUT
    for (sec = 0; sec < RS_N; sec++) {                    /* the four patterns, A..D */
        f = motion_guard();
        memset(&motion, 0, sizeof motion);                 /* (each pattern its own locks: the pans) */
        motion_unguard(f);
        memset(seen, 0, sizeof seen);
        for (k = 0; k < NTRK; k++) {
            motion_clear(&trk[k]);
            rs_part(&trk[k], cls[k], seen[cls[k]]++, sec);
        }
        motion_set_enabled(&trk[0], 1);
        motion_set_enabled(&trk[PERC_TRK], 1);
        f = motion_guard();
        bank_take(sec);
        bank.dirty |= (uint8_t)(1u << sec);
        motion_unguard(f);
    }
    f = motion_guard();
    bank.cur = RS_VERSE;                                  /* (the verse to play, to jam on) */
    bank.queue = CHAIN_OFF;
    bank_give(RS_VERSE);
    motion_unguard(f);
    chain_defaults(&chain_config);
    chain_config.count = (uint8_t)NELEM(ARR);
    for (k = 0; k < NELEM(ARR); k++)
        chain_config.row[k] = chain_row_of(ARR[k][0], ARR[k][1]);
#else
    memset(seen, 0, sizeof seen);
    for (k = 0; k < NTRK; k++)
        rs_part(&trk[k], cls[k], seen[cls[k]]++, 1u);
    (void)sec; (void)f;
#endif
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
    random_song_make((fm1_ms ^ (fm1_ticks() << 8)) * 2654435761u + ++rs_count * 40503u);
    morph_reset();
    nw.after = autosave_sig();                            /* (SAVE held: this roll back to what was) */
    new_undo_hook = random_undo;
    rs_fresh = 1;
    ui_message("RANDOM SONG");
    str_cpy(ui.msg2, "[SAVE] HOLD TO UNDO", sizeof ui.msg2);
}
#endif
#endif
