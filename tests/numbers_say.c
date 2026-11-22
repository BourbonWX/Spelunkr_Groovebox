/* SPDX-License-Identifier: GPL-3.0-only
 * NUMBERS' 16 phrases through the firmware engine, for tools/numbers_clarity.py (a speech recogniser scores them):
 *   numbers_say OUTDIR NOTE RADIO SPEED HUMAN [DROP TAIL]  -> OUTDIR/pNN.raw (float32, 44.1 kHz) */
#define main hostsim_main
#include "hostsim.c"
#undef main
int main(int argc, char **argv)
{
    int32_t o[CTL]; uint32_t i, k, j; char p[512];
    int note = atoi(argv[2]);
    host_tracks_init();
    for (k = 0; k < NW_NPHR; k++) {
        FILE *f; snprintf(p, sizeof p, "%s/p%02u.raw", argv[1], k); f = fopen(p, "wb");
        host_preset(&trk[2], ENGI_NUMBERS, 0);
        for (j = 0; j < 8; j++) trk[2].p[P_E0 + j] = 0;
        trk[2].p[P_E1] = atoi(argv[3]); trk[2].p[P_E2] = atoi(argv[4]); trk[2].p[P_E3] = atoi(argv[5]);
        trk[2].p[P_E6] = argc > 6 ? atoi(argv[6]) : 0; trk[2].p[P_E7] = argc > 7 ? atoi(argv[7]) : 0;
        trk[2].p[P_ATK] = 0; trk[2].p[P_SUS] = 127; trk[2].p[P_REL] = 10;
        float z[CTL] = {0}; for (i = 0; i < FS / 5 / CTL; i++) fwrite(z, 4, CTL, f);
        for (j = 0; j < 8 && NW_PHRASE[k][j] != 255; j++) {
            uint32_t w = NW_PHRASE[k][j], n = NW_NF[w] * FS / 40 + FS / 12;
            trk[2].p[P_E0] = w;
            trk_note_off(&trk[2], note); trk_note_on(&trk[2], note, 100);
            for (i = 0; i < n / CTL; i++) { float fl[CTL]; track_render(&trk[2], o, CTL); for (int q = 0; q < CTL; q++) fl[q] = o[q] / 32768.f; fwrite(fl, 4, CTL, f); }
        }
        trk_note_off(&trk[2], note);
        for (i = 0; i < FS / 3 / CTL; i++) { float fl[CTL]; track_render(&trk[2], o, CTL); for (int q = 0; q < CTL; q++) fl[q] = o[q] / 32768.f; fwrite(fl, 4, CTL, f); }
        fclose(f);
    }
    return 0;
}
