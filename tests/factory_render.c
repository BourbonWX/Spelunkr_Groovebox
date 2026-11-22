/* SPDX-License-Identifier: GPL-3.0-only
 * Phantom Cosmonaut M8 (CAPTURE, spec 8): the factory set "NOISE", rendered once from the firmware's own engines, as
 * the reference for the factory clip (Ed: "rendering the noise kit as the default"):
 *   the NOISE kit's CAPSULE, its eight GRID rows (BD SD CP CH OH TM RS CB: GM 36 38 39 42 46 45 37 56) one hit each,
 *   dry (the kit's sends off); the two grain tapes: MORPH on SOLAR WIND and DARK SIDE, a held C3 with SCAN swept
 *   0 -> 127 (WARP drifting a little), 3 s each.
 *   build/host/factory_render OUTDIR   ->  OUTDIR/noise-NN-name.wav (44.1 kHz, 16-bit, mono) */
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"

static int16_t mono[FS * 4];

static void write_mono(const char *dir, const char *name, const int16_t *s, uint32_t n)
{
    char path[512];
    FILE *f;
    uint32_t v;
    uint16_t a = 1, ch = 1, ba = 2, bits = 16;
    uint32_t sr = FS, br = FS * 2;
    snprintf(path, sizeof path, "%s/%s.wav", dir, name);
    f = fopen(path, "wb");
    if (!f) { perror(path); return; }
    fwrite("RIFF", 1, 4, f); v = 36 + n * 2; fwrite(&v, 4, 1, f);
    fwrite("WAVEfmt ", 1, 8, f); v = 16; fwrite(&v, 4, 1, f);
    fwrite(&a, 2, 1, f); fwrite(&ch, 2, 1, f); fwrite(&sr, 4, 1, f); fwrite(&br, 4, 1, f);
    fwrite(&ba, 2, 1, f); fwrite(&bits, 2, 1, f);
    fwrite("data", 1, 4, f); v = n * 2; fwrite(&v, 4, 1, f);
    fwrite(s, 2, n, f);
    fclose(f);
    printf("  %s (%.2f s)\n", path, n / (double)FS);
}

static void dry(track_t *t)
{
    t->p[P_DIST] = 0; t->p[P_CHOR] = 0; t->p[P_DLY] = 0; t->p[P_REV] = 0;
}

/* the mix for ms, summed to mono into mono[]; the length to the last sound above -66 dB (+ 20 ms) */
static uint32_t run(uint32_t ms, void (*each)(uint32_t block, uint32_t nblocks))
{
    int32_t o[2 * CTL];
    uint32_t b, i, n = ms * (FS / 1000u) / CTL, last = 0;
    for (b = 0; b < n; b++) {
        if (each) each(b, n);
        mix_block(o, CTL);
        for (i = 0; i < CTL; i++) {
            int32_t m = (o[2 * i] + o[2 * i + 1]) / 2;
            mono[b * CTL + i] = (int16_t)(m > 32767 ? 32767 : m < -32768 ? -32768 : m);
            if (m > 16 || m < -16) last = b * CTL + i;
        }
    }
    last += FS / 50u;
    return last < n * CTL ? last : n * CTL;
}

static uint32_t hit_note;
static void hit_off(uint32_t b, uint32_t n)
{
    (void)n;
    if (b == 80u * (FS / 1000u) / CTL)
        trk_note_off(&trk[0], hit_note);
}

static void sweep(uint32_t b, uint32_t n)        /* SCAN 0 -> 127 over the tape, WARP 20 -> 50 */
{
    trk[1].p[P_E1] = (int16_t)(b * 127u / (n - 1u));
    trk[1].p[P_E2] = (int16_t)(20u + b * 30u / (n - 1u));
}

/* argv[2] "sets": the kits 16..19 (Phantom Tracker 0.9.7: GLITCH HOP CHILLWAVE TAPE DECK WONK, synthesized in the
 * scrapped sampled sets' places), each row through the kit engine: OUTDIR/kit-<name>-NN.wav */
static void render_sets(const char *dir)
{
    static const uint8_t NOTE[8] = {36, 38, 39, 42, 46, 45, 37, 56};
    static const char *const NM[4] = {"glitch-hop", "chillwave", "tape-deck", "wonk"};
    uint32_t s, k, n;
    char name[64];
    for (s = 0; s < 4u; s++)
        for (k = 0; k < 8u; k++) {
            ui_power_on();
            song.playing = 0;
            song.master_q12 = 4096;
            apply_preset_to(&trk[0], 16u + s);
            dry(&trk[0]);
            trk[0].p[P_E7] = 0;                          /* (VARI off: the hit exactly as stored) */
            run(20, 0);
            hit_note = NOTE[k];
            trk_note_on(&trk[0], NOTE[k], 127);
            n = run(1200, hit_off);
            snprintf(name, sizeof name, "kit-%s-%02u", NM[s], k + 1u);
            write_mono(dir, name, mono, n);
        }
}

/* argv[2] "tapes": DEBRIS's GRAIN PAD (its preset 8; GRANULAR's before) on SIG 04 VOSTOK and on each tape (SIG
 * 17..20), a held C3 with SCAN swept: OUTDIR/gran-NN.wav */
static void render_tapes(const char *dir)
{
    static const uint8_t SIG[5] = {3, 16, 17, 18, 19};
    uint32_t k, n;
    char name[64];
    for (k = 0; k < 5u; k++) {
        ui_power_on();
        song.playing = 0;
        song.master_q12 = 4096;
        set_engine_of(&trk[1], ENGI_DEBRIS);
        run(100, 0);
        apply_preset_to(&trk[1], 8);
        run(100, 0);
        trk[1].p[P_E0] = SIG[k];
        dry(&trk[1]);
        printf("    eng %u (debris %u) preset %u E0 %d\n", trk[1].engine, ENGI_DEBRIS, trk[1].preset, trk[1].p[P_E0]);
        trk_note_on(&trk[1], 48, 100);
        n = run(3000, sweep);
        trk_note_off(&trk[1], 48);
        snprintf(name, sizeof name, "gran-%02u", SIG[k] + 1u);
        write_mono(dir, name, mono, n);
    }
}

/* argv[2] "demo": the default transmission, 8 bars, on CAPSULE (power-on) and each of the kits 16..19 (0.9.7: GLITCH
 * HOP CHILLWAVE TAPE DECK WONK; CHILLWAVE with FEED 2's DEBRIS on the SKYWAVE tape): OUTDIR/demo-<kit>.wav (stereo) */
static void render_demo(const char *dir)
{
    static const char *const NM[5] = {"capsule", "glitch-hop", "chillwave", "tape-deck", "wonk"};
    int32_t o[2 * CTL];
    uint32_t s, b, i, n = 16000u * (FS / 1000u) / CTL;
    char path[512];
    for (s = 0; s < 5u; s++) {
        FILE *w;
        ui_power_on();
        transmission();
        song.master_q12 = 4096;
        if (s) {
            apply_preset_to(&trk[0], 15u + s);
            if (s == 2u)
                trk[1].p[P_E0] = (int16_t)(SIG_N + 3);
        }
        snprintf(path, sizeof path, "%s/demo-%s.wav", dir, NM[s]);
        w = fopen(path, "wb");
        if (!w) { perror(path); continue; }
        wav_hdr(w, n * CTL);
        seq_start();
        song.playing = 1;
        for (b = 0; b < n; b++) {
            mix_block(o, CTL);
            for (i = 0; i < CTL; i++) wav_put(w, o[2 * i], o[2 * i + 1]);
        }
        song.playing = 0;
        seq_stop();
        fclose(w);
        printf("  %s\n", path);
    }
}

/* the mix for ms into w (wav_n: the frames written, for wav_done's header) */
static uint32_t wav_n;
static void mix_ms(FILE *w, uint32_t ms)
{
    int32_t o[2 * CTL];
    uint32_t b, i, n = ms * (FS / 1000u) / CTL;
    for (b = 0; b < n; b++) {
        mix_block(o, CTL);
        for (i = 0; i < CTL; i++) wav_put(w, o[2 * i], o[2 * i + 1]);
        wav_n += CTL;
    }
}
static FILE *wav_new(const char *dir, const char *name, uint32_t ms)
{
    char path[512];
    FILE *w;
    (void)ms;
    snprintf(path, sizeof path, "%s/%s", dir, name);
    w = fopen(path, "wb");
    if (!w) { perror(path); return 0; }
    wav_hdr(w, 0);
    wav_n = 0;
    printf("  %s\n", path);
    return w;
}
static void wav_done(FILE *w)
{
    fseek(w, 0, SEEK_SET);
    wav_hdr(w, wav_n);
    fclose(w);
}

/* argv[2] "fx" (Phantom Tracker 0.9.15: the FX flipped back): a tour over the default transmission on GLITCH HOP, two
 * bars each: OUTDIR/fx-tour.wav. 1 clean; 2 the drums' FX PAD: FILTR swept (low-pass down, then a high-pass up), the
 * rest untouched; 3 the bass's GATE (1/16, then 1/32); 4 the drums' ECHO ping-pong and the chords' FLANG jet, let go:
 * the drums' repeats ring into 5; 5 the glitch on the whole mix: REPEAT 1/8, then 1/16; 6 black key 1 held (the drums
 * dry, steady): REPEAT 1/4, then STOP on the rest; 7 USER 2 (REVERSE + REVERB) on everything; 8 ATTRITION's STUTTER
 * and CRUSH, the drums left out again. The cues on stdout */
static void pad(uint32_t k, int x1, int y1, int x2, int y2)
{
    xy_trk = (uint8_t)k;
    xy_live[0].x = (uint8_t)x1; xy_live[0].y = (uint8_t)y1; xy_live[0].on = x1 != 64 || y1 != 64;
    xy_live[1].x = (uint8_t)x2; xy_live[1].y = (uint8_t)y2; xy_live[1].on = x2 != 64 || y2 != 64;
}
static void render_fx(const char *dir)
{
    FILE *w;
    uint32_t bar, i, steps = 32u;
    ui_power_on();
    transmission();
    song.master_q12 = 4096;
    apply_preset_to(&trk[0], 16);                       /* (GLITCH HOP) */
    bar = 4u * 60000u / (uint32_t)song.g[G_BPM];
    if (!(w = wav_new(dir, "fx-tour.wav", 0)))
        return;
    seq_start();
    song.playing = 1;
    printf("    0:00 clean\n");
    mix_ms(w, 2u * bar);
    printf("    bar 3: the drums' FX PAD: FILTR, the low-pass down, then the high-pass up (the rest untouched)\n");
    for (i = 0; i < steps; i++) {
        int32_t x = i < steps / 2u ? 64 - (int32_t)(i * 54u / (steps / 2u)) : 66 + (int32_t)((i - steps / 2u) * 50u / (steps / 2u));
        pad(0, x, 64, 64, 64);
        mix_ms(w, 2u * bar / steps);
    }
    printf("    bar 5: the bass's GATE 1/16, then 1/32\n");
    pad(2, 64, 100, 64, 64);
    mix_ms(w, bar);
    pad(2, 64, 120, 64, 64);
    mix_ms(w, bar);
    printf("    bar 7: the drums' ECHO ping-pong, then the chords' FLANG jet; let go at bar 9, the drums' repeats ring on\n");
    pad(0, 64, 64, 110, 64);
    mix_ms(w, bar);
    pad(1, 64, 64, 64, 110);
    mix_ms(w, bar);
    xy_trk = 0xFFu;
    xy_live[0].on = xy_live[1].on = 0;
    printf("    bar 9: the glitch on the whole mix: REPEAT 1/8, then 1/16\n");
    perf_press(PF_R8, 1);
    mix_ms(w, bar);
    perf_press(PF_R8, 0);
    perf_press(PF_R16, 1);
    mix_ms(w, bar);
    perf_press(PF_R16, 0);
    printf("    bar 11: black key 1 held (the drums dry): REPEAT 1/4, then STOP on the rest\n");
    perf_press(PF_M1, 1);
    perf_press(PF_R4, 1);
    mix_ms(w, bar);
    perf_press(PF_R4, 0);
    perf_press(PF_TAPE, 1);
    mix_ms(w, bar);
    perf_press(PF_TAPE, 0);
    perf_press(PF_M1, 0);
    printf("    bar 13: USER 2 (REVERSE + REVERB) on everything\n");
    perf_press(PF_U2, 1);
    mix_ms(w, 2u * bar);
    perf_press(PF_U2, 0);
    printf("    bar 15: ATTRITION STUTTER + CRUSH, the drums dry (black key 1)\n");
    perf_press(PF_M1, 1);
    mk_a[0] = 60;
    perf_k[1] = 50;
    mix_ms(w, 2u * bar);
    perf_press(PF_M1, 0);
    mk_a[0] = 0;
    perf_k[1] = 0;
    mix_ms(w, bar);
    song.playing = 0;
    seq_stop();
    wav_done(w);
}

/* argv[2] "engines" (Phantom Tracker): the default transmission, 8 bars, its FEEDs 2..4 (pad, bass, the bell) moved
 * onto each engine back from Dungeon Tracker, the first sound of each one's category: OUTDIR/tracker-genesis.wav,
 * tracker-analog.wav, tracker-sid.wav; then the three together (GENESIS the pad, ANALOG the bass, SID the lead):
 * OUTDIR/tracker-all.wav */
static uint32_t first_of(uint32_t e, uint32_t cat)
{
    uint32_t k;
    for (k = 0; k < ENGINES[e]->npresets; k++)
        if (preset_cat(e, k) == cat)
            return k;
    return 0;
}
static void render_engines(const char *dir)
{
    static const uint32_t E[4] = {ENGI_GENESIS, 0u, ENGI_SID, 99u};
    static const char *const NM[4] = {"tracker-genesis.wav", "tracker-analog.wav", "tracker-sid.wav", "tracker-all.wav"};
    uint32_t r, k;
    for (r = 0; r < 4u; r++) {
        FILE *w;
        uint32_t cat[4];
        ui_power_on();
        transmission();
        song.master_q12 = 4096;
        for (k = 1; k < 4u; k++)
            cat[k] = preset_cat(trk[k].eng_req % NENGINES, trk[k].preset);
        for (k = 1; k < 4u; k++) {
            uint32_t e = r < 3u ? E[r] : E[k - 1u];   /* (all: GENESIS the pad, ANALOG the bass, SID the lead) */
            set_engine_of(&trk[k], e);
            apply_preset_to(&trk[k], first_of(e, cat[k]));
            printf("    T%u %s %s\n", k + 1u, ENGINES[e]->name, ENGINES[e]->presets[trk[k].preset].name);
        }
        if (!(w = wav_new(dir, NM[r], 0)))
            return;
        seq_start();
        song.playing = 1;
        mix_ms(w, 16000u);
        song.playing = 0;
        seq_stop();
        wav_done(w);
    }
}

/* argv[2] "keys" (0.9.4): CHORD KEYS on FEED 2 (DEBRIS GLASS PAD), played live: the white keys' chords, then the same
 * keys with the black keys held (7, sus4, add9, flip, spread): OUTDIR/chord-keys.wav */
static void render_keys(const char *dir)
{
    /* {white key (0 = F3 .. ), modifier black key (0: none, else 1 + its index), length in 1/8 at 100 bpm} */
    static const uint8_t SEQ[][3] = {
        {2, 0, 4}, {3, 0, 4}, {4, 0, 4}, {5, 0, 4}, {6, 0, 4}, {7, 0, 4}, {8, 0, 4}, {9, 0, 4},   /* A3 B3 C4 .. */
        {5, 3, 4}, {3, 3, 4}, {6, 3, 4}, {7, 6, 4},                                             /* with C# (7) */
        {5, 6, 4}, {5, 0, 4}, {9, 7, 4}, {9, 0, 4},                                             /* G# (flip) */
        {8, 9, 4}, {6, 4, 4}, {9, 11, 8},                                                       /* add9, sus4, spread */
    };
    uint32_t k;
    FILE *w;
    ui_power_on();
    song.master_q12 = 4096;
    track_select(1);
    go_home();
    frames(20);
    if (!(w = wav_new(dir, "chord-keys.wav", 25000u)))
        return;
    for (k = 0; k < NELEM(SEQ); k++) {
        uint32_t ms = SEQ[k][2] * 300u, bk = SEQ[k][1] ? key_at(1, SEQ[k][1] - 1u) : 0u, wk = key_at(0, SEQ[k][0]);
        if (SEQ[k][1]) { key_down(bk); frame(); }
        key_down(wk); frame();
        mix_ms(w, ms - 120u);
        key_up(wk); frame();
        if (SEQ[k][1]) { key_up(bk); frame(); }
        mix_ms(w, 120u);
    }
    mix_ms(w, 1500u);
    wav_done(w);
}

int main(int argc, char **argv)
{
    if (argc > 2 && !strcmp(argv[2], "fx")) {
        render_fx(argv[1]);
        return 0;
    }
    if (argc > 2 && !strcmp(argv[2], "engines")) {
        render_engines(argv[1]);
        return 0;
    }
    if (argc > 2 && !strcmp(argv[2], "pitched")) {   /* 0.9.13 (Ed: "hear these new kits pitched down but maintaining the
                                                       * same tempo"): OUTDIR/pitched.wav, each real kit (30..39) under the
                                                       * transmission, the song running on: 4 s at TUNE 0, 4 s at -6
                                                       * semitones (TUNE -32), 4 s at -12 (TUNE -64) */
        uint32_t k;
        FILE *w = wav_new(argv[1], "pitched.wav", 0);
        if (!w) return 1;
        for (k = 30; k < 40u; k++) {
            ui_power_on();
            transmission();
            song.master_q12 = 4096;
            apply_preset_to(&trk[0], k);
            printf("    %s (tempo %d)\n", CK_PRESETS[k].name, (int)song.g[G_BPM]);
            seq_start();
            song.playing = 1;
            mix_ms(w, 4000u);
            trk[0].p[P_E0] = -32;
            mix_ms(w, 4000u);
            trk[0].p[P_E0] = -64;
            mix_ms(w, 4000u);
            song.playing = 0;
            seq_stop();
        }
        wav_done(w);
        return 0;
    }
    if (argc > 2 && !strcmp(argv[2], "cymbals")) {   /* 0.9.12 (Ed: "some variety of high hats, ride cymbals, and crashes"):
                                                       * OUTDIR/cymbals.wav, each FM kit (30..34) then each SID kit (35..39):
                                                       * a bar of closed hats, one of open hats on the off-beats, one on the
                                                       * ride, one of crashes, the kick and snare under them (100 HBT) */
        static const uint8_t CYM[4] = {42, 46, 51, 49};
        const uint32_t stp = 60000u / 100u / 4u;
        uint32_t k, b, j;
        FILE *w = wav_new(argv[1], "cymbals.wav", 0);
        if (!w) return 1;
        for (k = 30; k < 40u; k++) {
            ui_power_on();
            song.master_q12 = 4096;
            apply_preset_to(&trk[0], k);
            printf("    %s\n", CK_PRESETS[k].name);
            for (b = 0; b < 4u; b++)
                for (j = 0; j < 16u; j++) {
                    if (j == 0 || j == 10) trk_note_on(&trk[0], 36, 115);
                    if (j == 4 || j == 12) trk_note_on(&trk[0], 38, 110);
                    if (b == 0 && !(j & 1)) trk_note_on(&trk[0], 42, j % 4 ? 70 : 100);
                    if (b == 1 && (j & 3) == 2) trk_note_on(&trk[0], 46, 95);
                    if (b == 1 && !(j & 3)) trk_note_on(&trk[0], 42, 80);
                    if (b == 2 && (!(j & 3) || (j & 3) == 3)) trk_note_on(&trk[0], 51, (j & 3) ? 70 : 105);
                    if (b == 3 && (j == 0 || j == 8)) trk_note_on(&trk[0], CYM[3], 120);
                    mix_ms(w, stp);
                }
            mix_ms(w, 1500);
        }
        wav_done(w);
        return 0;
    }
    if (argc > 2 && (!strcmp(argv[2], "kits") || !strcmp(argv[2], "real"))) {   /* (real: 0.9.12's 30..39 on a played kit's groove, toms in its fourth bar) */      /* 0.9.10: the ten kits on the engines' voices (20..29): OUTDIR/
                                                       * kits-solo.wav (each 4 bars of a glitch-hop groove alone, 95 HBT,
                                                       * velocities soft .. hard, the kit's own sends) and kits-mix.wav
                                                       * (each under the transmission, 4 bars) */
        static const struct { uint8_t step, note, vel; } G[] = {   /* a bar: BD SD CH OH, toms and clap as PERC */
            {0, 36, 120}, {2, 42, 70}, {3, 45, 60}, {4, 38, 115}, {6, 42, 90}, {6, 36, 90}, {7, 42, 50}, {8, 42, 80},
            {9, 36, 70}, {10, 36, 110}, {10, 46, 85}, {11, 50, 70}, {12, 38, 120}, {13, 39, 60}, {14, 42, 80},
            {15, 47, 75}, {15, 42, 40},
        };
        static const struct { uint8_t step, note, vel; } R[] = {   /* a rock / funk bar: BD SD CH, the snare's ghosts */
            {0, 36, 120}, {0, 42, 90}, {2, 42, 70}, {4, 38, 118}, {4, 42, 90}, {6, 42, 70}, {7, 38, 40}, {8, 36, 110},
            {8, 42, 90}, {10, 36, 100}, {10, 42, 70}, {12, 38, 120}, {12, 42, 90}, {14, 46, 80}, {15, 38, 35},
        }, F[] = {   /* its fill: down the toms, a crash */
            {0, 36, 120}, {0, 42, 90}, {2, 42, 70}, {4, 38, 118}, {6, 38, 60}, {8, 50, 110}, {9, 50, 90}, {10, 48, 110},
            {11, 48, 90}, {12, 45, 115}, {13, 45, 95}, {14, 41, 120}, {15, 41, 100},
        };
        const int real = !strcmp(argv[2], "real");
        const uint32_t stp = 60000u / (real ? 100u : 95u) / 4u, k0 = real ? 30u : 20u;   /* ms a 16th */
        uint32_t k, b, j;
        FILE *w = wav_new(argv[1], real ? "real-solo.wav" : "kits-solo.wav", 0);
        if (!w) return 1;
        for (k = k0; k < k0 + 10u; k++) {
            ui_power_on();
            song.master_q12 = 4096;
            apply_preset_to(&trk[0], k);
            printf("    %s\n", CK_PRESETS[k].name);
            for (b = 0; b < 4u; b++)
                for (j = 0; j < 16u; j++) {
                    uint32_t e;
                    if (!real) {
                        for (e = 0; e < NELEM(G); e++)
                            if (G[e].step == j)
                                trk_note_on(&trk[0], G[e].note, G[e].vel);
                    } else if (b < 3u) {
                        for (e = 0; e < NELEM(R); e++)
                            if (R[e].step == j)
                                trk_note_on(&trk[0], R[e].note, R[e].vel);
                    } else {
                        for (e = 0; e < NELEM(F); e++)
                            if (F[e].step == j)
                                trk_note_on(&trk[0], F[e].note, F[e].vel);
                    }
                    mix_ms(w, stp);
                }
            trk_note_on(&trk[0], 36, 120);                  /* (the bar after: kick and crash) */
            trk_note_on(&trk[0], 49, 110);
            mix_ms(w, 1400);
        }
        wav_done(w);
        w = wav_new(argv[1], real ? "real-mix.wav" : "kits-mix.wav", 0);
        if (!w) return 1;
        for (k = k0; k < k0 + 10u; k++) {
            ui_power_on();
            transmission();
            song.master_q12 = 4096;
            apply_preset_to(&trk[0], k);
            seq_start();
            song.playing = 1;
            mix_ms(w, 8000u);
            song.playing = 0;
            seq_stop();
        }
        wav_done(w);
        return 0;
    }
    if (argc > 2 && !strcmp(argv[2], "vult")) {      /* 0.9.10: the transmission, 8 bars, on GLITCH HOP: FEED 3's bass on
                                                       * VULT's LATERAL BASS, then FEED 4's lead on SQUELCH LEAD too */
        static const char *const NM[2] = {"vult-bass.wav", "vult-bass-lead.wav"};
        uint32_t r;
        for (r = 0; r < 2u; r++) {
            FILE *w;
            ui_power_on();
            transmission();
            song.master_q12 = 4096;
            apply_preset_to(&trk[0], 16);
            set_engine_of(&trk[2], ENGI_VULT);
            apply_preset_to(&trk[2], 0);
            if (r) {
                set_engine_of(&trk[3], ENGI_VULT);
                apply_preset_to(&trk[3], 7);
            }
            if (!(w = wav_new(argv[1], NM[r], 0)))
                return 1;
            seq_start();
            song.playing = 1;
            mix_ms(w, 16000u);
            song.playing = 0;
            seq_stop();
            wav_done(w);
        }
        return 0;
    }
    if (argc > 2 && !strcmp(argv[2], "glorp")) {     /* 0.9.8: the transmission, 8 bars: FEED 1 on GLITCH HOP, FEED 3's
                                                       * bass on GLORP, then FEED 4's lead too (GLOOP LEAD) */
        static const char *const NM[2] = {"glorp-bass.wav", "glorp-bass-lead.wav"};
        uint32_t r;
        for (r = 0; r < 2u; r++) {
            FILE *w;
            ui_power_on();
            transmission();
            song.master_q12 = 4096;
            apply_preset_to(&trk[0], 16);
            set_engine_of(&trk[2], ENGI_GLORP);
            apply_preset_to(&trk[2], 0);
            if (r) {
                set_engine_of(&trk[3], ENGI_GLORP);
                apply_preset_to(&trk[3], 3);
            }
            if (!(w = wav_new(argv[1], NM[r], 0)))
                return 1;
            seq_start();
            song.playing = 1;
            mix_ms(w, 16000u);
            song.playing = 0;
            seq_stop();
            wav_done(w);
        }
        return 0;
    }
    if (argc > 2 && !strcmp(argv[2], "keys")) {
        render_keys(argv[1]);
        return 0;
    }
    if (argc > 2 && !strcmp(argv[2], "demo")) {
        render_demo(argv[1]);
        return 0;
    }
    if (argc > 2 && !strcmp(argv[2], "tapes")) {
        render_tapes(argv[1]);
        return 0;
    }
    if (argc > 2 && !strcmp(argv[2], "sets")) {
        render_sets(argv[1]);
        return 0;
    }
    static const uint8_t NOTE[8] = {36, 38, 39, 42, 46, 45, 37, 56};
    static const char *const NAME[8] = {"noise-01-kick", "noise-02-snare", "noise-03-clap", "noise-04-hat-closed",
                                        "noise-05-hat-open", "noise-06-tom", "noise-07-rim", "noise-08-bell"};
    const char *dir = argc > 1 ? argv[1] : ".";
    uint32_t k, n;

    for (k = 0; k < 8u; k++) {
        ui_power_on();                               /* FEED 1: NOISE CAPSULE (engines.c TRK_DEF) */
        song.playing = 0;
        song.master_q12 = 4096;
        dry(&trk[0]);
        hit_note = NOTE[k];
        trk_note_on(&trk[0], NOTE[k], 110);
        n = run(NOTE[k] == 46 || NOTE[k] == 56 ? 1600 : 1200, hit_off);   /* (held 80 ms, as a sixteenth would) */
        write_mono(dir, NAME[k], mono, n);
    }
    {
        static const uint8_t SIG[2] = {6, 15};
        static const char *const TAPE[2] = {"noise-09-grain-solar-wind", "noise-10-grain-dark-side"};
        for (k = 0; k < 2u; k++) {
            ui_power_on();
            song.playing = 0;
            song.master_q12 = 4096;
            set_engine_of(&trk[1], ENGI_MORPH);
            trk[1].p[P_E0] = SIG[k];
            trk[1].p[P_E3] = 0;
            trk[1].p[P_E4] = 0;
            trk[1].p[P_SUS] = 127;
            dry(&trk[1]);
            run(100, 0);                             /* (the engine switch happens in the audio's time) */
            trk[1].p[P_E0] = SIG[k];
            trk_note_on(&trk[1], 48, 100);
            n = run(3000, sweep);
            write_mono(dir, TAPE[k], mono, n);
            trk_note_off(&trk[1], 48);
        }
    }
    return 0;
}
