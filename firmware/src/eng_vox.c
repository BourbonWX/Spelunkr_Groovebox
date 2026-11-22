/* SPDX-License-Identifier: GPL-3.0-only
 * Phantom Cosmonaut: VOX (26), a voice (Ed, Nov 2026: "four knobs control mouth shape, vocal chords, tongue placement
 * ... presets for common English mouth sounds like a vocaloid", "the presets would include our hard consonant sounds").
 * The prototype is tools/vox_proto.py; this is it in fixed point, on Felucca's VOICE engine's resonators (eng_formant.c:
 * Klatt's, Q30) and glottal pulse.
 *
 * The knobs are a mouth, not filters:
 *   MOUTH  the jaw, closed .. wide open    TONGUE  back .. front        LIPS  spread .. rounded
 *   CORDS  the vocal cords: 0 a whisper (no buzz), ~40 breathy, ~76 a plain voice, 127 pressed
 *   EDIT:  NASAL (the velum open: M N NG), TIP (the tongue's tip curled: R, ER), HISS (frication, from the lips' soft F
 *          to the teeth's sharp S: where the hiss is made, and how much), ONSET (a hard consonant at each note's start:
 *          P B T D K G CH J, releasing into the knobs' vowel: "pa", "ba" ..)
 * The mouth sets four formants (fitted to Hillenbrand et al. 1995's adult male vowels, F1 F2 F3; F4 follows):
 *   F1 = 280 + 520 m - 70 l + 30 t     F2 = 870 + 1550 t (1 - m/2) + 380 m (1 - t) - 420 l (1 - 0.4 t)
 *   F3 = 2420 + 520 t - 260 l - 900 tip    F4 = 3400 + 300 t - 200 l - 300 tip   (m t l tip 0..1)
 * The voice is a Rosenberg pulse (its open share and its brightness from CORDS) and the breath through the glottis,
 * into the four resonators in cascade; NASAL damps the mouth's resonances and mixes in the nose: a murmur (260 Hz and
 * 2100 Hz) with an anti-resonance at the closure's place (M ~1000 Hz .. N ~1700 Hz); HISS is noise through a band-pass
 * at the constriction, added after the tract (it is made in front of it). An ONSET is a movement, not a place: the seal
 * (silence, or the voice bar behind closed lips if voiced), the burst (a puff of noise shaped by where the seal was),
 * the aspiration (unvoiced: breath before the voice starts) or, for CH and J, a frication; meanwhile the mouth moves
 * from the seal's place to the knobs' vowel in 50 ms. PRESETS are the phonemes (on HOME they move the mouth only).
 * State: ph[0] the glottal phase, ph[1], ph[2] F4's y1 y2, s[0..5] F1..F3's, s[6] the source's tilt, s[7] the
 * high-pass; the rest (the nose, the hiss's band-pass, the voice's gain, the onset's clock, the noise) per voice in
 * vox_x (a voice of a FEED, or PHANTOM's) */
#define VOX_POLY 3u

#if DT_RETIRE   /* (Felucca's VOICE engine, eng_formant.c, is not built here: its resonators and pitch ratio, as they are) */
static const uint32_t SEMI_Q16[13] = {
    65536, 69433, 73562, 77936, 82570, 87480, 92682, 98193, 104032, 110218, 116772, 123715, 131072,
};
/* 2^(st16 / 192) in Q16 (st16 in 1/16 semitone), clamped to 1/16 .. 16 */
static uint32_t fmt_ratio(int32_t st16)
{
    int32_t o, k;
    uint32_t r;
    st16 = clamp(st16, -4 * 192, 4 * 192 - 1) + 4 * 192;   /* 0 .. 8 octaves */
    o = (st16 * 2731) >> 19;                            /* / 192, exact for 0 .. 1535 */
    k = st16 - o * 192;
    r = SEMI_Q16[k >> 4] + (((SEMI_Q16[(k >> 4) + 1] - SEMI_Q16[k >> 4]) * (uint32_t)(k & 15)) >> 4);
    return o >= 4 ? r << (o - 4) : r >> (4 - o);
}
typedef struct { int32_t a, b, c; } fres_t;
/* Klatt resonator coefficients (Q30) for F, BW in Hz * 16 */
static void fres_coef(fres_t *r, uint32_t f16, uint32_t bw16)
{
    int32_t s = sine_i(f16 * 3044u);                    /* sin(pi F / FS), Q15 (2^32 / (2 FS) / 16) */
    int32_t x = (int32_t)(bw16 * 4781u);                /* pi BW / FS, Q30 (<= 0.2) */
    int32_t x2 = (int32_t)(((int64_t)x * x) >> 30), x3 = (int32_t)(((int64_t)x2 * x) >> 30);
    int32_t x4 = (int32_t)(((int64_t)x3 * x) >> 30);
    int32_t e = (1 << 30) - x + (x2 >> 1) - (int32_t)(((int64_t)x3 * 178956971) >> 30)   /* exp(-x): x^3 / 6, */
                + (int32_t)(((int64_t)x4 * 44739243) >> 30);                            /* x^4 / 24 */
    int32_t cs = (1 << 30) - 2 * s * s;                 /* cos(2 pi F / FS) = 1 - 2 sin^2, Q30 */
    r->b = (int32_t)(((int64_t)e * cs) >> 29);          /* 2 r cos */
    r->c = -(int32_t)(((int64_t)e * e) >> 30);          /* -r^2 */
    r->a = (1 << 30) - r->b - r->c;
}
#endif

typedef struct {
    int32_t m1, m2, m3, m4;      /* the murmur's two resonators (y1 y2 each) */
    int32_t z1, z2;              /* the murmur before the anti-resonance (x1 x2) */
    int32_t b1, b2;              /* the noise's band-pass (transposed direct form II) */
    int32_t gv;                  /* the voice's gain now, Q15 (moves at most 8 ms end to end) */
    int32_t on;                  /* samples since the note began (the ONSET's clock; stops counting at 2^30) */
    int32_t nz;                  /* the noise */
} vox_x_t;
static vox_x_t vox_x[(NPART + 1u) * NVOICE] __attribute__((section(".pool")));   /* (zeroed at boot: main.c) */
/* voice v of track t's own: a FEED's (trk[0..NPART-1]), else PHANTOM's (the last row) */
static vox_x_t *vox_ext(const track_t *t, const voice_t *v)
{
    uintptr_t a = (uintptr_t)t;
    uint32_t p = a >= (uintptr_t)&trk[0] && a < (uintptr_t)&trk[NPART] ? (uint32_t)(t - trk) : NPART;
    return &vox_x[p * NVOICE + (uint32_t)(v - t->v) % NVOICE];
}

/* the hard consonants (vox_proto.py ONSET), in samples at 44.1 kHz: the seal, the burst, the aspiration, CH's and J's
 * frication; voiced; the burst's band (Hz, 0 = from F2, as the tongue's back makes it), its Q (x256) and level (Q15);
 * the seal's place (MOUTH TONGUE LIPS, -1 = the vowel's); the frication's band, Q, level */
typedef struct {
    uint16_t cl, burst, asp, fric;
    uint8_t voiced;
    uint16_t bfc, bq, bamp;
    int8_t loc[3];
    uint16_t ffc, fq, famp;
} vox_onset_t;
#define VOX_MOVE 2205u                                   /* the mouth's move from the seal to the vowel: 50 ms */
static const vox_onset_t VOX_ONSET[8] = {
    {2426, 529, 1985, 0, 0, 900, 154, 18022, {0, -1, 127}, 0, 0, 0},              /* P */
    {2205, 353, 265, 0, 1, 800, 154, 9830, {0, -1, 127}, 0, 0, 0},                /* B */
    {2205, 529, 2426, 0, 0, 4800, 307, 29491, {10, 121, 0}, 0, 0, 0},             /* T */
    {1985, 353, 353, 0, 1, 4200, 307, 14746, {10, 121, 0}, 0, 0, 0},              /* D */
    {2426, 706, 2867, 0, 0, 0, 768, 29491, {6, -1, -1}, 0, 0, 0},                 /* K */
    {2205, 441, 441, 0, 1, 0, 768, 14746, {6, -1, -1}, 0, 0, 0},                  /* G */
    {2205, 265, 0, 3749, 0, 4000, 307, 19661, {10, 108, 51}, 3300, 410, 32767},   /* CH */
    {1985, 265, 0, 2646, 1, 3600, 307, 11469, {10, 108, 51}, 3000, 410, 19661},   /* J */
};
static const char *const N_VOX_ONSET[10] = {"-", "P", "B", "T", "D", "K", "G", "CH", "J", 0};

/* the four formants and bandwidths (Hz x 16) of a mouth (MOUTH TONGUE LIPS TIP, 0..127; NASAL widens F2..F4) */
static void vox_formants(int32_t M, int32_t T, int32_t L, int32_t P, int32_t N, int32_t *f, int32_t *b)
{
    int32_t qm = M * 129, qt = T * 129, ql = L * 129, qp = P * 129, qn = N * 129;   /* Q14 */
    int32_t wide = 16384 + ((40960 * qn) >> 14);           /* x (1 + 2.5 NASAL), Q14 */
    f[0] = 280 * 16 + ((8320 * qm - 1120 * ql + 480 * qt) >> 14);
    f[1] = 870 * 16 + ((24800 * ((qt * (16384 - (qm >> 1))) >> 14)) >> 14) + ((6080 * ((qm * (16384 - qt)) >> 14)) >> 14)
           - ((6720 * ((ql * (16384 - ((qt * 2) / 5))) >> 14)) >> 14);
    f[2] = 2420 * 16 + ((8320 * qt - 4160 * ql - 14400 * qp) >> 14);
    f[3] = 3400 * 16 + ((4800 * qt - 3200 * ql - 4800 * qp) >> 14);
    b[0] = (60 * 16) + ((800 * qm) >> 14);
    b[1] = (((70 * 16) + ((640 * qm) >> 14)) * wide) >> 14;
    b[2] = (110 * 16 * wide) >> 14;
    b[3] = (160 * 16 * wide) >> 14;
}
/* an RBJ band-pass (0 dB at its peak) at fc Hz, Q q (x256): c[0] b0, c[1] a1, c[2] a2, Q28 (b1 = 0, b2 = -b0) */
static void vox_bp(int32_t *c, uint32_t fc, int32_t q)
{
    uint32_t w = (fc < 20000u ? fc : 20000u) * 97391u;   /* 2 pi fc / FS as a phase (2^32 / 44100) */
    int32_t sn = sine_i(w), cs = sine_i(w + 0x40000000u), al, inv;
    al = (sn << 7) / (q > 16 ? q : 16);                   /* sin / (2 Q), Q15 */
    inv = (int32_t)((1u << 30) / (uint32_t)(32768 + al)); /* 1 / (1 + alpha), Q15 */
    c[0] = (al * inv) >> 2;
    c[1] = -((cs * inv) >> 1);
    c[2] = ((32768 - al) * inv) >> 2;
}
static inline int32_t vox_bp_run(const int32_t *c, int32_t x, int32_t *s1, int32_t *s2)
{
    int32_t y = (int32_t)(((int64_t)c[0] * x) >> 28) + *s1;
    *s1 = (int32_t)(((int64_t)-c[1] * y) >> 28) + *s2;
    *s2 = (int32_t)(((int64_t)-c[0] * x - (int64_t)c[2] * y) >> 28);
    return y;
}
static inline int32_t vox_res(const fres_t *r, int32_t x, int32_t *y1, int32_t *y2)
{
    int32_t a = (int32_t)(((int64_t)r->a * x + (int64_t)r->b * *y1 + (int64_t)r->c * *y2 + (1 << 29)) >> 30);
    *y2 = *y1;
    *y1 = a;
    return a;
}

static void vox_note_on(track_t *t, voice_t *v)
{
    vox_x_t *x = vox_ext(t, v);
    if (!v->env && !v->env_out) {                       /* a fresh voice (not a retrigger): from rest */
        uint32_t i;
        v->ph[0] = v->ph[1] = v->ph[2] = 0;
        for (i = 0; i < 8u; i++)
            v->s[i] = 0;
        x->m1 = x->m2 = x->m3 = x->m4 = x->z1 = x->z2 = x->b1 = x->b2 = 0;
        x->gv = 0;
        x->nz = (int32_t)(0x2545F491u ^ ((uint32_t)v->note << 16) ^ (v->age * 2654435761u)) | 1;
    }
    x->on = 0;                                          /* every note a syllable: its ONSET again */
}

static void vox_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    vox_x_t *xs = vox_ext(t, v);
    fres_t r1, r2, r3, r4, n1, n2;
    int32_t f[4], b[4], bp[3], fh = 0, nf = 0, vgt = 32767, aspx = 0, h1 = 0, h2 = 0;
    int32_t M = clamp(p[P_E0] + (m->cutoff >> 8), 0, 127), T = clamp(p[P_E1], 0, 127), L = clamp(p[P_E2], 0, 127);
    int32_t V = clamp(p[P_E3] + ((m->shape - (64 << 8)) >> 8), 0, 127), N = clamp(p[P_E4], 0, 127);
    int32_t P = clamp(p[P_E5], 0, 127), H = clamp(p[P_E6], 0, 127), O = clamp(p[P_E7], 0, 8);
    int32_t vq = V * 258, vg, asp, oq, tilt, og, nasal = N > 0, k;
    uint32_t i, inc = m->inc, ph = v->ph[0], te, tp, rp, rn, f0, on = (uint32_t)xs->on;
    int32_t y1 = v->s[0], y2 = v->s[1], y3 = v->s[2], y4 = v->s[3], y5 = v->s[4], y6 = v->s[5];
    int32_t y7 = (int32_t)v->ph[1], y8 = (int32_t)v->ph[2], lp = v->s[6], hp = v->s[7];
    int32_t m1 = xs->m1, m2 = xs->m2, m3 = xs->m3, m4 = xs->m4, z1 = xs->z1, z2 = xs->z2, b1 = xs->b1, b2 = xs->b2;
    int32_t gv = xs->gv, nst = xs->nz | 1;
    if (n > CTL)
        n = CTL;

    /* the HISS: its place 0..1 (the lips' F .. the teeth's S), how much, band, level */
    if (H) {
        int32_t pl = H * 258, pl13 = (pl * (22938 + ((pl * 9830) >> 15))) >> 15, pl15 = (pl * (13107 + ((pl * 19661) >> 15))) >> 15;
        nf = 18022 + ((pl * 14746) >> 15);               /* 0.55 .. 1 */
        nf = (nf * (32768 - ((vq * 9830) >> 15))) >> 15;  /* (voiced: a little less, the air shared with the buzz) */
        vox_bp(bp, (uint32_t)(((1900 + ((5200 * pl13) >> 15)) * (32768 - L * 64)) >> 15), 141 + ((333 * pl) >> 15));
        fh = (((nf * (8192 + ((pl15 * 29491) >> 15))) >> 15) * 234) >> 15;   /* nf (0.25 + 0.9 pl^1.5) x 0.9, in the
                                                         * tract's scale (x 260: a proto 1.0 is 4.9 M there, the noise 18918 rms) */
    }
    /* an ONSET running: the mouth between the seal and the vowel, the voice's gain, a breath, a noise of its own */
    if (O && on < 30000u) {
        const vox_onset_t *o = &VOX_ONSET[O - 1];
        int32_t loc[3] = {o->loc[0] < 0 ? M : o->loc[0], o->loc[1] < 0 ? T : o->loc[1], o->loc[2] < 0 ? L : o->loc[2]};
        uint32_t t1 = o->cl, t2 = t1 + o->burst, t3 = t2 + o->fric, t4 = t3 + o->asp;
        k = on <= t1 ? 0 : on - t1 >= VOX_MOVE ? 32767 : (int32_t)(((on - t1) * 32767u) / VOX_MOVE);
        M = loc[0] + (((M - loc[0]) * k) >> 15);
        T = loc[1] + (((T - loc[1]) * k) >> 15);
        L = loc[2] + (((L - loc[2]) * k) >> 15);
        if (on < t1) {                                    /* the seal: silence, or the voice bar behind it */
            M = 0;
            vgt = o->voiced ? 3932 : 0;
            fh = 0;
        } else if (on < t2) {                             /* the burst */
            uint32_t fc = o->bfc;
            int32_t dec = 32767 - (int32_t)(((on - t1) * 19661u) / o->burst);
            if (!fc) {
                vox_formants(M, T, L, P, 0, f, b);
                fc = (uint32_t)clamp((f[1] * 18) >> 8, 1400, 3200);   /* F2 x 1.15 (Hz x 16 -> Hz) */
            }
            vgt = o->voiced ? 11469 : 0;
            vox_bp(bp, fc, o->bq);
            fh = (((o->bamp * dec) >> 15) * 330) >> 15;   /* (234 as the hiss's, +3 dB: the prototype's by ear) */
        } else if (on < t3) {                             /* CH, J: released through a hiss */
            vgt = o->voiced ? 19661 : 0;
            aspx = 4915;
            vox_bp(bp, o->ffc, o->fq);
            fh = (o->famp * 234) >> 15;
        } else if (on < t4) {                             /* the aspiration: breath through the opening mouth */
            vgt = o->voiced ? 32767 : 0;
            aspx = o->voiced ? 0 : 6000;                  /* (the prototype's 0.9 was 8 dB over the vowel after it;
                                                         * an aspiration sits under it, as speech's does) */
            if (!H)
                fh = 0;
        }
    }
    xs->on = (int32_t)(on + n < 0x40000000u ? on + n : on);
    vox_formants(M, T, L, P, N, f, b);
    f0 = (uint32_t)(((uint64_t)inc * 705600u) >> 32);   /* F0 in Hz x 16 */
    if ((uint32_t)f[0] < f0 + (f0 >> 4))                /* sung high: F1 follows F0 (sopranos do) */
        f[0] = (int32_t)(f0 + (f0 >> 4));
    for (k = 0; k < 4; k++) {
        f[k] = clamp(f[k], 60 * 16, 7000 * 16);
        b[k] = clamp(b[k], 20 * 16, 1500 * 16);
    }
    fres_coef(&r1, (uint32_t)f[0], (uint32_t)b[0]);
    fres_coef(&r2, (uint32_t)f[1], (uint32_t)b[1]);
    fres_coef(&r3, (uint32_t)f[2], (uint32_t)b[2]);
    fres_coef(&r4, (uint32_t)f[3], (uint32_t)b[3]);
    if (nasal) {                                        /* the nose: the murmur, the anti-resonance at the closure's place */
        int32_t fz = 1000 + ((1700 * ((T * (127 - L)) >> 7)) >> 7), cz = sine_i((uint32_t)fz * 97391u + 0x40000000u);
        fres_coef(&n1, 260u * 16u, 80u * 16u);
        fres_coef(&n2, 2100u * 16u, 300u * 16u);
        h1 = -((2 * 16210 * cz) >> 15);                  /* -2 r cos(w), Q14 (r: 150 Hz wide) */
        h2 = 16037;                                       /* r^2 */
    }

    /* the cords: open share 0.85 .. 0.40, brightness; the breath through them */
    vg = vq >= 8192 ? 32767 : vq * 4;                   /* the buzz: full from CORDS ~32 */
    if (V < 6)
        vg = 0;                                           /* (a whisper: no buzz at all) */
    oq = 55705 - ((29491 * vq) >> 15);                    /* Q16 */
    tilt = 5000 + ((vq * 10000) >> 15);                   /* the source's one-pole, Q14: dark .. bright */
    te = (uint32_t)oq << 16;
    tp = (te >> 8) * 169u;                                /* opening: 66 % of it */
    rp = (1u << 30) / ((tp >> 16) | 1u);
    rn = (1u << 30) / (((te - tp) >> 16) | 1u);
    {   /* the breath, in the pulse's units (4.07 x the prototype's): 0.06 + 0.5 (1 - v)^2, x 0.35 (x 0.9 a whisper) */
        int32_t u = 32768 - vq, a = 1966 + ((u * u) >> 16);
        asp = vg ? (a * 46667) >> 15 : 67100;            /* (x 1.42; a whisper 0.56 x 0.9 x 4.07: 2.05) */
        asp = (asp * (32768 - ((nf * 29491) >> 15))) >> 15;   /* (a fricative's air goes through its gap) */
    }
    og = 14000;
    og = (int32_t)(((int64_t)og * fmt_ratio((60 * 16 - v->pitch_cur) / 4)) >> 16);   /* 1.5 dB / oct: low notes are sparse pulses */

    for (i = 0; i < n; i++) {
        int32_t e, x, s, u1, u2, gn;
        if (ph < tp)                                    /* opening: + sin(pi u) * Tn / Tp */
            e = (sine_i(((ph >> 16) * rp) << 1) * 16880) >> 15;
        else if (ph < te)                               /* closing: - sin(pi / 2 u) */
            e = -sine_i(((ph - tp) >> 16) * rn);
        else
            e = 0;
        e += blep(ph - te, inc) >> 1;                   /* the closure step, band-limited */
        lp += ((e - lp) * tilt) >> 14;
        gv += clamp(vgt - gv, -93, 93);                 /* (8 ms end to end: a closure is fast, not instant) */
        gn = (vg * gv) >> 15;
        x = (lp * gn) >> 15;
        u1 = (int32_t)(noise32(&nst) >> 16) - 32768;
        u2 = (int32_t)(noise32(&nst) >> 16) - 32768;
        if (asp) {
            int32_t a = (((u1 >> 2) * asp) >> 13);
            a = (a * gv) >> 15;
            x += vg && ph >= te ? (a * 13107) >> 15 : a;  /* (the breath follows the glottis' opening) */
        }
        if (aspx)
            x += ((u2 >> 2) * aspx) >> 11;              /* an onset's breath (x 4.07) */
        x <<= 6;
        s = vox_res(&r1, x, &y1, &y2);
        s = vox_res(&r2, s, &y3, &y4);
        s = vox_res(&r3, s, &y5, &y6);
        s = vox_res(&r4, s, &y7, &y8);
        y7 = clamp(y7, -(1 << 28), 1 << 28);
        s = y7;
        if (nasal) {
            int32_t mu = vox_res(&n1, x, &m1, &m2), mz;
            mu = vox_res(&n2, mu, &m3, &m4);
            mz = mu + (int32_t)((((int64_t)h1 * z1) + ((int64_t)h2 * z2)) >> 14);
            z2 = z1;
            z1 = mu;
            s = (int32_t)(((int64_t)s * (32768 - ((N * 258 * 27853) >> 15)) + (int64_t)mz * ((N * 258 * 52429) >> 15)) >> 15);
        }
        if (fh) {                                       /* the hiss (and an onset's burst): in front of the tract */
            int32_t z = vox_bp_run(bp, u2, &b1, &b2) * fh;
            s += vg && !aspx && ph >= te ? (z * 18022) >> 15 : z;   /* (voiced: it pulses with the buzz) */
        }
        hp += ((s - hp) >> 7);                          /* the high-pass, ~55 Hz */
        s -= hp;
        s = soft_knee(clamp((int32_t)(((int64_t)s * og) >> 21), -200000, 200000), 14000);
        ph += inc;
        out[i] += voice_amp(s, m, i) << 1;
    }
    v->ph[0] = ph;
    v->ph[1] = (uint32_t)y7;
    v->ph[2] = (uint32_t)y8;
    v->s[0] = y1;
    v->s[1] = y2;
    v->s[2] = y3;
    v->s[3] = y4;
    v->s[4] = y5;
    v->s[5] = y6;
    v->s[6] = lp;
    v->s[7] = hp;
    xs->m1 = m1; xs->m2 = m2; xs->m3 = m3; xs->m4 = m4;
    xs->z1 = z1; xs->z2 = z2; xs->b1 = b1; xs->b2 = b2;
    xs->gv = gv;
    xs->nz = nst;
}

/* EDIT's picture (ui_graph.c graph_vox): the mouth's response, log2 in Q8 (as the analog two-pole each resonator is:
 * |H|^2 = F^4 / ((F^2 - f^2)^2 + (f B)^2), the four multiplied), at n frequencies from 150 Hz to 5 kHz on a log scale,
 * out[] in dB x 8; F[] its four formants in Hz. Drawn on the UI's side: never in the audio */
static int32_t vox_lg(uint64_t x)                       /* log2 x, Q8 (the mantissa linear) */
{
    int32_t e = 8;
    if (!x)
        return 0;
    while (x >= 512u) { x >>= 1; e++; }
    while (x < 256u) { x <<= 1; e--; }
    return e * 256 + (int32_t)(x - 256u);
}
static void vox_response(const int16_t *p, int16_t *out, uint32_t n, int32_t *F)
{
    int32_t f[4], b[4];
    uint32_t i, k;
    vox_formants(clamp(p[P_E0], 0, 127), clamp(p[P_E1], 0, 127), clamp(p[P_E2], 0, 127), clamp(p[P_E5], 0, 127),
                 clamp(p[P_E4], 0, 127), f, b);
    for (k = 0; k < 4u; k++)
        F[k] = f[k] >> 4;
    for (i = 0; i < n; i++) {
        uint32_t st = n > 1u ? i * 971u / (n - 1u) : 0u, hz = (150u << (st / 192u)) * fmt_ratio((int32_t)(st % 192u)) >> 16;   /* (971: 5.06
                                                         * octaves, 150 Hz .. 5 kHz, in 16ths of a semitone) */
        int32_t lg = 0;
        for (k = 0; k < 4u; k++) {
            uint64_t ff = (uint64_t)(f[k] >> 4) * (uint64_t)(f[k] >> 4), x = (uint64_t)hz * hz, bb = (uint64_t)hz * (uint64_t)(b[k] >> 4);
            uint64_t d = ff > x ? ff - x : x - ff;
            lg += vox_lg(ff * ff) - vox_lg(d * d + bb * bb);
        }
        out[i] = (int16_t)clamp((lg * 24) / 256, -32000, 32000);   /* (log2 Q8 -> dB x 8: x 3.01 x 8) */
    }
}

/* the phonemes: {MOUTH, TONGUE, LIPS, CORDS, NASAL, TIP, HISS, ONSET}; the vowels fitted (vox_proto.py fit_vowels) */
#define VOX_V(a, b, c, d, e_, f, g, h, env) {a, b, c, d, e_, f, g, h}, env, 0, 0, FX(0, 20, 30, 50), PAT(4)
#define VOX_SUNG {6, 80, 120, 40}
#define VOX_AIR {4, 70, 120, 24}
#define VOX_SYL {0, 80, 120, 36}
static const preset_t VOX_PRESETS[] = {
    {"AH", VOX_V(118, 29, 0, 76, 0, 0, 0, 0, VOX_SUNG)},
    {"EE", VOX_V(8, 123, 0, 76, 0, 0, 0, 0, VOX_SUNG)},
    {"IH", VOX_V(31, 107, 8, 76, 0, 0, 0, 0, VOX_SUNG)},
    {"EH", VOX_V(68, 98, 6, 76, 0, 0, 0, 0, VOX_SUNG)},
    {"AE", VOX_V(69, 118, 8, 76, 0, 0, 0, 0, VOX_SUNG)},
    {"OO", VOX_V(36, 35, 108, 76, 0, 0, 0, 0, VOX_SUNG)},
    {"OH", VOX_V(62, 11, 70, 76, 0, 0, 0, 0, VOX_SUNG)},
    {"AW", VOX_V(97, 4, 52, 76, 0, 0, 0, 0, VOX_SUNG)},
    {"UH", VOX_V(83, 14, 0, 76, 0, 0, 0, 0, VOX_SUNG)},
    {"ER", VOX_V(52, 58, 59, 76, 0, 95, 0, 0, VOX_SUNG)},
    {"M", VOX_V(3, 57, 114, 70, 127, 0, 0, 0, VOX_SUNG)},
    {"N", VOX_V(6, 95, 0, 70, 127, 0, 0, 0, VOX_SUNG)},
    {"NG", VOX_V(10, 32, 0, 70, 127, 0, 0, 0, VOX_SUNG)},
    {"L", VOX_V(38, 70, 0, 76, 0, 32, 0, 0, VOX_SUNG)},
    {"R", VOX_V(25, 51, 70, 76, 0, 127, 0, 0, VOX_SUNG)},
    {"W", VOX_V(6, 19, 127, 76, 0, 0, 0, 0, VOX_SUNG)},
    {"Y", VOX_V(4, 127, 0, 76, 0, 0, 0, 0, VOX_SUNG)},
    {"H", VOX_V(57, 29, 0, 0, 0, 0, 0, 0, VOX_AIR)},
    {"S", VOX_V(13, 108, 0, 0, 0, 0, 127, 0, VOX_AIR)},
    {"SH", VOX_V(19, 89, 76, 0, 0, 0, 76, 0, VOX_AIR)},
    {"F", VOX_V(13, 64, 0, 0, 0, 0, 19, 0, VOX_AIR)},
    {"Z", VOX_V(13, 108, 0, 57, 0, 0, 127, 0, VOX_AIR)},
    {"V", VOX_V(13, 64, 0, 57, 0, 0, 19, 0, VOX_AIR)},
    {"PA", VOX_V(118, 29, 0, 76, 0, 0, 0, 1, VOX_SYL)},
    {"BA", VOX_V(118, 29, 0, 76, 0, 0, 0, 2, VOX_SYL)},
    {"TA", VOX_V(118, 29, 0, 76, 0, 0, 0, 3, VOX_SYL)},
    {"DA", VOX_V(118, 29, 0, 76, 0, 0, 0, 4, VOX_SYL)},
    {"KA", VOX_V(118, 29, 0, 76, 0, 0, 0, 5, VOX_SYL)},
    {"GA", VOX_V(118, 29, 0, 76, 0, 0, 0, 6, VOX_SYL)},
    {"CHA", VOX_V(118, 29, 0, 76, 0, 0, 0, 7, VOX_SYL)},
    {"JA", VOX_V(118, 29, 0, 76, 0, 0, 0, 8, VOX_SYL)},
};
#undef VOX_V
static const engine_t ENG_VOX = {
    .name = "VOX",
    .page_title = {"MOUTH", "SPEECH"},
    .edit = {
        {"MOUTH", F_PCT, 0, 127, 118, 0, 0},
        {"TONGUE", F_PCT, 0, 127, 29, 0, 0},
        {"LIPS", F_PCT, 0, 127, 0, 0, 0},
        {"CORDS", F_PCT, 0, 127, 76, 0, 0},
        {"NASAL", F_PCT, 0, 127, 0, 0, 0},
        {"TIP", F_PCT, 0, 127, 0, 0, 0},
        {"HISS", F_PCT, 0, 127, 0, 0, 0},
        {"ONSET", F_ENUM, 0, 8, 0, N_VOX_ONSET, 0},
    },
    .presets = VOX_PRESETS,
    .npresets = NELEM(VOX_PRESETS),
    .note_on = vox_note_on,
    .render = vox_render,
    .knob = {P_E0, P_E1, P_E2, P_E3},                     /* HOME: MOUTH TONGUE LIPS CORDS */
    .poly = VOX_POLY,
};
