/* SPDX-License-Identifier: GPL-3.0-only
 * Phantom Tracker 0.10 demos (by hand): build/host/v010_demo OUT_DIR
 *   transmission.wav       the power-on transmission, 8 bars: PERC's shaker ping-ponging (per-step pan)
 *   transmission-b.wav     the same with B sides: the pad + GLORP an octave up (panned apart), the bass + a sub
 *   perc-tour.wav          PERC alone: the 16 sounds from four kits (CHILLWAVE, SID BREAKS, FM JAZZ, FOUNDRY), 2 bars
 *                          each (two sounds a pass), its two lanes panned per step */
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"
#include "../firmware/src/boot_song.c"

static FILE *wopen(const char *dir, const char *name, uint32_t frames)
{
    char path[512];
    FILE *w;
    snprintf(path, sizeof path, "%s/%s", dir, name);
    if (!(w = fopen(path, "wb")))
        return 0;
    wav_hdr(w, frames);
    printf("v010_demo: %s\n", path);
    return w;
}
static void play(FILE *w, uint32_t frames)
{
    int32_t o[2 * CTL];
    uint32_t n, i;
    for (n = 0; n < frames; n += CTL) {
        mix_block(o, CTL);
        for (i = 0; i < CTL; i++)
            wav_put(w, o[2 * i], o[2 * i + 1]);
    }
}
static void power_on(void)
{
    ui_power_on();
    song.master_q12 = 4096;
    song.g[G_BPM] = 120;
    transmission();
    frames(2);
}

int main(int argc, char **argv)
{
    uint32_t bar = FS * 2u, k;
    FILE *w;
    if (argc < 2) {
        printf("usage: v010_demo OUT_DIR\n");
        return 1;
    }
    power_on();
    if ((w = wopen(argv[1], "transmission.wav", 8u * bar))) {
        seq_start(); song.playing = 1;
        play(w, 8u * bar);
        song.playing = 0; seq_stop();
        fclose(w);
    }

    power_on();
    b_set(&trk[1], 1);
    b_flip(&trk[1]); set_engine_of(&trk[1], ENGI_GLORP); apply_preset_to(&trk[1], 4); b_flip(&trk[1]);
    bsr[0].s.tr = 12; bsr[0].s.lv = 92; bsr[0].s.pn = 36; trk[1].p[P_PAN] = -36;
    b_set(&trk[2], 1);
    b_flip(&trk[2]); set_engine_of(&trk[2], ENGI_MORPH); apply_preset_to(&trk[2], 0); b_flip(&trk[2]);
    bsr[1].s.tr = -12; bsr[1].s.lv = 88;
    frames(2);
    if ((w = wopen(argv[1], "transmission-b.wav", 8u * bar))) {
        seq_start(); song.playing = 1;
        play(w, 8u * bar);
        song.playing = 0; seq_stop();
        fclose(w);
    }

    {
        static const uint8_t KITS[4] = {17, 39, 31, 28};
        track_t *pc = &trk[PERC_TRK];
        uint32_t s, i;
        ui_power_on();
        song.master_q12 = 4096;
        song.g[G_BPM] = 120;
        for (k = 0; k < NTRK; k++)
            trk[k].p[P_MUTE] = k != PERC_TRK;
        for (i = 0; i < 16u; i++) {
            step_t *st = &pc->step[i];
            step_clear(st);
            st->time = ST_NOTE;
            st->hit = (uint8_t)(i % 4u == 0u ? 1u : (i & 1u) ? 2u : 0u);
            if (st->hit)
                motion_set_lock(pc, i, P_PAN, (int16_t)(i % 8u < 4u ? -44 : 44));
        }
        pc->p[P_SLEN] = 16;
        motion_set_enabled(pc, 1);
        if ((w = wopen(argv[1], "perc-tour.wav", 16u * bar))) {
            seq_start(); song.playing = 1;
            for (s = 0; s < 16u; s += 2u) {
                for (k = 0; k < 2u; k++) {
                    pc->p[PC_KIT(k)] = KITS[(s / 4u) % 4u];
                    pc->p[PC_SND(k)] = (int16_t)(s + k + 1u);
                    pc_source(pc, k);
                }
                play(w, 2u * bar);
                printf("  bars %2u-%2u: %s %s / %s\n", s + 1u, s + 2u, N_PC_KIT[pc->p[PC_KIT(0)]], N_PC_SND[s], N_PC_SND[s + 1u]);
            }
            song.playing = 0; seq_stop();
            fclose(w);
        }
    }
    return 0;
}
