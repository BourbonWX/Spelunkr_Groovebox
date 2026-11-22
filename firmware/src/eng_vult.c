/* SPDX-License-Identifier: GPL-3.0-only
 * Phantom Tracker: VULT (29) (Ed, Nov 2026: "add the Vult engine"). A voice built from Vult's open DSP (the Vult
 * language's examples, MIT, Copyright (C) 2017 Leonardo Laguna Ruiz: github.com/vult-dsp/vult examples/osc/phd.vult,
 * effects/rescomb.vult, filters/ladder.vult, effects/saturate_soft.vult, util/util.vult), ported by hand to this
 * firmware's fixed point (Q16 as Vult's own "-real fixed"; the ladder and the clippers in 32-bit products):
 *   1. the phase-distortion oscillator (Phd): a sine whose phase runs SHAPE semitones (0 .. 60) faster than the note
 *      and restarts with it, faded out over each period: CZ's "resonance" sweep
 *   2. the resonant comb (Rescomb): a delay of half the period of the note + TUNE, fed back through a DC blocker and
 *      Vult's soft saturation; TONE sqrt-curved, negative for the hollow odd-harmonic comb; COMB the feedback
 *   3. the diode ladder (Ladder, Euler, four steps a sample): four cubic-clipped one-poles with feedback, CUTOFF (with
 *      ENV / LFO -> FILTER and KEYTRK) and RESO (self-oscillating at the top); DRIVE pushes it into its clippers
 *   4. the soft saturation out (Saturate_soft: 16 tanh(x / 16))
 * Vult's notice (the MIT licence, LICENSES/MIT-Vult.txt): "Copyright (c) 2017 Leonardo Laguna Ruiz. Permission is
 * hereby granted, free of charge, to any person obtaining a copy of this software ... The above copyright notice and
 * this permission notice shall be included in all copies or substantial portions of the Software."
 * State: ph[0] the oscillator's phase, ph[1] the distorted phase, ph[2] the comb's write position; s[0..3] the ladder,
 * s[4] the comb's output, s[5] s[6] its DC blocker, s[7] the output's; the comb's line per voice in vlt_buf (int16, Q14) */
/* (VLT_POLY, VLT_N and vlt_buf are in eng_cycle.c: the kit's COMB voices share the lines; a part plays one engine) */

static inline int32_t vlt_cc(int32_t x)                 /* Util.cubic_clipper: x - x^3 / 3 within +-2/3, Q16 */
{
    int32_t x2, x3;
    if (x <= -43690)
        return -43690;
    if (x >= 43690)
        return 43690;
    x2 = ((x >> 1) * (x >> 1)) >> 14;
    x3 = ((x2 >> 1) * (x >> 1)) >> 14;
    return x - ((x3 * 21845) >> 16);
}
/* Ladder.process_euler: four Euler steps of the diode ladder (L[0..3] its stages, Q16), input c, step fh (Q16), RESO
 * res (Q16); returns the last stage */
static inline int32_t vlt_ladder(int32_t *L, int32_t c, uint32_t fh, int32_t res)
{
    int32_t k, p0 = L[0], p1 = L[1], p2 = L[2], p3 = L[3], f2 = (int32_t)(fh >> 2);
    for (k = 0; k < 4; k++) {
        int32_t w0 = vlt_cc(c - ((((res >> 4) * p3) >> 12) << 2)), w1 = vlt_cc(p0), w3 = vlt_cc(p1);
        int32_t w5 = vlt_cc(p2), w7 = vlt_cc(p3);
        p0 += (((w0 - w1) >> 2) * f2) >> 12;
        p1 += (((w1 - w3) >> 2) * f2) >> 12;
        p2 += (((w3 - w5) >> 2) * f2) >> 12;
        p3 += (((w5 - w7) >> 2) * f2) >> 12;
    }
    L[0] = p0; L[1] = p1; L[2] = p2; L[3] = p3;
    return p3;
}
/* Ladder.tune: the step for the cutoff cut (0 .. 127 x 256, Vult's CV 0 .. 1: note 24 + 120 cv), 2 pi f / (4 x 44.1 kHz),
 * Q16 */
static uint32_t vlt_fh(int32_t cut)
{
    uint32_t n = 24u * 16u + (((uint32_t)clamp(cut, 0, 127 << 8) * 1935u) >> 15), fh;   /* (x 120 x 16 / (127 x 256)) */
    fh = pitch_inc(n > 2047u ? 2047u : n);
    fh = ((fh >> 16) * 102944u) >> 16;                  /* inc x (pi / 2) / 2^32 */
    return fh > 40000u ? 40000u : fh;
}
static int16_t *vlt_line(const track_t *t, const voice_t *v)   /* voice v's comb line, 0 = none (PHANTOM, past the cap) */
{
    uintptr_t a = (uintptr_t)t;
    uint32_t k = (uint32_t)(v - t->v);
    if (a < (uintptr_t)&trk[0] || a >= (uintptr_t)&trk[NPART - 1u] || k >= VLT_POLY)   /* (0.10: PERC plays kits only) */
        return 0;
    return vlt_buf[t - trk][k];
}

static void vlt_note_on(track_t *t, voice_t *v)
{
    if (!v->env && !v->env_out) {                       /* a fresh voice: the filters and the line from rest */
        int16_t *b = vlt_line(t, v);
        uint32_t i;
        v->ph[0] = v->ph[1] = v->ph[2] = 0;
        for (i = 0; i < 8u; i++)
            v->s[i] = 0;
        if (b)
            for (i = 0; i < VLT_N; i++)
                b[i] = 0;
    }
}

static void vlt_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    int16_t *line = vlt_line(t, v);
    int32_t cut = (clamp(p[P_E0], 0, 127) << 8) + m->cutoff + ((clamp(p[P_E7], 0, 127) * (v->pitch16 - 60 * 16)) >> 4);
    int32_t res = (clamp(p[P_E1], 0, 127) * 72000) / 127;   /* RESO 0 .. 1.1 (self-oscillation from ~1), Q16 */
    int32_t fb = (clamp(p[P_E2], 0, 127) * 63570) / 127;    /* COMB: the feedback 0 .. 0.97, Q16 */
    int32_t gain = 65536 + clamp(p[P_E3], 0, 127) * 2064;   /* DRIVE: x 1 .. 5 into the ladder */
    int32_t tone = clamp(p[P_E5], -64, 63), stone;
    int32_t shift = (clamp(p[P_E4], 0, 127) * 960) / 127 + ((m->shape - (64 << 8)) * 15 >> 8);   /* SHAPE: 0 .. 60 st, 1/16 st */
    uint32_t inc = m->inc, inc2 = pitch_inc(clamp(m->pitch16 + clamp(shift, 0, 960), 0, 2047));
    uint32_t incc = pitch_inc(clamp(m->pitch16 + clamp(p[P_E6], -24, 24) * 16, 0, 2047)), per, fh;
    uint32_t i, ph = v->ph[0], ph2 = v->ph[1], wp = v->ph[2] % VLT_N;
    int32_t L[4] = {v->s[0], v->s[1], v->s[2], v->s[3]}, co = v->s[4], dx = v->s[5], dy = v->s[6], dc = v->s[7];
    if (n > CTL)
        n = CTL;
    fh = vlt_fh(cut);                                   /* the ladder's step */
    /* the comb: half the period of the note + TUNE, Q8 samples (2^31 / inc), within the line */
    per = (incc >> 8) ? (0x80000000u / (incc >> 8)) : (VLT_N - 2u) << 8;
    if (per > ((VLT_N - 2u) << 8))
        per = (VLT_N - 2u) << 8;
    if (per < (2u << 8))
        per = 2u << 8;
    stone = (int32_t)vlt_isqrt((uint32_t)(tone < 0 ? -tone : tone) * 67108864u);   /* Rescomb.toneCurve: sqrt(|TONE| / 64), Q16 */
    stone = tone < 0 ? -stone : stone;
    for (i = 0; i < n; i++) {
        int32_t o, x, c, y;
        /* 1 the oscillator (Phd): the distorted phase restarts as the note's wraps */
        if (ph + inc < ph)
            ph2 = 0;
        else
            ph2 += inc2;
        ph += inc;
        o = (sine_i(ph2) * (int32_t)(65535u - (ph >> 16))) >> 15;   /* sin x (1 - phase), Q16 */
        x = vlt_sat(o);
        /* 2 the comb (Rescomb) */
        if (line) {
            uint32_t r, t1, t2;
            int32_t fr, d, fbk, si;
            fbk = (int32_t)(((int64_t)co * fb) >> 16);
            dy = fbk - dx + dy - ((dy >> 8) * 328 >> 8);   /* Util.dcblock: y = x - x1 + 0.995 y1 */
            dx = fbk;
            si = vlt_sat(x + dy);
            wp = wp + 1u >= VLT_N ? 0u : wp + 1u;       /* (Rescomb.delay: the position moves, the input is written, */
            line[wp] = (int16_t)clamp(si >> 2, -32768, 32767);   /* then read a half period back) */
            r = ((wp << 8) + (VLT_N << 8) - per) % (VLT_N << 8);
            t1 = r >> 8;
            t2 = t1 + 1u >= VLT_N ? 0u : t1 + 1u;
            fr = (int32_t)(r & 255u);
            d = (line[t1] + (((line[t2] - line[t1]) * fr) >> 8)) << 2;
            co = x + (int32_t)(((int64_t)stone * d) >> 16);
            c = vlt_sat(co);
        } else {
            c = x;
        }
        /* 3 the ladder (Ladder.process_euler: four steps), driven */
        y = vlt_ladder(L, (int32_t)(((int64_t)c * gain) >> 16), fh, res);
        /* 4 out: Saturate_soft(y x 1.5), Q16 -> Q15 */
        y = vlt_sat(y + (y >> 1)) >> 1;
        dc += y - (dc >> 9);                            /* (the PD wave's DC: out at ~14 Hz) */
        y -= dc >> 9;
        y = voice_amp(clamp(y, -32767, 32767), m, i);
        out[i] += y + (y >> 1);                         /* (x 1.5: about MORPH's level) */
    }
    v->ph[0] = ph;
    v->ph[1] = ph2;
    v->ph[2] = wp;
    v->s[0] = L[0]; v->s[1] = L[1]; v->s[2] = L[2]; v->s[3] = L[3];
    v->s[4] = co; v->s[5] = dx; v->s[6] = dy; v->s[7] = dc;
}

/* {CUTOFF RESO COMB DRIVE SHAPE TONE TUNE KEYTRK}, the ADSR, FENV (ENV -> FILTER), MONO, the sends, a pattern */
static const preset_t VLT_PRESETS[] = {
    {"LATERAL BASS", {44, 70, 0, 80, 20, 0, 0, 40}, {0, 64, 80, 16}, 36, 1, FX(10, 0, 0, 10), PAT(2)},
    {"RESO PLUCK", {56, 60, 90, 30, 40, 40, 12, 60}, {0, 70, 0, 40}, 30, 0, FX(0, 20, 40, 40), PAT(7)},
    {"ACID VULT", {36, 104, 0, 70, 10, 0, 0, 30}, {0, 56, 40, 14}, 50, 1, FX(20, 0, 30, 10), PAT(1)},
    {"METAL COMB", {84, 30, 110, 40, 60, -50, 7, 60}, {0, 76, 0, 50}, 10, 0, FX(0, 10, 40, 50), PAT(3)},
    {"CZ SWEEP", {96, 20, 0, 20, 90, 0, 0, 50}, {0, 80, 90, 40}, 20, 1, FX(0, 20, 30, 30), PAT(4)},
    {"HOLLOW PAD", {70, 30, 70, 10, 30, -40, 0, 40}, {90, 100, 110, 100}, 10, 0, FX(0, 60, 30, 90), PAT(5)},
    {"DUST KEYS", {64, 40, 50, 50, 50, 30, 12, 50}, {0, 74, 30, 50}, 20, 0, FX(10, 30, 20, 50), PAT(6)},
    {"SQUELCH LEAD", {60, 96, 30, 90, 70, 20, 12, 60}, {0, 84, 100, 30}, 40, 1, FX(20, 10, 40, 30), PAT(4)},
};
static const engine_t ENG_VULT = {
    .name = "VULT",
    .page_title = {"LADDER", "COMB"},
    .edit = {
        {"CUTOFF", F_PCT, 0, 127, 64, 0, 0},
        {"RESO", F_PCT, 0, 127, 40, 0, 0},
        {"COMB", F_PCT, 0, 127, 0, 0, 0},
        {"DRIVE", F_PCT, 0, 127, 30, 0, 0},
        {"SHAPE", F_PCT, 0, 127, 20, 0, 0},
        {"TONE", F_BIPCT, -64, 63, 0, 0, 0},
        {"TUNE", F_SEMI, -24, 24, 0, 0, 0},
        {"KEYTRK", F_PCT, 0, 127, 40, 0, 0},
    },
    .presets = VLT_PRESETS,
    .npresets = NELEM(VLT_PRESETS),
    .note_on = vlt_note_on,
    .render = vlt_render,
    .knob = {P_E0, P_E1, P_E2, P_E3},                     /* HOME: CUTOFF RESO COMB DRIVE */
    .poly = VLT_POLY,
    .keep = 0xF3,                                         /* (a retrigger keeps the ladder and the comb: no click) */
};
