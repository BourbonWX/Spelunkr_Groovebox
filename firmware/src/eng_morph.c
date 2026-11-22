/* SPDX-License-Identifier: GPL-3.0-only
 * Phantom Cosmonaut: MORPH (engine 20, in WAVE 32's place; its DSP is WAVE 32's, Dungeon Tracker 1.5). The reference
 * engine of the spec (section 4): a FEED reads one of the 16 SIGNALs (signals_tables.h, tools/gen_signals.py), eight
 * band-limited frames each, and four "contort" values shape it, the same four names on every engine, on KNOB 1..4:
 *   SCAN     the place in the signal's frames (two neighbours cross-faded); SENV moves it with the envelope, the
 *            modulation matrix's SHAPE too
 *   WARP     phase distortion: the frame read at a bent phase (brighter, a little grit)
 *   FOLD     a wavefolder after it (a triangle fold, up to ~7 x): metallic, hull-scraping
 *   CLARITY  one knob, -64..63: left of the middle a low-pass closing (RES its resonance, the matrix's cutoff
 *            on it), the middle open, right of it static: bits dropped (to 5), the sample rate held down (to 1/24),
 *            noise mixed in
 * SUB adds a sine an octave down. Each frame is kept at seven sizes (a mipmap): the note reads the fullest whose top
 * harmonic stays under ~11.5 kHz, so SCAN never aliases; WARP, FOLD and the right side of CLARITY add what they add,
 * on purpose. PRESETS picks a SIGNAL: the 16 factory sounds are the 16 signals, each set up for its role.
 * morph_cycle draws one cycle as the voice would shape it (EDIT's oscilloscope, ui_graph.c graph_signal).
 *
 * Phantom Cosmonaut's other SIGNAL engines live here too (Ed, Nov 2026: all on the 16 SIGNALs, no table of their own):
 * DEBRIS (25: folded granular, FOLDER's and GRANULAR's place) and FM (24). Each makes its raw sample its own way and
 * hands the block to sig_tail, MORPH's end of the chain: SUB, CLARITY's low-pass (left) and static (right), the DC blocker, the
 * amplitude. The four contort knobs keep their names on every engine, their meaning follows the engine (the spec's
 * table, section 4). Included by engines.c after eng_cycle.c (FM's ratios) */
#ifndef PC_MORPH_C
#define PC_MORPH_C 1
#include "signals_tables.h"

static const char *const N_SIG_NUM[SIG_N + 1] = {"01", "02", "03", "04", "05", "06", "07", "08", "09", "10", "11", "12",
                                                 "13", "14", "15", "16", 0};
/* Phantom Cosmonaut M8: DEBRIS's SIG (GRANULAR's before) goes on past the 16 SIGNALs to the factory tapes, 17..20 (factory_tables.h:
 * GROAN, VENT, HETERODYNE, SKYWAVE); the other engines stop at 16 */
static const char *const N_SIG_NUM_GR[SIG_N + FK_NTAPE + 1] = {"01", "02", "03", "04", "05", "06", "07", "08", "09",
                                                 "10", "11", "12", "13", "14", "15", "16", "17", "18", "19", "20", 0};
typedef char fk_tapes_named[FK_NTAPE == 4 ? 1 : -1];
static uint32_t sig_max(uint32_t e) { return e == ENGI_DEBRIS ? SIG_N + FK_NTAPE - 1u : SIG_N - 1u; }
static const char *sig_name(uint32_t e, int32_t i)
{
    i = clamp(i, 0, (int32_t)sig_max(e));
    return i < SIG_N ? SIG_NAME[i] : FK_TAPE_NAME[i - SIG_N];
}

/* the level a note of increment inc reads (0 = 256 samples, 120 harmonics .. 6 = 8, 1) */
static uint32_t morph_level(uint32_t inc)
{
    uint32_t l = 0;
    while (l < SIG_NL - 1u && (uint64_t)inc * SIG_HARM[l] > 2058000000ull)   /* (11.5 kHz of 24 kHz, x 2^32) */
        l++;
    return l;
}
/* a frame's sample at phase ph, size 2^bits, interpolated, Q15 */
static inline int32_t morph_read(const int8_t *w, uint32_t bits, uint32_t ph)
{
    uint32_t i = ph >> (32u - bits), fr = (ph >> (17u - bits)) & 0x7fffu;
    int32_t a = (int32_t)w[i] << 8, b = (int32_t)w[i + 1u] << 8;
    return a + (((b - a) * (int32_t)fr) >> 15);
}
/* FOLD: x (Q15) times g (Q8, 256 = none) folded back into -32768..32767, a triangle fold for any drive */
static inline int32_t morph_fold(int32_t x, int32_t g)
{
    uint32_t t = ((uint32_t)((x * g) >> 8) + 32768u) & 0x1FFFFu;
    if (t >= 65536u)
        t = 131071u - t;
    return (int32_t)t - 32768;
}
static int32_t morph_fold_gain(int32_t fold) { return 256 + fold * 12; }   /* (FOLD 127: ~7 x) */

/* the shared part of a block: the signal, the two frames, their mix and the level */
typedef struct {
    const int8_t *w1, *w2;
    int32_t g1, g2, wk, fg;
    uint32_t bits;
} morph_src_t;
static void morph_src(const int16_t *p, int32_t pos, uint32_t inc, morph_src_t *o)
{
    uint32_t sig = (uint32_t)clamp(p[P_E0], 0, SIG_N - 1), fa, fb, lvl;
    int32_t fp = clamp(pos, 0, 127) * (SIG_NF - 1) * 256 / 127, warp = clamp(p[P_E2] + mk_x[3] * 2 / 5, 0, 127);   /* (RIDE) */
    fa = (uint32_t)(fp >> 8);
    fb = fa + 1u < SIG_NF ? fa + 1u : fa;
    lvl = morph_level(inc);
    o->bits = SIG_BITS[lvl];
    o->w1 = SIG_TAB[sig * SIG_NF + fa] + SIG_OFS[lvl];
    o->w2 = SIG_TAB[sig * SIG_NF + fb] + SIG_OFS[lvl];
    o->g2 = fp & 255;
    o->g1 = 256 - o->g2;
    o->wk = warp * warp * 2;
    o->fg = p[P_E3] > 0 ? morph_fold_gain(clamp(p[P_E3], 0, 127)) : 0;
}
/* SCAN, WARP and FOLD at phase ph */
static inline int32_t morph_shape(const morph_src_t *o, uint32_t ph)
{
    uint32_t r = o->wk ? ph + (uint32_t)(sine_i(ph) * o->wk) : ph;
    int32_t s = (morph_read(o->w1, o->bits, r) * o->g1 + morph_read(o->w2, o->bits, r) * o->g2) >> 8;
    return o->fg ? morph_fold(s, o->fg) : s;
}
/* CLARITY's right side: bits dropped (sh), the noise's level (Q15), the hold in samples */
static void morph_static(int32_t c, uint32_t *sh, int32_t *noise, uint32_t *hold)
{
    int32_t a = c > 0 ? (c > 63 ? 63 : c) : 0;
    *sh = (uint32_t)(a * 10 / 63);                        /* 16 bits .. 6 */
    *noise = a * a * 3;                                   /* .. ~ -11 dB under full scale */
    *hold = 1u + (uint32_t)(a * a) / 172u;                /* 1 .. 24 */
}

static void morph_note_on(track_t *t, voice_t *v)
{
    (void)t;
    v->s[0] = v->s[1] = 0;                                /* the filter */
    v->s[2] = v->s[3] = 0;                                /* the held sample and its count */
    v->s[7] = 0;                                          /* the DC blocker */
    v->ph[2] = 0;                                         /* the sub */
}

/* the end of every SIGNAL engine's chain, y[] (n raw samples, Q15) -> out[]: SUB (sub: Q15 of a sine an octave down,
 * at the note's inc), CLARITY c (-64..63: left the low-pass with resonance res, when filt; right static), the DC
 * blocker, the voice's amplitude. State: s[0], s[1] the filter, s[2] the held sample, s[3] its count, s[4] the noise,
 * s[7] the DC blocker, ph[2] the sub's phase */
static void sig_tail(voice_t *v, const vmod_t *m, const int32_t *y, int32_t *out, uint32_t n, int32_t c, int32_t res,
                     int32_t sub, int filt)
{
    uint32_t i, sh, hold, sph = v->ph[2], inc = m->inc;
    int32_t cut = ((c < 0 ? 127 + c * 100 / 64 : 127) << 8) + m->cutoff, namp;
    int32_t ic1 = v->s[0], ic2 = v->s[1], held = v->s[2], cnt = v->s[3], dc = v->s[7];
    uint32_t nz = (uint32_t)v->s[4] | 1u;
    tsvf_t flt;
    filt = filt && (c < 0 || m->cutoff < 0 || res > 0);
    morph_static(c, &sh, &namp, &hold);
    if (filt)
        tsvf_coef(&flt, clamp(cut, 0, 127 << 8), res);
    for (i = 0; i < n; i++) {
        int32_t s = y[i];
        if (sub) {                                        /* SUB: a sine an octave down */
            s += (int32_t)(((int64_t)(sine_i(sph) - s) * sub) >> 15);
            sph += inc >> 1;
        }
        if (filt) {                                       /* CLARITY's left: the low-pass, with a soft knee */
            int32_t f = tsvf_lp(&flt, s >> 1, &ic1, &ic2), a = f < 0 ? -f : f;
            if (a > 16000) {
                a = 16000 + (softclip((a - 16000) * 2) >> 1);
                f = f < 0 ? -a : a;
            }
            s = f << 1;
        }
        if (namp) {                                       /* CLARITY's right: static */
            if (++cnt >= (int32_t)hold) {
                cnt = 0;
                nz = nz * 1664525u + 1013904223u;
                held = ((s >> sh) << sh) + (((int32_t)nz >> 16) * namp >> 15);
            }
            s = held;
        }
        dc += ((s << 8) - dc) >> 11;
        s -= dc >> 8;
        out[i] += voice_amp(s, m, i) << 1;
    }
    v->ph[2] = sph;
    if (filt) {
        v->s[0] = ic1;
        v->s[1] = ic2;
    }
    v->s[2] = held;
    v->s[3] = cnt;
    v->s[4] = (int32_t)nz;
    v->s[7] = dc;
}
/* SCAN with its envelope (SENV-like amount e, -64..63) and the matrix's SHAPE */
static int32_t sig_scan(const int16_t *p, int32_t e, const vmod_t *m)
{
    return p[P_E1] + ((e * m->envq15) >> 15) + ((m->shape - (64 << 8)) >> 8);
}

static void morph_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    morph_src_t o;
    uint32_t i, ph = v->ph[0], inc = m->inc;
    int32_t y[CTL];
    if (n > CTL)
        n = CTL;
    morph_src(p, sig_scan(p, p[P_E6], m), inc, &o);
    for (i = 0; i < n; i++) {
        y[i] = morph_shape(&o, ph);
        ph += inc;
    }
    v->ph[0] = ph;
    sig_tail(v, m, y, out, n, clamp(p[P_E4], -64, 63), p[P_E5], p[P_E7] * 258, 1);
}

/* =============================================================================================== DEBRIS (25) ===
 * Folded granular (Ed, Nov 2026: "combine folder and granular into one ... more unique engine"; then "less harsh":
 * "leave the harsh sounds entirely out"). FOLDER (22) and GRANULAR (23) retired onto it (engines.c ENG_SUBST).
 * Two grains read the SIGNAL, half a grain apart, each under a Hann window (sin^2: the two add to exactly one), each
 * starting in phase with the note (the note's own phase, ph[2]: a grain is the wavetable read, not a random start, so
 * grains never fight) and each drawn a little its own way: SCAN's place (SCATTER moves it up to +-1.5 frames), a few
 * cents off (DRIFT: a chorus), its own share of the fold (SCATTER), now and then an octave up (CLARITY's right:
 * SHIMMER, at -7 dB). Every grain goes through FOLDER's sine fold, sin(pi/2 g x), with g only x1 .. x4 (the harsh
 * x16 is gone), faded in over FOLD's first third. The fold is kept off Nyquist by reading the SIGNAL's fuller mip
 * level less the fold's own spread (a level down from x1.6, two from x3): warm, never static.
 * SIG 17..20 are the factory tapes (M8: GROAN VENT HETERODYNE SKYWAVE): the grains read the tape at its own speed at
 * middle C, from SCAN (SCATTER: +-4 % of the tape).
 *   E0 SIG  E1 SCAN  E2 WARP (the grain: 17 .. 274 ms)  E3 FOLD (x1 .. x4)  E4 CLARITY (left the low-pass, right
 *   SHIMMER)  E5 SCATTER  E6 FENV (-64..63: the envelope moves FOLD)  E7 DRIFT (+-27 cents at 127)
 * State: ph[0], ph[1] the grains' phases (a tape: their pointers), ph[2] the note's phase; s[2], s[3] each grain's
 * draw (its frame x 256 bits 0..10, detune + 128 bits 11..18, fold share bits 19..26 (128 = 1), octave bit 27); s[5], s[6]
 * its age (low 16 bits) and length (high 16); s[4] the draws' generator; s[0], s[1] the low-pass and s[7] the DC
 * blocker (sig_tail; CLARITY's right is never static here, so sig_tail leaves s[2], s[3] as they are) */
static uint32_t gran_rng(uint32_t *r) { return *r = *r * 1664525u + 1013904223u; }
static uint32_t deb_len(int32_t w)
{
    uint32_t u = (uint32_t)clamp(w, 0, 127);
    return ((768u << (u >> 5)) * (32u + (u & 31u))) >> 5;   /* 768 .. 12096 samples */
}
/* a grain's draw: SCAN's frame (Q8, SCATTER's spread), its detune, its share of the fold, its octave */
static uint32_t deb_draw(const int16_t *p, int32_t scan, uint32_t *rng)
{
    int32_t sc = clamp(p[P_E5], 0, 127), dr = clamp(p[P_E7], 0, 127), sh = clamp(p[P_E4], 0, 63);
    uint32_t q = gran_rng(rng);
    int32_t r1 = (int32_t)(q >> 24) - 128, r2 = (int32_t)((q >> 16) & 255u) - 128, r3 = (int32_t)((q >> 8) & 255u) - 128;
    int32_t fp = clamp(scan, 0, 127) * (int32_t)((SIG_NF - 1u) * 256u) / 127 + ((r1 * sc * 3) >> 7);
    int32_t det = (r2 * dr) >> 10, fv = 128 + ((r3 * sc) >> 9);
    uint32_t oct = (gran_rng(rng) >> 25) < (uint32_t)sh ? 1u : 0u;   /* SHIMMER: up to half the grains (63 of 128) */
    fp = clamp(fp, 0, (int32_t)((SIG_NF - 1u) * 256u));
    fp = ((gran_rng(rng) >> 17) & 255u) < (uint32_t)(fp & 255) ? (fp | 255) + 1 : fp & ~255;   /* one frame a grain: the
                                                         * nearer more often (the grains between them blend them) */
    return (uint32_t)fp | ((uint32_t)(det + 128) << 11) | ((uint32_t)fv << 19) | (oct << 27);
}
/* tape SIG (17..20): the tape, else 0 */
static const int8_t *deb_tape(const int16_t *p)
{
    return p[P_E0] >= SIG_N ? FK_TAPE[clamp(p[P_E0] - SIG_N, 0, FK_NTAPE - 1)] : 0;
}
/* grain g starts over: its draw, its phase (the note's, or on a tape SCAN's place), age 0, its length */
static void deb_start(const int16_t *p, int32_t scan, uint32_t len, uint32_t *ph, int32_t *dr, int32_t *ag,
                      uint32_t *rng, uint32_t nph, int tape)
{
    *dr = (int32_t)deb_draw(p, scan, rng);
    if (tape) {
        int32_t r = (int32_t)(gran_rng(rng) >> 16) - 32768;
        *ph = (uint32_t)clamp(scan, 0, 127) * (0xE0000000u / 127u) + (uint32_t)(r * clamp(p[P_E5], 0, 127)) * 41u;
    } else
        *ph = ((uint32_t)*dr >> 27) & 1u ? nph << 1 : nph;   /* in phase with the note (an octave up: with its 2nd) */
    *ag = (int32_t)(len << 16);
}
static void deb_note_on(track_t *t, voice_t *v)
{
    uint32_t rng = (v->age + 1u) * 2654435761u ^ v->note, len = deb_len(t->p[P_E2]);
    int tape = deb_tape(t->p) != 0;
    morph_note_on(t, v);
    deb_start(t->p, t->p[P_E1], len, &v->ph[0], &v->s[2], &v->s[5], &rng, 0, tape);
    deb_start(t->p, t->p[P_E1], len, &v->ph[1], &v->s[3], &v->s[6], &rng, 0, tape);
    v->s[6] |= (int32_t)(len >> 1);                       /* (B half a grain on: its window at the top as A's starts) */
    v->s[4] = (int32_t)(rng | 1u);
}
/* the fold's amount now, 0..127: FOLD and FENV with the envelope (and RIDE) */
static int32_t deb_fold(const int16_t *p, int32_t envq15)
{
    return clamp(p[P_E3] + ((p[P_E6] * envq15) >> 14) + mk_x[3] * 2 / 5, 0, 127);
}
/* x (Q15) folded: the fold share fv (Q7) of fd (0..127): sin(pi/2 g x) at 0.8, g 1 .. ~4, faded in over 0 .. 42 */
static inline int32_t deb_shape(int32_t x, int32_t fdg)
{
    int32_t g = 256 + fdg * 6, wet = fdg * 6 > 256 ? 256 : fdg * 6, f;
    if (!wet)
        return x;
    f = (sine_i((uint32_t)(x * g) << 7) * 26214) >> 15;
    return x + (((f - x) * wet) >> 8);
}
/* a grain's reading for a block (and again as it starts over): its window step, rate, fold, level, frame */
typedef struct {
    const int8_t *wa;
    uint32_t hs, gi, bits;
    int32_t fdg, amp, w, dw;
} deb_rd_t;
/* the Hann window, Q15, at age a of a grain of len (hs: half a turn over it) */
static inline int32_t deb_win(uint32_t a, uint32_t hs)
{
    int32_t w = sine_i(a * hs);
    return (w * w) >> 15;
}
/* the window over the next 32 samples from age a: its value (Q20) and step, a straight line (a grain is 24 blocks
 * and more: the line is the curve) */
static inline void deb_wline(deb_rd_t *r, uint32_t a, uint32_t len)
{
    uint32_t b = a + CTL < len ? a + CTL : len;
    r->w = ((deb_win(a, r->hs) * r->amp) >> 15) << 5;     /* (x the grain's level: an octave up, -7 dB) */
    r->dw = (((deb_win(b, r->hs) * r->amp) >> 15) << 5) - r->w;
    r->dw = b - a == CTL ? r->dw >> 5 : r->dw / (int32_t)(b - a > 0 ? b - a : 1);
}
static void deb_reading(deb_rd_t *r, uint32_t d, uint32_t len, uint32_t base, int32_t fd, uint32_t sig, const uint32_t *lv)
{
    uint32_t o = (d >> 27) & 1u, fa = (d & 2047u) >> 8, l = lv[o];
    int32_t det = (int32_t)((d >> 11) & 255u) - 128;
    fa = fa < SIG_NF ? fa : SIG_NF - 1u;
    r->hs = 0x80000000u / (len | 1u);                     /* half a turn of the sine over the grain */
    r->gi = base + (uint32_t)((int32_t)(base >> 10) * det);
    r->gi = o ? r->gi << 1 : r->gi;
    r->fdg = clamp((fd * (int32_t)((d >> 19) & 255u)) >> 7, 0, 127);
    r->amp = o ? 14746 : 32767;                           /* (an octave up at -7 dB: a shimmer, not a second voice) */
    r->bits = SIG_BITS[l];
    r->wa = SIG_TAB[sig * SIG_NF + fa] + SIG_OFS[l];
}
static void debris_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    const int8_t *tape = deb_tape(p);
    uint32_t i, g, len = deb_len(p[P_E2]), rng = (uint32_t)v->s[4] | 1u, nph = v->ph[2], inc = m->inc, base, xf, lv[2];
    uint32_t sig = (uint32_t)clamp(p[P_E0], 0, SIG_N - 1), age[2], gl[2];
    int32_t y[CTL], fd = deb_fold(p, m->envq15), scan = sig_scan(p, 0, m);
    uint32_t ph[2] = {v->ph[0], v->ph[1]};
    int32_t dr[2] = {v->s[2], v->s[3]};
    deb_rd_t r[2];
    if (n > CTL)
        n = CTL;
    xf = fd * 6 >= 512 ? 2u : fd * 6 >= 154 ? 1u : 0u;   /* the fold's spread: a level (two) down */
    lv[0] = morph_level(inc) + xf;
    lv[1] = morph_level(inc < 0x80000000u ? inc << 1 : inc) + xf;
    lv[0] = lv[0] < SIG_NL ? lv[0] : SIG_NL - 1u;
    lv[1] = lv[1] < SIG_NL ? lv[1] : SIG_NL - 1u;
    base = tape ? inc / (pitch_inc(60u * 16u) >> 16) : inc;   /* (a tape: its own speed at middle C, as GRANULAR's) */
    for (g = 0; g < 2u; g++) {
        age[g] = (uint32_t)v->s[5 + g] & 0xFFFFu;
        gl[g] = (uint32_t)v->s[5 + g] >> 16;
        deb_reading(&r[g], (uint32_t)dr[g], gl[g], base, fd, sig, lv);
        deb_wline(&r[g], age[g], gl[g]);
    }
    for (i = 0; i < n; i++) {
        int32_t s = 0;
        for (g = 0; g < 2u; g++) {
            uint32_t x;
            int32_t z;
            if (age[g] >= gl[g]) {                        /* the grain is over: the next one, in phase with the note */
                int32_t ag;
                deb_start(p, scan, len, &ph[g], &dr[g], &ag, &rng, nph, tape != 0);
                deb_reading(&r[g], (uint32_t)dr[g], len, base, fd, sig, lv);
                deb_wline(&r[g], 0, len);
                age[g] = 0;
                gl[g] = len;
            }
            x = ph[g];
            if (tape) {                                   /* 16384 samples: the pointer's top 14 bits, then 16 of fraction */
                uint32_t ix = x >> 18, fr = (x >> 2) & 0xFFFFu;
                int32_t ta = tape[ix], tb = tape[(ix + 1u) & (FK_TAPE_N - 1u)];
                z = ((ta << 16) + (tb - ta) * (int32_t)fr) >> 8;
            } else
                z = morph_read(r[g].wa, r[g].bits, x);
            if (r[g].fdg)
                z = deb_shape(z, r[g].fdg);
            s += (z * (r[g].w >> 5)) >> 15;               /* the Hann window (x the shimmer's level), a line a block */
            r[g].w += r[g].dw;
            ph[g] = x + r[g].gi;
            age[g]++;
        }
        nph += inc;
        y[i] = s;
    }
    v->ph[0] = ph[0];
    v->ph[1] = ph[1];
    v->ph[2] = nph;
    v->s[2] = dr[0];
    v->s[3] = dr[1];
    v->s[5] = (int32_t)((gl[0] << 16) | age[0]);
    v->s[6] = (int32_t)((gl[1] << 16) | age[1]);
    v->s[4] = (int32_t)rng;
    sig_tail(v, m, y, out, n, clamp(p[P_E4], -64, 0), 12, 0, 1);   /* (CLARITY's right is SHIMMER: no static) */
}

/* ==================================================================================================== FM (24) ===
 * Pseudo-FM on the SIGNALs: one frame of the signal (the modulator: SCAN's place, two neighbours cross-faded, at
 * RATIO x the note, FOLD its feedback) bends the phase at which another frame (the carrier: CAR's place) is read.
 * WARP is the index (MENV moves it with the envelope: a bell's bright strike falling away); CLARITY as everywhere,
 * the low-pass at a fixed resonance on the left.
 *   E0 SIG  E1 SCAN (modulator)  E2 WARP (index)  E3 FOLD (feedback)  E4 CLARITY  E5 RATIO  E6 CAR  E7 MENV
 * State: ph[0] the carrier, ph[1] the modulator, s[5] the modulator's last sample (feedback) */
typedef struct {
    const int8_t *m1, *m2, *c1, *c2;
    int32_t mg, cg, k, fb;
    uint32_t bits, minc;
} pfm_src_t;
static void pfm_src(const int16_t *p, int32_t scan, int32_t env, uint32_t inc, pfm_src_t *o)
{
    uint32_t sig = (uint32_t)clamp(p[P_E0], 0, SIG_N - 1), lvl, ma, ca;
    int32_t mp = clamp(scan, 0, 127) * (SIG_NF - 1) * 256 / 127, cp = clamp(p[P_E6], 0, 127) * (SIG_NF - 1) * 256 / 127;
    int32_t kk = clamp(p[P_E2] + ((p[P_E7] * env) >> 14) + mk_x[3] * 2 / 5, 0, 127);   /* (RIDE) */
    o->minc = (inc >> 8) * CYC_RATIO[clamp(p[P_E5], 0, 15)];
    lvl = morph_level(o->minc > inc ? o->minc : inc);
    o->bits = SIG_BITS[lvl];
    ma = (uint32_t)(mp >> 8);
    ca = (uint32_t)(cp >> 8);
    o->m1 = SIG_TAB[sig * SIG_NF + ma] + SIG_OFS[lvl];
    o->m2 = SIG_TAB[sig * SIG_NF + (ma + 1u < SIG_NF ? ma + 1u : ma)] + SIG_OFS[lvl];
    o->c1 = SIG_TAB[sig * SIG_NF + ca] + SIG_OFS[lvl];
    o->c2 = SIG_TAB[sig * SIG_NF + (ca + 1u < SIG_NF ? ca + 1u : ca)] + SIG_OFS[lvl];
    o->mg = mp & 255;
    o->cg = cp & 255;
    o->k = kk * kk * 5;                                   /* (127: ~3.9 radians) */
    o->fb = clamp(p[P_E3], 0, 127) * 400;                 /* (127: ~0.4 of a turn, ~2.4 radians: to the edge of noise) */
}
static inline int32_t pfm_read(const int8_t *a, const int8_t *b, int32_t g, uint32_t bits, uint32_t ph)
{
    return (morph_read(a, bits, ph) * (256 - g) + morph_read(b, bits, ph) * g) >> 8;
}
static void pfm_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    pfm_src_t o;
    uint32_t i, ph = v->ph[0], mph = v->ph[1], inc = m->inc;
    int32_t y[CTL], md = v->s[5];
    if (n > CTL)
        n = CTL;
    pfm_src(p, sig_scan(p, 0, m), m->envq15, inc, &o);
    for (i = 0; i < n; i++) {
        md = pfm_read(o.m1, o.m2, o.mg, o.bits, mph + (uint32_t)(md * o.fb));
        mph += o.minc;
        y[i] = pfm_read(o.c1, o.c2, o.cg, o.bits, ph + (uint32_t)(md * o.k));
        ph += inc;
    }
    v->ph[0] = ph;
    v->ph[1] = mph;
    v->s[5] = md;
    sig_tail(v, m, y, out, n, clamp(p[P_E4], -64, 63), 24, 0, 1);
}
static void pfm_note_on(track_t *t, voice_t *v)
{
    morph_note_on(t, v);
    v->ph[1] = 0;
    v->s[5] = 0;
}

/* EDIT's oscilloscope: one cycle of track t's sound as its engine's contort knobs shape it (no filter, no envelope:
 * SENV, FENV, MENV at rest), n points, Q15, at the level a 220 Hz note reads (the scope's cycle is slow; the note is
 * not). DEBRIS: a grain's cycle (a tape: the stretch a grain at SCAN reads), folded */
static void morph_cycle(const track_t *t, int16_t *out, uint32_t n)
{
    const int16_t *p = t->p;
    uint32_t i, sh, hold, step = n ? 0xFFFFFFFFu / n : 0u, e = t->eng_req % NENGINES, inc = 21424615u;   /* (220 Hz) */
    int32_t namp, nz = 0x1234567, md = 0;
    morph_src_t o;
    pfm_src_t f;
    morph_src(p, p[P_E1], inc, &o);
    if (e == ENGI_DEBRIS)
        o.fg = 0;
    if (e == ENGI_PFM)
        pfm_src(p, p[P_E1], 0, inc, &f);
    morph_static(e == ENGI_DEBRIS ? 0 : clamp(p[P_E4], -64, 63), &sh, &namp, &hold);
    for (i = 0; i < n; i++) {
        uint32_t ph = step * i;
        int32_t s;
        if (e == ENGI_PFM) {
            md = pfm_read(f.m1, f.m2, f.mg, f.bits, (uint32_t)((uint64_t)ph * (f.minc >> 8) / (inc >> 8)) + (uint32_t)(md * f.fb));
            s = pfm_read(f.c1, f.c2, f.cg, f.bits, ph + (uint32_t)(md * f.k));
        } else if (e == ENGI_DEBRIS && p[P_E0] >= SIG_N) {   /* a tape: 2048 of its samples at SCAN, folded */
            const int8_t *tp = FK_TAPE[clamp(p[P_E0] - SIG_N, 0, FK_NTAPE - 1)];
            s = tp[((uint32_t)clamp(p[P_E1], 0, 127) * (FK_TAPE_N / 128u) + (ph >> 21)) & (FK_TAPE_N - 1u)] * 256;
            s = deb_shape(s, deb_fold(p, 0));
        } else if (e == ENGI_DEBRIS) {                   /* a grain's cycle: SCAN's frame, folded */
            s = deb_shape(morph_shape(&o, ph), deb_fold(p, 0));
        } else
            s = morph_shape(&o, ph);
        if (namp) {
            nz = (int32_t)((uint32_t)nz * 1664525u + 1013904223u);
            s = ((s >> sh) << sh) + ((nz >> 16) * namp >> 15);
        }
        out[i] = (int16_t)clamp(s, -32767, 32767);
    }
}

/* {SIG, SCAN, WARP, FOLD, CLARITY, RES, SENV, SUB}: one sound a signal, each set up for its role (PRESETS: SIG 01..16) */
static const preset_t MORPH_PRESETS[] = {
    {"CARRIER", {0, 40, 0, 0, -10, 20, 20, 0}, {0, 70, 110, 36}, 0, 0, FX(0, 20, 40, 30), PAT(4)},
    {"PULSAR", {1, 30, 0, 0, -30, 40, 30, 60}, {0, 60, 90, 18}, 30, 1, FX(10, 0, 0, 0), PAT(2)},
    {"TELEMETRY", {2, 64, 0, 0, 10, 10, 0, 0}, {0, 70, 110, 30}, 0, 1, FX(0, 10, 40, 30), PAT(4)},
    {"VOSTOK", {3, 20, 0, 0, -6, 15, 40, 0}, {80, 95, 110, 95}, 0, 0, FX(0, 60, 25, 90), PAT(5)},
    {"COLD START", {4, 50, 0, 0, -4, 10, -20, 0}, {0, 84, 30, 50}, 20, 0, FX(0, 30, 20, 40), PAT(6)},
    {"ORBIT DECAY", {5, 20, 0, 0, -8, 25, 50, 0}, {0, 70, 110, 34}, 10, 1, FX(20, 0, 40, 30), PAT(4)},
    {"SOLAR WIND", {6, 30, 10, 0, -14, 20, 30, 0}, {90, 100, 110, 100}, 0, 0, FX(0, 50, 40, 100), PAT(5)},
    {"IONOSPHERE", {7, 10, 0, 0, -10, 15, 60, 0}, {70, 95, 100, 95}, 0, 0, FX(0, 60, 30, 90), PAT(5)},
    {"DEAD STAR", {8, 60, 0, 10, -6, 10, -50, 0}, {0, 76, 0, 60}, 0, 0, FX(0, 20, 45, 70), PAT(3)},
    {"STATIC", {9, 70, 20, 0, 30, 20, 0, 0}, {40, 90, 100, 80}, 0, 0, FX(0, 0, 50, 80), PAT(7)},
    {"HULL GROAN", {10, 40, 0, 30, -20, 30, 20, 50}, {0, 60, 90, 16}, 30, 1, FX(30, 0, 0, 0), PAT(8)},
    {"NUMBERS", {11, 30, 0, 0, 6, 10, 40, 0}, {0, 60, 0, 40}, 0, 0, FX(0, 10, 40, 40), PAT(3)},
    {"LAST WORDS", {12, 20, 0, 0, -12, 20, 30, 0}, {80, 96, 110, 100}, 0, 0, FX(0, 50, 30, 90), PAT(5)},
    {"SPUTNIK", {13, 0, 0, 0, 0, 10, 70, 0}, {0, 58, 0, 36}, 0, 0, FX(0, 0, 40, 30), PAT(13)},
    {"RELAY", {14, 30, 0, 0, -16, 35, 50, 40}, {0, 56, 60, 14}, 30, 1, FX(20, 0, 0, 0), PAT(1)},
    {"DARK SIDE", {15, 20, 0, 20, -24, 25, 40, 60}, {0, 60, 90, 18}, 20, 1, FX(10, 0, 0, 0), PAT(2)},
};
typedef char morph_one_preset_a_signal[NELEM(MORPH_PRESETS) == SIG_N ? 1 : -1];

static const engine_t ENG_MORPH = {
    .name = "MORPH",
    .page_title = {"SIGNAL", "CLARITY"},
    .edit = {
        {"SIG", F_ENUM, 0, SIG_N - 1, 0, N_SIG_NUM, 0},
        {"SCAN", F_PCT, 0, 127, 32, 0, 0},
        {"WARP", F_PCT, 0, 127, 0, 0, 0},
        {"FOLD", F_PCT, 0, 127, 0, 0, 0},
        {"CLARITY", F_BIPCT, -64, 63, 0, 0, 0},
        {"RES", F_PCT, 0, 127, 20, 0, 0},
        {"SENV", F_BIPCT, -64, 63, 0, 0, 0},
        {"SUB", F_PCT, 0, 127, 0, 0, 0},
    },
    .presets = MORPH_PRESETS,
    .npresets = NELEM(MORPH_PRESETS),
    .note_on = morph_note_on,
    .render = morph_render,
    .knob = {P_E1, P_E2, P_E3, P_E4},                     /* HOME: SCAN WARP FOLD CLARITY (spec section 4) */
    .keep = 0x03,                                         /* the filter */
};
/* DEBRIS: {SIG, SCAN, WARP (grain), FOLD, CLARITY (left low-pass, right SHIMMER), SCATTER, FENV, DRIFT}: the second
 * pass's examples (tools/debris_soft.py), on the device */
static const preset_t DEBRIS_PRESETS[] = {
    {"GLASS PAD", {4, 30, 96, 28, 12, 45, 0, 50}, {90, 100, 110, 100}, 0, 0, FX(0, 30, 30, 90), PAT(5)},
    {"ROUND BASS", {13, 25, 80, 5, -30, 13, 57, 20}, {0, 62, 50, 22}, 0, 1, FX(10, 0, 0, 0), PAT(2)},
    {"MUSIC BOX", {0, 13, 72, 6, 0, 25, 63, 20}, {0, 74, 0, 60}, 0, 0, FX(0, 20, 45, 70), PAT(7)},
    {"SLOW DRONE", {15, 64, 110, 30, 16, 50, 0, 70}, {110, 100, 120, 110}, 0, 0, FX(0, 50, 30, 110), PAT(5)},
    {"HALO LEAD", {6, 23, 88, 25, 20, 25, 12, 35}, {6, 80, 105, 44}, 0, 1, FX(0, 20, 45, 60), PAT(4)},
    {"GHOST CHOIR", {SIG_N + 3, 13, 120, 10, -40, 38, 0, 60}, {100, 95, 115, 100}, 0, 0, FX(0, 50, 30, 110), PAT(5)},
    {"WRECK PAD", {SIG_N + 0, 30, 104, 30, -20, 50, 20, 50}, {80, 95, 110, 95}, 0, 0, FX(0, 40, 30, 100), PAT(5)},
    {"FOLD BASS", {0, 0, 127, 35, -24, 0, 30, 0}, {0, 60, 90, 16}, 0, 1, FX(20, 0, 0, 0), PAT(2)},
    {"GRAIN PAD", {3, 30, 100, 15, 0, 40, 0, 50}, {80, 95, 110, 95}, 0, 0, FX(0, 60, 30, 100), PAT(5)},
    {"TIDE KEYS", {2, 10, 84, 20, -10, 20, 30, 25}, {0, 84, 30, 50}, 0, 0, FX(0, 30, 20, 40), PAT(6)},
    {"VENT HAZE", {SIG_N + 1, 50, 112, 12, -34, 60, 0, 70}, {100, 100, 120, 110}, 0, 0, FX(0, 40, 30, 110), PAT(5)},
    {"SKYWAVE", {SIG_N + 2, 40, 100, 18, -16, 45, 10, 45}, {60, 95, 110, 95}, 0, 0, FX(0, 40, 40, 90), PAT(5)},
};
static const engine_t ENG_DEBRIS = {
    .name = "DEBRIS",
    .page_title = {"SIGNAL", "GRAIN"},
    .edit = {
        {"SIG", F_ENUM, 0, SIG_N + FK_NTAPE - 1, 0, N_SIG_NUM_GR, 0},   /* (17..20: the tapes, M8) */
        {"SCAN", F_PCT, 0, 127, 32, 0, 0},
        {"WARP", F_PCT, 0, 127, 96, 0, 0},
        {"FOLD", F_PCT, 0, 127, 20, 0, 0},
        {"CLARITY", F_BIPCT, -64, 63, 0, 0, 0},
        {"SCATTER", F_PCT, 0, 127, 30, 0, 0},
        {"FENV", F_BIPCT, -64, 63, 0, 0, 0},
        {"DRIFT", F_PCT, 0, 127, 30, 0, 0},
    },
    .presets = DEBRIS_PRESETS,
    .npresets = NELEM(DEBRIS_PRESETS),
    .note_on = deb_note_on,
    .render = debris_render,
    .knob = {P_E1, P_E2, P_E3, P_E4},                     /* HOME: SCAN WARP FOLD CLARITY */
    .keep = 0x03,                                         /* the low-pass */
};

/* FM: {SIG, SCAN (modulator), WARP (index), FOLD (feedback), CLARITY, RATIO, CAR, MENV} */
static const preset_t PFM_PRESETS[] = {
    {"CRYSTAL BELL", {0, 0, 60, 0, 0, 13, 0, 50}, {0, 90, 0, 70}, 0, 0, FX(0, 20, 40, 60), PAT(7)},
    {"ICE TINE", {0, 10, 40, 0, -6, 3, 0, 40}, {0, 84, 30, 50}, 0, 0, FX(0, 30, 20, 40), PAT(6)},
    {"GLASS PAD", {8, 40, 30, 10, -10, 1, 10, 0}, {80, 95, 110, 95}, 0, 0, FX(0, 60, 30, 100), PAT(5)},
    {"FM BASS", {0, 20, 50, 20, -20, 1, 0, 30}, {0, 60, 90, 16}, 0, 1, FX(20, 0, 0, 0), PAT(2)},
    {"SATELLITE", {13, 30, 40, 30, -8, 3, 0, 20}, {0, 70, 110, 30}, 0, 1, FX(10, 0, 40, 30), PAT(4)},
    {"CHIME ARRAY", {8, 60, 70, 0, 0, 15, 20, 60}, {0, 88, 0, 60}, 0, 0, FX(0, 20, 50, 70), PAT(7)},
    {"METAL RAIN", {10, 50, 90, 60, 10, 14, 40, 40}, {0, 76, 0, 60}, 0, 0, FX(10, 0, 50, 60), PAT(3)},
    {"DIGITAL EP", {0, 0, 35, 0, -4, 1, 0, 50}, {0, 86, 30, 50}, 0, 0, FX(0, 30, 20, 40), PAT(6)},
    {"PULSE CLOCK", {1, 30, 60, 10, 0, 7, 0, 63}, {0, 50, 0, 20}, 0, 0, FX(0, 0, 40, 30), PAT(13)},
    {"GHOST ORGAN", {4, 20, 25, 0, -6, 3, 30, 0}, {0, 0, 127, 20}, 0, 0, FX(0, 40, 20, 50), PAT(6)},
};
static const engine_t ENG_PFM = {
    .name = "FM",
    .page_title = {"SIGNAL", "FM"},
    .edit = {
        {"SIG", F_ENUM, 0, SIG_N - 1, 0, N_SIG_NUM, 0},
        {"SCAN", F_PCT, 0, 127, 0, 0, 0},
        {"WARP", F_PCT, 0, 127, 40, 0, 0},
        {"FOLD", F_PCT, 0, 127, 0, 0, 0},
        {"CLARITY", F_BIPCT, -64, 63, 0, 0, 0},
        {"RATIO", F_INT, 0, 15, 1, N_CYC_RATIO, 0},
        {"CAR", F_PCT, 0, 127, 0, 0, 0},
        {"MENV", F_BIPCT, -64, 63, 0, 0, 0},
    },
    .presets = PFM_PRESETS,
    .npresets = NELEM(PFM_PRESETS),
    .note_on = pfm_note_on,
    .render = pfm_render,
    .knob = {P_E1, P_E2, P_E3, P_E4},
    .keep = 0x03,
};
#endif
