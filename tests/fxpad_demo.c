/* SPDX-License-Identifier: GPL-3.0-only
 * Phantom Cosmonaut M5: an audio demo of the FX PAD (run by hand).   build/host/fxpad_demo OUT.wav [SEED]
 * A RANDOM SONG's loop; then the pad on the drums (FEED 1): a filter swept down and up with an echo under it, a gate
 * and a tape stop; on FEED 2 a ring modulator; then a pass printed (REPT and CRUSH on the drums) and played back with
 * the pad closed, the prints doing it. */
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"

static FILE *w;
static uint32_t frames_out;
static void run(uint32_t ms)
{
    int32_t o[2 * CTL];
    uint32_t n = ms * (FS / 1000u), i, k;
    for (i = 0; i < n; i += CTL) {
        memset(o, 0, sizeof o);
        mix_block(o, CTL);
        for (k = 0; k < CTL; k++)
            wav_put(w, o[2 * k], o[2 * k + 1]);
        frames_out += CTL;
    }
}
/* slot j of the pad on FEED k: effect ty, X from x0 to x1 and Y from y0 to y1 over ms */
static void sweep(uint32_t j, uint32_t ty, int32_t x0, int32_t x1, int32_t y0, int32_t y1, uint32_t ms)
{
    uint32_t t;
    xy_live[j].type = (uint8_t)ty;
    xy_live[j].on = 1;
    for (t = 0; t < ms; t += 20u) {
        xy_live[j].x = (uint8_t)(x0 + (x1 - x0) * (int32_t)t / (int32_t)ms);
        xy_live[j].y = (uint8_t)(y0 + (y1 - y0) * (int32_t)t / (int32_t)ms);
        run(20);
    }
}
int main(int argc, char **argv)
{
    uint32_t seed = argc > 2 ? (uint32_t)strtoul(argv[2], 0, 0) : 0x5EEDu;
    if (argc < 2) {
        fprintf(stderr, "fxpad_demo OUT.wav [SEED]\n");
        return 2;
    }
    w = fopen(argv[1], "wb");
    if (!w)
        return 1;
    wav_hdr(w, 0);
    ui_power_on();
    song.master_q12 = 4096;
    random_song_make(seed);
    boot_song_settle();
    song.g[G_BPM] = 112;
    if (getenv("SOLO")) { trk[1].p[P_LEVEL] = trk[2].p[P_LEVEL] = trk[3].p[P_LEVEL] = 0; }
    seq_start();
    song.playing = 1;
    run(4000);                                          /* the loop dry */
    xy_open(0);                                         /* the pad on the drums */
    sweep(1, XY_ECHO, 60, 60, 50, 50, 10);
    sweep(0, XY_FILTER, 127, 20, 30, 100, 3000);        /* the filter closing, resonance up, an echo under it */
    sweep(0, XY_FILTER, 20, 127, 100, 30, 2000);
    xy_live[1].on = 0;
    sweep(0, XY_GATE, 60, 100, 127, 127, 2500);         /* chopped, faster */
    sweep(0, XY_TAPE, 0, 127, 40, 40, 1800);            /* a tape stop */
    xy_open(0xFFu);
    run(1000);
    xy_open(1);                                         /* FEED 2: a ring modulator */
    sweep(0, XY_RING, 10, 110, 90, 90, 3000);
    xy_open(0xFFu);
    xy_open(0);                                         /* a pass printed on the drums: REPT, CRUSH */
    xy_print = 1;
    sweep(0, XY_REPEAT, 90, 90, 64, 64, 10);
    sweep(1, XY_CRUSH, 80, 80, 70, 70, 10);
    run(beat_samples() * 4u * 1000u / FS);              /* (a bar) */
    xy_open(0xFFu);                                     /* the pad closed: the prints play it */
    run(6000);
    song.playing = 0;
    seq_stop();
    run(1500);
    fseek(w, 0, SEEK_SET);
    wav_hdr(w, frames_out);
    fclose(w);
    printf("fxpad_demo: %s (%.1f s; %u prints)\n", argv[1], frames_out / (double)FS, fxp.count);
    return 0;
}
