/* SPDX-License-Identifier: GPL-3.0-only
 * Phantom Tracker 0.9.10: VULT (src/eng_vult.c) against Vult's own DSP. tests/vult_ref.h holds the harmonic levels of
 * Vult's ladder and comb (github.com/vult-dsp/vult examples, MIT), compiled by the Vult compiler and rendered in float
 * (tools/vult/make_ref.sh); the port runs the same cases in its fixed point: a 110 Hz saw at 0.8 into the ladder (CV,
 * RESO) and into the comb (CV, TONE 0.5, feedback). The harmonics within 40 dB of the loudest must match: median under
 * 1 dB, none past 6 dB. */
#define main hostsim_main
#include "hostsim.c"
#undef main
#include "vult_ref.h"

static int fails;
static void check(const char *what, int ok)
{
    printf("vult: %-96s %s\n", what, ok ? "ok" : "FAIL");
    fails += !ok;
}
static int cmpd(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return x < y ? -1 : x > y; }

int main(void)
{
    static int16_t line[VLT_N];
    static double y[8820];
    uint32_t c;
    for (c = 0; c < NELEM(VULT_REF); c++) {
        int32_t L[4] = {0, 0, 0, 0}, co = 0, dx = 0, dy = 0, i, k, nd = 0;
        uint32_t wp = 0, fh = vlt_fh((int32_t)(VULT_REF[c].cv * 127 * 256 + 0.5));
        double top = -999, d[39], med, worst = 0, lv[39];
        char what[200];
        memset(line, 0, sizeof line);
        for (i = 0; i < 8820; i++) {
            double ph = fmod(i * 110.0 / 44100.0, 1.0);
            int32_t x = (int32_t)(0.8 * (2 * ph - 1) * 65536);
            if (!VULT_REF[c].mode) {
                y[i] = vlt_ladder(L, x, fh, (int32_t)(VULT_REF[c].res * 65536)) / 65536.0;
            } else {   /* vlt_render's comb, its period from Vult's CV (note 24 + 120 cv) */
                uint32_t incc = pitch_inc((uint32_t)((24 + 120 * VULT_REF[c].cv) * 16)), per = 0x80000000u / (incc >> 8), r, t1, t2;
                int32_t fb = (int32_t)(VULT_REF[c].res * 65536), stone = (int32_t)vlt_isqrt(32u * 67108864u), fbk, si, dd, fr;
                if (per > ((VLT_N - 2u) << 8)) per = (VLT_N - 2u) << 8;
                fbk = (int32_t)(((int64_t)co * fb) >> 16);
                dy = fbk - dx + dy - ((dy >> 8) * 328 >> 8);
                dx = fbk;
                si = vlt_sat(x + dy);
                wp = wp + 1u >= VLT_N ? 0u : wp + 1u;
                line[wp] = (int16_t)clamp(si >> 2, -32768, 32767);
                r = ((wp << 8) + (VLT_N << 8) - per) % (VLT_N << 8);
                t1 = r >> 8; t2 = t1 + 1u >= VLT_N ? 0u : t1 + 1u; fr = (int32_t)(r & 255u);
                dd = (line[t1] + (((line[t2] - line[t1]) * fr) >> 8)) << 2;
                co = x + (int32_t)(((int64_t)stone * dd) >> 16);
                y[i] = vlt_sat(co) / 65536.0;
            }
        }
        for (k = 0; k < 39; k++) {   /* harmonic k + 1 of 110 Hz over the second half, Hann */
            double re = 0, im = 0;
            int32_t n = 4410;
            for (i = 0; i < n; i++) {
                double w = 0.5 - 0.5 * cos(2 * M_PI * i / n), a = 2 * M_PI * (k + 1) * 110.0 * i / 44100.0;
                re += w * y[4410 + i] * cos(a);
                im -= w * y[4410 + i] * sin(a);
            }
            lv[k] = 20 * log10(sqrt(re * re + im * im) + 1e-9);
            top = VULT_REF[c].db[k] > top ? VULT_REF[c].db[k] : top;
        }
        for (k = 0; k < 39; k++)
            if (VULT_REF[c].db[k] > top - 40) {
                d[nd] = fabs(lv[k] - VULT_REF[c].db[k]);
                worst = d[nd] > worst ? d[nd] : worst;
                nd++;
            }
        qsort(d, (size_t)nd, sizeof d[0], cmpd);
        med = d[nd / 2];
        snprintf(what, sizeof what, "%s CV %.2f %s %.2f: %d harmonics, median %.2f dB, worst %.2f dB from Vult's own",
                 VULT_REF[c].mode ? "comb" : "ladder", VULT_REF[c].cv, VULT_REF[c].mode ? "FB" : "RESO", VULT_REF[c].res, nd, med, worst);
        check(what, med < 1.0 && worst < 6.0);
    }
    printf("vult: %s\n", fails ? "FAILED" : "all checks ok");
    return fails != 0;
}
