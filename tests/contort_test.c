/* SPDX-License-Identifier: GPL-3.0-only
 * Phantom Cosmonaut M2: the contort knobs on a MORPH FEED through the real UI, sequencer and motion sources
 * (ui_test.c's harness). M2 is done when "the knobs record (INGEST) and lock per step" (spec section 8):
 *   1. power-on: FEED 2..4 on GRANULAR, FOLDER, FM; HOME's KNOB 1..4 are SCAN WARP FOLD CLARITY
 *   2. INGEST: REC armed and playing, KNOB 1..4 turned on HOME record SCAN WARP FOLD CLARITY into the FEED's motion,
 *      played back after a stop (the base kept); not armed, a turn only moves the sound
 *   3. a lock per step: SCAN locked on step 3 sounds on step 3 only; it is saved with the song and back after a load
 *   4. PRESETS on a MORPH FEED steps through the SIGNALs (SIG 01 .. 16 in order)
 *   build/host/contort_test */
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"

static int ev_of(const track_t *t, uint32_t id)
{
    uint32_t i, k = trk_index(t);
    for (i = 0; i < motion.count; i++)
        if ((motion.event[i].place >> 6) == k && motion.event[i].param == id)
            return 1;
    return 0;
}
static void run_to_step(track_t *t, uint32_t s)
{
    uint32_t guard = 0;
    while (t->seq_idx != s && guard++ < 200000u)
        seq_tick(t, CTL);
}

int main(void)
{
    int bad = 0, ok;
    uint32_t k;
    track_t *t;
    ui_power_on();
    bad += check("power-on (Spelunkr): FEED 2 SAMPLE, 3 GENESIS, 4 SAMPLE, FEED 1 the kit",
                 trk[1].eng_req == ENGI_SAMPLE && trk[2].eng_req == ENGI_GENESIS && trk[3].eng_req == ENGI_SAMPLE &&
                 ENG_KIT(trk[0].eng_req % NENGINES));
    track_select(3);
    t = TSEL;
    set_engine_of(t, ENGI_PFM);                          /* (Spelunkr: FEED 4 on FM, the SIGNAL engine these checks are on) */
    apply_preset_to(t, 0);
    t->engine = t->eng_req;
    frames(3);
    go_home();
    frames(3);
    ok = 1;
    for (k = 0; k < 4u; k++)
        ok &= ENGINES[t->eng_req % NENGINES]->knob[k] == P_E1 + k;
    bad += check("HOME on FEED 4: KNOB 1..4 are SCAN WARP FOLD CLARITY", ok && ui.home);

    /* 2 INGEST */
    {
        int16_t base[4];
        for (k = 0; k < 4u; k++) base[k] = t->p[P_E1 + k];
        song.rec = (uint8_t)(1u << 3);
        seq_start(); song.playing = 1;
        seq_tick(t, CTL);
        turn(EN_K1, 6); frame();
        turn(EN_K2, 4); frame();
        turn(EN_K3, 5); frame();
        turn(EN_K4, 3); frame();
        ok = ev_of(t, P_E1) && ev_of(t, P_E2) && ev_of(t, P_E3) && ev_of(t, P_E4);
        bad += check("INGEST: REC armed and playing, KNOB 1..4 on HOME record SCAN WARP FOLD CLARITY", ok);
        song.playing = 0; seq_stop(); song.rec = 0;
        ok = 1;
        for (k = 0; k < 4u; k++) ok &= t->p[P_E1 + k] == base[k] && motion_base_value(t, P_E1 + k) == base[k];
        bad += check("  stopped: the sound's own values back (the base kept under the motion)", ok);
        seq_start(); song.playing = 1;
        seq_tick(t, CTL);
        ok = t->p[P_E1] != base[0] || t->p[P_E2] != base[1] || t->p[P_E3] != base[2] || t->p[P_E4] != base[3];
        bad += check("  played again: the recorded motion moves them", ok);
        song.playing = 0; seq_stop();
        motion_clear(t);
        turn(EN_K1, 4); frame();
        bad += check("  not armed: a turn moves SCAN and records nothing", !motion_count(t) && t->p[P_E1] != base[0]);
    }

    /* 3 a lock per step */
    {
        int16_t v, base;
        project_t q, q2;
        project_store_t st;
        motion_clear(t);
        base = t->p[P_E1];
        for (k = 0; k < 8u; k++) {                   /* eight steps of notes */
            step_clear(&t->step[k]);
            t->step[k].time = ST_NOTE; t->step[k].n = 1; t->step[k].note[0] = 60; t->step[k].vel = 100;
        }
        t->p[P_SLEN] = 8;
        motion_set_lock(t, 3, P_E1, 120);
        motion_set_enabled(t, 1);
        seq_start(); song.playing = 1;
        run_to_step(t, 2);
        ok = t->p[P_E1] == base;
        run_to_step(t, 3);
        ok &= t->p[P_E1] == 120;
        run_to_step(t, 4);
        ok &= t->p[P_E1] == base;
        song.playing = 0; seq_stop();
        bad += check("a lock per step: SCAN 120 on step 3 only (the base on 2 and 4)", ok);
        project_capture(&q);
        ok = proj_pack(&st, &q) && proj_import(&q2, &st, sizeof st);
        motion_clear(t);
        if (ok) ok = !project_restore_runtime(&q2);
        bad += check("  saved with the song and back after a load", ok && motion_lock_get(&trk[3], 3, P_E1, &v) && v == 120);
    }

    /* 4 PRESETS: the SIGNALs */
    {
        ui_power_on();
        track_select(3);
        set_engine_of(TSEL, ENGI_MORPH);
        apply_preset_to(TSEL, 0);
        ok = TSEL->p[P_E0] == 0;
        for (k = 1; k < SIG_N; k++) {
            apply_preset_to(TSEL, k);
            ok &= TSEL->p[P_E0] == (int16_t)k && str_eq(ENG_MORPH.presets[k].name, SIG_NAME[k]);
        }
        bad += check("PRESETS on a MORPH FEED: the 16 sounds are SIG 01 .. 16 in order, named as the signals", ok);
    }
    printf("contort: %s\n", bad ? "FAILED" : "all checks ok");
    return bad != 0;
}
