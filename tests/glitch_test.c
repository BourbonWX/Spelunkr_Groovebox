/* SPDX-License-Identifier: GPL-3.0-only
 * The FX layer (perform.c: the glitch effects and ATTRITION) on the FEEDs it targets: Phantom Tracker 0.9.15 (Ed: "the
 * vibe of a track falls apart when the drums are randomly hopping and the bass and keys are hopping differently")
 * targets every FEED, a black key held leaving its FEED out (0.9.6–0.9.14: the selected FEED, black keys adding), through
 * the real UI, sequencer and mix (ui_test.c's harness):
 *   1. every FEED: CRUSH changes FEED 3 and FEED 2 alike, whichever is selected
 *   2. a black key held leaves its FEED out: black key 3 and FEED 3 is dry (not crushed, not muted)
 *   3. a REPEAT with black key 3 held: FEED 3 plays on live; let go, both back to the clean mix
 *   4. REPEAT 1/4 (the mono loop's 743 ms): there at 120 BPM, dark at 72
 *   5. a USER key holds its two (USER 1: 1/16 + HPF)
 *   6. JUMP skips the targeted FEEDs' steps only (a FEED left out never jumps)
 *   7. ATTRITION prints keep the FEEDs they were printed on (0.9.4's: all four)
 *   8. REVERB / ECHO (the throws) raise the targeted FEEDs' sends only
 *   9. 0.10.2: the black keys are switches (a press DRY on, a press again off)
 *  10. 0.10.3: the echo's return at full scale never wraps; the line's ceiling a knee
 *   build/host/glitch_test */
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"

static int32_t O[2 * CTL];
static float W[3][2 * 44100];
/* n blocks of the mix's left channel into w */
static void grab(float *w, uint32_t blocks)
{
    uint32_t b, i, n = 0;
    for (b = 0; b < blocks; b++) {
        memset(O, 0, sizeof O);
        mix_block(O, CTL);
        for (i = 0; i < CTL; i++)
            w[n++] = (float)O[2 * i];
    }
}
static void run(uint32_t blocks)
{
    uint32_t b;
    for (b = 0; b < blocks; b++) {
        memset(O, 0, sizeof O);
        mix_block(O, CTL);
    }
}
static double rms(const float *a, uint32_t n)
{
    double s = 0;
    uint32_t i;
    for (i = 0; i < n; i++) s += (double)a[i] * a[i];
    return sqrt(s / (n ? n : 1));
}
/* brightness: the first difference's share of the energy (CRUSH's steps raise it) */
static double bright(const float *a, uint32_t n)
{
    double s = 0, d = 0;
    uint32_t i;
    for (i = 1; i < n; i++) { s += (double)a[i] * a[i]; d += (double)(a[i] - a[i - 1]) * (a[i] - a[i - 1]); }
    return s > 0 ? d / s : 0;
}
static double diff(const float *a, const float *b, uint32_t n)
{
    double s = 0;
    uint32_t i;
    for (i = 0; i < n; i++) s += (double)(a[i] - b[i]) * (a[i] - b[i]);
    return sqrt(s / (n ? n : 1));
}
/* FEED 2 and FEED 3 each a steady note (MORPH CARRIER), the others silent, no sends; FEED 2 selected */
static void two(int lv2, int lv3)
{
    uint32_t k;
    ui_power_on();
    song.master_q12 = 4096;
    for (k = 0; k < NTRK; k++) {
        trk[k].p[P_REV] = trk[k].p[P_DLY] = trk[k].p[P_CHOR] = 0;
        trk[k].p[P_LEVEL] = 0;
    }
    for (k = 1; k < 3u; k++) {
        set_engine_of(&trk[k], ENGI_MORPH);
        apply_preset_to(&trk[k], 0u);
        trk[k].engine = trk[k].eng_req;
        trk[k].p[P_SUS] = 127;
        trk[k].p[P_E1] = trk[k].p[P_E2] = 0;           /* (no WARP FOLD: a plain tone, the envelope's SCAN off) */
        trk[k].p[P_REV] = trk[k].p[P_DLY] = trk[k].p[P_CHOR] = 0;   /* (after the preset: its sends off) */
    }
    trk[1].p[P_LEVEL] = (int16_t)lv2;
    trk[2].p[P_LEVEL] = (int16_t)lv3;
    track_select(1);
    run(64);
    trk_note_on(&trk[1], 64, 110);
    trk_note_on(&trk[2], 52, 110);
    run(200);
}

int main(void)
{
    int bad = 0, ok;
    uint32_t n = 8192u, nb = 8192u / CTL;
    double d, r;
    char what[220];

    /* 1 (the oscillators start where they will, so two runs are compared by their brightness, not sample by sample) */
    {
        double b0, b1, b2, b3;
        two(0, 100);                                   /* FEED 3 alone sounding */
        grab(W[0], nb);
        b0 = bright(W[0], n);
        r = rms(W[0], n);
        two(0, 100);
        perf_k[1] = 80;                                /* CRUSH (KNOB 2 with FX), FEED 2 selected */
        run(40);
        grab(W[1], nb);
        b1 = bright(W[1], n);
        d = rms(W[1], n);
        ok = pf_aim == TRK_ALL && pf.tgg[2] == 32768;
        perf_k[1] = 0;
        two(100, 0);                                   /* FEED 2 alone */
        grab(W[2], nb);
        b2 = bright(W[2], n);
        two(100, 0);
        perf_k[1] = 80;
        run(40);
        grab(W[1], nb);
        b3 = bright(W[1], n);
        perf_k[1] = 0;
        snprintf(what, sizeof what, "CRUSH on every FEED, FEED 2 selected: FEED 3 alone crushed (brightness %.4f -> %.4f, rms %.0f / %.0f), "
                 "FEED 2 too (%.4f -> %.4f)", b0, b1, r, d, b2, b3);
        bad += check(what, ok && b1 > b0 * 3.0 && d > r * 0.5 && b3 > b2 * 3.0);
    }

    /* 2 */
    two(0, 100);
    perf_held = PF_BIT(PF_M1 + 2u);                    /* black key 3 held: FEED 3 left out */
    perf_k[1] = 80;
    run(40);
    grab(W[1], nb);
    ok = pf_aim == (TRK_ALL & ~4u) && pf_tgt == (TRK_ALL & ~4u) && pf.mg[2] == 32768 && pf.tgg[2] == 0;   /* (FEED 3 off the bus, not muted) */
    perf_k[1] = 0;
    perf_held = 0;
    snprintf(what, sizeof what, "black key 3 held: FEED 3 left out, dry under CRUSH (brightness %.4f / %.4f), not muted (%.0f of %.0f)",
             bright(W[0], n), bright(W[1], n), rms(W[1], n), r);
    bad += check(what, ok && fabs(bright(W[1], n) - bright(W[0], n)) < bright(W[0], n) * 0.1 && fabs(rms(W[1], n) - r) < r * 0.1);

    /* 3 */
    {
        float *a = W[0], *b = W[1];
        double dl, back;
        two(0, 100);
        seq_start(); song.playing = 1;
        run(40);
        grab(a, nb);                                   /* FEED 3 alone, playing */
        two(0, 100);
        seq_start(); song.playing = 1;
        run(40);
        perf_held = PF_BIT(PF_M1 + 2u);                /* black key 3: FEED 3 left out */
        perf_press(PF_R16, 1);                         /* REPEAT 1/16 on the others (FEED 2 silent): FEED 3 plays on live */
        run(40);
        grab(b, nb);
        dl = fabs(rms(a, n) - rms(b, n)) + 1e9 * (pf.tgg[2] != 0 || pf.tgg[1] != 32768);   /* (FEED 3 not on the bus) */
        perf_press(PF_R16, 0);
        perf_held = 0;
        run(400);
        back = (double)(pf.tgg[1] | pf.tgg[2]) + (double)pf_tgt + (double)pf.busy;
        snprintf(what, sizeof what, "REPEAT 1/16, black key 3 held: FEED 3 plays on live, off the glitch bus (rms %.0f apart of %.0f); "
                 "let go: nothing left on the glitch bus", dl, rms(a, n));
        bad += check(what, dl < rms(a, n) * 0.1 && back == 0.0);
        seq_stop(); song.playing = 0;
    }

    /* 4 */
    ui_power_on();
    song.g[G_BPM] = 120;
    ok = (perf_avail() >> PF_R4) & 1u;
    song.g[G_BPM] = 72;
    ok &= !((perf_avail() >> PF_R4) & 1u) && ((perf_avail() >> PF_R8) & 1u);
    song.g[G_BPM] = 120;
    ok &= perf_len(PF_R4) == beat_samples() && perf_len(PF_R4) <= PB_MAX && PB_FRAMES == 16384u;
    bad += check("REPEAT 1/4: a beat long, there at 120 BPM (the mono loop's 743 ms), dark at 72 (1/8 still there)", ok);
    {   /* it repeats: one beat, then the next the same */
        float *a = W[0];
        double dd;
        two(100, 0);
        seq_start(); song.playing = 1;
        run(40);
        perf_press(PF_R4, 1);
        while (!(perf_act & PF_BIT(PF_R4)))
            run(1);
        run(beat_samples() / CTL + 8u);                /* (its beat recorded) */
        grab(a, 2u * beat_samples() / CTL);
        dd = diff(a, a + beat_samples(), beat_samples() - CTL);
        perf_press(PF_R4, 0);
        run(100);
        seq_stop(); song.playing = 0;
        snprintf(what, sizeof what, "  REPEAT 1/4 held: each beat the one before (difference %.1f of %.0f)", dd, rms(a, beat_samples()));
        bad += check(what, dd < rms(a, beat_samples()) * 0.05);
    }

    /* 5 */
    two(100, 0);
    seq_start(); song.playing = 1;
    run(10);
    perf_press(PF_U1, 1);
    while (!(perf_act & PF_BIT(PF_R16)))
        run(1);
    ok = (perf_act & PF_BIT(PF_HPF)) && (perf_act & PF_BIT(PF_R16)) && !(perf_act & PF_BIT(PF_U1));
    perf_press(PF_U1, 0);
    run(4);
    ok &= !(perf_act & (PF_BIT(PF_HPF) | PF_BIT(PF_R16)));
    seq_stop(); song.playing = 0;
    bad += check("a USER key holds its two: USER 1 runs REPEAT 1/16 and HPF; let go, both off", ok);

    /* 6 JUMP */
    {
        uint32_t k, pass, moved[2] = {0, 0};
        for (pass = 0; pass < 2u; pass++) {
            ui_power_on();
            for (k = 0; k < NTRK; k++) trk[k].p[P_SLEN] = 16;
            track_select(1);
            perf_held = pass ? 0u : PF_BIT(PF_M1 + 2u);   /* (pass 0: black key 3 held, FEED 3 left out) */
            run(2);
            mk_a[2] = 100;
            for (k = 0; k < 2000u; k++) {
                const track_t *t = &trk[2];
                uint32_t j = (mk_a[2] > 0 && ((pf_aim >> 2) & 1u)) ? seq_jump(t, 5u) : 5u;
                moved[pass] += j != 5u;
            }
            mk_a[2] = 0;
            perf_held = 0;
        }
        snprintf(what, sizeof what, "JUMP on the targeted FEEDs only: FEED 3 left out never jumps (%u of 2000), targeted it does (%u)",
                 moved[0], moved[1]);
        bad += check(what, moved[0] == 0u && moved[1] > 500u);
    }

    /* 7 ATTRITION prints */
    {
        fxp_store_t st;
        two(100, 100);
        memset(&st, 0, sizeof st);
        fxp.count = 0;
        song.playing = 1; song.rec = 1;               /* (no sequencer running: the steps by hand) */
        atp_live = 1;
        perf_held = PF_BIT(PF_M1 + 3u);                /* black key 4: FEED 4 left out */
        perf_k[1] = 60;
        perf_begin(CTL);
        atp_step(3, &fxp, 1);                          /* (printed on FEED 1's step 3) */
        ok = atp_count(0) == 1u && fxp.ev[0].ty == (TRK_ALL & ~8u) && (fxp.ev[0].x1 & 0x80u);
        perf_k[1] = 0; perf_held = 0; atp_live = 0; song.rec = 0;
        atp_step(3, &fxp, 1);                          /* (played: its FEEDs) */
        perf_begin(CTL);
        ok &= atp_on && atp_tgt == (TRK_ALL & ~8u) && pf_aim == (TRK_ALL & ~8u) && perf_k[1] > 0;
        atp_step(4, &fxp, 1);
        perf_begin(CTL);
        ok &= !atp_on && !atp_tgt && pf_aim == TRK_ALL;
        st.count = 1;
        st.ev[0] = fxp.ev[0];
        st.ev[0].ty = 0;                               /* (0.9.4's: no FEEDs: all four, the master's then) */
        atp_step(3, &st, 1);
        perf_begin(CTL);
        ok &= atp_tgt == TRK_ALL && pf_aim == TRK_ALL;
        atp_stop();
        song.playing = 0;
        bad += check("ATTRITION prints keep the FEEDs they were printed on (black key 4 held: all but FEED 4); "
                     "0.9.4's play on all four", ok);
    }

    /* 8 the throws */
    {
        int32_t s0[CTL], s1[CTL];
        uint32_t i;
        two(100, 100);
        perf_held = PF_BIT(PF_M1 + 2u);                /* (black key 3: FEED 3 left out) */
        perf_press(PF_RTHR, 1);
        run(200);
        ok = pf.tgg[1] == 32768 && pf.tgg[2] == 0;
        memset(s0, 0, sizeof s0); memset(s1, 0, sizeof s1);
        for (i = 0; i < CTL; i++) s0[i] = 0;
        perf_throw(&trk[1], part_buf, s1, s0, 0);      /* (just: callable) */
        perf_press(PF_RTHR, 0);
        perf_held = 0;
        run(200);
        bad += check("REVERB (REV THROW) raises the targeted FEEDs' sends only (black key 3 held: FEED 2's share 1, FEED 3's 0)", ok);
    }
    /* 9 0.10.2: the black keys are switches: a press turns a FEED's DRY on, letting go keeps it, a press again off;
     * FX LATCH's other effects aside; a menu keeps them, FX + OCT- clears them */
    {
        perf_latched = 0; perf_held = 0;
        perf_press(PF_M1 + 2u, 1);
        perf_press(PF_M1 + 2u, 0);
        run(4);
        ok = perf_latched == PF_BIT(PF_M1 + 2u) && !perf_held && pf_aim == (TRK_ALL & ~4u);
        perf_press(PF_M1 + 2u, 1);
        perf_press(PF_M1 + 2u, 0);
        run(4);
        ok &= perf_latched == 0 && pf_aim == TRK_ALL;
        perf_press(PF_M1 + 0u, 1);
        perf_press(PF_R16, 1);                          /* (a white key: momentary as ever) */
        perf_press(PF_R16, 0);
        ok &= perf_latched == PF_BIT(PF_M1) && !(perf_held & PF_BIT(PF_R16));
        perf_latched = 0;
        bad += check("black keys are switches: a press DRY on (kept when let go), a press again off; white keys momentary", ok);
    }

    /* 10 0.10.3: the echo at full scale, DMIX 127: its return never wraps (0.10.2's x 4 overflowed past half scale: a
     * full-scale pop each time a loud echo came round), and the line rounds off under a full feedback, never a wall */
    {
        static int32_t zc[CTL], zd[CTL], zr[CTL], w[CTL];
        uint32_t j, dl, minw = 0x7FFFFFFF, top = 0;
        int16_t dm0 = song.g[G_DMIX], df0 = song.g[G_DFDBK];
        song.g[G_DMIX] = 127;
        song.g[G_DFDBK] = 127;
        dl = delay_samples();
        for (j = 0; j < DLY_LEN; j++)
            dly_buf[j] = 32767;
        memset(zc, 0, sizeof zc); memset(zd, 0, sizeof zd); memset(zr, 0, sizeof zr);
        fx_buses(zc, zd, zr, w, CTL);
        for (j = 0; j < CTL; j++)
            if ((uint32_t)w[j] < minw) minw = (uint32_t)w[j];
        for (j = 0; j < CTL; j++)
            zd[j] = 1 << 22;                            /* (a huge send into a full line) */
        fx_buses(zc, zd, zr, w, CTL);
        for (j = 0; j < DLY_LEN; j++)
            if (dly_buf[j] > (int16_t)top) top = (uint32_t)dly_buf[j];
        (void)dl;
        ok = (int32_t)minw > 100000 && top <= 32767u && top > 30000u;
        snprintf(what, sizeof what, "the echo at full scale, DMIX 127: its return %d (no wrap), the line's top %u (a knee)", (int32_t)minw, top);
        bad += check(what, ok);
        memset(dly_buf, 0, sizeof dly_buf);
        song.g[G_DMIX] = dm0;
        song.g[G_DFDBK] = df0;
    }

    printf("glitch: %s\n", bad ? "FAILED" : "all checks ok");
    return bad != 0;
}
