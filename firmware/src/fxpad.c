/* SPDX-License-Identifier: GPL-3.0-only
 * Phantom Cosmonaut M5: the FX PAD (Ed, Nov 2026: "a baked in Kaoss Pad on a per step basis"); Phantom Tracker 0.9.6
 * kept four of the fifteen, FILTR GATE ECHO FLANG, each crosshair playing two at once, one an axis, on the whole mix;
 * **0.9.15 (Ed: "flip the per track - master effects back ... the X/Y being per track and the glitch effects being over
 * all"): the pad is the FEED's again**, the selected FEED's (ALGORITHM picks another), the four of 0.9.6 kept. FX's
 * first page is the pad: two crosshairs, KNOB 1 / 2 the first's X / Y (the solid one), KNOB 3 / 4 the second's:
 *   KNOB 1  FILTR  left of centre a low-pass closing (to ~80 Hz), right a high-pass opening (to ~5 kHz)
 *   KNOB 2  GATE   the FEED chopped on the tempo: up 1/16, then 1/32; down 1/8, then 1/4; deeper as it leaves the centre
 *   KNOB 3  ECHO   left 1/8 repeats, right dotted-1/8 ping-pong; more feedback (to 0.85) further out; the repeats
 *                  ring out after the pad is left (the time glides with the tempo, a tape's pitch bend)
 *   KNOB 4  FLANG  up a jet (feedback +), down hollow (feedback -), the sweep two bars, in time
 *   At the centre each is off. Live: an axis sounds once its knob is turned (on) and the crosshairs go back to the
 *     centre when the pad is left, opened, or moved to another FEED. Nothing is kept: playing with the pad changes nothing.
 *   REC on the pad prints: while the sequencer plays, each step of the FEED's that plays is written with the pad as it
 *     is (fxp, a pattern's own, as its motion: the bank, songs and projects keep it); a printed step plays it from then
 *     on, the pad open or not (open and touched on that FEED, the pad wins). OCT- on the pad clears the FEED's prints.
 * The DSP (fx.c mix_part / mix_spread, after the SLICER and the FX layer's mute, before the level, pan and sends): per
 * FEED on its mono sum (a SPREAD FEED's side narrows as FILTR or FLANG comes in, and is gated as the sum is): the FX
 * layer's state-variable filter (perform.c PF_SVF), a gate, a flanger on its own line (xyf, 712 B a FEED). ECHO is one
 * echo of two lines at 11 kHz (xyb, 32 KB: too much RAM for four) that each FEED sends into by its own crosshair (the
 * bus xeb, added after the parts like the glitch bus); its time and feedback follow the FEED sending the most. Only
 * that FEED's sound echoes, so the drums can echo while the bass stays dry. Included by fx.c */
enum { XM_FILTER, XM_GATE, XM_ECHO, XM_FLANGE, XM_N };
static const char *const XM_NAME[XM_N] = {"FILTR", "GATE", "ECHO", "FLANG"};
#define XM_DEAD 2                                     /* |v - 64| up to this: the axis off */

typedef struct { uint8_t type, x, y, on; } xy_val_t;
static fxp_store_t fxp __attribute__((section(".pool")));   /* the current pattern's prints (as motion is its motion) */
static xy_val_t xy_pr[NTRK][2];                 /* each FEED's prints of the step playing (the ISR's; .on 0: none) */
static xy_val_t xy_live[2];                     /* the pad as the hands have it (the main loop's) */
static volatile uint8_t xy_trk = 0xFFu;         /* the FEED the pad is open on (0xFF closed) */
static volatile uint8_t xy_print;               /* REC on the pad: printing */
static volatile uint8_t xy_full;                /* a print found no room (FXP_MAX): the UI says so */
#define XYB_LEN 8192u                           /* ECHO: a line, 11.025 kHz samples (743 ms) */
static int16_t xyb[2][XYB_LEN] __attribute__((section(".pool")));
static int16_t xyf[NTRK][FL_LEN] __attribute__((section(".pool")));   /* each FEED's FLANG line, at half the level */
typedef struct {
    int32_t fz[2], fc, fw, fhp;                 /* FILTR: the SVF's state, its cutoff (Q8 index), share, HP */
    int16_t fk[3];
    int32_t gg;                                 /* GATE: the gain now, Q15 */
    int32_t lw, lfb;                            /* FLANG: share, feedback (Q15, signed) */
    uint32_t lwp, ln;                           /* .. the frame written next, frames since it started */
    int32_t es;                                 /* ECHO: its send into the echo, Q15 (mix_part glides it) */
} xs_t;
static xs_t xpf[NTRK] = {{.gg = 32768}, {.gg = 32768}, {.gg = 32768}, {.gg = 32768}, {.gg = 32768}};
static struct {
    int32_t ow, efb, hold[2], lp[2], rd[2];     /* ECHO: output share, feedback, the 4-sample sums, the repeats
                                                 * smoothed, read */
    uint32_t wp, cnt, d, tail;                  /* the line's write, the time (line samples, gliding), ringing left */
    uint8_t pp, eon;                            /* ping-pong; the lines are live */
} xm;

/* the value FEED k's crosshair j plays: the pad's, open on k and touched; else the step's print */
static xy_val_t xy_now(uint32_t k, uint32_t j)
{
    return xy_trk == k && xy_live[j].on ? xy_live[j] : xy_pr[k][j];
}
/* an axis: -64 .. 63 from the centre, 0 when off (not touched, or within XM_DEAD of the centre) */
static int32_t xm_axis(xy_val_t v, int y)
{
    int32_t a = (int32_t)(y ? v.y : v.x) - 64;
    return v.on && (a > XM_DEAD || a < -XM_DEAD) ? a : 0;
}
/* anything for FEED k's pad to do this block (fx.c mix_part) */
static int xy_feed_want(uint32_t k)
{
    const xs_t *s = &xpf[k];
    return xy_pr[k][0].on || xy_pr[k][1].on || (xy_trk == k && (xy_live[0].on || xy_live[1].on)) || s->fw ||
           s->gg != 32768 || s->lw || s->es;
}
/* ECHO: the axis of the FEED sending the most (0: none), and which */
static int32_t xy_echo_lead(void)
{
    uint32_t k;
    int32_t best = 0;
    for (k = 0; k < NTRK; k++) {
        int32_t a = xm_axis(xy_now(k, 1u), 0);
        if ((a < 0 ? -a : a) > (best < 0 ? -best : best))
            best = a;
    }
    return best;
}
/* the echo to run this block (fx.c mix_block: perf_master) */
static int xy_want(void)
{
    return xm.eon || xy_echo_lead() != 0;
}
static __attribute__((noinline)) void xf_filter(xs_t *s, int32_t a, int32_t *b, int32_t *d, uint32_t n)
{
    uint32_t i, k;
    int32_t hp = a > 0, ta = a < 0 ? -a : a, tc, tw;
    if (!s->fw && a) {
        s->fhp = hp;                                    /* (from rest: the side it is on, from its open end) */
        s->fc = hp ? 0 : PF_TOP;
    }
    tw = a && hp == s->fhp ? 32768 : 0;                 /* (crossed over: out, then in on the other side) */
    tc = s->fhp ? ta * (52 << 8) / 64 : PF_TOP - ta * (PF_TOP - (10 << 8)) / 64;
    if (!a)
        tc = s->fhp ? 0 : PF_TOP;
    s->fc += clamp(tc - s->fc, -192, 192);
    for (k = 0; k < 3u; k++) {
        int32_t j = s->fc >> 8, f = s->fc & 255;
        s->fk[k] = (int16_t)(PF_SVF[j][k] + (((PF_SVF[j < 63 ? j + 1 : 63][k] - PF_SVF[j][k]) * f) >> 8));
    }
    for (i = 0; i < n; i++) {
        int32_t x = b[i];
        s->fw += clamp(tw - s->fw, -SL_SLOPE, SL_SLOPE);
        b[i] = pf_mix(x, pf_svf(clamp(x >> 2, -32767, 32767), s->fz, s->fk, (int)s->fhp) << 2, s->fw);
        if (d)
            d[i] = pf_mix(d[i], 0, s->fw);              /* (a SPREAD FEED's side: filtered, it narrows) */
    }
    if (!s->fw && !tw) {
        s->fz[0] = s->fz[1] = 0;
        if (a)
            s->fhp = hp;
    }
}
static __attribute__((noinline)) void xf_gate(xs_t *s, int32_t a, int32_t *b, int32_t *d, uint32_t n)
{
    uint32_t i, bs = beat_samples(), len, at;
    int32_t ta = a < 0 ? -a : a, dep = (ta > 24 ? 24 : ta) * 32768 / 24;
    len = a > 0 ? (ta < 40 ? bs / 4u : bs / 8u) : (ta < 40 ? bs / 2u : bs);
    if (len < 64u)
        len = 64u;
    at = (pf.ph - n) % len;                             /* (in time with the transport: perform.c's clock) */
    for (i = 0; i < n; i++) {
        uint32_t pos = at + i;
        int32_t t;
        while (pos >= len)
            pos -= len;
        t = !a || pos < len / 2u ? 32768 : 32768 - dep;
        s->gg += (t - s->gg) >> 5;                      /* (~0.7 ms: no click) */
        if (s->gg > 32700 && t == 32768)
            s->gg = 32768;                              /* (the last steps: >> 5 stops short) */
        b[i] = mulq16(b[i], (uint32_t)s->gg << 1);
        if (d)
            d[i] = mulq16(d[i], (uint32_t)s->gg << 1);
    }
}
static __attribute__((noinline)) void xf_flange(xs_t *s, int16_t *line, int32_t a, int32_t *b, int32_t *d, uint32_t n)
{
    uint32_t i, per = 32u * (beat_samples() >> 2), inc = 0xFFFFFFFFu / (per | 1u), q = ((pf.ph - n) % (per | 1u)) * inc;
    int32_t ta = a < 0 ? -a : a, tw = a ? (ta > 16 ? 16 : ta) * 2048 : 0;
    if (a)
        s->lfb = (a > 0 ? 1 : -1) * ta * 384;           /* (to +-0.74) */
    if (!s->lw && !a) {
        s->ln = 0;
        return;
    }
    if (!s->ln)
        memset(line, 0, FL_LEN * sizeof line[0]);
    for (i = 0; i < n; i++, q += inc) {
        uint32_t t = pf_tri(q), dl = (FL_MIN << 16) + (FL_MAX - FL_MIN) * ((t * t) >> 16);
        int32_t r0 = (int32_t)s->lwp - (int32_t)(dl >> 16), r1, v, x = b[i], fr = (int32_t)((dl >> 1) & 0x7FFFu);
        s->lw += clamp((s->ln > FL_MAX + 1u ? tw : 0) - s->lw, -SL_SLOPE, SL_SLOPE);
        if (r0 < 0)
            r0 += (int32_t)FL_LEN;
        r1 = r0 ? r0 - 1 : (int32_t)FL_LEN - 1;
        v = line[r0] + (((line[r1] - line[r0]) * fr) >> 15);
        line[s->lwp] = (int16_t)clamp((x >> 1) + softclip(mulq15(v, s->lfb)), -32767, 32767);
        b[i] = pf_mix(x, mulq16((x >> 1) + v, 55706u), s->lw);
        if (d)
            d[i] = pf_mix(d[i], 0, s->lw >> 1);
        if (++s->lwp >= FL_LEN)
            s->lwp = 0;
        if (s->ln <= FL_MAX + 1u)
            s->ln++;
    }
}
/* FEED t's pad on its mono sum b (and a SPREAD FEED's side d; 0: none): FILTR, GATE, FLANG (ECHO is its send: fx.c) */
static __attribute__((noinline)) void xy_feed(const track_t *t, int32_t *b, int32_t *d, uint32_t n)
{
    uint32_t k = (uint32_t)(t - trk) % NTRK;
    xs_t *s = &xpf[k];
    xy_val_t v0 = xy_now(k, 0), v1 = xy_now(k, 1);
    int32_t f = xm_axis(v0, 0), g = xm_axis(v0, 1), l = xm_axis(v1, 1);
    if (f || s->fw)
        xf_filter(s, f, b, d, n);
    if (g || s->gg != 32768)
        xf_gate(s, g, b, d, n);
    if (l || s->lw)
        xf_flange(s, xyf[k], l, b, d, n);
}
/* FEED k's ECHO send this block: where it glides to (Q15) */
static int32_t xy_send_to(uint32_t k)
{
    return xm_axis(xy_now(k, 1), 0) ? 32768 : 0;
}
#define XM_TAIL (3u * FS)                            /* ECHO: rings out this long after it is let go */
/* the echo: in, the FEEDs' sends (at MASTER), into its lines; the repeats added to the mix bl br (fx.c perf_master) */
static __attribute__((noinline)) void xy_echo(const int32_t *in, int32_t *bl, int32_t *br, uint32_t n)
{
    uint32_t i, c, dt, bs = beat_samples() >> 2;      /* (a beat in line samples: 11.025 kHz) */
    int32_t a = xy_echo_lead(), ta = a < 0 ? -a : a;
    if (a) {
        if (!xm.eon) {                                  /* starting: the lines empty */
            memset(xyb, 0, sizeof xyb);
            memset(xm.hold, 0, sizeof xm.hold + sizeof xm.lp + sizeof xm.rd);
            xm.wp = 64;
            xm.cnt = 0;
            xm.d = 0;
            xm.ow = 0;
            xm.eon = 1;
        }
        xm.pp = a > 0;
        xm.efb = ta * 442;                              /* (to 0.85) */
        xm.tail = XM_TAIL;
    }
    if (!xm.eon)
        return;
    dt = xm.pp ? bs * 3u / 4u : bs / 2u;                /* dotted 1/8, 1/8 */
    dt = dt < 64u ? 64u : dt > XYB_LEN - 64u ? XYB_LEN - 64u : dt;
    if (!xm.d)
        xm.d = dt;
    for (i = 0; i < n; i++) {
        int32_t x[2] = {bl[i], br[i]};
        xm.ow += clamp((xm.tail ? 32768 : 0) - xm.ow, -SL_SLOPE, SL_SLOPE);
        xm.hold[0] += in[i] >> 3;
        if (++xm.cnt >= 4u) {                           /* a line sample: read the repeats, write in + feedback */
            int32_t v = xm.hold[0] >> 2, fbk[2];
            xm.cnt = 0;
            xm.hold[0] = 0;
            for (c = 0; c < 2u; c++) {
                xm.rd[c] = xyb[c][(xm.wp - xm.d) & (XYB_LEN - 1u)];
                fbk[c] = (xm.rd[c] * xm.efb) >> 15;
            }
            if (xm.pp) {                                /* ping-pong: in into the left, each side into the other */
                xyb[0][xm.wp] = (int16_t)clamp(v + fbk[1], -32767, 32767);
                xyb[1][xm.wp] = (int16_t)clamp(fbk[0], -32767, 32767);
            } else {
                xyb[0][xm.wp] = (int16_t)clamp(v + fbk[0], -32767, 32767);
                xyb[1][xm.wp] = (int16_t)clamp(v + fbk[1], -32767, 32767);
            }
            xm.wp = (xm.wp + 1u) & (XYB_LEN - 1u);
            xm.d += xm.d < dt ? 1u : xm.d > dt ? (uint32_t)-1 : 0u;   /* (the time glides: a tape's bend) */
        }
        for (c = 0; c < 2u; c++) {
            xm.lp[c] += (xm.rd[c] - xm.lp[c]) >> 2;
            x[c] += mulq16((xm.lp[c] * 45) >> 3, (uint32_t)xm.ow << 1);   /* (the repeats at ~0.7) */
        }
        bl[i] = x[0];
        br[i] = x[1];
        if (xm.tail && !a)
            xm.tail--;
    }
    if (!xm.tail && !xm.ow)
        xm.eon = 0;                                     /* rung out */
}

/* ---------------------------------------------------------------------------------------------- the prints --- */
/* Phantom Tracker 0.9.15: a pad print is FEED k's (0.10: place = its step, k in ty's bits 1..3: five FEEDs), ty bit 0
 * crosshair 1 on, bit 4 crosshair 2, x1 .. y2 their X Y (bit 7 clear), in a store marked FXP_FEEDS (rsv; a project's mark PROJ_FXP_MARK2).
 * A store without the mark holds 0.9.6–0.9.14's prints of the mix (place = the step): each plays on every FEED, as the
 * mix's did, until the store is printed into (fxp_feeds: then each becomes four, one a FEED). 0.9.4's per-FEED prints
 * were read as the mix's by 0.9.6 (fxp_bound: the first of a step's kept) and stay so */
#define FXP_ATP(e) (((e).x1 & 0x80u) != 0u)            /* an ATTRITION print (below) */
#define FXP_FEEDS 1u                                   /* rsv: the prints are per FEED (0.9.15) */
#define FXP_FEED(e) (((uint32_t)(e).ty >> 1) & 7u)             /* a pad print's FEED (0.10) */
static int32_t fxp_find(const fxp_store_t *st, uint32_t k, uint32_t place)
{
    uint32_t i;
    for (i = 0; i < st->count && i < FXP_MAX; i++) {
        const fxp_ev_t *e = &st->ev[i];
        if ((e->place & 63u) == (place & 63u) && e->ty && !FXP_ATP(*e) &&
            (!(st->rsv & FXP_FEEDS) || FXP_FEED(*e) == k))
            return (int32_t)i;
    }
    return -1;
}
/* a store of the mix's prints made per FEED: each print one a FEED (as many as fit) */
static void fxp_feeds(fxp_store_t *st)
{
    uint32_t i, k, n = st->count;
    if (st->rsv & FXP_FEEDS)
        return;
    for (i = 0; i < n && i < FXP_MAX; i++)
        if (st->ev[i].ty && !FXP_ATP(st->ev[i])) {
            st->ev[i].place &= 63u;
            st->ev[i].ty &= 0x11u;
            for (k = 1; k < NTRK && st->count < FXP_MAX; k++) {
                st->ev[st->count] = st->ev[i];
                st->ev[st->count++].ty = (uint8_t)(st->ev[i].ty | k << 1);
            }
        }
    st->rsv |= FXP_FEEDS;
}

/* ------------------------------------------------------------------------------- ATTRITION's prints (0.9.4) ---
 * Ed: REC armed and FX held while playing should print ATTRITION, as the FX PAD prints. They ride in the same store,
 * so every place that keeps the pad's prints (the pattern, the FREQ bank, song slots, the project, ORBIT's sources)
 * keeps them as they are: an event on FEED 1's step (its step & 63), x1 y1 x2 y2 the four ATTRITION knobs (0..127
 * over each one's range; bit 7 of x1 marks it), the gate held (0 none, 1 + PF_* the effect) in the place's bits 6, 7
 * and bit 7 of y1 x2 y2 (3). One gate a step: the lowest held. ty: the FEEDs it was printed on (0.9.6: the layer's
 * targets, perform.c pf_aim; 0, a 0.9.4 print: all four, the master's then).
 * Played on FEED 1's steps: the knobs as printed, the gate held for the step (perform.c perf_begin), live FX held
 * wins; recorded on FEED 1's steps while armed, playing and FX held: the step as it is (nothing up: the print there
 * goes, an overdub erases). FX + OCT- with REC armed clears them all */
static volatile uint8_t atp_live;                    /* the ISR's: FX held now (seq.c keyboard_block) */
static volatile uint8_t atp_on;                      /* a printed step's values are out (to spring back after) */
static int32_t atp_find(const fxp_store_t *st, uint32_t step)
{
    uint32_t i;
    for (i = 0; st && i < st->count && i < FXP_MAX; i++)
        if (FXP_ATP(st->ev[i]) && (st->ev[i].place & 63u) == (step & 63u))
            return (int32_t)i;
    return -1;
}
static uint32_t atp_count(uint64_t *steps)
{
    uint32_t i, n = 0;
    uint64_t m = 0;
    for (i = 0; i < fxp.count && i < FXP_MAX; i++)
        if (FXP_ATP(fxp.ev[i])) {
            n++;
            m |= 1ull << (fxp.ev[i].place & 63u);
        }
    if (steps)
        *steps = m;
    return n;
}
static uint32_t atp_q(uint32_t e, int32_t v)          /* an ATTRITION value -> 0..127 */
{
    int32_t lo = mk_min(e);
    return (uint32_t)clamp((v - lo) * 127 / (100 - lo), 0, 127);
}
static int32_t atp_v(uint32_t e, uint32_t q)          /* .. and back */
{
    int32_t lo = mk_min(e);
    return lo + (int32_t)(q & 127u) * (100 - lo) / 127;
}
/* step `step` of FEED 1 starts (seq.c seq_play): print it (armed, FX held), then play what is printed there */
static void atp_step(uint32_t step, const fxp_store_t *st, uint32_t play)
{
    int32_t i;
    uint32_t c;
    if (st == &fxp && atp_live && song.rec && song.playing) {   /* printing: the step as it is */
        uint32_t g = 0, held = perf_held & ((1u << PF_NFX) - 1u), any = 0;
        fxp_ev_t e;
        for (c = 0; c < PF_NFX && !g; c++)
            if ((held >> c) & 1u)
                g = c + 1u;
        e.place = (uint8_t)((step & 63u) | (g & 3u) << 6);
        e.ty = (uint8_t)(pf_aim & TRK_ALL);
        for (c = 0; c < 4u; c++) {
            uint32_t ef = mk_eff(c), q = atp_q(ef, *mk_val(ef));
            any |= *mk_val(ef) != 0;
            (&e.x1)[c] = (uint8_t)(q | (c == 0u ? 0x80u : ((g >> (c + 1u)) & 1u) << 7));
        }
        i = atp_find(&fxp, step);
        if (!any && !g) {                                /* nothing up: the print there goes */
            if (i >= 0) {
                fxp.ev[i] = fxp.ev[fxp.count - 1u];
                memset(&fxp.ev[--fxp.count], 0, sizeof fxp.ev[0]);
            }
        } else if (i >= 0)
            fxp.ev[i] = e;
        else if (fxp.count < FXP_MAX)
            fxp.ev[fxp.count++] = e;
        else
            xy_full = 1;
        return;                                          /* (live: what the hands do, nothing printed plays) */
    }
    if (atp_live)
        return;
    i = st && play ? atp_find(st, step) : -1;
    if (i < 0) {                                         /* no print here: back to rest, if a print had moved them */
        if (atp_on) {
            for (c = 0; c < 4u; c++)
                *mk_val(mk_eff(c)) = 0;
            atp_on = 0;
            atp_gate = 0;
            atp_tgt = 0;
        }
        return;
    }
    {
        const fxp_ev_t *e = &st->ev[i];
        uint32_t g = (uint32_t)(e->place >> 6) | ((uint32_t)(e->y1 >> 7) << 2) | ((uint32_t)(e->x2 >> 7) << 3) |
                     ((uint32_t)(e->y2 >> 7) << 4);
        for (c = 0; c < 4u; c++) {
            uint32_t ef = mk_eff(c);
            *mk_val(ef) = (int8_t)atp_v(ef, (&e->x1)[c]);
        }
        atp_gate = g && g <= PF_NFX ? PF_BIT(g - 1u) : 0u;
        atp_tgt = (uint8_t)(e->ty & TRK_ALL ? e->ty & TRK_ALL : TRK_ALL);
        atp_on = 1;
    }
}
/* the transport stopped, or FX taken in hand: printed values back to rest */
static void atp_stop(void)
{
    uint32_t c;
    if (atp_on && !atp_live)
        for (c = 0; c < 4u; c++)
            *mk_val(mk_eff(c)) = 0;
    atp_on = 0;
    atp_gate = 0;
    atp_tgt = 0;
}
/* every ATTRITION print gone (interrupts off, or the ISR) */
static uint32_t atp_clear(void)
{
    uint32_t i, n = 0, was = 0;
    for (i = 0; i < fxp.count && i < FXP_MAX; i++)
        if (!FXP_ATP(fxp.ev[i]))
            fxp.ev[n++] = fxp.ev[i];
        else
            was++;
    memset(fxp.ev + n, 0, (FXP_MAX - n) * sizeof fxp.ev[0]);
    fxp.count = (uint8_t)n;
    atp_stop();
    return was;
}
/* a step of FEED t starts (seq.c seq_play): its pad prints (st: the pattern playing, 0 none) for the step; the pad
 * printing on t: the pad written first */
static void fxp_step(track_t *t, uint32_t step, const fxp_store_t *st, uint32_t play)
{
    uint32_t place = step & 63u, k = (uint32_t)(t - trk);
    int32_t i;
    if (k >= NTRK)
        return;
    if (st == &fxp && xy_print && xy_trk == k && (xy_live[0].on || xy_live[1].on)) {
        fxp_ev_t e;
        fxp_feeds(&fxp);
        e.place = (uint8_t)place;
        e.ty = (uint8_t)((xy_live[0].on ? 1u : 0u) | (xy_live[1].on ? 0x10u : 0u) | k << 1);
        e.x1 = xy_live[0].x & 127u; e.y1 = xy_live[0].y & 127u; e.x2 = xy_live[1].x & 127u; e.y2 = xy_live[1].y & 127u;
        if ((i = fxp_find(&fxp, k, place)) >= 0)
            fxp.ev[i] = e;
        else if (fxp.count < FXP_MAX)
            fxp.ev[fxp.count++] = e;
        else
            xy_full = 1;
    }
    i = st && play ? fxp_find(st, k, place) : -1;
    if (i < 0) {
        xy_pr[k][0].on = xy_pr[k][1].on = 0;
        return;
    }
    {
        const fxp_ev_t *e = &st->ev[i];
        xy_pr[k][0].x = e->x1; xy_pr[k][0].y = e->y1; xy_pr[k][0].on = (e->ty & 1u) != 0;
        xy_pr[k][1].x = e->x2; xy_pr[k][1].y = e->y2; xy_pr[k][1].on = (e->ty >> 4) != 0;
    }
}
/* the transport stopped: no step plays a print (the pad's live ones still sound) */
static void fxp_stop(void)
{
    memset(xy_pr, 0, sizeof xy_pr);
    atp_stop();
}
/* store st's entries in range and order, the rest zero (a store read back: the bank, a song, a project) */
static void fxp_bound(fxp_store_t *st)
{
    uint32_t i, n = 0, feeds = st->rsv & FXP_FEEDS;
    if (st->count > FXP_MAX)
        st->count = 0;
    for (i = 0; i < st->count; i++) {
        fxp_ev_t e = st->ev[i];
        if (FXP_ATP(e)) {                                /* ATTRITION's (0.9.4): marked, one a step */
            if ((e.place & 63u) >= NSTEP || atp_find(st, e.place & 63u) != (int32_t)i)
                continue;
            e.ty &= TRK_ALL;
            st->ev[n++] = e;
            continue;
        }
        if (!e.ty || (e.place & 63u) >= NSTEP || fxp_find(st, FXP_FEED(e), e.place) != (int32_t)i || FXP_FEED(e) >= NTRK)
            continue;                                    /* (one a step, a FEED's; the mix's: the first of a step's) */
        e.place &= 63u;
        e.ty = (uint8_t)((e.ty & 1u) | (e.ty & 0x10u) | (feeds ? e.ty & 0x0Eu : 0u));
        e.y1 &= 127u; e.x2 &= 127u; e.y2 &= 127u;
        st->ev[n++] = e;
    }
    memset(st->ev + n, 0, (FXP_MAX - n) * sizeof st->ev[0]);
    st->count = (uint8_t)n;
    st->rsv = (uint8_t)feeds;
}
/* a pad print of FEED k's (the mix's: every FEED's) */
static int fxp_on(const fxp_store_t *st, const fxp_ev_t *e, uint32_t k)
{
    return e->ty && !FXP_ATP(*e) && (!(st->rsv & FXP_FEEDS) || k >= NTRK || FXP_FEED(*e) == k);
}
/* the pad's prints in the current pattern of the FEED it is open on (closed: all): how many, and on which steps */
static uint32_t fxp_count(uint64_t *steps)
{
    uint32_t i, n = 0;
    uint64_t m = 0;
    for (i = 0; i < fxp.count && i < FXP_MAX; i++)
        if (fxp_on(&fxp, &fxp.ev[i], xy_trk)) {
            n++;
            m |= 1ull << (fxp.ev[i].place & 63u);
        }
    if (steps)
        *steps = m;
    return n;
}
/* the pad's prints of the FEED it is open on cleared (closed: all; interrupts off, or the ISR); ATTRITION's kept */
static void fxp_clear_pad(void)
{
    uint32_t i, n = 0, k = xy_trk;
    if (k < NTRK)
        fxp_feeds(&fxp);
    for (i = 0; i < fxp.count && i < FXP_MAX; i++)
        if (!fxp_on(&fxp, &fxp.ev[i], k))
            fxp.ev[n++] = fxp.ev[i];
    memset(fxp.ev + n, 0, (FXP_MAX - n) * sizeof fxp.ev[0]);
    fxp.count = (uint8_t)n;
    for (i = 0; i < NTRK; i++)
        if (k >= NTRK || i == k)
            xy_pr[i][0].on = xy_pr[i][1].on = 0;
}

/* --------------------------------------------------------------------------------- the pad (the main loop) --- */
/* the pad opened on FEED k (0xFF: closed): the crosshairs at the centre, untouched, nothing printing */
static void xy_open(uint32_t k)
{
    uint32_t j;
    k = k < NTRK ? k : 0xFFu;
    if (xy_trk == k)
        return;
    xy_print = 0;
    for (j = 0; j < 2u; j++) {
        xy_live[j].on = 0;
        xy_live[j].x = xy_live[j].y = 64;
        xy_live[j].type = (uint8_t)j;
    }
    xy_trk = (uint8_t)k;
}
/* KNOB c's card on the pad: what it is at (val: "LP 40", "1/16", "PING", "JET"; "--" off), its bar 0..1000 (-1 off) */
static int32_t xm_card(uint32_t c, char *val, uint32_t sz)
{
    const xy_val_t *v = &xy_live[(c >> 1) & 1u];
    int32_t a = xm_axis(*v, (int)(c & 1u)), ta = a < 0 ? -a : a;
    if (!a) {
        str_cpy(val, "--", sz);
        return -1;
    }
    switch (c & 3u) {
    case XM_FILTER: str_cpy(val, a < 0 ? "LP " : "HP ", sz); fmt_int(val + 3, ta * 100 / 64); break;
    case XM_GATE: str_cpy(val, a > 0 ? (ta < 40 ? "1/16" : "1/32") : (ta < 40 ? "1/8" : "1/4"), sz); break;
    case XM_ECHO: str_cpy(val, a > 0 ? "PING" : "1/8", sz); break;
    default: str_cpy(val, a > 0 ? "JET" : "HOLLOW", sz); break;
    }
    return ta * 1000 / 64;
}
/* KNOB 1..4 on the pad: crosshair k / 2's X (even) or Y (odd); the first turn touches it (it sounds) */
static void xy_knob(uint32_t knob, int32_t steps)
{
    xy_val_t *v = &xy_live[(knob >> 1) & 1u];
    uint8_t *a = knob & 1u ? &v->y : &v->x;
    *a = (uint8_t)clamp((int32_t)*a + steps, 0, 127);
    v->on = 1;
}
