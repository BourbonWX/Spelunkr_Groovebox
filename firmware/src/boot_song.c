/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Dungeon Tracker contributors. Built on Felucca (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* MENU > SYSTEM > RANDOM SONG (from Dungeon Tracker 1.0; 0.10 rewritten for the five FEEDs), a song to start from. From
 * one seed: a style, a key, a mode (minor, dorian, major) and two chords of a progression, then for each FEED's fixed
 * role (core.h trk_cls) a sound and a part. Spelunkr (Nov 22 2026): the style is a grammar, not only a tempo and kits:
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
/* the four sections, PATTERN BANK A..D: what each class plays in them; Spelunkr (Ed, Nov 22 2026: item 3, the song's
 * shape): E a BUILD (the drums rising into the chorus) and F the style's extra (a LIFT: the last chorus a tone up; an
 * OUTRO; or VERSE 2), the sections 2 or 4 bars (the chords every half bar or every bar), the arrangement from a few
 * templates with tracks left out of some passes (the lead coming in late, the drums dropping out) */
enum { RS_INTRO, RS_VERSE, RS_CHORUS, RS_BREAK, RS_BUILD, RS_EXTRA, RS_N };
enum { FX_LIFT, FX_OUTRO, FX_VERSE2 };
/* Spelunkr (Ed, Nov 22 2026: "they all sound like the same pattern just with different instruments"): each style its
 * own grammar (the drums' feel, the bass's job, the chords' voicing and rhythm, the lead's), and the beats generated
 * from it (weighted kick positions, euclidean percussion, rolls, ghost notes) rather than picked from a few */
enum { ST_CHIP, ST_CHILL, ST_ELECTRO, ST_LOFI, ST_N };
#ifndef ENGI_ANALOG
#define ENGI_ANALOG 0u                                    /* (ANALOG: engine 0, engines.c ENG_TO_ANALOG) */
#endif
enum { BM_GROOVE, BM_KICK, BM_OCTAVE, BM_WALK, BM_808 };     /* the bass's job */
enum { CR_HELD, CR_STAB, CR_OFF, CR_ARP, CR_PUSH };          /* the chords' rhythm */
typedef struct { uint8_t r[16]; int8_t c[16]; } rs_motif_t;  /* a bar of lead: 1 a note, 2 held, 0 rest; its degrees */
static struct {
    const int8_t *sc;
    int32_t tonic;
    int8_t prog[2][4];                                    /* the verse's four chords, the chorus's (scale degrees) */
    uint8_t kicks, perc, extra, groove[2], rhythm[2], stab[NTRK], sixteen;
    uint8_t sh, ppan, hpan;                               /* 0.10: PERC 2's density, its pan; the hats' pan */
    uint8_t sty, half, snare, hat, oh, voice, crhy, bmode, lead_up;
    uint16_t kick[2][2];                                  /* [verse, chorus][odd bar, even bar]: bit i a kick on step i */
    uint16_t ghost, roll[2], p1[2], p2;                   /* ghost snares; hat rolls [verse, chorus]; PERC's lanes */
    uint8_t hr, len, fx, v2;                              /* steps a chord (8 / 16), a section (4 chords), F's kind */
    rs_motif_t mot[2];                                    /* the hooks: [verse, chorus] */
} rsg;
static int32_t rs_chord(uint32_t sec, uint32_t i)        /* the chord (a degree) under step i of section sec */
{
    uint32_t n = (i % rsg.len) / rsg.hr % 4u;
    if (sec == RS_BREAK) return rsg.prog[1][(n / 2u) * 2u];   /* (two, twice as long) */
    if (sec == RS_BUILD) return rsg.prog[0][2u + n / 2u];      /* (the verse's last two, leaning into the chorus) */
    return rsg.prog[sec == RS_CHORUS][n];
}
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
        {1, 0, 1, 0, 0, 0, 1, 0, 1, 0, 1, 0, 0, 0, 0, 0},   /* 6, 7: a short riff, repeated (electro) */
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
    static const uint8_t W[ST_N][16] = {
        {0, 0, 2, 2, 0, 0, 3, 2, 4, 0, 4, 2, 0, 1, 3, 1},    /* CHIP: driving */
        {0, 0, 1, 2, 0, 1, 2, 3, 0, 1, 4, 3, 1, 0, 2, 1},    /* CHILLWAVE: around a half-time snare */
        {0, 0, 2, 4, 0, 0, 4, 3, 0, 2, 5, 4, 0, 2, 3, 1},    /* ELECTRO / 808: syncopated */
        {0, 0, 1, 3, 0, 1, 2, 4, 2, 3, 6, 3, 0, 1, 3, 2},    /* LO-FI: boom bap */
    };
    uint16_t m = 1u, snare = rsg.half ? (uint16_t)(1u << 8) : (uint16_t)(1u << 4 | 1u << 12);
    uint32_t tries;
    for (tries = 0; n && tries < 64u; tries++) {
        uint32_t i, tot = 0, r;
        for (i = 0; i < 16u; i++)
            if (!((m | snare) >> i & 1u)) tot += W[rsg.sty][i];
        if (!tot) break;
        r = bs_in(tot);
        for (i = 0; i < 16u; i++) {
            uint32_t w = (m | snare) >> i & 1u ? 0u : W[rsg.sty][i];
            if (r < w) { m |= (uint16_t)(1u << i); n--; break; }
            r -= w;
        }
    }
    return m;
}
static void rs_drums_make(void)
{
    static const uint8_t KN[ST_N][2] = {{2, 3}, {1, 2}, {3, 3}, {2, 3}};   /* more kicks: lo + 0..n-1 */
    uint32_t c, n;
    for (c = 0; c < 2u; c++) {                            /* [verse, chorus] */
        if (rsg.sty == ST_CHIP && rsg.kicks == 0u) {      /* (chip: four on the floor a quarter of the time) */
            rsg.kick[c][0] = rsg.kick[c][1] = (uint16_t)(0x1111u | (c ? 1u << 14 : 0u));
            continue;
        }
        n = KN[rsg.sty][0] + bs_in(KN[rsg.sty][1]) + c;
        rsg.kick[c][0] = rs_kick_bar(n);
        rsg.kick[c][1] = rsg.kick[c][0];                  /* bar 2: bar 1 with its second half moved */
        if (bs_chance(70)) {
            uint16_t late = (uint16_t)(rsg.kick[c][0] & 0xFF00u), mv = rs_kick_bar(1 + bs_in(2)) & 0xFF00u;
            rsg.kick[c][1] = (uint16_t)((rsg.kick[c][0] & 0x00FFu) | (bs_chance(50) ? mv : (uint16_t)(late | mv)));
        }
    }
    rsg.ghost = 0;                                        /* ghost snares: lo-fi always, chillwave sometimes */
    if (rsg.sty == ST_LOFI || (rsg.sty == ST_CHILL && bs_chance(40))) {
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
        static const uint8_t PK[ST_N][2] = {{3, 3}, {2, 3}, {3, 3}, {3, 2}}, SK[ST_N][2] = {{8, 9}, {4, 5}, {6, 7}, {5, 4}};
        uint32_t k1 = PK[rsg.sty][0] + bs_in(PK[rsg.sty][1]);
        rsg.p1[0] = rs_euclid(k1, 2u + bs_in(5));
        rsg.p1[1] = rs_euclid(k1 + 1u, 1u + bs_in(5));
        rsg.p2 = rs_euclid(SK[rsg.sty][0] + bs_in(SK[rsg.sty][1]), bs_in(2));
    }
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
            uint32_t b = i % 16u, bar = i / 16u, hit = 0, vel = 0, ratch = 0;
            uint32_t ch = sec == RS_CHORUS, kick = (rsg.kick[ch][bar & 1u] >> b) & 1u, snare = rsg.half ? b == 8u : b == 4u || b == 12u;
            if (nth) {                                    /* (a second ROGUE: percussion around the beat) */
                if ((sec == RS_VERSE || sec == RS_CHORUS) && ((PERC[rsg.perc] >> b) & 1u))
                    hit = b % 4u == 3u ? 32u : (i & 4u) ? 64u : rsg.extra;
                rs_hit(t, &t->step[i], hit, b % 4u == 0u);
                continue;
            }
            if (sec == RS_INTRO) {                        /* the kick from bar 2 (chillwave: none), the hats throughout */
                if (kick && i >= 16u && rsg.sty != ST_CHILL) hit |= 1u;
                if (rsg.hat == 2u ? b % 4u == 2u : !(b & 1u)) hit |= 8u;
                if (i < 8u && rsg.sty != ST_CHIP) hit &= ~8u;
            } else if (sec == RS_BREAK) {                 /* a kick to start, a fill to end (the style's) */
                if (i == 0u) hit = 1u;
                if (rsg.sty == ST_ELECTRO && i >= L - 8u && !(i & 1u)) { hit = 2u | 4u; vel = 60u + (i - (L - 8u)) * 8u; }
                else if (rsg.sty == ST_CHIP && i >= L - 4u) { hit = 2u; ratch = i == L - 1u ? 4u : 2u; }
                else if (i == L - 4u || i == L - 2u) hit = 2u;
                else if (i == L - 1u) hit = 32u;
            } else if (sec == RS_BUILD) {                 /* item 5: the kick on the beats, the snare in 8ths then
                                                           * 16ths, louder and louder, the last 16th left empty */
                uint32_t q = i * 4u / L;                  /* (the quarter of the section) */
                if (!(b & 3u) && q < 3u) hit |= 1u;
                if (q >= 1u && (q == 3u || !(b & 1u)) && (q >= 2u || !(b & 3u))) hit |= 2u;
                if (hit) vel = 56u + i * 70u / L;
                if (i == L - 1u) hit = 0;
                if (i == L - 2u && rsg.sty != ST_CHILL) hit |= 16u;
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
                if (i >= last && b >= 12u && (ch || rsg.v2 || L == 64u) && bs_chance(55)) {   /* (a fill into the next) */
                    hit = (hit & 1u) | (b == 15u ? 32u : 2u);
                    vel = 70u + (b - 12u) * 14u;
                }
                if (ch && i == L - 1u) hit |= 32u;        /* (a tom into the next) */
            }
            rs_hit(t, &t->step[i], hit, b % 4u == 0u && !vel);
            if (hit && vel && !(hit & 1u && b % 4u == 0u && sec != RS_BUILD)) t->step[i].vel = (uint8_t)vel;
            if (ratch) t->step[i].flags |= (uint8_t)((ratch - 1u) << SF_RATCH_SH);
            if (hit == 8u && (b & 2u) && sec != RS_BREAK && bar < 2u)   /* 0.10: a hat alone off the beat: panned, kept */
                motion_set_lock(t, i, P_PAN, (int16_t)(b & 4u ? (int32_t)rsg.hpan : -(int32_t)rsg.hpan));
        }
        break;
    case CLS_CLERIC:
        if (sec == RS_INTRO && nth)
            break;
        for (i = 0; i < L; i++) {
            uint32_t len = sec == RS_BREAK || sec == RS_BUILD ? 2u * rsg.hr : rsg.hr;
            uint32_t push = rsg.crhy == CR_PUSH && (sec == RS_VERSE || sec == RS_CHORUS);
            uint32_t at = (i + push) % len, nv = rsg.voice ? 4u : 3u;
            uint32_t rhy = sec == RS_BREAK || sec == RS_INTRO || sec == RS_BUILD ? CR_HELD : rsg.crhy;
            int32_t d = rs_chord(sec, i + push);
            uint8_t chd[4];
            uint32_t k;
            for (k = 0; k < nv; k++) {                    /* (into 36..96 by octaves, so a tone stays in the key) */
                int32_t n = bs_deg(rsg.sc, rsg.tonic + 12 + (nth ? 12 : 0), d + 2 * (int32_t)k + (rsg.voice == 2u ? 2 : 0));
                while (n > 96) n -= 12;
                while (n < 48) n += 12;
                chd[k] = (uint8_t)n;
            }
            if (nth) {                                    /* the arpeggio: 8ths up the chord */
                if (sec != RS_BREAK && !(i & 1u))
                    bs_note(&t->step[i], &chd[(at / 2u) % 3u], 1, at ? 84 : 96, 0);
                continue;
            }
            if (rhy == CR_ARP) {                          /* chip: the chord as a fast arpeggio, up and over */
                static const uint8_t UP[8] = {0, 1, 2, 3, 2, 1, 0, 1};
                uint8_t n1 = chd[UP[at % 8u] % nv];
                if (UP[at % 8u] >= nv) n1 = (uint8_t)(chd[0] + 12);
                bs_note(&t->step[i], &n1, 1, at % 4u ? 80 : 100, 0);
            } else if (rhy == CR_STAB ? (at % 8u == 0u || at % 8u == 3u || at % 8u == 6u) : rhy == CR_OFF ? (at % 4u == 2u) : at == 0u) {
                bs_note(&t->step[i], chd, nv, rhy == CR_OFF ? 84 : 92, 0);
            } else if ((rhy == CR_HELD || rhy == CR_PUSH) && at < len - 1u) {
                bs_tie(&t->step[i]);
            }
            if (sec == RS_BUILD && !(i % 4u))             /* item 5: the build swells (LEVEL, step by step) */
                motion_set_lock(t, i, P_LEVEL, (int16_t)(64 + i * 52u / L));
        }
        break;
    case CLS_FIGHTER:
        for (i = 0; i < L; i++) {
            uint32_t len = sec == RS_BREAK || sec == RS_BUILD ? 2u * rsg.hr : rsg.hr, at = i % len, b = i % 16u, g, ch = sec == RS_CHORUS;
            int32_t root = bs_deg(rsg.sc, rsg.tonic - 12, rs_chord(sec, i));
            uint8_t n;
            while (root > 50) root -= 12;                 /* (the bass's register: E1..D3) */
            while (root < 28) root += 12;
            if (nth && root >= 36) root -= 12;
            if (sec == RS_BUILD) {                        /* the build: the root in 8ths, then 16ths */
                n = (uint8_t)root;
                if (!(b & 1u) || i >= L / 2u) bs_note(&t->step[i], &n, 1, 80 + i * 40u / L, 0);
                continue;
            }
            if (nth || sec == RS_BREAK || sec == RS_INTRO) {   /* the roots held (the intro: two beats each) */
                n = (uint8_t)root;
                if (!at) bs_note(&t->step[i], &n, 1, 100, 1);
                else if (at < (sec == RS_INTRO && !nth ? 4u : len - 1u)) bs_tie(&t->step[i]);
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
                    int32_t nd = rs_chord(sec, i + len - at), nx = bs_deg(rsg.sc, rsg.tonic - 12, nd + (bs_chance(50) ? -1 : 1));
                    while (nx > root + 6) nx -= 12;               /* (a scale step next to the next chord's root) */
                    while (nx < root - 6) nx += 12;
                    n = (uint8_t)(at == 0u ? root : at + 4u >= len ? nx : bs_chance(50) ? root + 7 :
                                  bs_deg(rsg.sc, rsg.tonic - 12, rs_chord(sec, i) + 2) % 12 + (root / 12) * 12);
                    if (n < root) n = (uint8_t)(n + 12);
                    if (n > 64) n = (uint8_t)(n - 12);
                    bs_note(&t->step[i], &n, 1, at == 0u ? 100 : 86, 0);
                    if (b + 1u < 16u) bs_tie(&t->step[i + 1u]), i++;
                }
                break;
            default:
                g = BASS_GROOVE[rsg.groove[ch]][at % 8u];
                n = (uint8_t)(g == 2u ? root + 12 : g == 3u ? root + 7 : root);
                if (g) bs_note(&t->step[i], &n, 1, 100, at == 0u);
                break;
            }
        }
        break;
    case CLS_PERC: {                                      /* PERC 1 (lane 1) a euclidean syncopation, PERC 2 (lane 2) a
                                                           * shaker line ping-ponging; the break a PERC 1 fill swept across */
        int32_t side = -1;
        for (i = 0; i < L; i++) {
            uint32_t b = i % 16u, hit = 0, ch = sec == RS_CHORUS;
            if (sec == RS_BREAK) {
                if (i >= L - 8u && !(i & 1u))
                    hit = 1u;
            } else if (sec == RS_BUILD) {                 /* the shaker in 16ths over the build's second half */
                if (i >= L / 2u) hit = 2u;
            } else {
                if (sec != RS_INTRO && ((rsg.p1[ch] >> b) & 1u))
                    hit |= 1u;
                if (sec != RS_INTRO || i >= 16u) {
                    if ((rsg.p2 >> b) & 1u || (ch && rsg.sh && !(b & 1u)))
                        hit |= 2u;
                }
            }
            rs_hit(t, &t->step[i], hit, b % 4u == 2u);
            if (sec == RS_BUILD && hit) t->step[i].vel = (uint8_t)(50 + (i - L / 2u) * 140u / L);
            if (hit == 2u && (b & 1u) && sec != RS_BUILD && i < 32u) {   /* (the shaker's off 16ths: left, right) */
                t->step[i].vel = 72;
                motion_set_lock(t, i, P_PAN, (int16_t)(side * (int32_t)rsg.ppan));
                side = -side;
            } else if (hit == 1u && sec == RS_BREAK) {    /* (the fill: left to right) */
                motion_set_lock(t, i, P_PAN, (int16_t)(-48 + (int32_t)(i - (L - 8u)) * 16));
            }
        }
        break;
    }
    default: {                                            /* item 4, the lead: its hook (rs_motif_make) sung over each bar,
                                                           * moved to the bar's chord, and the phrase's last bar answering */
        const rs_motif_t *m = &rsg.mot[sec == RS_CHORUS];
        uint32_t bars = L / 16u, bar;
        int32_t anchor = 7 + (sec == RS_CHORUS ? 2 : 0) + (rsg.v2 ? 2 : 0), hi = rsg.lead_up ? 12 : 0;
        if (sec == RS_INTRO || sec == RS_BUILD || (nth && sec != RS_CHORUS))
            break;
        if (sec == RS_BREAK) {                            /* the break: the hook's first note, held long */
            uint8_t n = (uint8_t)bs_deg(rsg.sc, rsg.tonic + 12 + hi, anchor + rs_chord(sec, 0) % 7 + m->c[0]);
            while (n > 98) n -= 12;
            bs_note(&t->step[0], &n, 1, 96, 0);
            for (i = 1; i < 12u; i++) bs_tie(&t->step[i]);
            break;
        }
        for (bar = 0; bar < bars; bar++) {
            uint32_t answer = bar == bars - 1u, b;
            int32_t ch0 = rs_chord(sec, bar * 16u), j, best = anchor;
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
#undef S_
    static const rs_snd_t (*const BY[ST_N])[10] = {CHIP, CHILL, ELECTRO, LOFI};
    uint32_t tries;
    for (tries = 0; tries < 4u; tries++) {
        const rs_snd_t *s = &BY[rsg.sty % ST_N][role % RR_N][bs_in(10)];
        if (rs_named(t, s->e, s->name))
            return;
    }
    rs_sound(t, cat);
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
    /* the arrangements (item 3): {pattern, 4-bar blocks, the tracks left out (bit k: T1 drums, chords, bass, lead,
     * PERC)}; then F put in: VERSE 2 for the second verse on, LIFT for the last chorus, OUTRO at the end */
    enum { I_ = RS_INTRO, V_ = RS_VERSE, C_ = RS_CHORUS, D_ = RS_BREAK, B_ = RS_BUILD };
    static const uint8_t ARRS[5][12][3] = {
        {{I_, 1, 0}, {V_, 1, 8}, {V_, 1, 0}, {C_, 2, 0}, {V_, 1, 0}, {B_, 1, 0}, {C_, 2, 0}, {D_, 1, 0}, {C_, 2, 0}},
        {{I_, 1, 0}, {V_, 2, 0}, {B_, 1, 0}, {C_, 2, 0}, {D_, 1, 0}, {C_, 2, 0}},
        {{I_, 1, 8}, {V_, 1, 17}, {V_, 1, 0}, {C_, 1, 0}, {C_, 1, 8}, {V_, 1, 0}, {B_, 1, 0}, {C_, 2, 0}, {D_, 1, 0},
         {B_, 1, 0}, {C_, 2, 0}},
        {{I_, 2, 0}, {V_, 1, 25}, {V_, 1, 8}, {V_, 2, 0}, {C_, 2, 0}, {D_, 2, 0}, {C_, 2, 0}},
        {{I_, 1, 0}, {B_, 1, 0}, {C_, 2, 0}, {V_, 2, 0}, {B_, 1, 0}, {C_, 1, 1}, {C_, 2, 0}, {D_, 1, 0}, {C_, 2, 0}},
    };
    static const struct {                                 /* the styles: a tempo range (lo + 2 x 0..n-1), a swing range
                                                           * (lo + 0..sw-1), DRUMS' kits, PERC's (CK_PRESETS' numbers) */
        uint8_t bpm, n, swing, sw, kit[4], pkit[4];
    } STYLE[ST_N] = {
        /* (Spelunkr: the drums the sampled kits, CK_PRESETS 40.. CRATE BEDROOM VAPOR 808 LINN GATED NES GAMEBOY SNES) */
        {116, 18, 0, 1, {45, 46, 47, 16}, {32, 26, 13, 27}},     /* CHIPTUNE 116..150: NES GAMEBOY SNES GLITCH HOP */
        {70, 11, 8, 18, {42, 41, 42, 17}, {31, 22, 5, 1}},       /* CHILLWAVE 70..90: VAPOR 808 BEDROOM VAPOR 808 CHILLWAVE */
        {96, 15, 0, 14, {43, 44, 42, 8}, {26, 15, 34, 7}},       /* ELECTRO / 808 96..124: LINN GATED VAPOR 808 ELECTRO */
        {72, 11, 28, 30, {40, 41, 40, 6}, {38, 10, 29, 21}},     /* LO-FI 72..92, swung: CRATE BEDROOM CRATE BOOM BAP */
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
    sty = bs_in(ST_N);
    rsg.sty = (uint8_t)sty;
    song.g[G_BPM] = (int16_t)(STYLE[sty].bpm + 2 * bs_in(STYLE[sty].n));   /* 70..150 by the style */
    song.g[G_SWING] = (int16_t)(STYLE[sty].swing + bs_in(STYLE[sty].sw));
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
    rsg.sh = (uint8_t)bs_in(2);
    rsg.ppan = (uint8_t)(32 + bs_in(25));
    rsg.hpan = (uint8_t)(14 + bs_in(19));
    {                                                     /* the style's grammar (rs_part): rolled within its own choices */
        static const uint8_t HALF[ST_N] = {0, 75, 30, 10}, HAT[ST_N][4] = {{1, 1, 3, 0}, {0, 2, 3, 0}, {1, 3, 1, 0}, {0, 0, 3, 2}};
        static const uint8_t VOICE[ST_N][4] = {{0, 0, 0, 0}, {1, 2, 1, 2}, {0, 0, 1, 1}, {1, 2, 2, 1}};
        static const uint8_t CRHY[ST_N][4] = {{CR_ARP, CR_ARP, CR_STAB, CR_HELD}, {CR_HELD, CR_HELD, CR_PUSH, CR_HELD},
                                              {CR_STAB, CR_OFF, CR_OFF, CR_HELD}, {CR_PUSH, CR_HELD, CR_PUSH, CR_STAB}};
        static const uint8_t BMODE[ST_N][4] = {{BM_OCTAVE, BM_OCTAVE, BM_GROOVE, BM_KICK}, {BM_KICK, BM_808, BM_OCTAVE, BM_GROOVE},
                                               {BM_808, BM_808, BM_KICK, BM_GROOVE}, {BM_KICK, BM_WALK, BM_WALK, BM_GROOVE}};
        static const uint8_t RHY[ST_N][4] = {{4, 5, 4, 2}, {0, 1, 2, 3}, {6, 7, 6, 3}, {0, 1, 2, 3}};
        uint32_t r;
        rsg.half = (uint8_t)bs_chance(HALF[sty]);
        rsg.snare = (uint8_t)(sty == ST_ELECTRO ? (bs_chance(60) ? 4u : 6u) : sty == ST_CHILL && bs_chance(50) ? 6u : 2u);
        rsg.hat = HAT[sty][bs_in(4)];
        rsg.oh = (uint8_t)(sty == ST_CHIP ? 0u : bs_in(3));
        rsg.voice = VOICE[sty][bs_in(4)];
        rsg.crhy = CRHY[sty][bs_in(4)];
        rsg.bmode = BMODE[sty][bs_in(4)];
        rsg.lead_up = (uint8_t)(sty == ST_CHIP);
        rsg.rhythm[0] = rsg.rhythm[1] = RHY[sty][bs_in(4)];
        for (r = 0; r < 4u && rsg.rhythm[1] == rsg.rhythm[0]; r++)
            rsg.rhythm[1] = RHY[sty][bs_in(4)];
        if (rsg.rhythm[1] == rsg.rhythm[0]) rsg.rhythm[1] = (uint8_t)(sty == ST_CHIP ? 5u : sty == ST_ELECTRO ? 7u : (rsg.rhythm[0] + 1u) % 4u);
        rs_drums_make();
        {                                                 /* item 3: the chords' pace (the section 2 or 4 bars), F's kind */
            static const uint8_t SLOW[ST_N] = {30, 75, 50, 60}, FXW[ST_N][3] = {{60, 15, 25}, {15, 45, 40}, {35, 30, 35}, {10, 40, 50}};
            uint32_t x = bs_in(100);
            rsg.hr = (uint8_t)(bs_chance(SLOW[sty]) ? 16u : 8u);
            rsg.len = (uint8_t)(rsg.hr * 4u);
            rsg.fx = (uint8_t)(x < FXW[sty][0] ? FX_LIFT : x < FXW[sty][0] + FXW[sty][1] ? FX_OUTRO : FX_VERSE2);
            rsg.v2 = 0;
        }
        rs_motif_make(&rsg.mot[0], rsg.rhythm[0], 0);    /* item 4: the hooks, the verse's and the chorus's */
        rs_motif_make(&rsg.mot[1], rsg.rhythm[1], 1);
    }
#if SPK_NU                                                /* Spelunkr's web build: VAPOR on the master (felucca_web.c
                                                           * vap_get: G_DRLVL = SLOWED << 8 | WOW, G_DRREV = VINYL << 8 |
                                                           * CRUSH): chillwave often slowed and warped, lo-fi often dusty */
    {
        uint32_t sl = 0, wo = 0, vi = 0, cr = 0;
        if (sty == ST_CHILL && bs_chance(55)) { sl = 18 + bs_in(30); wo = 15 + bs_in(30); vi = bs_in(2) ? 20 + bs_in(25) : 0u; }
        if (sty == ST_LOFI && bs_chance(60)) { vi = 25 + bs_in(30); wo = bs_in(2) ? 8 + bs_in(15) : 0u; }
        if (sty == ST_CHIP && bs_chance(15)) cr = 20 + bs_in(30);
        song.g[G_DRLVL] = (int16_t)(sl << 8 | wo);
        song.g[G_DRREV] = (int16_t)(vi << 8 | cr);
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
        uint32_t kind = sec;
        int32_t tonic = rsg.tonic;
        if (sec == RS_EXTRA) {                            /* F: the style's extra */
            kind = rsg.fx == FX_LIFT ? RS_CHORUS : rsg.fx == FX_OUTRO ? RS_INTRO : RS_VERSE;
            if (rsg.fx == FX_LIFT) rsg.tonic += 2;        /* (the last chorus a tone up) */
            rsg.v2 = (uint8_t)(rsg.fx == FX_VERSE2);
        }
        f = motion_guard();
        memset(&motion, 0, sizeof motion);                 /* (each pattern its own locks: the pans) */
        motion_unguard(f);
        memset(seen, 0, sizeof seen);
        for (k = 0; k < NTRK; k++) {
            motion_clear(&trk[k]);
            rs_part(&trk[k], cls[k], seen[cls[k]]++, kind);
        }
        rsg.tonic = tonic;
        rsg.v2 = 0;
        motion_set_enabled(&trk[0], 1);
        motion_set_enabled(&trk[1], 1);                   /* (the build's swell: LEVEL locks) */
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
    {                                                     /* the arrangement: a template, F put in, the drops */
        const uint8_t (*arr)[3] = ARRS[bs_in(5)];
        uint32_t n = 0, r, verses = 0, lastc = 0, per = 64u / rsg.len;
        for (r = 0; r < 12u && arr[r][1]; r++, n++) {
            chain_row_t *row = &chain_config.row[n];
            uint32_t p = arr[r][0], blocks = arr[r][1], drop = arr[r][2];
            if (p == RS_VERSE && verses++ && rsg.fx == FX_VERSE2) p = RS_EXTRA;
            if ((p == RS_VERSE || p == RS_CHORUS) && r && arr[r + 1u][1]) {   /* (a little of its own: lengths, drops) */
                if (blocks == 2u && bs_chance(30)) blocks = 1;
                else if (blocks == 1u && p == RS_CHORUS && bs_chance(25)) blocks = 2;
                if (p == RS_VERSE && !drop && bs_chance(20)) drop = 16u;   /* (a verse without PERC) */
            }
            if (p == RS_CHORUS) lastc = n;
            for (k = 0; k < NTRK; k++)
                row->slot[k] = (uint8_t)((drop >> k) & 1u ? CHAIN_SILENT : chain_val(p));
            row->repeat = (uint8_t)(blocks * per);
        }
        if (rsg.fx == FX_VERSE2 && verses < 2u && n < CHAIN_ROWS) {   /* (one verse row only: its second half F) */
            for (r = 0; r < n && chain_pat(chain_config.row[r].slot[1]) != RS_VERSE; r++)
                ;
            if (r < n) {
                uint32_t h = chain_config.row[r].repeat;
                memmove(&chain_config.row[r + 1u], &chain_config.row[r], (n - r) * sizeof chain_config.row[0]);
                n++;
                if (lastc >= r) lastc++;
                if (h < 2u) h = 2u;
                chain_config.row[r].repeat = (uint8_t)(h / 2u);
                chain_config.row[r + 1u].repeat = (uint8_t)(h - h / 2u);
                for (k = 0; k < NTRK; k++)
                    if (chain_config.row[r + 1u].slot[k] != CHAIN_SILENT) chain_config.row[r + 1u].slot[k] = (uint8_t)chain_val(RS_EXTRA);
            }
        }
        if (rsg.fx == FX_LIFT)                            /* (the last chorus: F, a tone up) */
            for (k = 0; k < NTRK; k++)
                if (chain_config.row[lastc].slot[k] != CHAIN_SILENT) chain_config.row[lastc].slot[k] = (uint8_t)chain_val(RS_EXTRA);
        if (rsg.fx == FX_OUTRO && n < CHAIN_ROWS) {       /* (the outro after it all) */
            for (k = 0; k < NTRK; k++)
                chain_config.row[n].slot[k] = (uint8_t)chain_val(RS_EXTRA);
            chain_config.row[n++].repeat = (uint8_t)per;
        }
        chain_config.count = (uint8_t)n;
    }
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
