/* SPDX-License-Identifier: GPL-3.0-only
 * Phantom Cosmonaut M6: the jitter (gfx.c cv_jitter, ui_draw.c jit_frame) and HOME's starfield (ui_graph.c
 * graph_stars), through ui_test.c's harness and its stub display (host_screen). ATTRITION's knobs: tests/livefx_test.c.
 *   1. a page drawn clean; a jitter kicked: the next frames torn (rows shifted), within JIT_MS clean again, the very
 *      same pixels as before it; MENU > ANIM OFF: none
 *   2. what kicks it: a PHANTOM phrase's first note (not a legato slide), an INGEST stamp, an ATTRITION knob from rest,
 *      an FX key's gate
 *   3. HOME is the starfield (MENU > HOME STARS, the default): it moves, faster playing than stopped, still with ANIM
 *      OFF; the telemetry under it; MENU > HOME SCOPE: M1's terminal over the scope
 *   4. the shockwave: a PHANTOM phrase or ATTRITION sends a ring of ACCENT dots out from the centre, gone after
 *   5. cost: a starfield frame against a scope frame (the host's clock)
 *   build/host/m6_test [GIFDIR]   (GIFDIR: HOME's frames as PPMs, a phrase and ATTRITION over the playing groove) */
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"
#include <time.h>

static uint16_t snap[240 * 240];
static uint32_t diff_px(void)                    /* host_screen against snap */
{
    uint32_t i, n = 0;
    for (i = 0; i < 240u * 240u; i++)
        n += host_screen[i] != snap[i];
    return n;
}
static uint32_t panel_px(uint16_t col, int32_t y0, int32_t y1)   /* pixels of colour col in the graph panel's rows */
{
    uint32_t n = 0;
    int32_t x, y;
    for (y = y0; y < y1; y++)
        for (x = 12; x < 228; x++)
            n += host_screen[y * 240 + x] == swap16(col);
    return n;
}
static void redraw(void) { ui.force = 1; frame(); }

static void dump(const char *dir, uint32_t i)
{
    char path[512];
    FILE *f;
    uint32_t k;
    snprintf(path, sizeof path, "%s/m6_%03u.ppm", dir, i);
    f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P6\n240 240\n255\n");
    for (k = 0; k < 240u * 240u; k++) {
        uint16_t c = swap16(host_screen[k]);
        fputc(((c >> 11) & 31) * 255 / 31, f);
        fputc(((c >> 5) & 63) * 255 / 63, f);
        fputc((c & 31) * 255 / 31, f);
    }
    fclose(f);
}

int main(int argc, char **argv)
{
    int bad = 0, ok;
    uint32_t n0, n1, n2, i;
    char what[200];

    /* 1. the jitter on a still page (EDIT's scope: nothing moves on it by itself) */
    ui_power_on();
    track_select(1);
    open_family(FAM_EDIT);
    frames(200);
    redraw();
    memcpy(snap, host_screen, sizeof snap);
    redraw();
    n0 = diff_px();
    jit_kick(2u);
    frame();
    n1 = diff_px();
    ok = jit_amp == 2u;
    frames(32);
    n2 = diff_px();
    frames(JIT_MS + 64u);
    snprintf(what, sizeof what, "jitter: a still page (%u px apart redrawn), kicked: torn (%u, %u px), then clean again (%u px)",
             n0, n1, n2, diff_px());
    bad += check(what, n0 == 0u && ok && n1 > 300u && n2 > 300u && diff_px() == 0u && !jit_amp);
    ui_prefs |= PREF_ANIM_OFF;
    jit_kick(2u);
    frame();
    bad += check("  MENU > ANIM OFF: no jitter", !jit_amp && diff_px() == 0u);
    ui_prefs &= (uint8_t)~PREF_ANIM_OFF;

    /* 2. what kicks it */
    ui_power_on();
    frames(300);
    jit_req = 0;
    ph_key_on(3, 60);
    ok = jit_req != 0;
    jit_req = 0;
    ph_key_on(4, 62);                                    /* (held over the first: a legato slide) */
    ok &= jit_req == 0;
    ph_key_off(3);
    ph_key_off(4);
    jit_req = 0;
    ph_key_on(5, 64);                                    /* (a new phrase) */
    bad += check("  kicked by a PHANTOM phrase's first note, not a legato slide; a new phrase again", ok && jit_req != 0);
    ph_key_off(5);
    ui_power_on();
    track_select(1);
    song.rec = 1u << 1;
    seq_start();
    song.playing = 1;
    frames(100);
    jit_req = 0;
    motion_capture(TSEL, P_E1, 90);
    bad += check("  kicked by an INGEST stamp (a knob recorded)", jit_req != 0);
    song.playing = 0;
    seq_stop();
    song.rec = 0;
    ui_power_on();
    song.g[G_ENGGO] = 0;                                 /* (ATTRITION's knobs, the default) */
    go_home();
    frames(100);
    btn_down(B_FX);
    frames(900);
    frames(JIT_MS + 64u);
    jit_req = 0;
    jit_amp = 0;
    star_req = 0;
    turn(EN_K2, 3);                                      /* TUNNEL from rest (the frame takes the request: jit_amp) */
    ok = (jit_req || jit_amp) && star_req != 0;
    jit_req = 0;
    jit_amp = 0;
    star_req = 0;
    turn(EN_K2, 1);                                      /* (on: the jitter again, no new shockwave) */
    ok &= (jit_req || jit_amp) && star_req == 0;
    btn_up(B_FX);
    frames(100);
    bad += check("  kicked by ATTRITION: a knob from rest tears and sends a shockwave; turned on, tears", ok);
    jit_req = 0;
    star_req = 0;
    perf_press(PF_R8, 1);                                /* (FX + F3: REPEAT 1/8, a gate) */
    ok = jit_req == 2u && star_req != 0;
    perf_press(PF_R8, 0);
    bad += check("  .. and by an FX key's gate (strong)", ok);

    /* 3. HOME's starfield */
    ui_power_on();
    go_home();
    frames(400);
    bad += check("HOME: the starfield by default (MENU > HOME STARS), the telemetry under it",
                 ui_home_view == HV_STARS && panel_px(T_MID, 150, 240) > 0u);
    redraw();
    memcpy(snap, host_screen, sizeof snap);
    frames(160);
    n0 = diff_px();
    seq_start();
    song.playing = 1;
    frames(64);
    redraw();
    memcpy(snap, host_screen, sizeof snap);
    frames(160);
    n1 = diff_px();
    song.playing = 0;
    seq_stop();
    frames(64);
    ui_prefs |= PREF_ANIM_OFF;
    redraw();
    memcpy(snap, host_screen, sizeof snap);
    frames(160);
    n2 = diff_px();
    ui_prefs &= (uint8_t)~PREF_ANIM_OFF;
    snprintf(what, sizeof what, "  the field moves: %u px in 160 ms stopped, %u playing (HBT 124); ANIM OFF still (%u)", n0, n1, n2);
    bad += check(what, n0 > 0u && n1 > n0 && n2 == 0u);
    menu_put(MI_HOME, HV_SCOPE);
    redraw();
    bad += check("  MENU > HOME SCOPE: the terminal over the scope (M1's)", ui_home_view == HV_SCOPE);
    menu_put(MI_HOME, HV_STARS);

    /* 4. the shockwave */
    ui_power_on();
    go_home();
    frames(400);
    n0 = panel_px(T_ACCENT, 60, 136);
    star_kick();
    frames(96);
    n1 = panel_px(T_ACCENT, 60, 136);
    frames(600);
    n2 = panel_px(T_ACCENT, 60, 136);
    snprintf(what, sizeof what, "shockwave: a ring of ACCENT dots from the centre (%u px; %u before, %u after)", n1, n0, n2);
    bad += check(what, n1 > n0 + 20u && n2 <= n0 + 2u);

    /* 5. cost: HOME's graph drawn, starfield against scope */
    {
        struct timespec a, b;
        double ns_st, ns_sc;
        uint32_t k;
        ui_power_on();
        go_home();
        seq_start();
        song.playing = 1;
        frames(200);
        clock_gettime(CLOCK_MONOTONIC, &a);
        for (k = 0; k < 400u; k++) { fm1_ms += 32u; redraw(); }
        clock_gettime(CLOCK_MONOTONIC, &b);
        ns_st = ((b.tv_sec - a.tv_sec) * 1e9 + (b.tv_nsec - a.tv_nsec)) / 400.0;
        menu_put(MI_HOME, HV_SCOPE);
        clock_gettime(CLOCK_MONOTONIC, &a);
        for (k = 0; k < 400u; k++) { fm1_ms += 32u; redraw(); }
        clock_gettime(CLOCK_MONOTONIC, &b);
        ns_sc = ((b.tv_sec - a.tv_sec) * 1e9 + (b.tv_nsec - a.tv_nsec)) / 400.0;
        menu_put(MI_HOME, HV_STARS);
        song.playing = 0;
        seq_stop();
        snprintf(what, sizeof what, "cost: a HOME frame drawn whole, the starfield %.0f us, the scope %.0f us (%.2f x)",
                 ns_st / 1000.0, ns_sc / 1000.0, ns_st / ns_sc);
        bad += check(what, getenv("NOCOST") || ns_st < 1.5 * ns_sc);
    }

    /* the frames, for a look */
    if (argc > 1) {
        ui_power_on();
        palette_set(UI_PHANTOM_INDEX);                   /* (the device's default look) */
        song.g[G_ENGGO] = 0;
        go_home();
        seq_start();
        song.playing = 1;
        frames(300);
        for (i = 0; i < 90u; i++) {
            if (i == 20u) ph_key_on(3, 64);
            if (i == 38u) ph_key_off(3);
            if (i == 55u) { mk_a[1] = 70; jit_kick(2u); star_kick(); }
            if (i == 70u) mk_a[1] = 0;
            frames(32);
            dump(argv[1], i);
        }
        song.playing = 0;
        seq_stop();
        printf("m6: frames in %s\n", argv[1]);
    }
    printf("m6: %s\n", bad ? "FAILED" : "all checks ok");
    return bad != 0;
}
