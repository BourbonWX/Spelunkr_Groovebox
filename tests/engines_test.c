/* SPDX-License-Identifier: GPL-3.0-only
 * Phantom Tracker: the engines after MORPH: DEBRIS and FM (src/eng_morph.c), on the host through hostsim.c; the ones
 * back from Dungeon Tracker (GENESIS ANALOG SID) in the keys' order (their sounds: genesis_test.c, sid_test.c).
 *   build/host/engines_test [DEMODIR]
 * 1. every factory sound sounds, no DC, its voices free, the level near MORPH's. 2. the knobs do what each engine
 * says (DEBRIS: never static, Ed's "less harsh"). 3. EDIT's scope draws DEBRIS's and FM's. 4. cost against MORPH.
 * 5. a demo of every sound (DEMODIR/debris.wav, fm.wav, genesis.wav; glorp.wav: sketch F's riff, soft .. hard, on each).
 * GLORP (0.9.8): the wah opens with the velocity and closes, WAH SENS VOWEL DIP do what they say. */
#define main hostsim_main
#include "hostsim.c"
#undef main
#ifndef CAT_BASS
#include "category.c"                   /* (preset_cat: hostsim has no ui.c) */
#endif

static int fails;
static void check(const char *what, int ok)
{
    printf("engines: %-91s %s\n", what, ok ? "ok" : "FAIL");
    fails += !ok;
}

#define NMAX (FS * 6)
static float buf[NMAX];
static uint32_t buf_n;

static void parts_clear(void)
{
    uint32_t p;
    host_tracks_init();
    memset(eng_mem, 0, sizeof eng_mem);
    vage = 0;                                      /* (DEBRIS seeds its grains by the voice's age: each render alike) */
    for (p = 0; p < NPART; p++)
        memset(trk[p].v, 0, sizeof trk[p].v);
}
static int32_t render_part(uint32_t part, uint32_t n)
{
    int32_t o[CTL], pk = 0;
    uint32_t i, k;
    if (n > NMAX)
        n = NMAX;
    for (i = 0; i < n; i += CTL) {
        track_render(&trk[part], o, CTL);
        for (k = 0; k < CTL && i + k < n; k++) {
            int32_t a = abs(o[k]);
            pk = a > pk ? a : pk;
            buf[i + k] = o[k] / 32768.0f;
        }
    }
    buf_n = n;
    return pk;
}
static double rms(uint32_t a, uint32_t e)
{
    double s = 0;
    for (uint32_t i = a; i < e && i < buf_n; i++)
        s += (double)buf[i] * buf[i];
    return sqrt(s / (double)(e > a ? e - a : 1));
}
static uint32_t zc(uint32_t a, uint32_t e)
{
    uint32_t n = 0;
    for (uint32_t i = a + 1; i < e && i < buf_n; i++)
        n += (buf[i] >= 0) != (buf[i - 1] >= 0);
    return n;
}
static double bright(uint32_t a, uint32_t e)     /* the share of the first difference's energy: higher, brighter */
{
    double s = 0, d = 0;
    for (uint32_t i = a + 1; i < e && i < buf_n; i++) {
        s += (double)buf[i] * buf[i];
        d += (double)(buf[i] - buf[i - 1]) * (buf[i] - buf[i - 1]);
    }
    return s > 0 ? d / s : 0;
}
static double dc_from(uint32_t a)
{
    double s = 0, pk = 0;
    for (uint32_t i = a; i < buf_n; i++)
        s += buf[i], pk = fabs(buf[i]) > pk ? fabs(buf[i]) : pk;
    return pk > 0 ? fabs(s / (double)(buf_n - a)) / pk : 0;
}
static int voices_free(uint32_t part)
{
    for (uint32_t i = 0; i < NVOICE; i++)
        if (trk[part].v[i].active)
            return 0;
    return 1;
}

static double ns_per(uint32_t e, uint32_t pi)
{
    struct timespec a, b;
    uint32_t i;
    int32_t o[CTL];
    parts_clear();
    host_preset(&trk[1], e, pi);
    for (i = 0; i < 4u; i++) trk_note_on(&trk[1], 48 + 5 * i, 100);
    track_render(&trk[1], o, CTL);
    clock_gettime(CLOCK_MONOTONIC, &a);
    for (i = 0; i < FS * 2 / CTL; i++) track_render(&trk[1], o, CTL);
    clock_gettime(CLOCK_MONOTONIC, &b);
    return ((b.tv_sec - a.tv_sec) * 1e9 + (b.tv_nsec - a.tv_nsec)) / (FS * 2.0 * 4.0);
}
/* 5 */
static FILE *wav_open(const char *dir, const char *name, uint32_t frames)
{
    char path[512];
    FILE *f;
    snprintf(path, sizeof path, "%s/%s", dir, name);
    f = fopen(path, "wb");
    if (f)
        wav_hdr(f, frames);
    return f;
}
static uint32_t mixed;
static void mix_to(FILE *w, uint32_t frames)
{
    int32_t o[2 * CTL];
    uint32_t i, k;
    for (i = 0; i < frames; i += CTL) {
        mix_block(o, CTL);
        for (k = 0; k < CTL; k++)
            wav_put(w, o[2 * k], o[2 * k + 1]);
        mixed += CTL;
    }
}
static void raw(uint32_t part, int sig, int scan, int warp, int fold, int clar, int sub)
{
    host_preset(&trk[part], ENGI_MORPH, 0);
    trk[part].p[P_E0] = (int16_t)sig; trk[part].p[P_E1] = (int16_t)scan; trk[part].p[P_E2] = (int16_t)warp;
    trk[part].p[P_E3] = (int16_t)fold; trk[part].p[P_E4] = (int16_t)clar; trk[part].p[P_E5] = 0; trk[part].p[P_E6] = 0;
    trk[part].p[P_E7] = (int16_t)sub;
    trk[part].p[P_ATK] = 0; trk[part].p[P_DEC] = 64; trk[part].p[P_SUS] = 127; trk[part].p[P_REL] = 20;
    trk[part].p[P_ED_FLT] = 0;
}
static double rms_of(uint32_t e, uint32_t pi)
{
    parts_clear(); host_preset(&trk[1], e, pi); trk_note_on(&trk[1], 57, 127); render_part(1, FS / 2);
    return rms(FS / 4, FS / 2);
}
static double one(int sig, int scan, int warp, int fold, int clar, int sub, uint32_t note, double *br)
{
    parts_clear(); raw(1, sig, scan, warp, fold, clar, sub); trk_note_on(&trk[1], note, 100); render_part(1, FS / 2);
    if (br) *br = bright(FS / 10, FS / 2);
    return rms(FS / 10, FS / 2);
}

/* 1 every factory sound of engine e: sounds, never at full scale x3, no DC, its voices free after the release; the
 * sustained ones' level (one voice) within 4 dB of MORPH's */
static double morph_level_ref(void)
{
    uint32_t pi, n = 0;
    double w = 0;
    for (pi = 0; pi < ENG_MORPH.npresets; pi++)
        if (MORPH_PRESETS[pi].env[2] > 60) w += rms_of(ENGI_MORPH, pi), n++;
    return w / (n ? n : 1);
}
static void presets(uint32_t e, double ref)
{
    static const uint8_t CHORD[3] = {57, 60, 64};
    const engine_t *E = ENGINES[e];
    uint32_t pi, quiet = 0, dc = 0, held = 0, over = 0, nw = 0;
    double w = 0;
    char what[160];
    for (pi = 0; pi < E->npresets; pi++) {
        const preset_t *pr = &E->presets[pi];
        uint32_t part = pr->mono ? 2 : 1, k, rel;
        int32_t pk;
        parts_clear();
        host_preset(&trk[part], e, pi);
        if (pr->mono)
            trk_note_on(&trk[part], 36, 110);
        else
            for (k = 0; k < 3u; k++) trk_note_on(&trk[part], CHORD[k], 100);
        pk = render_part(part, FS * 6 / 10);
        quiet |= (pk < 2048) << pi;
        over |= (pk >= 32767 * 3) << pi;
        for (k = 0; k < 128u; k++) trk_note_off(&trk[part], k);
        rel = (uint32_t)(TIME_MS_X10[trk[part].p[P_REL] & 127] / 10u) * FS / 1000u * 2u + FS;
        render_part(part, rel < NMAX ? rel : NMAX);
        held |= !voices_free(part) << pi;
        parts_clear(); host_preset(&trk[1], e, pi); trk[1].p[P_SUS] = 127; trk[1].p[P_ED_FLT] = 0;
        trk_note_on(&trk[1], 48, 100); render_part(1, FS);
        dc |= (dc_from(FS / 2) > 0.02) << pi;
        if (pr->env[2] > 60) w += rms_of(e, pi), nw++;
        printf("engines:   %-8s %02u %-12s the part %5d (%.1f dBFS)\n", E->name, pi + 1u, pr->name, pk, 20.0 * log10((pk + 1) / 32768.0));
    }
    w /= nw ? nw : 1;
    snprintf(what, sizeof what, "%s: every sound sounds, no DC, voices free; level %.1f dB from MORPH's (q 0x%X o 0x%X d 0x%X h 0x%X)",
             E->name, 20.0 * log10(w / ref), quiet, over, dc, held);
    check(what, !quiet && !over && !dc && !held && fabs(20.0 * log10(w / ref)) < 4.0 && E->npresets >= 8u);
}
/* a held note of engine e, its E values set, rendered: brightness and rms after the attack */
static double tone(uint32_t e, const int16_t *v8, uint32_t note, double *br)
{
    uint32_t k;
    parts_clear(); host_preset(&trk[1], e, 0);
    for (k = 0; k < 8u; k++) trk[1].p[P_E0 + k] = v8[k];
    trk[1].p[P_ATK] = 0; trk[1].p[P_DEC] = 64; trk[1].p[P_SUS] = 127; trk[1].p[P_REL] = 20; trk[1].p[P_ED_FLT] = 0;
    trk_note_on(&trk[1], note, 100); render_part(1, FS / 2);
    if (br) *br = bright(FS / 10, FS / 2);
    return rms(FS / 10, FS / 2);
}
static double diff_of(uint32_t e, const int16_t *a8, const int16_t *b8)   /* the two renders' difference, rms */
{
    static float a[FS / 2];
    double s = 0;
    uint32_t i;
    tone(e, a8, 57, 0);
    memcpy(a, buf, sizeof a);
    tone(e, b8, 57, 0);
    for (i = FS / 10; i < FS / 2; i++) s += (double)(a[i] - buf[i]) * (a[i] - buf[i]);
    return sqrt(s / (FS / 2 - FS / 10));
}
/* the spectrum's centre between lo and hi Hz over buf (after the attack): a formant's place, roughly */
static double band_centre(double lo, double hi)
{
    uint32_t a = FS / 5, n = 8192, k, i;
    double num = 0, den = 0;
    for (k = (uint32_t)(lo * n / FS); k <= (uint32_t)(hi * n / FS); k++) {
        double re = 0, im = 0;
        for (i = 0; i < n; i++) {
            double w = 0.5 - 0.5 * cos(2 * M_PI * i / n), x = buf[a + i] * w;
            re += x * cos(2 * M_PI * k * i / n);
            im -= x * sin(2 * M_PI * k * i / n);
        }
        num += (re * re + im * im) * k * FS / n;
        den += re * re + im * im;
    }
    return den > 0 ? num / den : 0;
}
/* the normalised autocorrelation at note's period over buf: 1 a steady pitch, ~0 noise */
static double periodicity(uint32_t note)
{
    double f0 = 440.0 * pow(2.0, (note - 69.0) / 12.0), s = 0, a = 0, b = 0;
    uint32_t lag = (uint32_t)(FS / f0 + 0.5), i, st = FS / 5;
    for (i = st; i < st + FS / 10; i++) {
        s += (double)buf[i] * buf[i + lag];
        a += (double)buf[i] * buf[i];
        b += (double)buf[i + lag] * buf[i + lag];
    }
    return a > 0 && b > 0 ? s / sqrt(a * b) : 0;
}
/* engine e at v8 on a held note: the energy between the note's harmonics against the harmonics', dB (aliasing,
 * noise: static); a Hann-windowed DFT at bins a quarter of a harmonic apart */
static double inharmonic(uint32_t e, const int16_t *v8, uint32_t note)
{
    double f0 = 440.0 * pow(2.0, (note - 69.0) / 12.0), h = 0, x = 0;
    uint32_t n = 8192, st = FS / 5, i, k;                /* (inside tone's half second) */
    tone(e, v8, note, 0);
    for (k = 1; f0 * k / 4.0 < 20000.0; k++) {
        double f = f0 * k / 4.0, re = 0, im = 0;
        for (i = 0; i < n; i++) {
            double w = 0.5 - 0.5 * cos(2 * M_PI * i / n), s = buf[st + i] * w;
            re += s * cos(2 * M_PI * f * i / FS);
            im -= s * sin(2 * M_PI * f * i / FS);
        }
        if (k % 4u == 0u)
            h += re * re + im * im;
        else if (k % 4u == 2u)
            x += re * re + im * im;
    }
    return 10.0 * log10((x + 1e-20) / (h + 1e-20));
}
/* 2 the contort knobs on each engine */
static void controls(void)
{
    double b0, b1, r0, r1;
    char what[160];
    {   /* DEBRIS: FOLD folds (brighter) and stays harmonic at its top on a high note (no static: Ed's "less harsh");
         * WARP sizes the grains, SCATTER and DRIFT move them, CLARITY's left darkens, its right shimmers (an octave) */
        int16_t a[8] = {0, 20, 96, 0, 0, 0, 0, 0}, b[8] = {0, 20, 96, 127, 0, 0, 0, 0}, c[8] = {0, 20, 96, 0, 0, 127, 0, 0};
        int16_t d[8] = {0, 20, 20, 0, 0, 60, 0, 60}, f[8] = {0, 20, 96, 0, -64, 0, 0, 0}, g[8] = {0, 20, 96, 0, 63, 0, 0, 0};
        int16_t h[8] = {0, 20, 96, 0, 0, 0, 0, 127}, w[8] = {15, 60, 96, 127, 0, 0, 0, 0};
        double inh;
        tone(ENGI_DEBRIS, a, 45, &b0); r0 = tone(ENGI_DEBRIS, b, 45, &b1);
        snprintf(what, sizeof what, "DEBRIS: FOLD folds a sine (brightness %.4f -> %.4f), the level kept (rms %.3f)", b0, b1, r0);
        check(what, b1 > b0 * 3.0 && r0 > 0.05);
        inh = inharmonic(ENGI_DEBRIS, w, 81);
        snprintf(what, sizeof what, "DEBRIS: FOLD at the top, DARK SIDE, A5: between the harmonics %.1f dB down (no static)", inh);
        check(what, inh < -60.0);
        r0 = diff_of(ENGI_DEBRIS, a, c);
        r1 = diff_of(ENGI_DEBRIS, a, h);
        snprintf(what, sizeof what, "DEBRIS: SCATTER moves the grains (difference rms %.3f), DRIFT detunes them (%.3f)", r0, r1);
        check(what, r0 > 0.02 && r1 > 0.02);
        r0 = diff_of(ENGI_DEBRIS, a, d);
        snprintf(what, sizeof what, "DEBRIS: WARP down, short grains (difference rms %.3f)", r0);
        check(what, r0 > 0.02);
        tone(ENGI_DEBRIS, b, 57, &b0); tone(ENGI_DEBRIS, f, 57, &b1);
        snprintf(what, sizeof what, "DEBRIS: CLARITY far left closes the low-pass (brightness %.4f -> %.4f)", b0, b1);
        check(what, b1 < b0 * 0.5);
        tone(ENGI_DEBRIS, a, 57, &b0); tone(ENGI_DEBRIS, g, 57, &b1);
        snprintf(what, sizeof what, "DEBRIS: CLARITY right: SHIMMER, grains an octave up (brightness %.4f -> %.4f)", b0, b1);
        check(what, b1 > b0 * 1.2);
        {   /* the factory tapes on SIG 17..20 */
            int16_t t8[8] = {SIG_N + 3, 30, 110, 10, 0, 30, 0, 40};
            r0 = tone(ENGI_DEBRIS, t8, 48, 0);
            snprintf(what, sizeof what, "DEBRIS: SIG 20 reads the SKYWAVE tape (rms %.3f)", r0);
            check(what, r0 > 0.03 && sig_max(ENGI_DEBRIS) == SIG_N + FK_NTAPE - 1u && sig_max(ENGI_PFM) == SIG_N - 1u);
        }
    }
    {   /* FM: WARP is the index (brighter), RATIO another spectrum, FOLD the feedback */
        int16_t a[8] = {0, 0, 0, 0, 0, 1, 0, 0}, b[8] = {0, 0, 90, 0, 0, 1, 0, 0}, c[8] = {0, 0, 90, 0, 0, 13, 0, 0};
        int16_t d[8] = {0, 0, 80, 0, 0, 1, 0, 0}, f[8] = {0, 0, 80, 120, 0, 1, 0, 0};
        tone(ENGI_PFM, a, 57, &b0); tone(ENGI_PFM, b, 57, &b1);
        snprintf(what, sizeof what, "FM: WARP (the index) brightens a sine carrier (%.4f -> %.4f)", b0, b1);
        check(what, b1 > b0 * 2.0);
        r0 = diff_of(ENGI_PFM, b, c);
        snprintf(what, sizeof what, "FM: RATIO 1 -> 2.76 another spectrum (difference rms %.3f)", r0);
        check(what, r0 > 0.05);
        tone(ENGI_PFM, d, 57, &b0); tone(ENGI_PFM, f, 57, &b1);
        snprintf(what, sizeof what, "FM: FOLD (the feedback) brightens (%.4f -> %.4f)", b0, b1);
        check(what, b1 > b0 * 1.2);
    }
    {   /* the three: HOME's knobs SCAN WARP FOLD CLARITY, the four names on EDIT */
        static const uint32_t E4[3] = {ENGI_MORPH, ENGI_DEBRIS, ENGI_PFM};
        uint32_t k, ok = 1;
        for (k = 0; k < 3u; k++) {
            const engine_t *E = ENGINES[E4[k]];
            ok &= E->knob[0] == P_E1 && E->knob[1] == P_E2 && E->knob[2] == P_E3 && E->knob[3] == P_E4;
            ok &= !strcmp(E->edit[1].label, "SCAN") && !strcmp(E->edit[2].label, "WARP") && !strcmp(E->edit[3].label, "FOLD") &&
                  !strcmp(E->edit[4].label, "CLARITY") && !strcmp(E->edit[0].label, "SIG") && ENG_SIG(E4[k]);
            ok &= preset_cat(E4[k], 0) != CAT_OTHER && E->npresets == strlen(PRESET_CAT[E4[k]]);
        }
        check("the three SIGNAL engines: HOME's knobs SCAN WARP FOLD CLARITY, SIG first; a category a sound", ok);
        ok = NENG_SHOWN == 8u && ENGINE_ORDER[0] == ENGI_CKIT && ENGINE_ORDER[1] == ENGI_SAMPLE && ENGINE_ORDER[2] == ENGI_GENESIS &&
             ENGINE_ORDER[3] == ENGI_SID && ENGINE_ORDER[4] == 0u && ENGINE_ORDER[5] == ENGI_MORPH && ENGINE_ORDER[6] == ENGI_GLORP &&
             ENGINE_ORDER[7] == ENGI_PFM;
        for (k = 0; k < NENG_SHOWN; k++)
            ok &= eng_ok(ENGINE_ORDER[k]);
        ok &= !eng_ok(ENGI_DEBRIS) && !eng_ok(ENGI_VULT) && eng_subst(ENGI_DEBRIS) == ENGI_MORPH && eng_subst(ENGI_VULT) == 0u &&
              !eng_ok(ENGI_FOLDER) && !eng_ok(ENGI_GRAN) && !eng_ok(ENGI_VOX) && !eng_ok(ENGI_NUMBERS) &&
              eng_subst(ENGI_FOLDER) == ENGI_MORPH && eng_subst(ENGI_GRAN) == ENGI_MORPH && eng_subst(2) == ENGI_MORPH &&
              eng_subst(ENGI_VOX) == ENGI_MORPH && eng_subst(ENGI_NUMBERS) == ENGI_MORPH &&
              !strcmp(ENGINES[0]->name, "ANALOG") && !strcmp(ENGINES[ENGI_SID]->name, "SID") &&
              !strcmp(ENGINES[ENGI_GENESIS]->name, "GENESIS") && !strcmp(ENGINES[ENGI_SAMPLE]->name, "SAMPLE");
        check("Spelunkr: the keys KIT SAMPLE GENESIS SID ANALOG MORPH GLORP FM; DEBRIS -> MORPH, VULT -> ANALOG, FOLDER, "
              "GRANULAR, PHASE (once DEBRIS) -> MORPH, VOX and NUMBERS -> MORPH", ok);
    }
}
/* 3 the scope draws each of them, the knobs change it */
static void scopes(void)
{
    static const uint32_t E3[2] = {ENGI_DEBRIS, ENGI_PFM};
    int16_t a[220], b[220];
    uint32_t k, i, ok = 1;
    for (k = 0; k < 2u; k++) {
        uint32_t big = 0, diff = 0;
        parts_clear(); host_preset(&trk[1], E3[k], 0);
        morph_cycle(&trk[1], a, 220);
        trk[1].p[P_E1] = 100; trk[1].p[P_E3] = 110;
        morph_cycle(&trk[1], b, 220);
        for (i = 0; i < 220u; i++) big |= abs(a[i]) > 8000, diff += abs(a[i] - b[i]) > 2000;
        ok &= big && diff > 20u;
    }
    check("EDIT's scope: DEBRIS's, FM's cycle drawn; SCAN and FOLD change it", ok);
}
/* GLORP (eng_glorp.c): a held note of velocity vel, E values v8, ms milliseconds in: where F2 is (kHz, the engine's own
 * formants: glo_formants); the render in buf */
static double glo_f2(const int16_t *v8, uint32_t vel, uint32_t ms, uint32_t *zcs)
{
    uint32_t k;
    int32_t f[3] = {0, 0, 0};
    parts_clear(); host_preset(&trk[1], ENGI_GLORP, 0);
    for (k = 0; k < 8u; k++) trk[1].p[P_E0 + k] = v8[k];
    trk[1].p[P_ATK] = 0; trk[1].p[P_DEC] = 64; trk[1].p[P_SUS] = 127; trk[1].p[P_REL] = 20; trk[1].p[P_ED_FLT] = 0;
    trk_note_on(&trk[1], 48, vel); render_part(1, FS * ms / 1000);
    if (zcs) *zcs = zc(0, FS * 30 / 1000);
    for (k = 0; k < NVOICE; k++)
        if (trk[1].v[k].active) glo_formants(trk[1].p, &trk[1].v[k], f);
    return f[1] / 1000.0;
}
static void glorp(void)
{
    static const int16_t DEF[8] = {64, 127, 64, 64, 64, 64, 127, 52}, EE[8] = {127, 127, 64, 64, 64, 64, 127, 52};
    static float a[FS / 2];
    int16_t v[8];
    double hard, soft, late, h0, l0, s0, s1, oh, ee, d = 0, r = 0;
    uint32_t z0, z1, i;
    char what[200];
    hard = glo_f2(EE, 127, 80, 0);
    soft = glo_f2(EE, 40, 80, 0);
    late = glo_f2(EE, 127, 600, 0);
    snprintf(what, sizeof what, "GLORP: a hard note opens the wah wider than a soft one (F2 %.2f vs %.2f kHz) and it closes (%.2f)",
             hard, soft, late);
    check(what, hard > soft + 0.4 && late < 1.1 && soft > 0.95);
    memcpy(v, EE, sizeof v); v[1] = 0;
    h0 = glo_f2(v, 127, 80, 0); l0 = glo_f2(v, 127, 600, 0);
    snprintf(what, sizeof what, "GLORP: WAH 0 holds OO (F2 %.2f, %.2f kHz)", h0, l0);
    check(what, h0 < 0.9 && l0 < 0.9);
    memcpy(v, EE, sizeof v); v[6] = 0;
    s0 = glo_f2(v, 40, 80, 0); s1 = glo_f2(v, 127, 80, 0);
    snprintf(what, sizeof what, "GLORP: SENS 0 opens every note alike, for the FM-1's keys (F2 %.2f at 40, %.2f at 127)", s0, s1);
    check(what, s0 == s1 && s0 > soft + 0.3);
    memcpy(v, DEF, sizeof v); v[0] = 32; oh = glo_f2(v, 100, 80, 0);
    v[0] = 127; ee = glo_f2(v, 100, 80, 0);
    snprintf(what, sizeof what, "GLORP: VOWEL moves where it opens to (F2 %.2f towards OH, %.2f towards EE)", oh, ee);
    check(what, ee > oh + 0.8);
    memcpy(v, DEF, sizeof v); v[4] = 0; glo_f2(v, 127, 30, &z0);
    v[4] = 127; glo_f2(v, 127, 30, &z1);
    snprintf(what, sizeof what, "GLORP: DIP drops the pitch into the note (%u zero crossings in 30 ms, %u without)", z1, z0);
    check(what, z1 > z0 + 2u);
    /* the sound follows: WAH on and off differ (the band moving), at the same level */
    memcpy(v, DEF, sizeof v); v[1] = 0; glo_f2(v, 127, 500, 0);
    memcpy(a, buf, sizeof a);
    glo_f2(DEF, 127, 500, 0);
    for (i = FS / 50; i < FS / 2; i++) d += (double)(buf[i] - a[i]) * (buf[i] - a[i]), r += (double)a[i] * a[i];
    snprintf(what, sizeof what, "GLORP: the wah is heard: WAH 127 against WAH 0, %.1f dB of difference", 10.0 * log10(d / r));
    check(what, d > 0.1 * r);
}
/* VULT (eng_vult.c): a held A2 with E values v8: brightness over 0.1..0.5 s, and the render in buf */
static double vlt_tone(const int16_t *v8, double *rm)
{
    uint32_t k;
    parts_clear(); host_preset(&trk[1], ENGI_VULT, 0);
    for (k = 0; k < 8u; k++) trk[1].p[P_E0 + k] = v8[k];
    trk[1].p[P_ATK] = 0; trk[1].p[P_DEC] = 64; trk[1].p[P_SUS] = 127; trk[1].p[P_REL] = 20; trk[1].p[P_ED_FLT] = 0;
    trk_note_on(&trk[1], 45, 100); render_part(1, FS / 2);
    if (rm) *rm = rms(FS / 10, FS / 2);
    return bright(FS / 10, FS / 2);
}
static void vult(void)
{
    static const int16_t DEF[8] = {64, 40, 0, 30, 20, 0, 0, 0};
    static float a[FS / 2];
    int16_t v[8];
    double lo, hi, r0, r1, d = 0, r = 0, s0, s1;
    uint32_t i;
    char what[200];
    memcpy(v, DEF, sizeof v); v[0] = 30; lo = vlt_tone(v, &r0);
    v[0] = 110; hi = vlt_tone(v, &r1);
    snprintf(what, sizeof what, "VULT: CUTOFF opens the ladder (brightness %.4f -> %.4f), it sounds (rms %.3f, %.3f)", lo, hi, r0, r1);
    check(what, hi > 3.0 * lo && r0 > 0.01 && r1 > 0.01);
    memcpy(v, DEF, sizeof v); v[2] = 0; vlt_tone(v, 0); memcpy(a, buf, sizeof a);
    v[2] = 110; v[5] = -40; vlt_tone(v, 0);
    for (i = FS / 10; i < FS / 2; i++) d += (double)(buf[i] - a[i]) * (buf[i] - a[i]), r += (double)a[i] * a[i];
    snprintf(what, sizeof what, "VULT: COMB and TONE ring the comb (%.1f dB of difference)", 10.0 * log10(d / r));
    check(what, d > 0.1 * r);
    memcpy(v, DEF, sizeof v); v[0] = 90; v[4] = 0; s0 = vlt_tone(v, 0);
    v[4] = 110; s1 = vlt_tone(v, 0);
    snprintf(what, sizeof what, "VULT: SHAPE sweeps the phase distortion brighter (%.4f -> %.4f)", s0, s1);
    check(what, s1 > 1.5 * s0);
    memcpy(v, DEF, sizeof v); v[1] = 127; v[0] = 70;
    {   /* RESO at the top: the ladder sings on by itself after the note's input is gone (a hold of the last stages) */
        double ring;
        vlt_tone(v, 0);
        ring = rms(FS * 4 / 10, FS / 2);
        snprintf(what, sizeof what, "VULT: RESO at the top resonates hard and stays bounded (rms %.3f, peak under full scale)", ring);
        check(what, ring > 0.01 && ring < 1.0);
    }
}
static void cost(void)
{
    double m = ns_per(ENGI_MORPH, 0), f = ns_per(ENGI_DEBRIS, 3), p = ns_per(ENGI_PFM, 0), g;
    char what[160];
    g = ns_per(ENGI_GENESIS, 0);
    snprintf(what, sizeof what, "cost per voice-sample: DEBRIS %.1f, GENESIS %.1f, FM %.1f ns; MORPH %.1f "
             "(2.3 x, 3 x, 2 x)", f, g, p, m);
    check(what, f < 2.3 * m && g < 3.0 * m && p < 2.0 * m);
    g = ns_per(ENGI_GLORP, 0);
    snprintf(what, sizeof what, "cost per voice-sample: GLORP %.1f ns, MORPH %.1f (2 x)", g, m);
    check(what, g < 2.0 * m);
    g = ns_per(ENGI_VULT, 0);
    snprintf(what, sizeof what, "cost per voice-sample: VULT %.1f ns, MORPH %.1f (3 x)", g, m);
    check(what, g < 3.0 * m);
}
/* GLORP's and VULT's demos: sketch F's riff (tools/glorp_proto.py), its velocities soft .. hard, on each sound */
static void riff_demo(const char *dir, uint32_t e, const char *name)
{
    static const uint8_t ST[8] = {0, 3, 4, 7, 8, 11, 12, 14}, NOTE[8] = {48, 48, 55, 53, 51, 48, 58, 55}, LEN[8] = {2, 1, 2, 1, 3, 1, 2, 2},
                         VEL[8] = {40, 70, 127, 55, 100, 35, 127, 80};
    const uint32_t stp = FS * 60 / 95 / 4;
    FILE *w = wav_open(dir, name, 0x7FFFFFF);
    uint32_t pi, b, k;
    if (!w) return;
    mixed = 0;
    for (pi = 0; pi < ENGINES[e]->npresets; pi++) {
        const preset_t *pr = &ENGINES[e]->presets[pi];
        uint32_t part = pr->mono ? 2 : 1, t = 0;
        parts_clear();
        host_preset(&trk[part], e, pi);
        for (b = 0; b < 2u; b++)
            for (k = 0; k < 8u; k++) {
                uint32_t at = (b * 16u + ST[k]) * stp, len = LEN[k] * stp * 9u / 10u;
                mix_to(w, at - t);
                trk_note_on(&trk[part], NOTE[k], VEL[k]);
                mix_to(w, len);
                trk_note_off(&trk[part], NOTE[k]);
                t = at + len;
            }
        mix_to(w, 32u * stp - t + FS / 2);
    }
    fseek(w, 0, SEEK_SET);
    wav_hdr(w, mixed);
    fclose(w);
}
static void demo(const char *dir)
{
    static const uint8_t CHORD[3] = {57, 60, 64}, LINE[8] = {36, 36, 43, 36, 39, 36, 41, 43};
    static const uint32_t E3[3] = {ENGI_DEBRIS, ENGI_PFM, ENGI_GENESIS};
    static const char *const NAME[3] = {"debris.wav", "fm.wav", "genesis.wav"};
    uint32_t e, pi, k;
    for (e = 0; e < 3u; e++) {
        const engine_t *E = ENGINES[E3[e]];
        FILE *w = wav_open(dir, NAME[e], 0x7FFFFFF);
        if (!w) return;
        mixed = 0;
        for (pi = 0; pi < E->npresets; pi++) {
            const preset_t *pr = &E->presets[pi];
            uint32_t part = pr->mono ? 2 : 1;
            parts_clear();
            host_preset(&trk[part], E3[e], pi);
            if (pr->mono) {
                for (k = 0; k < 8u; k++) {
                    trk_note_on(&trk[part], LINE[k], 100);
                    mix_to(w, FS / 5 - FS / 40);
                    trk_note_off(&trk[part], LINE[k]);
                    mix_to(w, FS / 40);
                }
            } else {
                for (k = 0; k < 3u; k++) trk_note_on(&trk[part], CHORD[k], 100);
                mix_to(w, FS * 6 / 5);
                for (k = 0; k < 3u; k++) trk_note_off(&trk[part], CHORD[k]);
            }
            mix_to(w, FS * 2 / 5);
        }
        fseek(w, 0, SEEK_SET);
        wav_hdr(w, mixed);
        fclose(w);
    }
}
int main(int argc, char **argv)
{
    double ref = morph_level_ref();
    presets(ENGI_DEBRIS, ref);
    presets(ENGI_PFM, ref);
    presets(ENGI_GLORP, ref);
    glorp();
    presets(ENGI_VULT, ref);
    vult();
    controls();
    scopes();
    if (!getenv("NOCOST"))
        cost();
    if (argc > 1) {
        demo(argv[1]);
        riff_demo(argv[1], ENGI_GLORP, "glorp.wav");
        riff_demo(argv[1], ENGI_VULT, "vult.wav");
        printf("engines: demos in %s\n", argv[1]);
    }
    printf("engines: %s\n", fails ? "FAILED" : "all checks ok");
    return fails != 0;
}
