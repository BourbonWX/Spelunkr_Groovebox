/* SPDX-License-Identifier: GPL-3.0-only
 * Phantom Cosmonaut M2: the MORPH engine and its 16 SIGNALs (src/eng_morph.c, tools/gen_signals.py) on the host,
 * through hostsim.c (after Dungeon Tracker 1.5's wt32_test.c).
 *   build/host/signal_test [DEMODIR]
 * 1. every signal's sound sounds, no DC, its voices free, no clipping; the level near WAVES'. 2. the contort knobs:
 * SCAN moves through every signal; WARP and FOLD brighten; CLARITY darkens to the left and adds static to the right;
 * SUB adds the octave below. 3. aliasing under -30 dB up to C7 (CARRIER's saw). 4. one level across the mipmap's sizes.
 * 5. the lists: names, categories, HOME's knobs. 6. the scope's cycle. 7. cost against WAVES. 8. a demo of every
 * signal. */
#define main hostsim_main
#include "hostsim.c"
#undef main
#ifndef CAT_BASS
#include "category.c"                   /* (preset_cat: hostsim has no ui.c) */
#endif

static int fails;
static void check(const char *what, int ok)
{
    printf("signal: %-92s %s\n", what, ok ? "ok" : "FAIL");
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
/* 1 every signal's factory sound */
static void presets(void)
{
    static const uint8_t CHORD[3] = {57, 60, 64};
    uint32_t pi, quiet = 0, dc = 0, held = 0, over = 0, nw = 0, nv = 0;
    double w = 0, v = 0;
    for (pi = 0; pi < ENG_MORPH.npresets; pi++) {
        const preset_t *pr = &MORPH_PRESETS[pi];
        uint32_t part = pr->mono ? 2 : 1, k, rel;
        int32_t pk;
        parts_clear();
        host_preset(&trk[part], ENGI_MORPH, pi);
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
        {   /* DC: one note held (SUS full) over whole periods of its octave below (SUB) */
            double f = 440.0 * pow(2.0, (48 - 69) / 12.0) / 2.0, m = 0, mx = 0;
            uint32_t a = FS / 2, e = a + (uint32_t)(floor(0.4 * f) * FS / f), i;
            parts_clear(); host_preset(&trk[1], ENGI_MORPH, pi); trk[1].p[P_SUS] = 127; trk[1].p[P_ED_FLT] = 0;
            trk_note_on(&trk[1], 48, 100); render_part(1, FS);
            for (i = a; i < e; i++) m += buf[i], mx = fabs(buf[i]) > mx ? fabs(buf[i]) : mx;
            m = mx > 0 ? fabs(m / (e - a)) / mx : 0;
            dc |= (m > 0.01) << pi;
        }
        printf("signal:   %02u %-12s the part %5d (%.1f dBFS)\n", pi + 1u, pr->name, pk, 20.0 * log10((pk + 1) / 32768.0));
    }
    for (pi = 0; pi < ENG_MORPH.npresets; pi++)
        if (MORPH_PRESETS[pi].env[2] > 60) w += rms_of(ENGI_MORPH, pi), nw++;
    v = 0.499; nv = 1;                                  /* (WAVES' level, measured so before it was retired: the
                                                         * reference the engines were set by since 1.1) */
    w /= nw ? nw : 1;
    printf("signal:   (quiet 0x%X, over 0x%X, DC 0x%X, held 0x%X; one voice's rms MORPH %.3f, WAVES %.3f)\n", quiet, over, dc, held, w, v);
    check("every signal's sound sounds, no DC, its voices free after the release; level within 3 dB of WAVES' (0.499 rms)",
          !quiet && !over && !dc && !held && fabs(20.0 * log10(w / v)) < 3.0);
}
/* 2 the contort knobs */
static void controls(void)
{
    uint32_t s, ok = 1, z0, z1;
    double b0, b1, r0, r1;
    char what[160];
    for (s = 0; s < SIG_N; s++) {
        r0 = one((int)s, 0, 0, 0, 0, 0, 45, &b0);
        r1 = one((int)s, 127, 0, 0, 0, 0, 45, &b1);
        printf("signal:   (%02u %-12s SCAN 0 / 127: brightness %.4f / %.4f, rms %.3f / %.3f)\n", s + 1u, SIG_NAME[s], b0, b1, r0, r1);
        ok &= fabs(b1 - b0) > 0.1 * (b0 > b1 ? b0 : b1) && r0 > 0.02 && r1 > 0.02;
    }
    check("every signal sounds at both ends; SCAN moves through it (the brightness changes by 10 % or more)", ok);
    one(0, 60, 0, 0, 0, 0, 57, &b0); one(0, 60, 120, 0, 0, 0, 57, &b1);
    snprintf(what, sizeof what, "WARP brightens (%.4f -> %.4f)", b0, b1);
    check(what, b1 > b0 * 1.2);
    one(0, 0, 0, 0, 0, 0, 57, &b0); one(0, 0, 0, 127, 0, 0, 57, &b1);
    snprintf(what, sizeof what, "FOLD brightens a sine (%.4f -> %.4f)", b0, b1);
    check(what, b1 > b0 * 2.0);
    one(0, 127, 0, 0, 0, 0, 57, &b0); one(0, 127, 0, 0, -64, 0, 57, &b1);
    snprintf(what, sizeof what, "CLARITY left: the low-pass darkens a saw (%.4f -> %.4f)", b0, b1);
    check(what, b1 < b0 * 0.5);
    one(0, 0, 0, 0, 0, 0, 57, &b0); one(0, 0, 0, 0, 63, 0, 57, &b1);
    snprintf(what, sizeof what, "CLARITY right: static on a sine (brightness %.4f -> %.4f)", b0, b1);
    check(what, b1 > b0 * 3.0);
    {   /* CLARITY at 0: as the engine plays it unshaped (open, no static): the same as a hair to the left */
        double a = one(6, 64, 0, 0, 0, 0, 57, 0), b = one(6, 64, 0, 0, -1, 0, 57, 0);
        snprintf(what, sizeof what, "CLARITY in the middle: open (level %.3f, one step left %.3f)", a, b);
        check(what, fabs(20.0 * log10(a / b)) < 1.0);
    }
    parts_clear(); raw(1, 4, 127, 0, 0, 0, 0); trk_note_on(&trk[1], 69, 100); render_part(1, FS / 2); z0 = zc(FS / 10, FS / 2);
    parts_clear(); raw(1, 0, 0, 0, 0, 0, 127); trk_note_on(&trk[1], 69, 100); render_part(1, FS / 2); z1 = zc(FS / 10, FS / 2);
    {
        uint32_t zs;
        parts_clear(); raw(1, 0, 0, 0, 0, 0, 0); trk_note_on(&trk[1], 69, 100); render_part(1, FS / 2); zs = zc(FS / 10, FS / 2);
        snprintf(what, sizeof what, "SUB at 127: a sine an octave below (%u -> %u zero crossings)", zs, z1);
        check(what, z1 * 3u < zs * 2u);
    }
    (void)z0;
}
/* 3 aliasing: CARRIER at SCAN 127 (a saw), WARP FOLD 0, CLARITY open, up to C7 (C8 printed) */
static void alias(void)
{
    static const uint8_t NOTE[] = {48, 72, 84, 96, 108};
    double worst = 0;
    char what[120];
    for (uint32_t k = 0; k < NELEM(NOTE); k++) {
        double f0 = 440.0 * pow(2.0, (NOTE[k] - 69) / 12.0), off = 0, all = 0;
        uint32_t a = FS / 4, n = FS / 2, j, b;
        parts_clear(); raw(1, 0, 127, 0, 0, 0, 0); trk_note_on(&trk[1], NOTE[k], 100); render_part(1, a + n);
        for (b = 20; b < n / 2; b += 5) {        /* a DFT, 2 Hz bins (every fifth), Hann */
            double re = 0, im = 0, f = b * (double)FS / n;
            for (j = 0; j < n; j++) {
                double w = 0.5 - 0.5 * cos(2 * M_PI * j / n), x = buf[a + j] * w;
                re += x * cos(2 * M_PI * f * j / FS); im += x * sin(2 * M_PI * f * j / FS);
            }
            double e = re * re + im * im, h = f / f0, d = fabs(h - floor(h + 0.5)) * f0;
            all += e;
            if (d > 25.0) off += e;
        }
        double r = all > 0 ? off / all : 0;
        printf("signal:   (note %u, %.0f Hz: off-harmonic energy %.2e)\n", NOTE[k], f0, r);
        if (NOTE[k] <= 96u)
            worst = r > worst ? r : worst;
    }
    snprintf(what, sizeof what, "aliasing up to C7 (CARRIER's saw): the energy off the harmonics under -30 dB (%.1f dB)", 10 * log10(worst + 1e-12));
    check(what, worst < 1e-3);
}
/* 4 one level across the mipmap: a held saw, note by note, C2 .. C7: no step of more than 1.5 dB from one to the next */
static void levels(void)
{
    double prev = 0, worst = 0;
    uint32_t n;
    char what[120];
    for (n = 36; n <= 96; n++) {
        double r = one(0, 127, 0, 0, 0, 0, n, 0);
        if (n > 36) {
            double d = fabs(20.0 * log10(r / prev));
            worst = d > worst ? d : worst;
        }
        prev = r;
    }
    snprintf(what, sizeof what, "one level across the frames' sizes: C2 .. C7 a semitone at a time, the largest step %.2f dB", worst);
    check(what, worst < 1.5);
}
/* 5 the lists */
static void lists(void)
{
    uint32_t n, at = 99, ok = 1;
    for (n = 0; n < NENG_SHOWN; n++)
        if (eng_vis(n) == ENGI_MORPH) at = n;
    check("MORPH in PRESETS and on an EDIT layer key", at < NENG_KEYS && eng_ok(ENGI_MORPH) && str_eq(ENG_MORPH.name, "MORPH"));
    for (n = 0; n < ENG_MORPH.npresets; n++) {
        ok &= preset_cat(ENGI_MORPH, n) != 0u;
        ok &= MORPH_PRESETS[n].pat <= NPATTERNS;
        ok &= str_len(MORPH_PRESETS[n].name) <= 12u && str_eq(MORPH_PRESETS[n].name, SIG_NAME[n]);
        ok &= MORPH_PRESETS[n].e[0] == (int8_t)n;
    }
    check("16 sounds, one a signal in SIG order, named as it, a category each, pattern hints among the PHRASES", ok && ENG_MORPH.npresets == SIG_N && SIG_N == 16u);
    check("HOME's knobs: SCAN WARP FOLD CLARITY (the spec's contort knobs)",
          ENG_MORPH.knob[0] == P_E1 && ENG_MORPH.knob[1] == P_E2 && ENG_MORPH.knob[2] == P_E3 && ENG_MORPH.knob[3] == P_E4 &&
          str_eq(ENG_MORPH.edit[1].label, "SCAN") && str_eq(ENG_MORPH.edit[2].label, "WARP") &&
          str_eq(ENG_MORPH.edit[3].label, "FOLD") && str_eq(ENG_MORPH.edit[4].label, "CLARITY"));
    check("the signals fit the budget (<= 96 KB) and the tables are 8-bit", sizeof SIG_TAB <= 96u * 1024u && sizeof SIG_TAB[0][0] == 1u);
}
/* 6 the scope's cycle (EDIT's oscilloscope) follows the knobs */
static void cycle(void)
{
    static int16_t a[220], b[220];
    uint32_t i, diff = 0, big = 0;
    parts_clear(); raw(1, 0, 0, 0, 0, 0, 0); morph_cycle(&trk[1], a, 220);
    for (i = 0; i < 220u; i++) big = abs(a[i]) > (int)big ? (uint32_t)abs(a[i]) : big;
    trk[1].p[P_E3] = 127; morph_cycle(&trk[1], b, 220);
    for (i = 0; i < 220u; i++) diff += abs(a[i] - b[i]) > 2000;
    check("the scope's cycle: CARRIER's sine near full scale; FOLD changes it", big > 20000u && diff > 40u);
}
static void cost(void)
{
    double gn = ns_per(ENGI_PFM, 0), m0 = ns_per(ENGI_MORPH, 0), m1 = ns_per(ENGI_MORPH, 9);   /* (10 STATIC: CLARITY right) */
    char what[160];
    snprintf(what, sizeof what, "cost per voice-sample: MORPH %.1f ns / %.1f (STATIC), FM %.1f: %.2f x", m0, m1, gn, (m0 > m1 ? m0 : m1) / gn);
    check(what, (m0 > m1 ? m0 : m1) < 1.5 * gn);   /* (the yardstick: FM, four frame reads a sample, ~0.9 x on the host in
                                                     * Nov 2026; WAVES, then GENESIS before they were retired) */
}
static void demo_presets(const char *dir)
{
    static const uint8_t CHORD[3] = {57, 60, 64}, LINE[8] = {36, 36, 43, 36, 39, 36, 41, 43};
    uint32_t pi, k;
    FILE *w = wav_open(dir, "signals.wav", 0x7FFFFFF);
    if (!w) return;
    mixed = 0;
    for (pi = 0; pi < ENG_MORPH.npresets; pi++) {
        const preset_t *pr = &MORPH_PRESETS[pi];
        uint32_t part = pr->mono ? 2 : 1;
        parts_clear();
        host_preset(&trk[part], ENGI_MORPH, pi);
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
int main(int argc, char **argv)
{
    presets();
    controls();
    alias();
    levels();
    lists();
    cycle();
    if (!getenv("NOCOST"))
        cost();
    if (argc > 1) {
        demo_presets(argv[1]);
        printf("signal: demos in %s\n", argv[1]);
    }
    printf("signal: %s\n", fails ? "FAILED" : "all checks ok");
    return fails != 0;
}
