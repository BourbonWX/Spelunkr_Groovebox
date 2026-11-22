/* SPDX-License-Identifier: GPL-3.0-only
 * Phantom Cosmonaut M3: PHANTOM, the jam (firmware/src/phantom.c), through the real UI, sequencer, voices and mix
 * (ui_test.c's harness). M3 is done when PHANTOM never cuts the loop or takes a FEED voice, nothing it plays is
 * recorded, and its sound survives a save and load (spec section 8):
 *   1. power-on: the theremin (MORPH CARRIER near its sine, GLIDE, VIBRATO), outside the FEEDs
 *   2. a key on HOME plays PHANTOM, not a FEED; it sounds, no clipping, no DC
 *   3. legato: a key over a held one slides (no new attack, the pitch glides); let go, back to the held one; the last
 *      one let go releases it
 *   4. VIBRATO creeps in after its delay
 *   5. the loop: every FEED's voices busy to the budget, PHANTOM plays and takes none; the sequencer runs on
 *   6. never recorded: played over a running sequence (and its knobs turned with REC armed on a FEED), no FEED's steps,
 *      motion or bank change; with REC armed the keys play and record the FEED instead (Dungeon Tracker's live entry)
 *   7. focus: ALGORITHM past FEED 4 is PHANTOM; KNOB 1..4, PRESETS, EDIT act on it; back to FEED 4
 *   8. saved with the project, back after a load; an older project keeps the PHANTOM playing
 *   build/host/phantom_test */
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"

static int32_t L[CTL], R[CTL], D[CTL], V[CTL];
static double ph_run(uint32_t blocks, int32_t *pk, double *dc)
{
    double s = 0, m = 0;
    uint32_t b, i, n = 0;
    *pk = 0;
    for (b = 0; b < blocks; b++) {
        memset(L, 0, sizeof L); memset(R, 0, sizeof R); memset(D, 0, sizeof D); memset(V, 0, sizeof V);
        phantom_render(L, R, D, V, CTL);
        for (i = 0; i < CTL; i++, n++) {
            int32_t a = L[i] < 0 ? -L[i] : L[i];
            *pk = a > *pk ? a : *pk;
            s += (double)L[i] * L[i];
            m += L[i];
        }
    }
    if (dc) *dc = n ? fabs(m / n) : 0;
    return n ? sqrt(s / n) : 0;
}
static uint32_t feed_gates(void)
{
    uint32_t p, i, n = 0;
    for (p = 0; p < NTRK; p++)
        for (i = 0; i < NVOICE; i++)
            n += trk[p].v[i].gate != 0;
    return n;
}
static void keys(uint32_t k, int down)
{
    if (down) key_down(white(k)); else key_up(white(k));
    events_block(CTL);
    frame();
}

static void focus_phantom(void)                  /* ALGORITHM past FEED 4: the keys are PHANTOM's (Ed, Nov 2026: only then) */
{
    track_select(3);
    turn(EN_ALGO, 1);
    frame();
}

int main(void)
{
    int bad = 0, ok;
    int32_t pk;
    double r, dc;
    ui_power_on();

    /* 1 */
    bad += check("power-on: PHANTOM the theremin (MORPH CARRIER, SCAN near the sine, GLIDE, VIBRATO), outside the FEEDs",
                 ph_trk.engine == ENGI_MORPH && ph_trk.p[P_E0] == 0 && ph_trk.p[P_E1] < 16 && ph_trk.p[P_GLIDE] > 30 &&
                 ph_trk.p[P_LD_PIT] > 20 && (&ph_trk < trk || &ph_trk >= trk + NTRK));

    /* 2 */
    go_home(); frames(3);
    focus_phantom();
    keys(4, 1);
    ok = ph.gate && ph_trk.v[0].active && !feed_gates() && ph.n == 1u;
    r = ph_run(FS / 2 / CTL, &pk, &dc);
    {
        char what[160];
        snprintf(what, sizeof what, "a key on HOME plays PHANTOM, no FEED: it sounds (rms %.0f, peak %d), no clipping, no DC (%.1f)",
                 r, pk, dc);
        bad += check(what, ok && r > 1000.0 && pk < 32000 && dc < r * 0.05);
    }

    /* 3 legato */
    {
        int32_t from = ph.cur, env0, mid;
        keys(9, 1);                                   /* a higher key over it */
        env0 = ph.env;
        ph_run(4, &pk, 0);
        mid = ph.cur;
        ok = ph.gate && ph.env >= env0 && mid != from && mid != ph.tgt;   /* no new attack; on its way */
        ph_run(FS / CTL, &pk, 0);
        ok &= ph.cur == ph.tgt && ph.tgt > from;
        bad += check("legato: a key held over another slides there (no new attack, a glide, not a jump)", ok);
        keys(9, 0);
        ph_run(FS / CTL, &pk, 0);
        ok = ph.gate && ph.cur == from;
        bad += check("  that one let go: back to the key still held, still sounding", ok);
        keys(4, 0);
        ok = !ph.gate;
        ph_run(FS * 2 / CTL, &pk, 0);
        ok &= !ph_trk.v[0].active && ph.env == 0;
        bad += check("  the last one let go: it releases and goes quiet", ok);
    }

    /* 4 vibrato */
    {
        uint32_t b;
        int32_t lo0 = 0x7FFFFFFF, hi0 = 0, lo1 = 0x7FFFFFFF, hi1 = 0;
        keys(4, 1);
        for (b = 0; b < 200u; b++) {                  /* the first 0.15 s: steady */
            ph_run(1, &pk, 0);
            lo0 = ph_trk.v[0].ph[0] ? (int32_t)pitch_inc(0) : lo0;
        }
        (void)lo0; (void)hi0;
        for (b = 0; b < 1500u; b++)                   /* past its delay and rise */
            ph_run(1, &pk, 0);
        for (b = 0; b < 400u; b++) {                  /* the pitch the voice reads, around the note */
            int32_t depth = ph_trk.p[P_LD_PIT] * 20, fade = 32767;
            int32_t v = (int32_t)(((int64_t)sine_i(ph.vph + PH_VIB_INC) * depth >> 15) * fade >> 15);
            lo1 = v < lo1 ? v : lo1; hi1 = v > hi1 ? v : hi1;
            ph_run(1, &pk, 0);
        }
        bad += check("VIBRATO: none at first (its delay), then the pitch swings around the note",
                     ph.held > PH_VIB_DELAY + PH_VIB_RISE && hi1 - lo1 > 400);
        keys(4, 0);
        ph_run(FS * 2 / CTL, &pk, 0);
    }

    /* 5 the loop */
    {
        uint32_t p, i, busy0, kills0, idx0;
        static const uint8_t N[8] = {48, 50, 52, 53, 55, 57, 59, 60};
        static const uint8_t KN[8] = {36, 38, 42, 45, 36, 38, 42, 45};   /* (a kit: a hit on each lane; PERC: its two) */
        for (p = 0; p < NPART; p++) {                 /* every FEED's voices to the budget */
            trk[p].p[P_SUS] = 127;
            for (i = 0; i < 8u; i++) trk_note_on(&trk[p], trk_is_kit(p) ? KN[i] : N[i], 100);
        }
        for (i = 0; i < 4u; i++) events_block(CTL);
        busy0 = voices_busy();
        kills0 = voice_kills;
        keys(6, 1);
        ph_run(8, &pk, 0);
        ok = voices_busy() == busy0 && busy0 + 3u >= NBUDGET && voice_kills == kills0 && ph.gate && ph_trk.v[0].active;
        {
            char what[160];
            snprintf(what, sizeof what, "the FEEDs' voices near the budget (%u of %u: the kit's lanes), PHANTOM plays and takes none (%u taken)",
                     busy0, (unsigned)NBUDGET, voice_kills - kills0);
            bad += check(what, ok);
        }
        keys(6, 0);
        for (p = 0; p < NPART; p++)
            for (i = 0; i < 128u; i++) trk_note_off(&trk[p], i);
        for (i = 0; i < 8u; i++) {                    /* a sequence playing on FEED 2, PHANTOM played over it */
            step_clear(&trk[1].step[i]);
            trk[1].step[i].time = ST_NOTE; trk[1].step[i].n = 1; trk[1].step[i].note[0] = 60; trk[1].step[i].vel = 100;
        }
        trk[1].p[P_SLEN] = 8;
        seq_start(); song.playing = 1;
        for (i = 0; i < 40u; i++) seq_tick(&trk[1], CTL);
        idx0 = trk[1].seq_idx;
        keys(6, 1); keys(9, 1); keys(9, 0);
        for (i = 0; i < 4000u; i++) seq_tick(&trk[1], CTL);
        ok = trk[1].seq_idx != idx0 && song.playing && seq_steps(&trk[1])[0].note[0] == 60;
        keys(6, 0);
        song.playing = 0; seq_stop();
        bad += check("  the loop runs on under the jam (the sequence plays, untouched)", ok);
    }

    /* 6 never recorded */
    {
        static track_t before[NTRK];
        uint32_t m0, k;
        ui_power_on();
        go_home(); frames(3);
        focus_phantom();
        for (k = 0; k < NTRK; k++) before[k] = trk[k];
        m0 = motion.count;
        seq_start(); song.playing = 1;
        for (k = 0; k < 6u; k++) {                    /* a jam over the running loop */
            keys(k, 1); frames(5); keys(k, 0); frames(5);
        }
        ok = motion.count == m0;
        for (k = 0; k < NTRK; k++)
            ok &= !memcmp(before[k].step, trk[k].step, sizeof trk[k].step);
        bad += check("never recorded: a jam over the running sequence changes no FEED's steps, no motion", ok);
        song.rec = (uint8_t)(1u << 3);                /* REC armed on FEED 4, PHANTOM focused: its knobs turned */
        ok = ph_focus;
        turn(EN_K1, 6); frame();
        turn(EN_K3, 6); frame();
        ok &= motion.count == m0;
        bad += check("  REC armed on a FEED: PHANTOM's knobs turned record nothing", ok);
        turn(EN_ALGO, -1); frame();
        keys(2, 1);
        ok = !ph.gate && feed_gates() >= 1u;
        keys(2, 0);
        bad += check("  REC armed: the keys play (and record) the FEED instead, Dungeon Tracker's live entry", ok);
        song.playing = 0; seq_stop(); song.rec = 0;
    }

    /* 7 focus */
    {
        int16_t s4, scan0, sig0, g0;
        ui_power_on();
        go_home(); frames(3);
        track_select(3);
        s4 = trk[3].p[P_E1];
        turn(EN_ALGO, 1); frame();
        ok = ph_focus && ui.home && song.sel == 3u;
        scan0 = ph_trk.p[P_E1];
        turn(EN_K1, 10); frame();
        ok &= ph_trk.p[P_E1] != scan0 && trk[3].p[P_E1] == s4;
        sig0 = ph_trk.p[P_E0];
        turn(EN_PRESET, 1); frame();
        ok &= ph_trk.p[P_E0] == sig0 + 1;
        bad += check("focus: ALGORITHM past FEED 4 is PHANTOM; KNOB 1 its SCAN (FEED 4's untouched); PRESETS its SIGNAL", ok);
        press(B_EDIT); frame();
        g0 = ph_trk.p[P_GLIDE];
        turn(EN_K1, 5); frame();
        ok = ph_page == 1u && ph_trk.p[P_GLIDE] != g0 && ui.home;
        bad += check("  EDIT: its other four, KNOB 1 GLIDE (HOME stays)", ok);
        turn(EN_ALGO, -1); frame();
        bad += check("  ALGORITHM back: FEED 4", !ph_focus && song.sel == 3u);
        turn(EN_ALGO, 1); frame();
        go_title("EDIT 1"); frame();
        bad += check("  a page opened: the FEED's again (PHANTOM lives on HOME)", !ph_focus);
    }

    /* 8 saved */
    {
        project_t q, q2;
        project_store_t st;
        int16_t want[6];
        ui_power_on();
        ph_signal(6);
        ph_trk.p[P_E1] = 90; ph_trk.p[P_E2] = 30; ph_trk.p[P_E3] = 64; ph_trk.p[P_E4] = 20;
        ph_trk.p[P_GLIDE] = 96; ph_trk.p[P_LD_PIT] = 72;
        want[0] = ph_trk.p[P_E1]; want[1] = ph_trk.p[P_E2]; want[2] = ph_trk.p[P_E3]; want[3] = ph_trk.p[P_E4];
        want[4] = ph_trk.p[P_GLIDE]; want[5] = ph_trk.p[P_LD_PIT];
        project_capture(&q);
        ok = proj_pack(&st, &q) && proj_import(&q2, &st, sizeof st);
        phantom_init();
        if (ok) ok = !project_restore_runtime(&q2);
        ok &= ph_trk.p[P_E0] == 6 && ph_trk.p[P_E1] == want[0] && ph_trk.p[P_E2] == want[1] && ph_trk.p[P_E3] == want[2] &&
              ph_trk.p[P_E4] == want[3] && ph_trk.p[P_GLIDE] == want[4] && ph_trk.p[P_LD_PIT] == want[5];
        bad += check("saved with the project: its SIGNAL, SCAN WARP FOLD CLARITY, GLIDE, VIBRATO back after a load", ok);
        q.ph[0] = 0;                                  /* a project without one (its first byte's bit 7 clear) */
        ok = proj_pack(&st, &q);
        ph_trk.p[P_E1] = 11;
        ok &= proj_import(&q2, &st, sizeof st) && !project_restore_runtime(&q2) && ph_trk.p[P_E1] == 11;
        bad += check("  a project without one: the PHANTOM playing stays as it is", ok);
    }

    /* Phantom Tracker: the speech gone. PRESETS stops at SIG 16; a 0.9.4 project's speaking PHANTOM loads as MORPH (SIG
     * 1, its GLIDE VIBRATO kept); a MORPH PHANTOM saved with SUB high still loads as one */
    {
        project_t q, q2;
        project_store_t st;
        uint8_t b[5];
        ui_power_on();
        go_home(); frames(3);
        focus_phantom();
        ph_signal(SIG_N - 1);
        turn(EN_PRESET, 1);
        frames(20);
        ok = ph_trk.eng_req == ENGI_MORPH && ph_trk.p[P_E0] == SIG_N - 1 && ph_voice_at() == SIG_N - 1u;
        b[0] = 0x80u | 0x70u | 3u;                       /* (0.9.4: speaking, MAYDAY's phrase) */
        b[1] = 0x12; b[2] = 0x34; b[3] = 0x56; b[4] = (uint8_t)(5u | 6u << 4);
        ok &= phantom_unpack(b) && ph_trk.eng_req == ENGI_MORPH && ph_trk.p[P_E0] == 0 && ph_trk.p[P_GLIDE] == 40 &&
              ph_trk.p[P_LD_PIT] == 48;
        ph_signal(SIG_N - 1);
        ph_trk.p[P_E7] = 120;
        project_capture(&q);
        ok &= proj_pack(&st, &q) && proj_import(&q2, &st, sizeof st);
        phantom_init();
        ok &= !project_restore_runtime(&q2) && ph_trk.eng_req == ENGI_MORPH && ph_trk.p[P_E0] == SIG_N - 1 &&
              ph_trk.p[P_E7] == 108;
        bad += check("no speech (Phantom Tracker): PRESETS stops at SIG 16 DARK SIDE; a 0.9.4 speaking PHANTOM loads as "
                     "MORPH, GLIDE VIBRATO kept; a MORPH PHANTOM saved (SUB high) still loads as one", ok);
    }

    /* 0.9.15: LEVEL (Ed: "the phantom theremin track is way too loud ... it doesn't have a volume control"): EDIT's page,
     * KNOB 4 (SIG's place: PRESETS still steps SIG), 90 by default, heard, saved with the project; a project before
     * loads the default */
    {
        project_t q, q2;
        project_store_t st;
        int16_t *vp;
        const param_desc_t *d;
        double r0, r1;
        char what[200];
        ui_power_on();
        go_home(); frames(3);
        focus_phantom();
        ok = ph_trk.p[P_LEVEL] == 86;
        press(B_EDIT); frames(4);
        d = ph_param(3, &vp);
        ok &= ph_page == 1u && str_eq(d->label, "LEVEL") && vp == &ph_trk.p[P_LEVEL] && msg_is("GLIDE VIBRATO SUB LEVEL");
        keys(4, 1);
        ph_run(FS / 4 / CTL, &pk, 0);
        r0 = ph_run(FS / 4 / CTL, &pk, 0);
        turn(EN_K4, -20); frames(4);
        ph_run(FS / 8 / CTL, &pk, 0);
        r1 = ph_run(FS / 4 / CTL, &pk, 0);
        keys(4, 0);
        ok &= ph_trk.p[P_LEVEL] == 66;
        project_capture(&q);
        ok &= proj_pack(&st, &q) && proj_import(&q2, &st, sizeof st);
        phantom_init();
        ok &= ph_trk.p[P_LEVEL] == 86 && !project_restore_runtime(&q2) && ph_trk.p[P_LEVEL] == 66;
        q.ph_lvl = 0;                                 /* (a project before 0.9.15: the byte 0) */
        ok &= proj_pack(&st, &q) && proj_import(&q2, &st, sizeof st) && q2.ph_lvl == 0u;
        ok &= !project_restore_runtime(&q2) && ph_trk.p[P_LEVEL] == 86;
        snprintf(what, sizeof what, "LEVEL: EDIT's KNOB 4, 86 by default; 20 down 10 dB quieter (rms %.0f -> %.0f); saved in the "
                 "project, a project before loads 86", r0, r1);
        bad += check(what, ok && r1 < r0 * 0.4 && r1 > r0 * 0.25);
    }

    printf("phantom: %s\n", bad ? "FAILED" : "all checks ok");
    return bad != 0;
}
