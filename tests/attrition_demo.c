/* SPDX-License-Identifier: GPL-3.0-only
 * Phantom Cosmonaut M6: an audio demo of ATTRITION over the power-on groove (run by hand).
 *   build/host/attrition_demo OUT.wav [SEED]
 * A RANDOM SONG's verse on the four FEEDs; then each of ATTRITION's knobs in turn, as a hand would ride it with FX
 * held (STUTTER up through 1/8, 1/16, 1/32; TUNNEL in and out; JUMP; CRUSH), springing back between; then two FX key
 * gates (REVERSE, TAPE STOP); then the groove alone */
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"

static FILE *w;
static uint32_t frames_out;
static void run(uint32_t ms)
{
    int32_t o[2 * CTL];
    uint32_t n = ms * (FS / 1000u), i, k;
    for (i = 0; i < n; i += CTL) {
        mix_block(o, CTL);
        for (k = 0; k < CTL; k++)
            wav_put(w, o[2 * k], o[2 * k + 1]);
        frames_out += CTL;
    }
}
static void ride(volatile int8_t *v, int32_t from, int32_t to, uint32_t ms)   /* a knob turned from .. to over ms */
{
    uint32_t i, steps = ms / 20u;
    for (i = 0; i <= steps; i++) {
        *v = (int8_t)(from + (to - from) * (int32_t)i / (int32_t)steps);
        run(20);
    }
}
int main(int argc, char **argv)
{
    uint32_t seed = argc > 2 ? (uint32_t)strtoul(argv[2], 0, 0) : 0x7A57u;
    if (argc < 2) {
        fprintf(stderr, "attrition_demo OUT.wav [SEED]\n");
        return 2;
    }
    w = fopen(argv[1], "wb");
    if (!w)
        return 1;
    wav_hdr(w, 0);
    ui_power_on();
    song.g[G_ENGGO] = 0;
    song.master_q12 = 4096;
    random_song_make(seed);
    boot_song_settle();
    song.g[G_BPM] = 104;
    seq_start();
    song.playing = 1;
    run(4000);                                          /* the groove */
    ride(&mk_a[0], 1, 30, 1600);                        /* STUTTER 1/8 */
    ride(&mk_a[0], 50, 70, 1200);                       /* .. 1/16 */
    ride(&mk_a[0], 80, 100, 1000);                      /* .. 1/32 */
    mk_a[0] = 0;
    run(2000);
    ride(&mk_a[1], 0, 100, 2500);                       /* TUNNEL in */
    ride(&mk_a[1], 100, 0, 1500);                       /* .. and out */
    run(1800);
    mk_a[2] = 80;                                       /* JUMP */
    run(4500);
    mk_a[2] = 0;
    run(1500);
    ride(&perf_k[1], 0, 90, 2000);                      /* CRUSH */
    perf_k[1] = 0;
    run(1500);
    perf_press(PF_REV, 1);                              /* an FX key's gate: REVERSE */
    run(1200);
    perf_press(PF_REV, 0);
    run(1200);
    perf_press(PF_TAPE, 1);                             /* .. TAPE STOP */
    run(1500);
    perf_press(PF_TAPE, 0);
    run(3000);
    song.playing = 0;
    seq_stop();
    run(1500);
    fseek(w, 0, SEEK_SET);
    wav_hdr(w, frames_out);
    fclose(w);
    printf("attrition_demo: %s (%.1f s)\n", argv[1], frames_out / (double)FS);
    return 0;
}
