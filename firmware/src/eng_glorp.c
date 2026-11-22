/* SPDX-License-Identifier: GPL-3.0-only
 * Phantom Tracker: GLORP (28), the "glorpy" frog synth (Ed, Nov 2026: "the viral glorpy frog synth effect", "an almost
 * automatic wah effect that opens and closes based on how hard the keys are pressed"; he picked sketch F of six,
 * tools/glorp_proto.py). A vowel wah on every note: the mouth rests on OO, opens on the note towards VOWEL's vowel
 * (OH .. AH .. EH .. EE) and closes back, as far and as slowly as the note is hard (its velocity; SENS 0: every note
 * as a firm one, for the FM-1's own keys, which have one velocity); the hardest notes open past VOWEL by up to one
 * vowel. The pitch dips into the note from above (DIP), the source is a glottal pulse with a saw (BUZZ), the three
 * formants are resonant band-passes (Chamberlin's state-variable filter, as the sketch) in parallel, 1, 0.6, 0.25,
 * their Q rising with the velocity, and the sum is driven into a tanh (GLOOP: the resonance and the drive).
 *
 * State: ph[0] the phase; s[2] the opening's attack (1 - e^-t/A), s[3] its fall (e^-t/D), s[6] the dip (e^-t/25 ms),
 * all Q24, per control block (the slots a retrigger does not restore: every note opens again); the three filters' low
 * and band (Q12) in s[0] s[1], s[4] s[5], ph[1] ph[2] (kept over a retrigger), s[7] the DC blocker (x 256). The formants move once a block (0.7 ms) */
#define GLO_POLY 4u

/* the vowels' F1 F2 F3 (Hz, Peterson & Barney's male averages, as the sketch): OO OH AH EH EE */
static const uint16_t GLO_VOW[5][3] = {
    {300, 870, 2240}, {450, 800, 2830}, {730, 1090, 2440}, {530, 1840, 2480}, {270, 2290, 3010},
};
static const uint8_t GLO_FQ[3] = {10, 12, 14};          /* the formants' Q at velocity 0 (+6 at 127 for F1 and F2) */
static const uint8_t GLO_FG[3] = {128, 77, 32};         /* their gains, Q7: 1, 0.6, 0.25 */
static const char *const N_GLO_VOW[6] = {"OO", "OH", "AH", "EH", "EE", 0};

/* VOWEL's place on OO OH AH EH EE, Q8 (0 .. 4 x 256), and the formant k there */
static int32_t glo_vowel_f(int32_t pos, uint32_t k)
{
    int32_t i = clamp(pos, 0, 4 * 256 - 1) >> 8, fr = clamp(pos, 0, 4 * 256) - i * 256;
    return GLO_VOW[i][k] + (((GLO_VOW[i + (i < 4)][k] - GLO_VOW[i][k]) * fr) >> 8);
}
/* the note's velocity as the wah takes it, Q15: SENS 127 the velocity itself, 0 a firm 0.8 for every note */
static int32_t glo_vel(const int16_t *p, const voice_t *v)
{
    int32_t vq = (v->mvel ? v->mvel : v->vel) * 258, sens = clamp(p[P_E6], 0, 127);
    return 26214 + (((vq - 26214) * sens) / 127);
}

static void glo_note_on(track_t *t, voice_t *v)
{
    (void)t;
    if (!v->env && !v->env_out) {                       /* a fresh voice (not a retrigger): the filters from rest */
        v->ph[0] = v->ph[1] = v->ph[2] = 0;
        v->s[0] = v->s[1] = v->s[4] = v->s[5] = v->s[7] = 0;
    }
    v->s[2] = 0;                                        /* every note opens the mouth again (s[2], s[3], s[6]: the */
    v->s[3] = 1 << 24;                                  /* slots voice.c does not put back on a retrigger) */
    v->s[6] = 1 << 24;
}

/* the mouth now (voice v's opening, s[2] s[3]): the three formants in Hz, and the note's velocity as the wah takes it */
static int32_t glo_formants(const int16_t *p, const voice_t *v, int32_t *f)
{
    int32_t vs = glo_vel(p, v), o = (int32_t)(((int64_t)v->s[2] * v->s[3]) >> 33), op, pos, k;   /* Q15 */
    op = (int32_t)(((int64_t)o * ((vs * 39322) >> 15 > 32767 ? 32767 : (vs * 39322) >> 15)) >> 15);   /* x min(1, 1.2 v) */
    op = (op * clamp(p[P_E1], 0, 127)) / 127;           /* x WAH */
    pos = ((clamp(p[P_E0], 0, 127) * 1024) / 127)       /* VOWEL, and the hardest notes past it */
          + (vs > 24576 ? ((vs - 24576) * 256) >> 13 : 0);
    for (k = 0; k < 3; k++) {
        int32_t u = GLO_VOW[0][k];
        f[k] = clamp(u + (((glo_vowel_f(pos, (uint32_t)k) - u) * op) >> 15), 80, 7350);   /* (Chamberlin's: stable to FS / 6) */
    }
    return vs;
}

static void glo_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    int32_t vs = glo_vel(p, v), sp = 191 - clamp(p[P_E2], 0, 127), gl = clamp(p[P_E3], 0, 127);
    int32_t at = v->s[2], de = v->s[3], dip = v->s[6], k, f[3], Fc[3], Dc[3], G[3];
    int32_t ms_a = 1 + (clamp(p[P_E7], 0, 127) * clamp(p[P_E7], 0, 127)) / 200;   /* the opening: 1 .. 81 ms */
    int32_t ms_d = ((70 + ((200 * vs) >> 15)) * sp * sp) / (127 * 127);           /* the closing: (70 + 200 v) ms x
                                                         * SPEED's 0.25 .. 2.26 */
    int32_t qm = 128 + gl * 2, drive = (20 * qm) >> 8;  /* GLOOP (Q8, 1.0 at 64): Q x 0.5 .. 1.5, the drive 2.5 x the same (y Q12 -> Q15: x 8) */
    int32_t dc = v->s[7], buzz = clamp(p[P_E5], 0, 127) * 64, l1 = v->s[0], b1 = v->s[1], l2 = v->s[4], b2 = v->s[5];
    int32_t l3 = (int32_t)v->ph[1], b3 = (int32_t)v->ph[2];
    uint32_t i, ph = v->ph[0], inc;
    if (n > CTL)
        n = CTL;
    /* the opening (per block): attack towards 1, the fall towards 0, the dip's fall (25 ms) */
    at += (int32_t)((((int64_t)((1 << 24) - at)) * ((1u << 29) / (uint32_t)(ms_a * 44 + 1))) >> 24);
    de -= (int32_t)(((int64_t)de * ((1u << 29) / (uint32_t)((ms_d < 4 ? 4 : ms_d) * 44 + 1))) >> 24);
    dip -= (int32_t)(((int64_t)dip * ((1u << 29) / 1103u)) >> 24);
    v->s[2] = at;
    v->s[3] = de;
    v->s[6] = dip;
    glo_formants(p, v, f);
    for (k = 0; k < 3; k++) {
        int32_t q = ((GLO_FQ[k] * 256 + (k < 2 ? (6 * 256 * vs) >> 15 : 0)) * qm) >> 8;   /* Q x 256 */
        Fc[k] = sine_i((uint32_t)f[k] * 48696u) >> 2;   /* 2 sin(pi f / FS), Q12 (sine_i Q15: x 2 / 8) */
        Dc[k] = (4096 * 256) / (q < 256 ? 256 : q);     /* 1 / Q, Q12 */
        G[k] = GLO_FG[k];
    }
    /* the pitch: the dip into the note, 0 .. +70 % of the pitch, harder further */
    inc = m->inc + (uint32_t)(((uint64_t)m->inc * (uint32_t)((((clamp(p[P_E4], 0, 127) * 361 * (vs >> 3)) >> 12) *
                                                              (dip >> 9)) >> 15)) >> 16);
    for (i = 0; i < n; i++) {
        int32_t g = 0, x, h, y;
        if (ph < 1717986918u) {                         /* the glottal pulse: sin^2 over the period's first 40 % */
            int32_t s = sine_i(ph + (ph >> 2));
            g = (s * s) >> 15;
        }
        x = ((g - 8192) >> 3) + ((osc_saw(ph, inc) * buzz) >> 16);   /* Q12: the pulse - 0.25 (1.0 = 4096), the saw x BUZZ */
        h = x - l1 - ((Dc[0] * b1) >> 12); b1 = clamp(b1 + ((Fc[0] * h) >> 12), -250000, 250000); l1 = clamp(l1 + ((Fc[0] * b1) >> 12), -250000, 250000);
        h = x - l2 - ((Dc[1] * b2) >> 12); b2 = clamp(b2 + ((Fc[1] * h) >> 12), -250000, 250000); l2 = clamp(l2 + ((Fc[1] * b2) >> 12), -250000, 250000);
        h = x - l3 - ((Dc[2] * b3) >> 12); b3 = clamp(b3 + ((Fc[2] * h) >> 12), -250000, 250000); l3 = clamp(l3 + ((Fc[2] * b3) >> 12), -250000, 250000);
        y = (b1 * G[0] + b2 * G[1] + b3 * G[2]) >> 7;  /* Q12 */
        y = softclip(clamp(y * drive, -(1 << 24), 1 << 24));   /* tanh(2.5 y) */
        dc += (y - (dc >> 8)) ;                         /* the DC the pulse's offset leaves through the tanh: ~27 Hz */
        y -= dc >> 8;
        ph += inc;
        out[i] += voice_amp(y, m, i);
    }
    v->ph[0] = ph;
    v->ph[1] = (uint32_t)l3;
    v->ph[2] = (uint32_t)b3;
    v->s[0] = l1; v->s[1] = b1; v->s[4] = l2; v->s[5] = b2;
    v->s[7] = dc;
}

/* {VOWEL WAH SPEED GLOOP DIP BUZZ SENS ATTACK}, the ADSR, FENV, MONO, the sends, a pattern */
static const preset_t GLO_PRESETS[] = {
    {"GLORP", {64, 127, 64, 64, 64, 64, 127, 52}, {0, 80, 100, 24}, 0, 1, FX(0, 0, 20, 20), PAT(2)},
    {"FROG BASS", {48, 110, 50, 84, 90, 48, 127, 40}, {0, 70, 90, 18}, 0, 1, FX(10, 0, 0, 10), PAT(2)},
    {"TOAD", {30, 127, 40, 110, 110, 30, 127, 30}, {0, 60, 70, 14}, 0, 1, FX(20, 0, 10, 10), PAT(1)},
    {"GLOOP LEAD", {84, 100, 72, 70, 40, 84, 110, 52}, {0, 90, 110, 40}, 0, 1, FX(0, 20, 40, 40), PAT(4)},
    {"BUBBLE KEYS", {100, 90, 36, 60, 30, 30, 127, 30}, {0, 70, 60, 40}, 0, 0, FX(0, 30, 40, 50), PAT(6)},
    {"WAH CHORDS", {72, 120, 90, 50, 10, 90, 100, 70}, {0, 90, 100, 50}, 0, 0, FX(0, 40, 30, 60), PAT(6)},
    {"OOZE", {40, 80, 120, 40, 0, 40, 60, 127}, {60, 100, 110, 80}, 0, 0, FX(0, 60, 40, 90), PAT(5)},
    {"SWAMP TALK", {127, 127, 80, 90, 70, 100, 127, 60}, {0, 80, 100, 30}, 0, 1, FX(10, 10, 30, 30), PAT(3)},
};
static const engine_t ENG_GLORP = {
    .name = "GLORP",
    .page_title = {"GLORP", "THROAT"},
    .edit = {
        {"VOWEL", F_PCT, 0, 127, 64, 0, 0},
        {"WAH", F_PCT, 0, 127, 127, 0, 0},
        {"SPEED", F_PCT, 0, 127, 64, 0, 0},
        {"GLOOP", F_PCT, 0, 127, 64, 0, 0},
        {"DIP", F_PCT, 0, 127, 64, 0, 0},
        {"BUZZ", F_PCT, 0, 127, 64, 0, 0},
        {"SENS", F_PCT, 0, 127, 127, 0, 0},
        {"ATTACK", F_PCT, 0, 127, 52, 0, 0},
    },
    .presets = GLO_PRESETS,
    .npresets = NELEM(GLO_PRESETS),
    .note_on = glo_note_on,
    .render = glo_render,
    .knob = {P_E0, P_E1, P_E2, P_E3},                     /* HOME: VOWEL WAH SPEED GLOOP */
    .poly = GLO_POLY,
    .keep = 0xB3,                                         /* (a retrigger keeps the filters and the DC: no click) */
};
