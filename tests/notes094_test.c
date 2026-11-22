/* SPDX-License-Identifier: GPL-3.0-only
 * Phantom Cosmonaut 0.9.4: Ed's hardware notes on 0.9.3 (docs/NEXT-BUILD.md), through ui_test.c's harness:
 *   1. the EDIT layer's No. steps every sound one by one (LIST ROLE no longer skips there)
 *   2. the FX PAD redraws as KNOB 1-4 move the crosshairs, and keeps moving with the tempo while playing (still when
 *      stopped, or ANIM OFF)
 *   3. CHORD KEYS, FEED 2's default: white keys the scale's chords, black keys held modifiers (7 sus add9 flip spread)
 *   4. ATTRITION prints: REC armed, playing, FX held: the steps print; they play back; an empty overdub erases; they
 *      survive the pattern's store; FX + OCT- clears
 *   5. heard in the mix: PHANTOM over the transmission; FEED 4 on the engines Phantom Tracker brought back (GENESIS
 *      ANALOG SID) near its FM lead (0.9.4's NUMBERS checks went with the speech)
 *   build/host/notes094_test */
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"

static void live(uint32_t ms)                     /* the UI a frame each 16 ms, the audio between */
{
    int32_t o[2 * CTL];
    uint32_t i, b;
    for (i = 0; i < ms / 16u; i++) {
        frame();
        for (b = 0; b < 22u; b++)
            mix_block(o, CTL);
    }
}
static void chord_of(uint32_t mods, uint32_t key, char *nm, uint8_t *out, uint32_t *n)
{
    int32_t r;
    uint16_t mask;
    ch_mods = (uint8_t)mods;
    *n = chord_make(&trk[1], kb_map(&trk[1], key), out, &r, &mask);
    chord_name(nm, (uint32_t)r, mask);
    ch_mods = 0;
}

/* the loudness (dB) of: who 0 the transmission's FEEDs, 1 PHANTOM alone (a key every half second, D4), 2 FEED 4
 * alone (a note a beat); eng: FEED 4 on engine eng - 1's first sound, 0 its own (FM) */
static float mixbuf[FS * 10];
static double mix_loud(int who, int eng, int unused)
{
    int32_t o[2 * CTL];
    uint32_t b, i, k, n = 0, on = 0, m = 0;
    double w[32], s = 0;
    (void)unused;
    ui_power_on();
    transmission();
    song.master_q12 = 4096;
    if (who == 1) {
        for (k = 0; k < 4u; k++) trk[k].p[P_MUTE] = 1;
    } else if (who == 2) {
        track_t *t = &trk[3];
        if (eng) {   /* the engine's first LEAD (0.9.14: GENESIS's first sound is GEN BASS, a pluck that falls 21 dB:
                      * it passed only while its operators were never loaded and it sounded as static) */
            uint32_t e = (uint32_t)eng - 1u, pi = 0;
            while (pi < ENGINES[e]->npresets && preset_cat(e, pi) != CAT_LEAD) pi++;
            set_engine_of(t, e);
            apply_preset_to(t, pi < ENGINES[e]->npresets ? pi : 0u);
        }
        for (k = 0; k < 32u; k++) { t->step[k].n = 0; t->step[k].time = ST_REST; }
        for (k = 0; k < 32u; k += 4u) { t->step[k].note[0] = 50; t->step[k].n = 1; t->step[k].time = ST_NOTE; }
        trk[0].p[P_MUTE] = trk[1].p[P_MUTE] = trk[2].p[P_MUTE] = 1;
    } else
        trk[3].p[P_MUTE] = 1;
    seq_start();
    song.playing = 1;
    for (b = 0; b < FS * 10 / CTL; b++) {
        if (who == 1 && b % (FS / 2 / CTL) == 0) { if (on) ph_key_off(0); ph_key_on(0, 62); on = 1; }
        mix_block(o, CTL);
        for (i = 0; i < CTL; i++) mixbuf[n++] = (float)(o[2 * i] + o[2 * i + 1]) * 0.5f;
    }
    if (on) ph_key_off(0);
    song.playing = 0;
    seq_stop();
    for (k = 0; k + FS * 4 / 10 <= n && m < 32u; k += FS * 4 / 10, m++) {
        double e = 0;
        for (i = k; i < k + FS * 4 / 10; i++) e += (double)mixbuf[i] * mixbuf[i];
        w[m] = sqrt(e / (FS * 0.4));
    }
    for (i = 0; i < m; i++)
        for (k = i + 1u; k < m; k++)
            if (w[k] > w[i]) { double t = w[i]; w[i] = w[k]; w[k] = t; }
    for (i = 0; i < m * 3u / 10u + 1u; i++) s += w[i];
    return 20.0 * log10(s / (m * 3u / 10u + 1u) / 32768.0 + 1e-9);
}

int main(void)
{
    int bad = 0, ok;
    char what[240];

    /* 1 */
    {
        uint32_t i, total, seen = 0, prev, steps = 0;
        ui_power_on();
        track_select(3);                                /* FEED 4: FM */
        go_home(); frames(20);
        list_set(LIST_CLASS);
        btn_down(B_EDIT); frames(500);
        eng_list_pos(&total);
        prev = TSEL->preset;
        ok = ui.layer == LAYER_EDIT && total >= 8u;
        for (i = 0; i < total; i++) {
            turn(EN_K2, 1);
            frames(10);
            ok &= TSEL->preset == (prev + 1u) % total;
            seen |= 1u << (TSEL->preset & 31u);
            prev = TSEL->preset;
            steps++;
        }
        btn_up(B_EDIT); frames(20);
        snprintf(what, sizeof what, "EDIT layer, LIST ROLE: No. steps every sound one by one (%u of %u, round)", steps, total);
        bad += check(what, ok && seen == (total >= 32u ? 0xFFFFFFFFu : (1u << total) - 1u));
    }

    /* 2 */
    {
        uint32_t s0, s1, k, moved = 0, still = 0;
        ui_power_on();
        track_select(1);
        go_home(); frames(20);
        press(B_FX); frames(50);
        ok = !ui.home && cur_page()->graph == GR_XY;
        s0 = graph_signature();
        turn(EN_K1, 5); frames(5);
        s1 = graph_signature();
        ok &= s1 != s0;
        turn(EN_K3, -5); frames(5);
        ok &= graph_signature() != s1;
        bad += check("FX PAD: KNOB 1 and 3 move the crosshairs and the pad is drawn again (the signature hashes them)", ok);
        s0 = graph_signature();
        for (k = 0; k < 20u; k++) { frame(); still += graph_signature() == s0; }
        seq_start(); song.playing = 1;
        s0 = graph_signature();
        for (k = 0; k < 20u; k++) { live(16); moved += graph_signature() != s0; s0 = graph_signature(); }
        song.playing = 0; seq_stop(); frames(5);
        snprintf(what, sizeof what, "  playing: alive with the tempo (%u of 20 frames new); stopped: still (%u of 20 the same)",
                 moved, still);
        bad += check(what, moved >= 8u && still == 20u);
    }

    /* 3 */
    {
        static const uint32_t W[7] = {7, 9, 11, 12, 14, 16, 18};   /* the white keys from C4 */
        static const char *const NM[7] = {"C", "Dm", "Em", "F", "G", "Am", "Bdim"};
        char nm[16], line[160] = "";
        uint8_t out[8];
        uint32_t i, n;
        ui_power_on();
        ok = trk[1].p[P_CHRD] == (int16_t)CHRD_KEYS && trk[0].p[P_CHRD] == 0 && trk[2].p[P_CHRD] == 0 &&
             trk[3].p[P_CHRD] == 0;
        bad += check("CHORD KEYS: FEED 2 powers on with KEYS, the other FEEDs OFF", ok);
        ok = 1;
        for (i = 0; i < 7u; i++) {
            chord_of(0, W[i], nm, out, &n);
            ok &= !strcmp(nm, NM[i]) && n == 3u;
            str_cpy(line + str_len(line), nm, sizeof line - str_len(line));
            str_cpy(line + str_len(line), " ", sizeof line - str_len(line));
        }
        snprintf(what, sizeof what, "  the white keys: the scale's chords (%s)", line);
        bad += check(what, ok);
        ok = kb_map(&trk[1], 1) == KB_SILENT && ch_mod_of(&trk[1], 1) != 0u;
        bad += check("  a black key alone plays nothing: it is a modifier", ok);
        ok = 1; line[0] = 0;
        chord_of(CHM_7, W[5], nm, out, &n); ok &= !strcmp(nm, "Am7") && n == 4u; str_cpy(line, nm, sizeof line);
        chord_of(CHM_SUS4, W[4], nm, out, &n); ok &= !strcmp(nm, "Gsus4"); str_cpy(line + str_len(line), " ", 4); str_cpy(line + str_len(line), nm, 16);
        chord_of(CHM_ADD9, W[5], nm, out, &n); ok &= !strcmp(nm, "Amadd9"); str_cpy(line + str_len(line), " ", 4); str_cpy(line + str_len(line), nm, 16);
        chord_of(CHM_FLIP, W[5], nm, out, &n); ok &= !strcmp(nm, "A"); str_cpy(line + str_len(line), " ", 4); str_cpy(line + str_len(line), nm, 16);
        chord_of(CHM_7 | CHM_ADD9, W[4], nm, out, &n); ok &= !strcmp(nm, "G9"); str_cpy(line + str_len(line), " ", 4); str_cpy(line + str_len(line), nm, 16);
        chord_of(CHM_SUS2, W[1], nm, out, &n); ok &= !strcmp(nm, "Dsus2"); str_cpy(line + str_len(line), " ", 4); str_cpy(line + str_len(line), nm, 16);
        chord_of(CHM_SPREAD, W[0], nm, out, &n);
        ok &= n >= 3u && out[0] + 12u <= out[1];        /* (the root an octave down, under the rest) */
        snprintf(what, sizeof what, "  held with C# 7, D# sus4, F# add9, G# flip, C#+F# 9, upper D# sus2: %s; A# spread opens it", line);
        bad += check(what, ok);
    }

    /* 4 */
    {
        uint64_t st;
        uint32_t n, seen = 0, k, after;
        ui_power_on(); go_home(); frames(50);
        song.g[G_ENGGO] = 0; trk[0].p[P_SLEN] = 16;
        song.rec = 1u;
        seq_start(); song.playing = 1; live(200);
        btn_down(B_FX); frames(900); turn(EN_K1, 6); turn(EN_K4, 8);   /* STUTTER, CRUSH up */
        ok = !strncmp(layer_head(), "[FX]", 4) && strstr(layer_head(), "PRINT") != 0;
        live(2200);                                     /* over a bar at 120 */
        btn_up(B_FX); live(100);
        n = atp_count(&st);
        song.rec = 0;
        for (k = 0; k < 150u; k++) { live(16); seen += mk_a[0] > 0; }
        snprintf(what, sizeof what, "ATTRITION prints: REC armed, FX held: %u steps printed; let go and disarmed they play (STUTTER up on %u of 150 frames)",
                 n, seen);
        bad += check(what, ok && n == 16u && seen > 20u && seen < 150u);
        song.rec = 1u; btn_down(B_FX); frames(900); mk_reset(); live(2200); btn_up(B_FX); live(50);
        after = atp_count(0);
        snprintf(what, sizeof what, "  an overdub with nothing up erases them (%u left)", after);
        bad += check(what, after == 0u);
        btn_down(B_FX); frames(900); turn(EN_K2, 5); live(2200); btn_up(B_FX); live(50);
        {
            fxp_store_t c = fxp;
            fxp_bound(&c);
            n = atp_count(0);
            ok = n > 0u && c.count == fxp.count && fxp_count(0) == 0u;
        }
        bad += check("  they survive the pattern's store (fxp_bound), the FX PAD's counts don't see them", ok);
        btn_down(B_FX); frames(900); press(B_OCTDN); frames(30);
        ok = atp_count(0) == 0u && msg_is("ATTRITION CLEARED");
        btn_up(B_FX);
        song.playing = 0; seq_stop(); song.rec = 0;
        bad += check("  FX + OCT- with REC armed clears them (ATTRITION CLEARED)", ok);
    }

    /* 5 (Ed, after 0.9.4's demos: "I can't hear the voice synth at all ... buried under the rest"): heard in the
     * mix. Loudness: the loudest 30 % of 400 ms windows, through the master. Phantom Tracker: PHANTOM (MORPH) over the
     * transmission; FEED 4's lead on GENESIS, ANALOG and SID as loud as on FM, within 6 dB (their first LEADs) */
    {
        static const uint32_t E[3] = {ENGI_GENESIS, 0u, ENGI_SID};
        double ph, bed, f4[3], fm;
        uint32_t i, ok;
        ph = mix_loud(1, 0, 0);
        bed = mix_loud(0, 0, 0);
        fm = mix_loud(2, 0, 0);
        ok = ph > bed - 6.0;
        for (i = 0; i < 3u; i++) {
            f4[i] = mix_loud(2, (int)E[i] + 1, 0);
            ok &= fabs(f4[i] - fm) < 6.0;
        }
        snprintf(what, sizeof what, "heard in the mix: PHANTOM %.1f dB over the transmission %.1f; FEED 4 on FM %.1f, GENESIS %.1f, "
                 "ANALOG %.1f, SID %.1f", ph, bed, fm, f4[0], f4[1], f4[2]);
        bad += check(what, ok);
    }

    printf("notes094: %s\n", bad ? "FAILED" : "all checks ok");
    return bad != 0;
}
