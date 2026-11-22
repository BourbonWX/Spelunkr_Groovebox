/* SPDX-License-Identifier: GPL-3.0-only
 * Phantom Cosmonaut M3: an audio demo of PHANTOM, the jam, over a loop (run by hand).
 *   build/host/phantom_demo OUT.wav [SEED]
 * A RANDOM SONG's verse loops on the four FEEDs (the power-on sounds: GEN KIT, MORPH SOLAR WIND, HULL GROAN, CARRIER);
 * over it PHANTOM, the theremin, plays a phrase held legato (each key over the last: the glide), then the same phrase
 * with FOLD and CLARITY turned up, as a player would turn them mid-jam. */
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
/* a phrase: {note, ms held, legato into the next} */
static const struct { uint8_t note; uint16_t ms; uint8_t legato; } PHRASE[] = {
    {69, 700, 1}, {72, 500, 1}, {76, 900, 0}, {74, 450, 1}, {72, 450, 1}, {69, 1200, 0},
    {64, 600, 1}, {67, 600, 1}, {69, 1400, 0},
};
static void phrase(void)
{
    uint32_t i;
    int held = -1;
    for (i = 0; i < NELEM(PHRASE); i++) {
        ph_key_on(i % 2u, PHRASE[i].note);            /* (two keys, crossing: a new one before the last is let go) */
        if (held >= 0)
            ph_key_off((uint32_t)held);
        held = (int)(i % 2u);
        run(PHRASE[i].ms);
        if (!PHRASE[i].legato) {
            ph_key_off((uint32_t)held);
            held = -1;
            run(180);
        }
    }
}
int main(int argc, char **argv)
{
    uint32_t seed = argc > 2 ? (uint32_t)strtoul(argv[2], 0, 0) : 0x7A57u;
    if (argc < 2) {
        fprintf(stderr, "phantom_demo OUT.wav [SEED]\n");
        return 2;
    }
    w = fopen(argv[1], "wb");
    if (!w)
        return 1;
    wav_hdr(w, 0);
    ui_power_on();
    song.master_q12 = 4096;                           /* (the MASTER pot: main.c reads it; 0 on the host) */
    random_song_make(seed);
    boot_song_settle();
    song.g[G_BPM] = 96;
    seq_start();
    song.playing = 1;
    run(2500);                                        /* the loop alone */
    phrase();                                         /* PHANTOM over it */
    ph_trk.p[P_E3] = 60;                              /* then FOLD and CLARITY up: metal and static */
    ph_trk.p[P_E4] = 30;
    phrase();
    run(1500);
    song.playing = 0;
    seq_stop();
    run(1200);
    fseek(w, 0, SEEK_SET);
    wav_hdr(w, frames_out);
    fclose(w);
    printf("phantom_demo: %s (%.1f s)\n", argv[1], frames_out / (double)FS);
    return 0;
}
