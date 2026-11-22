/* SPDX-License-Identifier: GPL-3.0-only
 * Phantom Tracker 0.10 through ui_test.c's harness:
 *   1. the per-step pan: a PAN lock on a step pans that step's hit, and the hit keeps its pan while it rings (the next
 *      step's lock elsewhere); no lock: the FEED centred, L = R sample for sample; SPREAD still spreads
 *   2. PERC: power-on GLITCH HOP's CLAP and SHAKER; each slot plays its source as the kit plays it; SOURCE's knobs; the
 *      roll's two lanes; kits only; two voices; saved and loaded
 *   3. B sides: on (A's sound an octave up), a note on both sides on their own voices, FLIP edits B, B's LEVEL and PAN,
 *      a MONO B over a POLY A, off rings out, no kit, saved and loaded (unflipped)
 *   4. 0.10.1's shortcuts: the FX column's list (commands, PAN, LEVEL, HOME's four, the rest); PERC's HOME knobs (1 KIT
 *      1 SOUND 2 KIT 2 SOUND); ALGORITHM stops at a B side (3 3B 4 ...), the MIXER's LEVEL / PAN there are B's;
 *      OCT+ / OCT- turn the strip's page on MIXER and HOME LEVELS (the selection and the octave stay)
 *   build/host/v010_test */
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"

static double el, er, es;                              /* the left / right / side (L - R) energy of the last run */
static int same;                                       /* L == R on every sample of the last run */
static void run_ms(uint32_t ms)
{
    int32_t o[2 * CTL];
    uint32_t b, i, n = ms * (FS / 1000u) / CTL;
    el = er = es = 0;
    same = 1;
    for (b = 0; b < n; b++) {
        mix_block(o, CTL);
        for (i = 0; i < CTL; i++) {
            el += (double)o[2 * i] * o[2 * i];
            er += (double)o[2 * i + 1] * o[2 * i + 1];
            es += (double)(o[2 * i] - o[2 * i + 1]) * (o[2 * i] - o[2 * i + 1]);
            same &= o[2 * i] == o[2 * i + 1];
        }
    }
}
static void only(uint32_t k)                            /* FEED k alone, dry */
{
    uint32_t j;
    for (j = 0; j < NTRK; j++) {
        trk[j].p[P_MUTE] = j != k;
        trk[j].p[P_REV] = trk[j].p[P_DLY] = trk[j].p[P_CHOR] = 0;
    }
    song.g[G_BPM] = 120;
    song.master_q12 = 4096;
}
static void beat(track_t *t)                            /* a kick on step 0, a snare on step 4, 16 steps */
{
    uint32_t s;
    for (s = 0; s < NSTEP; s++)
        step_clear(&t->step[s]);
    t->step[0].time = ST_NOTE; t->step[0].n = 1; t->step[0].note[0] = 36; t->step[0].vel = 110;
    t->step[4].time = ST_NOTE; t->step[4].n = 1; t->step[4].note[0] = 38; t->step[4].vel = 110;
    t->p[P_SLEN] = 16;
    t->p[P_PAN] = 0;
}
static voice_t *voice_of(track_t *t, uint32_t note)
{
    uint32_t i;
    for (i = 0; i < NVOICE; i++)
        if (t->v[i].active && t->v[i].note == note)
            return &t->v[i];
    return 0;
}

int main(void)
{
    int bad = 0, ok;
    track_t *t;

    /* 1 the per-step pan */
    ui_power_on();
    only(0);
    t = &trk[0];
    beat(t);
    motion_clear(t);
    seq_start(); song.playing = 1;
    run_ms(110);
    ok = el > 0 && same;
    song.playing = 0; seq_stop();
    bad += check("no lock: the kick centred, L = R on every sample", ok);

    ui_power_on();
    only(0);
    t = &trk[0];
    beat(t);
    motion_clear(t);
    motion_set_lock(t, 0, P_PAN, -64);
    motion_set_lock(t, 4, P_PAN, 63);
    motion_set_enabled(t, 1);
    seq_start(); song.playing = 1;
    run_ms(110);                                        /* (step 4 at 500 ms: 120 BPM, a 16th 125 ms) */
    bad += check("a PAN lock on step 0: its kick hard left", el > 100.0 * (er + 1.0));
    {
        voice_t *k;
        run_ms(400);
        k = voice_of(t, 36);
        ok = k && k->pfix && k->pan == -64;
        run_ms(40);                                     /* (into step 4) */
        k = voice_of(t, 36);
        ok &= !k || (k->pfix && k->pan == -64);
        k = voice_of(t, 38);
        ok &= k && k->pfix && k->pan == 63;
        bad += check("  step 4 locked right: its snare right, the kick ringing keeps its left", ok);
        run_ms(80);
        bad += check("  the snare's step heard right", er > 4.0 * el);
    }
    {   /* the UI: the GRID's FX column, KNOB 4 to PAN, KNOB 3 on row 3: a PAN lock on that step */
        int16_t v = 0;
        uint32_t n;
        song.playing = 0; seq_stop();
        motion_clear(&trk[0]);
        track_select(1); track_select(0);
        go_page(GR_ROLL); frames(2);
        for (n = 0; n < 12u; n++) turn(EN_K2, -1);
        for (n = 0; n < 12u && tk_col() != TC_FX; n++) turn(EN_K2, 1);
        for (n = 0; n < 64u && tk_fx_id(&trk[0]) != P_PAN; n++) turn(EN_K4, 1);
        ui.cursor = 3;
        turn(EN_K3, -6); frame();
        ok = tk_fx_id(&trk[0]) == P_PAN && motion_lock_get(&trk[0], 3, P_PAN, &v) && v < 0;
        bad += check("  the GRID: the FX column, KNOB 4 to PAN, KNOB 3 on a row: its PAN locked (left)", ok);
        motion_set_lock(t, 0, P_PAN, -64);
        motion_set_lock(t, 4, P_PAN, 63);
        motion_set_enabled(t, 1);
        seq_start(); song.playing = 1;
        run_ms(560);
    }
    run_ms(200);                                        /* (step 6 .. : no lock, PAN back to its base) */
    bad += check("  the steps after: PAN back to 0", t->p[P_PAN] == 0 && !(pan_held & 1u));
    song.playing = 0; seq_stop();

    /* SPREAD: a chord on FEED 2 spread wide still has a left and a right (its voices alternate) */
    {
        static const uint8_t C[4] = {48, 52, 55, 59};
        uint32_t i;
        double m0;
        ui_power_on();
        only(1);
        t = &trk[1];
        run_ms(2000);                                   /* (the last section's hits rung out) */
        t->p[P_VOICE] = V_POLY; t->p[P_SUS] = 127; t->p[P_SPRD] = 0; t->p[P_PAN] = 0;
        for (i = 0; i < 4u; i++) trk_note_on(t, C[i], 100);
        run_ms(300);
        ok = es < 1e-4 * (el + er);                  /* (a whisper of the last sections' reverb tail) */
        m0 = el + er;
        for (i = 0; i < 128u; i++) trk_note_off(t, i);
        run_ms(1500);
        memset(t->v, 0, sizeof t->v);
        t->p[P_SPRD] = 127;
        for (i = 0; i < 4u; i++) trk_note_on(t, C[i], 100);
        run_ms(300);
        ok &= es > 0.1 * (el + er) && el > 0 && er > 0 && el + er > 0.2 * m0 && el + er < 2.0 * m0;
        bad += check("SPREAD 127: a chord's voices left and right (SPRD 0: no side), its level kept", ok);
    }

    /* 2 PERC */
    {
        track_t *pc = &trk[PERC_TRK], *dr = &trk[0];
        const ck_kit_t *K = &CK_KITS[16];
        double a, d;
        uint32_t f;
        ui_power_on();
        ok = ENG_KIT(pc->eng_req) && pc->p[PC_KIT(0)] == 16 && pc->p[PC_KIT(1)] == 16 && pc_snd(pc->p, 0) == 3u &&
             pc_snd(pc->p, 1) == 15u;
        for (f = 0; f < 5u; f++)                        /* CLAP: the kit's PERC voice; SHAKER: its HAT */
            ok &= pc->p[P_FM1_ATK + f] == K->v[CK_PERC][f] && pc->p[P_FM2_ATK + f] == K->v[CK_HAT][f];
        ok &= pc->p[P_LN0] == K->v[CK_PERC][5] && pc->p[P_LN5] == K->v[CK_HAT][6];
        bad += check("PERC: power-on GLITCH HOP's CLAP (its PERC voice's values) and SHAKER (its HAT's)", ok);
        ok = trk_cap(PERC_TRK) == 2u && trk_cap(0) == 4u;
        bad += check("  two voices (DRUMS four)", ok);

        /* the slot plays its source as the kit plays it: SID BREAKS's snare on PERC 2 vs DRUMS on SID BREAKS */
        apply_preset_to(dr, 39); apply_preset_to(pc, 39);
        pc->p[PC_SND(1)] = 2;                           /* SNARE */
        pc_source(pc, 1);
        dr->p[P_E7] = pc->p[P_E7] = 0;                  /* (no VARI: the same hit) */
        only(0); dr->p[P_LEVEL] = pc->p[P_LEVEL] = 100; dr->p[P_PAN] = pc->p[P_PAN] = 0;
        trk_note_on(dr, 38, 110); run_ms(300); d = el + er; trk_note_off(dr, 38); run_ms(700);
        only(PERC_TRK); pc->p[P_LEVEL] = 100;
        trk_note_on(pc, 38, 110);
        {
            voice_t *v = voice_of(pc, 38);
            ok = v && ((uint32_t)v->s[0] & 3u) == 1u && (((uint32_t)v->s[0] ^ ((uint32_t)v->s[0] >> 2)) & 3u) == CK_SNARE;
        }
        run_ms(300); a = el + er;
        ok &= d > 0 && a > 0.79 * d && a < 1.26 * d && pc->p[P_LN4 + 1] == CK_KITS[39].v[CK_SNARE][6];
        bad += check("  PERC 2 on SID BREAKS's SNARE: the kit's snare (CHIP), as loud as DRUMS playing it (1 dB)", ok);
        trk_note_off(pc, 38); run_ms(700);
        trk_note_on(pc, 36, 110);
        {
            voice_t *v = voice_of(pc, 36);
            ok = v && ((uint32_t)v->s[0] & 3u) == 0u;
        }
        run_ms(200);
        bad += check("  C (36) plays PERC 1, D (38) PERC 2", ok && el + er > 0);

        /* SOURCE's knobs */
        ui_power_on();
        track_select(PERC_TRK);
        go_title("SOURCE");
        ok = str_eq(cur_page()->title, "SOURCE");
        turn(EN_K2, 1);                                 /* 1 SOUND: CLAP -> CL HAT */
        ok &= pc_snd(pc->p, 0) == 4u && pc->p[P_FM1_ATK] == CK_KITS[16].v[CK_HAT][0];
        turn(EN_K3, 2);                                 /* 2 KIT: GLITCH HOP -> TAPE DECK */
        ok &= pc->p[PC_KIT(1)] == 18 && pc->p[P_FM2_ATK + 1] == CK_KITS[18].v[CK_HAT][1];
        bad += check("  SOURCE: KNOB 2 the first sound (CLAP -> CL HAT), KNOB 3 the second's kit; the values follow", ok);
        track_select(0);
        go_title("KICK");
        ok = str_eq(cur_page()->title, "KICK");
        track_select(PERC_TRK);
        frame();
        ok &= !str_eq(cur_page()->title, "KICK") && track_desc(pc, PC_KIT(0)) == &PC_SRC_DESC[0];
        bad += check("  the kit's pages on DRUMS, PERC's own on PERC (SOURCE: 1 KIT ..)", ok);
        ok = drum_nlanes(pc) == 2u && drum_nlanes(&trk[0]) == NLANE && str_eq(drum_lane_abbr(pc, 0), "CH") &&
             str_eq(drum_lane_name(pc, 1), "SHAKR") && str_eq(drum_lane_abbr(pc, 2), "-");
        bad += check("  the roll: two lanes named by their sounds", ok);
        set_engine_of(pc, ENGI_MORPH);
        bad += check("  kits only: MORPH refused on PERC", ENG_KIT(pc->eng_req));
        {
            project_t q, q2;
            static project_store_t st;
            project_capture(&q);
            ok = proj_pack(&st, &q) && proj_import(&q2, &st, sizeof st);
            pc->p[PC_SND(0)] = 1; pc->p[PC_KIT(1)] = 0;
            if (ok) ok = !project_restore_runtime(&q2);
            ok &= pc_snd(pc->p, 0) == 4u && pc->p[PC_KIT(1)] == 18;
            bad += check("  saved with the project and back after a load", ok);
        }
    }

    /* 3 B sides */
    {
        track_t *t1 = &trk[1];
        bsr_t *b = &bsr[0];
        voice_t *va, *vb;
        double a0, ab;
        uint32_t i, nb;
        ui_power_on();
        only(1);
        set_engine_of(t1, ENGI_MORPH); apply_preset_to(t1, 0); t1->engine = t1->eng_req;   /* (MORPH: no grains at random) */
        t1->p[P_PAN] = 0; t1->p[P_VOICE] = V_POLY; t1->p[P_SUS] = 127; t1->p[P_LEVEL] = 70;   /* (under the limiter) */
        run_ms(20);
        trk_note_on(t1, 60, 100); run_ms(300); a0 = el + er; trk_note_off(t1, 60); run_ms(1500);
        memset(t1->v, 0, sizeof t1->v);
        ok = !b_on(t1) && b_set(t1, 1) && b_on(t1) && b->s.tr == 12 && b->s.engine == t1->engine && b->s.lv == 104;
        bad += check("B SIDE on FEED 2: on, a copy of A's sound an octave up, LEVEL 104", ok);
        trk_note_on(t1, 60, 100);
        events_block(CTL);
        va = voice_of(t1, 60); vb = voice_of(t1, 72);
        ok = va && vb && va - t1->v < 4 && vb - t1->v >= 4 && !v_lay(va) && v_lay(vb);
        run_ms(300); ab = el + er;
        ok &= ab > 1.4 * a0;
        bad += check("  a note: A's voice (0..3) and B's (4..7) an octave up, louder together", ok);
        trk_note_off(t1, 60);
        ok = va && vb && !va->gate && !vb->gate;
        bad += check("  its note off lets both go", ok);
        run_ms(1500);
        memset(t1->v, 0, sizeof t1->v);

        b->s.lv = 0;                                    /* (B silent: A alone again) */
        trk_note_on(t1, 60, 100); run_ms(300);
        ok = el + er > 0.7 * a0 && el + er < 1.4 * a0;
        trk_note_off(t1, 60); run_ms(1500); memset(t1->v, 0, sizeof t1->v);
        b->s.lv = 104; b->s.pn = -64;                   /* (B hard left) */
        trk_note_on(t1, 60, 100); run_ms(300);
        ok &= el > 1.3 * er;
        trk_note_off(t1, 60); run_ms(1500); memset(t1->v, 0, sizeof t1->v);
        b->s.pn = 0;
        bad += check("  B LEVEL 0: A alone; B PAN hard left: the left louder", ok);

        track_select(1);
        b_flip(t1);
        set_engine_of(t1, ENGI_GENESIS);
        ok = b->flip && t1->eng_req == ENGI_GENESIS && b->s.engine == ENGI_MORPH;
        b_flip(t1);
        ok &= !b->flip && t1->eng_req == ENGI_MORPH && b->s.engine == ENGI_GENESIS;
        bad += check("  FLIP: the engine changed is B's (GENESIS), A's back after (MORPH)", ok);
        set_engine_of(t1, ENGI_CKIT);
        bad += check("  no kit while B is on", t1->eng_req == ENGI_MORPH);

        b_flip(t1); t1->p[P_VOICE] = V_MONO; b_flip(t1);   /* (B MONO, A POLY) */
        run_ms(20);                                     /* (the engine change's release, then the chord) */
        trk_note_on(t1, 60, 100); trk_note_on(t1, 64, 100); trk_note_on(t1, 67, 100);
        events_block(CTL);
        for (i = 0, nb = 0; i < NVOICE; i++)
            nb += t1->v[i].active && v_lay(&t1->v[i]) && t1->v[i].gate;
        ok = nb == 1u && t1->v[4].note == 79 && voice_of(t1, 60) && voice_of(t1, 64) && voice_of(t1, 67);
        bad += check("  B MONO over A POLY: a chord of 3, A's three voices, B's one on the last note", ok);
        for (i = 60; i < 68; i++) trk_note_off(t1, i);
        run_ms(1500);

        {
            project_t q, q2;
            static project_store_t st;
            b->s.tr = -5; b->s.pn = 20;
            b_flip(t1);                                 /* (saved flipped: back unflipped) */
            project_capture(&q);
            ok = proj_pack(&st, &q) && proj_import(&q2, &st, sizeof st);
            b_set(t1, 0); memset(b, 0, sizeof *b);
            if (ok) ok = !project_restore_runtime(&q2);
            ok &= b_on(t1) && !b->flip && b->s.engine == ENGI_GENESIS && t1->eng_req == ENGI_MORPH && b->s.tr == -5 &&
                  b->s.pn == 20 && b->s.p[P_VOICE] == V_MONO;
            bad += check("  saved (flipped) with the project, back unflipped: B GENESIS MONO, TRN -5, PAN 20", ok);
        }
        trk_note_on(t1, 60, 100); events_block(CTL);
        b_set(t1, 0);
        vb = voice_of(t1, 55);
        ok = !b_on(t1) && b->s.on == 2 && vb && !vb->gate && vb->active;
        trk_note_off(t1, 60);
        run_ms(2000);
        ok &= !voice_of(t1, 55);
        trk_note_on(t1, 62, 100); events_block(CTL);
        ok &= !voice_of(t1, 57) && voice_of(t1, 62);
        bad += check("  off: B's voice let go, rings out; the next note A's alone", ok);
    }

    /* 4 0.10.1's shortcuts */
    {
        uint8_t ids[64];
        uint32_t n, i, k;
        track_t *s1 = &trk[FD_AT[2]], *pc = &trk[PERC_TRK];
        const engine_t *e;
        int16_t *vp, was;
        bsr_t *b = bs_of(s1);
        ui_power_on();
        frames(64);
        e = ENGINES[s1->eng_req % NENGINES];
        n = tk_fx_list(s1, ids);
        for (i = 0; i < n && ids[i] >= P_COUNT; i++)
            ;
        ok = i > 0 && i + 6u <= n && ids[i] == P_PAN && ids[i + 1] == P_LEVEL;
        for (k = 0; ok && k < 4u; k++)
            ok = ids[i + 2u + k] == e->knob[k];
        bad += check("FX column list: the commands, then PAN, LEVEL, HOME's four knobs", ok);
        n = tk_fx_list(pc, ids);
        for (i = 0; i < n && ids[i] >= P_COUNT; i++)
            ;
        bad += check("  PERC's the same: PAN right after the commands", i < n && ids[i] == P_PAN && ids[i + 1] == P_LEVEL);

        ui.home = 1; ui_home_view = HV_STARS; ui.force = 1;
        track_select(PERC_TRK); frame();
        ok = 1;
        for (k = 0; k < 4u; k++) {
            static const uint8_t W[4] = {PC_KIT(0), PC_SND(0), PC_KIT(1), PC_SND(1)};
            home_param(k, &vp);
            ok &= vp == &pc->p[W[k]];
        }
        was = pc->p[PC_SND(1)];
        turn(EN_K4, -2); frames(64);              /* (SHAKER: the list's last) */
        ok &= pc->p[PC_SND(1)] != was;
        bad += check("PERC's HOME knobs: 1 KIT 1 SOUND 2 KIT 2 SOUND (KNOB 4 turns sound 2)", ok);

        b_set(s1, 1);
        track_select(FD_AT[2]); frame();
        turn(EN_ALGO, 1); frames(32);
        ok = song.sel == FD_AT[2] && b->flip;
        turn(EN_ALGO, 1); frames(32);
        ok &= song.sel == FD_AT[3] && !b->flip;
        turn(EN_ALGO, -1); frames(32);
        ok &= song.sel == FD_AT[2] && b->flip;
        turn(EN_ALGO, -1); frames(32);
        ok &= song.sel == FD_AT[2] && !b->flip;
        bad += check("ALGORITHM: SYNTH 1, its B side, SYNTH 2; back: B, then A", ok);

        turn(EN_ALGO, 1); frames(32);                   /* (the B stop) */
        go_page(GR_TRK); frame();
        was = s1->p[P_LEVEL];
        {
            int16_t blv = b->s.lv, bpn = b->s.pn;
            turn(EN_K1, -6); turn(EN_K2, 5); frames(32);
            ok = b->flip && b->s.lv < blv && b->s.pn > bpn && s1->p[P_LEVEL] == was;
        }
        bad += check("  MIXER on the B stop: LEVEL / PAN turn B's, A's level stays", ok);
        turn(EN_ALGO, 1); frames(32);
        b_set(s1, 0);

        go_page(GR_TRK);
        track_select(FD_AT[0]); frame();
        {
            int8_t oct = song.octave;
            uint32_t sel = song.sel;
            ok = strip_page() == 0;
            press(B_OCTUP);
            ok &= strip_page() == 1 && strip_at(0) == FD_AT[4] && song.sel == sel && song.octave == oct;
            press(B_OCTDN);
            ok &= strip_page() == 0 && song.octave == oct;
            bad += check("OCT+ / OCT- on MIXER: the strip's page 2 / 1, the selection and the octave stay", ok);
            press(B_OCTUP);
            turn(EN_ALGO, 1); frames(32);
            ok = strip_page() == 0;
            ui.home = 1; ui_home_view = HV_LEVELS; ui.force = 1; frame();
            press(B_OCTUP);
            ok &= strip_page() == 1 && home_trk(0) == &trk[FD_AT[4]] && song.octave == oct;
            ui_home_view = HV_STARS; ui.force = 1; frame();
            press(B_OCTUP);
            ok &= song.octave == oct + 1;
            bad += check("  the selection moves: back to its page; HOME LEVELS pages too; HOME STARS: the octave", ok);
        }
    }

    printf("v010: %s\n", bad ? "FAILED" : "all checks ok");
    return bad != 0;
}
