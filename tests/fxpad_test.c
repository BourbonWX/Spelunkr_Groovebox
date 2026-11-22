/* SPDX-License-Identifier: GPL-3.0-only
 * The FX PAD (firmware/src/fxpad.c): the FEED's (Phantom Tracker 0.9.15, Ed: "the X/Y being per track"; on the whole
 * mix 0.9.6–0.9.14), its four FILTR GATE (crosshair 1's X Y, KNOB 1 2) ECHO FLANG (crosshair 2's, KNOB 3 4), through
 * the real UI, sequencer, mix and project code (ui_test.c's harness):
 *   1. FX's first page is the selected FEED's pad; untouched it does nothing (the mix as without it)
 *   2. FILTR: left a low-pass, right a high-pass, on the pad's FEED only (FEED 3 sounding beside it untouched)
 *   3. GATE: the FEED chopped on the tempo (up 1/16, down 1/8)
 *   4. ECHO: repeats after the sound stops; they ring out after the pad is left, then stop; only its FEED's sound echoes
 *   5. FLANG: up and down two different sounds
 *   6. the cards say what each knob does; PRESETS names the four, ALGORITHM moves the pad to another FEED
 *   7. left and opened again: the crosshairs back at the centre, untouched, silent
 *   8. REC prints the FEED's steps; they play with the pad closed; OCT- clears them; the bank and the project keep them;
 *      0.9.6–0.9.14's prints of the mix play on every FEED and split into four when printed into; 0.9.4's load as the
 *      mix's
 *   9. every axis at its ends: no runaway level
 *  10. cost: the four at once against the mix without
 *   build/host/fxpad_test */
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"

static int32_t O[2 * CTL];
/* blocks of the whole mix: its rms and peak (left), and (br) its brightness, the first difference's share */
static double mix_run(uint32_t blocks, int32_t *pk, double *br)
{
    double s = 0, d = 0;
    int32_t prev = 0;
    uint32_t b, i, n = 0;
    if (pk) *pk = 0;
    for (b = 0; b < blocks; b++) {
        memset(O, 0, sizeof O);
        mix_block(O, CTL);
        for (i = 0; i < CTL; i++, n++) {
            int32_t a = O[2 * i] < 0 ? -O[2 * i] : O[2 * i];
            if (pk && a > *pk) *pk = a;
            s += (double)O[2 * i] * O[2 * i];
            d += (double)(O[2 * i] - prev) * (O[2 * i] - prev);
            prev = O[2 * i];
        }
    }
    if (br) *br = s > 0 ? d / s : 0;
    return n ? sqrt(s / n) : 0;
}
/* FEED 2 (a chord) and FEED 3 (a high note) held, the others at level 0, no sends; no release (REL 0) */
static void two_feeds(void)
{
    uint32_t k;
    ui_power_on();
    song.master_q12 = 4096;
    for (k = 0; k < NTRK; k++) {
        trk[k].p[P_REV] = trk[k].p[P_DLY] = trk[k].p[P_CHOR] = 0;
        if (k != 1u && k != 2u) trk[k].p[P_LEVEL] = 0;
    }
    for (k = 1; k < 3u; k++) {
        set_engine_of(&trk[k], ENGI_MORPH);
        apply_preset_to(&trk[k], 0u);                  /* (MORPH CARRIER: a steady tone) */
        trk[k].engine = trk[k].eng_req;
        trk[k].p[P_SUS] = 127;
        trk[k].p[P_REL] = 0;
        trk[k].p[P_REV] = trk[k].p[P_DLY] = trk[k].p[P_CHOR] = 0;   /* (after the preset: its sends off) */
    }
    track_select(1);
    mix_run(64, 0, 0);
    trk_note_on(&trk[1], 60, 110);
    trk_note_on(&trk[1], 67, 110);
    trk_note_on(&trk[2], 76, 110);
    mix_run(40, 0, 0);
}
/* the pad's values set by hand, open (the ISR reads them as the knobs would leave them) */
static void pad_set(int32_t x1, int32_t y1, int32_t x2, int32_t y2)
{
    xy_trk = 1;                                         /* (FEED 2's: the chord, the selected FEED) */
    xy_live[0].x = (uint8_t)x1; xy_live[0].y = (uint8_t)y1; xy_live[0].on = 1;
    xy_live[1].x = (uint8_t)x2; xy_live[1].y = (uint8_t)y2; xy_live[1].on = 1;
}
static void pad_off(void)
{
    xy_trk = 0xFFu;
    xy_live[0].on = xy_live[1].on = 0;
}
static int pad_open(void) { return !ui.home && cur_page()->graph == GR_XY && xy_trk == song.sel % NTRK; }
/* the mix's level in windows of w samples over `blocks`: their max / min ratio (a gate's chop) */
static double chop(uint32_t blocks, uint32_t w)
{
    double s = 0, lo = 1e30, hi = 0;
    uint32_t b, i, n = 0;
    for (b = 0; b < blocks; b++) {
        memset(O, 0, sizeof O);
        mix_block(O, CTL);
        for (i = 0; i < CTL; i++) {
            s += (double)O[2 * i] * O[2 * i];
            if (++n == w) {
                double r = sqrt(s / w);
                lo = r < lo ? r : lo;
                hi = r > hi ? r : hi;
                s = 0;
                n = 0;
            }
        }
    }
    return lo > 0 ? hi / lo : hi > 0 ? 1e9 : 1.0;
}

int main(void)
{
    int bad = 0, ok;
    int32_t pk;
    double r0, r1, b0, b1;
    uint32_t i;
    char what[220];

    /* 1 */
    two_feeds();
    r0 = mix_run(60, 0, &b0);
    press(B_FX);
    frames(50);
    ok = pad_open() && !xy_live[0].on && !xy_live[1].on && xy_live[0].x == 64 && xy_live[1].y == 64 && !xy_want() &&
         song.sel == 1u;
    r1 = mix_run(60, 0, &b1);
    snprintf(what, sizeof what, "FX tap: the pad (FEED 2's, the selected), both crosshairs at the centre, untouched: nothing runs (rms %.0f / %.0f)", r0, r1);
    bad += check(what, ok && fabs(r1 - r0) < r0 * 0.2 + 1.0);   /* (CARRIER's envelope moving SCAN) */

    /* 2 FILTR: on FEED 2 alone (FEED 3 at level 0); then FEED 3 beside a silent FEED 2, against the same run without */
    go_home(); frames(400);
    two_feeds();
    trk[2].p[P_LEVEL] = 0;
    r0 = mix_run(60, 0, &b0);
    press(B_FX); frames(50);
    turn(EN_K1, -50);
    frames(32);
    r1 = mix_run(80, 0, &b1);
    {
        char val[8];
        xm_card(0, val, sizeof val);
        snprintf(what, sizeof what, "KNOB 1 left: FILTR's low-pass on FEED 2, duller (brightness %.4f -> %.4f); the card %s", b0, b1, val);
        bad += check(what, xy_live[0].on && xy_live[0].x == 14 && !xy_live[1].on && b1 < b0 * 0.8 && str_eq(val, "LP 78"));
    }
    go_home(); frames(50);
    pad_set(120, 64, 64, 64);                          /* right: a high-pass */
    mix_run(80, 0, 0);
    r1 = mix_run(60, 0, &b1);
    snprintf(what, sizeof what, "  right: a high-pass, thinner (brightness %.4f, open %.4f; rms %.0f of %.0f)", b1, b0, r1, r0);
    bad += check(what, b1 > b0 * 2.0 && r1 < r0 * 0.7);
    pad_off();
    {   /* FEED 2's pad: FEED 3 untouched (0.9.15); FEED 3's own pad filters it */
        double lo[3];
        uint32_t j;
        for (j = 0; j < 3u; j++) {
            two_feeds();
            trk[1].p[P_LEVEL] = 0;
            if (j == 1u)
                pad_set(14, 64, 64, 64);
            if (j == 2u) {
                xy_trk = 2;
                xy_live[0].x = 14; xy_live[0].y = 64; xy_live[0].on = 1;
            }
            mix_run(60, 0, 0);
            lo[j] = mix_run(60, 0, 0);
            pad_off();
        }
        snprintf(what, sizeof what, "  .. FEED 2's only: FEED 3 beside it %.0f, under FEED 2's pad %.0f; under its own %.0f",
                 lo[0], lo[1], lo[2]);
        bad += check(what, fabs(lo[1] - lo[0]) < lo[0] * 0.01 && lo[2] < lo[0] * 0.65);
    }

    /* 3 GATE */
    {
        double c0, c1, c2;
        two_feeds();
        trk[2].p[P_LEVEL] = 0;                         /* (FEED 2 alone: its pad's) */
        seq_start(); song.playing = 1;
        mix_run(40, 0, 0);
        c0 = chop(beat_samples() * 2u / CTL, beat_samples() / 8u);
        pad_set(64, 110, 64, 64);                      /* up: 1/16 at 46 from the centre: 1/32 */
        mix_run(40, 0, 0);
        c1 = chop(beat_samples() * 2u / CTL, beat_samples() / 32u);
        pad_set(64, 30, 64, 64);                       /* down: 1/8 */
        mix_run(40, 0, 0);
        c2 = chop(beat_samples() * 2u / CTL, beat_samples() / 8u);
        snprintf(what, sizeof what, "GATE: up FEED 2 chopped on 1/32s (max / min %.1f), down on 1/8s (%.1f); open %.1f", c1, c2, c0);
        bad += check(what, c1 > 4.0 && c2 > 4.0 && c0 < 1.5);
        pad_off();
        mix_run(40, 0, 0);
        seq_stop(); song.playing = 0;
    }

    /* 4 ECHO: against the same run without it (the difference: the repeats; what still sounds after the notes, the
     * buses and the limiter's recovery, is the same in both) */
    {
        static float ref[3][44100], w[44100];
        double held, d1 = 0, d2 = 0, d3 = 0;
        uint32_t n, b, j, nb = beat_samples() / CTL;
        for (j = 0; j < 3u; j++) {                      /* 0: no pad; 1: ECHO right, the pad open; 2: KNOB 3 left, the pad left */
            float *o = j ? w : ref[0];
            two_feeds();
            if (j == 1u)
                pad_set(64, 64, 120, 64);
            if (j == 2u) {
                frames(400);
                press(B_FX); frames(50);
                turn(EN_K3, -50); frames(32);
            }
            mix_run(200, 0, 0);
            held = mix_run(20, 0, 0);
            if (j == 2u) {
                go_home(); frames(50);
            }
            trk_note_off(&trk[1], 60); trk_note_off(&trk[1], 67); trk_note_off(&trk[2], 76);
            mix_run(nb, 0, 0);
            for (n = 0, b = 0; b < nb && n + CTL <= 44100u; b++) {
                memset(O, 0, sizeof O);
                mix_block(O, CTL);
                for (i = 0; i < CTL; i++) o[n++] = (float)O[2 * i];
            }
            if (j) {
                double d = 0;
                uint32_t q;
                for (q = 0; q < n; q++) d += (double)(w[q] - ref[0][q]) * (w[q] - ref[0][q]);
                d = sqrt(d / n);
                if (j == 1u) d1 = d; else d2 = d;
            }
            if (j == 2u) {
                mix_run(4u * FS / CTL, 0, 0);
                d3 = xm.eon || xy_want() ? 1e9 : 0;
            }
            pad_off();
        }
        snprintf(what, sizeof what, "ECHO: the notes off, its repeats go on (a beat after, %.0f against the run without; held %.0f)", d1, held);
        bad += check(what, d1 > held * 0.1);
        snprintf(what, sizeof what, "  KNOB 3 left (1/8), the pad left: the repeats ring out (%.0f), then stop (the lines off)", d2);
        bad += check(what, d2 > held * 0.1 && d3 == 0);
        {   /* only the pad's FEED echoes: FEED 2 silent, FEED 3 playing, FEED 2's ECHO on: nothing repeats */
            double dq = 0;
            uint32_t q;
            for (j = 0; j < 2u; j++) {
                float *o = j ? w : ref[0];
                two_feeds();
                trk[1].p[P_LEVEL] = 0;
                if (j)
                    pad_set(64, 64, 120, 64);
                mix_run(200, 0, 0);
                trk_note_off(&trk[1], 60); trk_note_off(&trk[1], 67); trk_note_off(&trk[2], 76);
                mix_run(nb, 0, 0);
                for (n = 0, b = 0; b < nb && n + CTL <= 44100u; b++) {
                    memset(O, 0, sizeof O);
                    mix_block(O, CTL);
                    for (i = 0; i < CTL; i++) o[n++] = (float)O[2 * i];
                }
                pad_off();
            }
            for (q = 0; q < n; q++) dq += (double)(w[q] - ref[0][q]) * (w[q] - ref[0][q]);
            dq = sqrt(dq / n);
            snprintf(what, sizeof what, "  only its FEED echoes: FEED 2 silent under its ECHO, FEED 3 playing: no repeats (%.1f)", dq);
            bad += check(what, dq < held * 0.01 + 1.0);
        }
    }

    /* 5 FLANG */
    {
        static float a[8192], u[8192], d[8192];
        double du = 0, dd = 0, ud = 0, e = 0;
        uint32_t n, b, j;
        for (j = 0; j < 3u; j++) {
            float *w = j == 0 ? a : j == 1 ? u : d;
            two_feeds();
            if (j)
                pad_set(64, 64, 64, j == 1u ? 120 : 8);
            mix_run(100, 0, 0);
            for (n = 0, b = 0; b < 8192u / CTL; b++) {
                memset(O, 0, sizeof O);
                mix_block(O, CTL);
                for (i = 0; i < CTL; i++) w[n++] = (float)O[2 * i];
            }
            pad_off();
        }
        for (n = 0; n < 8192u; n++) {
            e += (double)a[n] * a[n];
            du += (double)(u[n] - a[n]) * (u[n] - a[n]);
            dd += (double)(d[n] - a[n]) * (d[n] - a[n]);
            ud += (double)(u[n] - d[n]) * (u[n] - d[n]);
        }
        snprintf(what, sizeof what, "FLANG: up (a jet) and down (hollow) each change the mix and differ (%.2f, %.2f, %.2f of its rms)",
                 sqrt(du / e), sqrt(dd / e), sqrt(ud / e));
        bad += check(what, du > e * 0.05 && dd > e * 0.05 && ud > e * 0.05);
    }

    /* 6 the cards, PRESETS, ALGORITHM */
    {
        char v[4][8];
        int32_t bar[4];
        two_feeds();
        press(B_FX); frames(50);
        turn(EN_K1, 20); turn(EN_K2, -40); turn(EN_K3, -20); turn(EN_K4, 30);
        frames(32);
        for (i = 0; i < 4u; i++)
            bar[i] = xm_card(i, v[i], sizeof v[i]);
        ok = str_eq(v[0], "HP 31") && str_eq(v[1], "1/4") && str_eq(v[2], "1/8") && str_eq(v[3], "JET") && bar[1] > 600;
        turn(EN_PRESET, 1); frames(4);
        ok &= msg_is("FILTR GATE ECHO FLANG") && TSEL->preset == 0u;
        turn(EN_ALGO, 1); frames(32);
        ok &= song.sel == 2u && pad_open() && xy_trk == 2u && !xy_live[0].on && xy_live[0].x == 64;
        snprintf(what, sizeof what, "the cards: %s %s %s %s; PRESETS names the four (no sound loaded), ALGORITHM moves the pad to "
                 "FEED 3 (its crosshairs at the centre)", v[0], v[1], v[2], v[3]);
        bad += check(what, ok);
    }

    /* 7 */
    go_home();
    frames(50);
    ok = xy_trk == 0xFFu;
    mix_run(5u * FS / CTL, 0, 0);                      /* (their fade, the echo's ringing) */
    ok &= !xy_want();
    frames(400);                                       /* (not a double tap: that locks FX's layer) */
    press(B_FX);
    frames(50);
    ok &= pad_open() && !xy_live[0].on && !xy_live[1].on && xy_live[0].x == 64 && xy_live[1].y == 64;
    bad += check("left: the effects stop (the echo rings out); opened again: the crosshairs back at the centre, untouched", ok);

    /* 8 prints */
    {
        step_t st[NSTEP];
        motion_store_t mo;
        uint8_t rec;
        uint64_t pr;
        uint32_t n;
        go_home(); frames(50);
        two_feeds();
        trk[2].p[P_LEVEL] = 0;                         /* (FEED 2 alone: its prints heard) */
        memcpy(st, trk[1].step, sizeof st);
        mo = motion;
        rec = song.rec;
        press(B_FX); frames(50);
        turn(EN_K1, 40); turn(EN_K2, 30);             /* (a high-pass: the printed steps thinner) */
        frames(32);
        press(B_REC); frames(32);
        ok = xy_print && song.rec == rec;
        seq_start(); song.playing = 1;
        mix_run(beat_samples() * 2u / CTL, 0, 0);     /* (two beats: 8 of FEED 1's steps) */
        n = fxp_count(&pr);
        snprintf(what, sizeof what, "REC on the pad: PRINTING; playing, every step of FEED 2's printed (%u, steps 0x%llx); no REC arming",
                 n, (unsigned long long)pr);
        bad += check(what, ok && n >= 7u && (pr & 1u) && FXP_FEED(fxp.ev[0]) == 1u && (fxp.ev[0].ty & 0x11u) == 0x01u && fxp.ev[0].place < NSTEP &&
                           (fxp.rsv & FXP_FEEDS));
        ok = !memcmp(st, trk[1].step, sizeof st) && !memcmp(&mo, &motion, sizeof mo) && song.rec == rec;
        bad += check("  the FEEDs' steps, the motion and REC arming untouched", ok);
        go_home(); frames(50);
        ok = !xy_print && xy_trk == 0xFFu;
        n = fxp_count(0);
        mix_run(beat_samples() * 4u / CTL, 0, 0);
        ok &= fxp_count(0) == n;                      /* (left: no more prints) */
        {   /* a printed step plays with the pad closed: the mix differs from its prints cleared */
            double a, c;
            seq_stop(); seq_start(); song.playing = 1;
            a = mix_run(beat_samples() / CTL, 0, &b0);
            seq_stop();
            fm1_irq_off(); fxp_clear_pad(); fm1_irq_on();
            ok &= fxp_count(0) == 0u;
            seq_start(); song.playing = 1;
            mix_run(200, 0, 0);
            seq_stop(); seq_start(); song.playing = 1;
            c = mix_run(beat_samples() / CTL, 0, &b1);
            seq_stop();
            snprintf(what, sizeof what, "  left: printing stops; the printed steps play, the pad closed (rms %.0f / %.0f without)",
                     a, c);
            bad += check(what, ok && a < c * 0.75 && a > 1.0 && c > 1.0);
        }
        song.playing = 0;
    }
    {   /* OCT- on the pad clears the prints, ATTRITION's kept */
        two_feeds();
        press(B_FX); frames(50);
        turn(EN_K1, -40); frames(32);
        press(B_REC); frames(32);
        seq_start(); song.playing = 1;
        mix_run(beat_samples() / CTL, 0, 0);
        ok = fxp_count(0) > 0u;
        fxp.ev[fxp.count].place = 5; fxp.ev[fxp.count].ty = 2; fxp.ev[fxp.count].x1 = 0x80u | 40u;   /* (an ATTRITION one) */
        fxp.count++;
        frames(400);
        press(B_REC); frames(32);
        ok &= !xy_print;
        frames(400);
        press(B_OCTDN); frames(32);
        ok &= fxp_count(0) == 0u && atp_count(0) == 1u && song.octave == 0 && pad_open();
        seq_stop(); song.playing = 0;
        bad += check("REC again: print off; OCT- on the pad: its prints cleared, ATTRITION's kept (the octave untouched)", ok);
    }
    {
        static project_store_t st;
        project_t *q = &proj_scratch;
        fxp_store_t keep;
        go_home(); frames(50);
        ui_power_on();
        memset(&fxp, 0, sizeof fxp);
        fxp.count = 3;
        for (i = 0; i < 3u; i++) {
            fxp.ev[i].place = (uint8_t)(i * 4u);
            fxp.ev[i].ty = 0x11u;
            fxp.ev[i].x1 = (uint8_t)(10 + i); fxp.ev[i].y1 = 20; fxp.ev[i].x2 = 30; fxp.ev[i].y2 = (uint8_t)(40 + i);
        }
        keep = fxp;
        bank_take(bank.cur);
        fxp.count = 0;
        bank_give(bank.cur);
        ok = !memcmp(&fxp, &keep, sizeof keep);
        bad += check("the prints are the pattern's: stored with it in the bank and back", ok);
        project_capture(q);
        ok = proj_pack(&st, q);
        fxp.count = 0;
        ok &= proj_unpack(q, st.raw, PROJ_STORE_SIZE) && q->fxp.count == 3u && !memcmp(q->fxp.ev, keep.ev, 3u * sizeof keep.ev[0]);
        ok &= project_restore_runtime(q) == 0 && !memcmp(&fxp, &keep, sizeof keep);
        fxp.rsv = FXP_FEEDS;                           /* (per FEED: 0.10, the FEED in ty's bits 1..3; FEED 5, PERC, too) */
        fxp.ev[1].ty = (uint8_t)(0x11u | PERC_TRK << 1);
        keep = fxp;
        project_capture(q);
        ok &= proj_pack(&st, q);
        fxp.count = 0; fxp.rsv = 0;
        ok &= proj_unpack(q, st.raw, PROJ_STORE_SIZE) && project_restore_runtime(q) == 0 && !memcmp(&fxp, &keep, sizeof keep) &&
              FXP_FEED(fxp.ev[1]) == PERC_TRK && fxp_find(&fxp, PERC_TRK, 4u) == 1;
        bad += check("  .. and the project's: packed and loaded back, the mix's and the FEEDs' (FEED 5's too)", ok);
        {   /* 0.9.6–0.9.14's prints of the mix: on every FEED; printed into, each becomes four */
            fxp_store_t o;
            uint32_t k;
            memset(&o, 0, sizeof o);
            o.count = 2;
            o.ev[0].place = 3u; o.ev[0].ty = 0x01u; o.ev[0].x1 = 20;
            o.ev[1].place = 7u; o.ev[1].ty = 0x10u; o.ev[1].x2 = 99;
            ok = 1;
            for (k = 0; k < NTRK; k++)
                ok &= fxp_find(&o, k, 3u) == 0 && fxp_find(&o, k, 7u) == 1 && fxp_find(&o, k, 4u) < 0;
            fxp_feeds(&o);
            ok &= o.count == 2u * NTRK && (o.rsv & FXP_FEEDS);
            for (k = 0; k < NTRK; k++) {
                int32_t a = fxp_find(&o, k, 3u), c = fxp_find(&o, k, 7u);
                ok &= a >= 0 && c >= 0 && o.ev[a].x1 == 20 && o.ev[c].x2 == 99 && FXP_FEED(o.ev[a]) == k;
            }
            fxp_bound(&o);
            ok &= o.count == 2u * NTRK && (o.rsv & FXP_FEEDS);
            bad += check("  0.9.6–0.9.14's prints of the mix play on every FEED; printed into, one a FEED, kept as such", ok);
        }
    }

    /* 9 every axis at its ends */
    {
        static const int8_t E[8][4] = {{0, 64, 64, 64}, {127, 64, 64, 64}, {64, 127, 64, 64}, {64, 0, 64, 64},
                                       {64, 64, 127, 64}, {64, 64, 0, 64}, {64, 64, 64, 127}, {64, 64, 64, 0}};
        uint32_t wild = 0, quiet = 0;
        for (i = 0; i < 8u; i++) {
            two_feeds();
            trk[1].p[P_LEVEL] = trk[2].p[P_LEVEL] = 127;
            seq_start(); song.playing = 1;
            pad_set(E[i][0], E[i][1], E[i][2], E[i][3]);
            mix_run(100, 0, 0);
            r1 = mix_run(FS / CTL, &pk, 0);
            wild |= (pk >= 32767) << i;
            quiet |= (r1 < 30.0) << i;
            pad_off();
            seq_stop(); song.playing = 0;
        }
        snprintf(what, sizeof what, "every axis at both ends, two FEEDs at LEVEL 127: none runs away or goes silent (clipped 0x%X, "
                 "silent 0x%X)", wild, quiet);
        bad += check(what, !wild && !quiet);
    }

    /* 10 cost */
    if (!getenv("NOCOST")) {
        struct timespec a, b;
        double t0, t1;
        uint32_t k, j;
        ui_power_on();
        song.master_q12 = 4096;
        mix_run(64, 0, 0);
        for (k = 0; k < NTRK; k++) { trk[k].p[P_SUS] = 127; for (j = 0; j < 3u; j++) trk_note_on(&trk[k], 48 + 4 * j + k, 100); }
        seq_start(); song.playing = 1;
        mix_run(100, 0, 0);
        clock_gettime(CLOCK_MONOTONIC, &a); mix_run(2000, 0, 0); clock_gettime(CLOCK_MONOTONIC, &b);
        t0 = (b.tv_sec - a.tv_sec) * 1e9 + (b.tv_nsec - a.tv_nsec);
        pad_set(20, 110, 110, 110);
        mix_run(100, 0, 0);
        clock_gettime(CLOCK_MONOTONIC, &a); mix_run(2000, 0, 0); clock_gettime(CLOCK_MONOTONIC, &b);
        t1 = (b.tv_sec - a.tv_sec) * 1e9 + (b.tv_nsec - a.tv_nsec);
        snprintf(what, sizeof what, "cost: the four at once on FEED 2 %.0f%% on top of it (%.1f -> %.1f us a block)",
                 100.0 * (t1 - t0) / t0, t0 / 2000e3, t1 / 2000e3);
        bad += check(what, t1 < t0 * 1.6);
        pad_off();
        seq_stop(); song.playing = 0;
    }
    printf("fxpad: %s\n", bad ? "FAILED" : "all checks ok");
    return bad != 0;
}
