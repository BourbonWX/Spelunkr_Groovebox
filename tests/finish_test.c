/* SPDX-License-Identifier: GPL-3.0-only
 * Phantom Cosmonaut, the spec's last pieces before the release, through ui_test.c's harness:
 *   1. VOICES per FEED (core.h trk_cap): AUTO, MONO, 2, 3; the kit's four lanes; the budget shared
 *   build/host/finish_test */
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"

static uint32_t sounding(uint32_t k)
{
    uint32_t i, n = 0;
    for (i = 0; i < NVOICE; i++)
        n += trk[k].v[i].active && trk[k].v[i].stage != 4u;
    return n;
}
static uint32_t chord_on(uint32_t k)            /* a four-note chord held on FEED k + 1, its voices after a moment */
{
    static const uint8_t C[4] = {48, 52, 55, 59};
    uint32_t i;
    for (i = 0; i < 128u; i++) trk_note_off(&trk[k], i);
    events_block(CTL);
    memset(trk[k].v, 0, sizeof trk[k].v);               /* (the last chord's voices gone, not releasing) */
    trk[k].p[P_SUS] = 127;
    trk[k].p[P_VOICE] = V_POLY;
    for (i = 0; i < 4u; i++) trk_note_on(&trk[k], C[i], 100);
    for (i = 0; i < 4u; i++) events_block(CTL);
    return sounding(k);
}

static uint32_t heard;                          /* bits: the notes 48..79 FEED 2's voices started while bar() ran */
static void bar(uint32_t ms)                    /* the audio for ms, the UI a frame each 16 ms between: the notes
                                                 * that start in it (a voice's age: a new note on it) */
{
    static uint32_t was[NVOICE];
    uint32_t b, i, n = ms * FS / 1000u / CTL;
    for (i = 0; i < NVOICE; i++)
        was[i] = trk[1].v[i].age;
    for (b = 0; b < n; b++) {
        events_block(CTL);
        for (i = 0; i < NVOICE; i++) {
            if (trk[1].v[i].active && trk[1].v[i].age != was[i] && trk[1].v[i].note >= 48u && trk[1].v[i].note < 80u)
                heard |= 1u << (trk[1].v[i].note - 48u);
            was[i] = trk[1].v[i].age;
        }
        if (b % (16u * FS / 1000u / CTL) == 0u)
            frame();
    }
}
static double out_rms(uint32_t ms)               /* the mix's rms for ms (nothing playing but what is held / rings) */
{
    int32_t o[2 * CTL];
    uint32_t b, i, n = ms * (FS / 1000u) / CTL;
    double e = 0;
    for (b = 0; b < n; b++) {
        mix_block(o, CTL);
        for (i = 0; i < 2u * CTL; i++) e += (double)o[i] * o[i];
    }
    return sqrt(e / (2.0 * CTL * (n ? n : 1)));
}

/* the mix's left channel over ms: its brightness, sum (x[i] - x[i-1])^2 / sum x^2 (a tone ~0.01; white noise 2) */
static double out_bright(uint32_t ms)
{
    int32_t o[2 * CTL];
    uint32_t b, i, n = ms * (FS / 1000u) / CTL;
    double e = 0, d = 0, prev = 0;
    for (b = 0; b < n; b++) {
        mix_block(o, CTL);
        for (i = 0; i < CTL; i++) {
            double x = o[2 * i];
            e += x * x;
            d += (x - prev) * (x - prev);
            prev = x;
        }
    }
    return d / (e + 1e-9);
}

/* the mix's left channel over ms: its strongest frequency between lo and hi Hz (a DFT a Hz, Hann) */
static double out_peak_hz(uint32_t ms, double lo, double hi)
{
    static float x[FS];
    int32_t o[2 * CTL];
    uint32_t b, i, n = ms * (FS / 1000u) / CTL, m;
    double best = -1, bf = 0, f;
    for (b = 0; b < n; b++) {
        mix_block(o, CTL);
        for (i = 0; i < CTL; i++) x[b * CTL + i] = (float)o[2 * i];
    }
    m = n * CTL;
    for (f = lo; f <= hi; f += 1.0) {
        double re = 0, im = 0, e;
        for (i = 0; i < m; i++) {
            double w = 0.5 - 0.5 * cos(2 * M_PI * i / m);
            re += w * x[i] * cos(2 * M_PI * f * i / FS);
            im += w * x[i] * sin(2 * M_PI * f * i / FS);
        }
        e = re * re + im * im;
        if (e > best) best = e, bf = f;
    }
    return bf;
}

/* .. its high end: the rms of the mix's slope (a crusher's grit raises it, a hit's loudness much less) over its rms */
static double out_grit(uint32_t ms)
{
    int32_t o[2 * CTL], pl = 0;
    uint32_t b, i, n = ms * (FS / 1000u) / CTL;
    double e = 0, d = 0;
    for (b = 0; b < n; b++) {
        mix_block(o, CTL);
        for (i = 0; i < CTL; i++) {
            e += (double)o[2 * i] * o[2 * i];
            d += (double)(o[2 * i] - pl) * (o[2 * i] - pl);
            pl = o[2 * i];
        }
    }
    return e > 0 ? sqrt(d / e) : 0;
}
static void one_note(uint32_t i, uint32_t note)
{
    trk[1].step[i].note[0] = (uint8_t)note;
    trk[1].step[i].n = 1;
    trk[1].step[i].time = ST_NOTE;
}

int main(int argc, char **argv)
{
    int bad = 0;
    char what[200];

    /* 1. VOICES */
    {
        uint32_t a, m, two, three, b2, b3, b4;
        ui_power_on();
        song.playing = 0;
        a = chord_on(1);
        trk[1].p[P_VOICES] = 1; m = chord_on(1);
        trk[1].p[P_VOICES] = 2; two = chord_on(1);
        trk[1].p[P_VOICES] = 3; three = chord_on(1);
        snprintf(what, sizeof what, "VOICES on FEED 2, a 4-note chord: AUTO %u (0.10.5: its pad's 4, shared), MONO %u, 2 %u, 3 %u",
                 a, m, two, three);
        bad += check(what, a >= 2u && m == 1u && two == 2u && three == 3u);
        trk[1].p[P_VOICES] = 3; trk[2].p[P_VOICES] = 3; trk[3].p[P_VOICES] = 3;
        b2 = trk_cap(1); b3 = trk_cap(2); b4 = trk_cap(3);
        snprintf(what, sizeof what, "  all three at 3: the budget less the kits' 4 + 2 shared (%u %u %u), never past %u", b2, b3, b4, NBUDGET - 6u);
        bad += check(what, b2 + b3 + b4 <= NBUDGET - 6u && b2 >= 1u && b3 >= 1u && b4 >= 1u && trk_cap(0) == 4u && trk_cap(PERC_TRK) == 2u);
        trk[2].p[P_VOICES] = 0;
        bad += check("  FEED 3 back to AUTO (its bass: 1): FEED 2 and 4 share the 5 left (0.10.5: 3 + 2, the rest to the first short)",
                     trk_cap(2) == 1u && trk_cap(1) + trk_cap(3) == 5u && trk_cap(1) >= 2u && trk_cap(3) >= 2u);
        trk[0].p[P_VOICES] = 1;
        bad += check("  the kit (FEED 1) keeps its four lanes whatever VOICES says", trk_cap(0) == 4u);
        bad += check("  EDIT > VOICE: VOICES first, AUTO MONO 2 3 4 (0.10.5)", str_eq(TP[P_VOICES].label, "VOICES") &&
                     str_eq(TP[P_VOICES].names[1], "MONO") && TP[P_VOICES].max == 4 && str_eq(TP[P_VOICES].names[4], "4"));
        {   /* 0.10.5 (Ed: no roles): AUTO is the sound's own count, by its category; past the synths' 6, shared in
             * proportion; HOME's PRESETS lists every synth sound on a synth FEED, only kits on DRUMS */
            uint32_t k, pad = 99, bass = 99, ok, total, i, kits = 0, synths = 0;
            const engine_t *en = ENGINES[ENGI_MORPH];
            for (k = 0; k < en->npresets; k++) {
                if (pad == 99 && preset_cat(ENGI_MORPH, k) == CAT_PAD) pad = k;
                if (bass == 99 && preset_cat(ENGI_MORPH, k) == CAT_BASS) bass = k;
            }
            ui_power_on();
            for (k = 1; k <= 3u; k++) trk[k].p[P_VOICES] = 0;
            set_engine_of(&trk[1], ENGI_MORPH); apply_preset_to(&trk[1], pad);
            set_engine_of(&trk[2], ENGI_MORPH); apply_preset_to(&trk[2], bass);
            set_engine_of(&trk[3], ENGI_PFM); apply_preset_to(&trk[3], 0);   /* (Spelunkr: FEED 4's sound of before, a bell) */
            frames(50);
            ok = vox_req(1) == 4u && vox_req(2) == 1u && trk_cap(1) == 4u;
            set_engine_of(&trk[2], ENGI_MORPH); apply_preset_to(&trk[2], pad);   /* (a second pad, on the "bass" FEED) */
            frames(50);
            ok &= vox_req(2) == 4u && trk_cap(1) + trk_cap(2) + trk_cap(3) <= NBUDGET - 6u && trk_cap(1) >= 2u && trk_cap(2) >= 2u &&
                  trk_cap(3) >= 1u;
            snprintf(what, sizeof what, "  0.10.5: AUTO by the sound: a PAD 4, a BASS 1; two pads + the lead share the 6 (%u %u %u)",
                     trk_cap(1), trk_cap(2), trk_cap(3));
            bad += check(what, ok);
            track_select(2);
            preset_pos(&total);
            for (i = 0; i < total; i++) {
                uint32_t e = preset_at(i, &k);
                if (e < NENGINES && ENG_KIT(e)) kits++;
                if (e < NENGINES && preset_cat(e, k) == CAT_PAD) synths++;
            }
            ok = !kits && synths > 5u && list_mode() == 0u;
            track_select(0);
            preset_pos(&total);
            for (i = 0, kits = 0; i < total; i++) {
                uint32_t e = preset_at(i, &k);
                kits += e < NENGINES && ENG_KIT(e);
            }
            ok &= kits == total && total >= 40u;
            bad += check("  LIST ALL by default: SYNTH 2 lists pads too, no kits; DRUMS lists only the kits", ok);
        }
        {   /* an old ANALOG sound (its filter TYPE HP in this byte) loads with VOICES AUTO */
            int16_t p[P_COUNT];
            memset(p, 0, sizeof p);
            p[P_VOICES] = 2;
            eng_retire_values(0u, CAT_BASS, 0u, p);
            bad += check("  an old ANALOG sound (its filter TYPE in VOICES' byte): VOICES AUTO", p[P_VOICES] == 0);
        }
    }

    /* 2. SCRAMBLE */
    {
        int16_t before[4], a[4], b[4];
        uint32_t k, moved = 0, again = 0, oct, kid[4];
        ui_power_on();
        track_select(1);
        go_home();
        frames(100);
        oct = song.octave;
        for (k = 0; k < 4u; k++) kid[k] = ENGINES[trk[1].eng_req % NENGINES]->knob[k];   /* (Spelunkr: SAMPLE's SET CUT WOW AGE) */
        for (k = 0; k < 4u; k++) before[k] = trk[1].p[kid[k]];
        btn_down(B_EDIT);
        frames(900);
        press(B_OCTUP);
        for (k = 0; k < 4u; k++) { a[k] = trk[1].p[kid[k]]; moved += a[k] != before[k]; }
        frames(50);
        press(B_OCTUP);
        for (k = 0; k < 4u; k++) { b[k] = trk[1].p[kid[k]]; again += b[k] != a[k]; }
        snprintf(what, sizeof what, "SCRAMBLE: EDIT + OCT+ throws the four knobs (%d %d %d %d -> %d %d %d %d), again another (%u moved)",
                 before[0], before[1], before[2], before[3], a[0], a[1], a[2], a[3], again);
        bad += check(what, moved >= 3u && again >= 3u && song.octave == oct && ui.layer);
        press(B_OCTDN);
        frames(50);
        btn_up(B_EDIT);
        frames(100);
        for (k = 0, moved = 0; k < 4u; k++) moved += trk[1].p[kid[k]] != before[k];
        bad += check("  EDIT + OCT-: all four back as they were (the engine and sound too)", !moved &&
                     trk[1].eng_req == TRK_DEF[1][0] && !ui.layer);
    }

    /* 3. PRESETS on HOME (0.10.4, Ed: "it definitely needs to scroll through all possible presets"): whole sounds on
     * every engine, a SIGNAL engine's too, in the LIST (ROLE: the FEED's role); its SIG on the EDIT page; the kit's kits */
    {
        uint32_t eng, pre, i, moved = 0, other = 0;
        ui_power_on();
        track_select(1);
        go_home();
        frames(100);
        eng = trk[1].eng_req; pre = trk[1].preset;
        turn(EN_PRESET, 1);
        frames(10);
        snprintf(what, sizeof what, "PRESETS on HOME, FEED 2 (DEBRIS): a whole sound (%s %u -> %s %u), not its SIG",
                 ENGINES[eng]->name, pre, ENGINES[trk[1].eng_req]->name, trk[1].preset);
        bad += check(what, trk[1].preset != pre || trk[1].eng_req != eng);
        for (i = 0; i < 80u; i++) {
            turn(EN_PRESET, 1);
            moved += 1;
            other |= trk[1].eng_req != eng && trk[1].eng_req != ENGI_DEBRIS;
        }
        bad += check("  .. on past the SIGNAL engines' sounds into the other engines' (no stop at SIG 16)", other);
        track_select(0);
        pre = trk[0].preset;
        turn(EN_PRESET, 1);
        bad += check("  FEED 1 (the NOISE kit): PRESETS loads the next kit", trk[0].preset == pre + 1u && ENG_KIT(trk[0].eng_req % NENGINES));
        track_select(1);
        go_page(GR_BROWSE);
        pre = trk[1].preset;
        eng = trk[1].eng_req;
        turn(EN_PRESET, 1);
        bad += check("  whole sounds on SAVE > PRESETS as ever", trk[1].preset != pre || trk[1].eng_req != eng);
    }

    /* 4. RELAY LOCK */
    {
        uint32_t i, a, b, c, d;
        ui_power_on();
        track_select(1);
        go_home();
        for (i = 0; i < NSTEP; i++) step_clear(&trk[1].step[i]);
        trk[1].p[P_SLEN] = 16;
        trk[1].p[P_SUS] = 0;
        one_note(0, 60);
        seq_start();
        song.playing = 1;
        heard = 0; bar(2200); a = heard;                 /* (HBT 124: a bar ~1.94 s) */
        press(B_LFO);
        c = relay_on && msg_is("RELAY LOCK");
        step_clear(&trk[1].step[0]);
        one_note(8, 67);                                 /* edited while locked */
        heard = 0; bar(4400); b = heard;
        d = trk[1].step[8].note[0] == 67 && !trk[1].step[0].n;   /* (the edits are there, on the GRID) */
        snprintf(what, sizeof what, "RELAY LOCK: LFO tapped, the bar as it was plays on (C4 %s, the edit's G4 %s), the edits kept",
                 (b >> 12) & 1u ? "heard" : "gone", (b >> 19) & 1u ? "heard" : "not");
        bad += check(what, ((a >> 12) & 1u) && c && ((b >> 12) & 1u) && !((b >> 19) & 1u) && d);
        press(B_LFO);
        c = !relay_on && msg_is("RELAY OPEN");
        heard = 0; bar(2200); b = heard;
        bad += check("  LFO tapped again: the edits play (G4, no C4)", c && ((b >> 19) & 1u) && !((b >> 12) & 1u));
        song.playing = 0;
        seq_stop();
        hold(B_LFO);
        bad += check("  LFO held: its pages (no lock)", !relay_on && !ui.home && cur_page()->fam == FAM_LFO);
        go_home();
        btn_down(B_LFO);
        frames(100);
        turn(EN_K1, 3);
        frames(900);
        btn_up(B_LFO);
        frames(50);
        bad += check("  LFO held with a knob (MOMENTARY): no lock, no page", !relay_on && ui.home);
    }

    /* 5. PHANTOM CH (MIDI OUT), USB LEVEL hidden */
    {
        uint32_t w0, on, off, i, seen = 0;
        ui_power_on();
        usb.config = 1;
        mo_r = mo_w;
        track_select(FD_AT[NTRK - 1u]);
        go_home();
        frames(50);
        turn(EN_ALGO, 1);                                /* (PHANTOM focused: the keys are its) */
        frames(50);
        bad += check("PHANTOM CH: MENU > MIDI, after FEED 5 CH, 6 by default (0.10: PERC on 5)", MI_TAB[MI_PHCH] == MTAB_MIDI &&
                     MI_PHCH == MI_TCH5 + 1u && str_eq(MI_NAME[MI_PHCH], "PHANTOM CH") && menu_get(MI_PHCH) == 5u &&
                     str_eq(menu_vname(MI_PHCH, 5), "6") && str_eq(menu_vname(MI_PHCH, 16), "OFF"));
        w0 = mo_w;
        key_down(5);
        on = mo_w > w0 ? midi_out_q[w0 % MQ] : 0u;
        w0 = mo_w;
        key_up(5);
        off = mo_w > w0 ? midi_out_q[w0 % MQ] : 0u;
        snprintf(what, sizeof what, "  a PHANTOM key: note-on %08x, note-off %08x on channel 6", on, off);
        bad += check(what, (on & 0xFFFFu) == 0x9509u && (on >> 24) != 0u && (off & 0xFFFFu) == 0x8508u &&
                     ((on >> 16) & 0xFFu) == ((off >> 16) & 0xFFu) && ph_focus);
        menu_put(MI_PHCH, 0);
        w0 = mo_w;
        key_down(5);
        menu_put(MI_PHCH, 9);                            /* (changed while held: the note-off where the note went) */
        key_up(5);
        bad += check("  PHANTOM CH 1: on channel 1, its note-off there though CH changed while it was held",
                     mo_w == w0 + 2u && (midi_out_q[w0 % MQ] & 0xFF00u) == 0x9000u && (midi_out_q[(w0 + 1u) % MQ] & 0xFF00u) == 0x8000u);
        menu_put(MI_PHCH, 16);
        w0 = mo_w;
        key_down(5); key_up(5);
        bad += check("  PHANTOM CH OFF: nothing out", mo_w == w0 && menu_get(MI_PHCH) == 16u);
        menu_put(MI_PHCH, 5);
        for (i = 0; i < MTAB_COUNT; i++) seen |= (mtab_first(i) <= MI_USB && MI_USB < mtab_first(i) + mtab_rows(i));
        bad += check("USB LEVEL: hidden (no tab shows it), its editor id 10 kept", !seen && MI_TAB[MI_USB] == MTAB_HIDDEN &&
                     mtab_rows(MTAB_MIDI) == 10u && mtab_rows(MTAB_AUDIO) == 5u && MTAB_OF(MI_USB) == MTAB_AUDIO);
        usb.config = 0;
    }

    /* 5b. the keys: PHANTOM only while it is focused (Ed, Nov 2026) */
    {
        uint32_t i, feed, phn;
        ui_power_on();
        song.playing = 0;
        track_select(1);
        go_home();
        frames(50);
        key_down(white(5));                             /* (a white key: FEED 2's black keys are CHORD KEYS' modifiers) */
        for (i = 0; i < 4u; i++) events_block(CTL);
        feed = sounding(1);
        phn = ph.n;
        key_up(white(5));
        frames(50);
        bad += check("the keys on HOME, FEED 2 selected: FEED 2 plays (its sound auditioned), not PHANTOM",
                     feed >= 1u && !phn && !ph_keys);
        track_select(FD_AT[NTRK - 1u]);
        frames(20);
        turn(EN_ALGO, 1);
        frames(50);
        key_down(5);
        for (i = 0; i < 4u; i++) events_block(CTL);
        phn = ph.n;
        key_up(5);
        bad += check("  ALGORITHM past FEED 4 (PHANTOM focused): the keys play PHANTOM", ph_focus && ph_keys && phn);
        turn(EN_ALGO, -1);
        frames(50);
        song.rec = 1u << 1;
        track_select(1);
        go_home();
        frames(50);
        bad += check("  REC armed on FEED 2 from HOME: the keys are the FEED's (live recording from HOME)", !ph_keys && !ph_focus);
        song.rec = 0;
    }

    /* 6. the GRID: the cursor row's FX in plain words */
    {
        char b[40];
        int ok;
        ui_power_on();
        track_select(1);
        set_engine_of(&trk[1], ENGI_MORPH); apply_preset_to(&trk[1], 0);   /* (Spelunkr: FEED 2 is SAMPLE's; SCAN is MORPH's) */
        motion_set_lock(&trk[1], 7, CMD_LOOP, 10);      /* L2x3 */
        motion_set_lock(&trk[1], 9, CMD_SPD, 1);        /* HALF */
        motion_set_lock(&trk[1], 12, P_E1, 74);         /* SCAN */
        motion_set_lock(&trk[1], 13, CMD_LPIT, -3);
        ok = tk_plain(&trk[1], 7, b) && str_eq(b, "07 > LOOP: BACK 2 ROWS, 3 TIMES");
        ok &= tk_plain(&trk[1], 9, b) && str_eq(b, "09 > SPEED: HALF TIME");
        ok &= tk_plain(&trk[1], 13, b) && str_eq(b, "13 > LOOP PITCH: -3 ST A PASS");
        ok &= tk_plain(&trk[1], 12, b) && b[0] == '1' && b[1] == '2' && b[5] != 0;
        printf("  (row 12: %s)\n", b);
        ok &= !tk_plain(&trk[1], 8, b);
        bad += check("GRID: the cursor row's FX in plain words (LOOP, SPEED, LOOP PITCH, a lock), none on an empty row", ok);
    }

    /* 7. the default transmission (boot_song.c, main.c felucca_init after TRK_DEF) */
    {
        uint32_t k, i, b, n, starts[NTRK] = {0}, was[NTRK][NVOICE];
        int32_t o[2 * CTL];
        FILE *w = argc > 1 ? fopen(argv[1], "wb") : 0;
        uint32_t frames_out = 0;
        ui_power_on();
        transmission();
        song.master_q12 = 4096;
        for (k = 0; k < NTRK; k++)
            for (i = 0; i < NVOICE; i++) was[k][i] = trk[k].v[i].age;
        if (w) wav_hdr(w, 0);
        seq_start();
        song.playing = 1;
        n = (argc > 1 ? 16000u : 3900u) * FS / 1000u / CTL;   /* (two bars at HBT 120; the demo eight) */
        for (b = 0; b < n; b++) {
            mix_block(o, CTL);
            if (w) { for (i = 0; i < CTL; i++) wav_put(w, o[2 * i], o[2 * i + 1]); frames_out += CTL; }
            for (k = 0; k < NTRK; k++)
                for (i = 0; i < NVOICE; i++) {
                    if (trk[k].v[i].active && trk[k].v[i].age != was[k][i]) starts[k]++;
                    was[k][i] = trk[k].v[i].age;
                }
        }
        song.playing = 0;
        seq_stop();
        if (w) { fseek(w, 0, SEEK_SET); wav_hdr(w, frames_out); fclose(w); printf("  (the transmission: %s)\n", argv[1]); }
        snprintf(what, sizeof what, "the default transmission: two bars, notes started per FEED %u %u %u %u; LEN 32 on FEEDs 2-4, D minor",
                 starts[0], starts[1], starts[2], starts[3]);
        bad += check(what, starts[0] >= 8u && starts[1] >= 3u * 3u && starts[2] >= 12u && starts[3] >= 6u &&
                     trk[1].p[P_SLEN] == 32 && trk[2].p[P_SLEN] == 32 && trk[3].p[P_SLEN] == 32 &&
                     trk[1].step[0].note[0] == 50 && trk[2].step[0].note[0] == 38 && pat_sig[1] == steps_sig(&trk[1]));
    }

    /* 8. M8: DEBRIS's tapes (GRANULAR's before); the kit's SET (0.9.2's DERELICT, SHORTWAVE) scrapped in Phantom
     * Tracker 0.9.7: kits 16..19 synthesized (GLITCH HOP CHILLWAVE TAPE DECK WONK), a sound with a SET plays the
     * synthesis */
    {
        uint32_t i, ok, k;
        double lv[20], lo = 1e9, hi = 0, smp;
        static const char *const NM[4] = {"GLITCH HOP", "CHILLWAVE", "TAPE DECK", "WONK"};
        ui_power_on();
        song.playing = 0;
        song.master_q12 = 4096;
        ok = track_desc(&trk[0], P_LN7) == 0 || !str_eq(track_desc(&trk[0], P_LN7)->label, "SET");
        for (k = 0; k < 20u; k++) {                                      /* every kit: its kick, the synthesis */
            apply_preset_to(&trk[0], k);
            trk[0].p[P_E7] = 0;
            ok &= trk[0].p[P_LN7] == 0;
            if (k >= 16u)
                ok &= str_eq(ENG_CKIT.presets[k].name, NM[k - 16u]);
            trk_note_on(&trk[0], 36, 120);
            lv[k] = out_rms(150);
            trk_note_off(&trk[0], 36);
            out_rms(900);
            if (k >= 16u) { lo = lv[k] < lo ? lv[k] : lo; hi = lv[k] > hi ? lv[k] : hi; }
        }
        snprintf(what, sizeof what, "the kits 16..19 synthesized (GLITCH HOP CHILLWAVE TAPE DECK WONK, no SET): kicks %.0f .. %.0f, "
                 "CAPSULE's %.0f", lo, hi, lv[0]);
        bad += check(what, ok && lo > lv[0] * 0.4 && hi < lv[0] * 2.5);
        apply_preset_to(&trk[0], 0);
        trk[0].p[P_LN7] = 1;                                             /* a song's DERELICT (SET 1, 0.9.2 .. 0.9.6) */
        trk_note_on(&trk[0], 36, 120);
        lv[0] = out_rms(150);
        trk_note_off(&trk[0], 36);
        out_rms(900);
        bad += check("  a kit sound with a SET (0.9.2 .. 0.9.6) plays the synthesis (its kick; 0.9.10: SET 1 / 2 is PERC's MODEL)", lv[0] > 300.0);
        {   /* 8b. 0.9.10: each lane's MODEL (P_LN4 + lane: 1 FM 2 CHIP 3 COMB 4 BLOOP) and the ten kits on them */
            static const char *const KN[10] = {"SIXTEEN BIT", "BREADBIN", "RESONATOR", "POND", "GLITCHWAVE", "VAPOR FM",
                                               "ARCADE", "FROGBEAT", "FOUNDRY", "TIN CAN"};
            static const uint8_t NOTE[CK_N] = {36, 38, 42, 45};
            uint32_t used[CKM_N] = {0}, l, mo, same = 0, quiet = 0, wild = 0;
            double klo = 1e9, khi = 0, ref[CK_N], cap;
            ok = NELEM(CK_KITS) == 48u;                                  /* (Spelunkr: + the eight sampled, 40..47) */
            apply_preset_to(&trk[0], 0);                                 /* (CAPSULE's kick, the yardstick) */
            trk[0].p[P_E7] = 0;
            trk_note_on(&trk[0], 36, 120);
            cap = out_rms(150);
            trk_note_off(&trk[0], 36);
            out_rms(900);
            for (k = 0; k < 10u; k++) {                                  /* the ten: named, each with a model of an engine */
                uint32_t any = 0;
                ok &= str_eq(ENG_CKIT.presets[20u + k].name, KN[k]) && preset_cat(ENGI_CKIT, 20u + k) == CAT_DRUM;
                apply_preset_to(&trk[0], 20u + k);
                trk[0].p[P_E7] = 0;
                for (l = 0; l < CK_N; l++) {
                    used[ck_model(trk[0].p, l)]++;
                    any |= ck_model(trk[0].p, l) != CKM_NOISE;
                }
                ok &= any;
                trk_note_on(&trk[0], 36, 120);
                lv[0] = out_rms(150);
                trk_note_off(&trk[0], 36);
                out_rms(900);
                klo = lv[0] < klo ? lv[0] : klo;
                khi = lv[0] > khi ? lv[0] : khi;
            }
            for (mo = 1; mo < CKM_N; mo++)
                ok &= used[mo] >= 3u;
            apply_preset_to(&trk[0], 0);
            snprintf(what, sizeof what, "  ten kits on the engines' voices (SIXTEEN BIT .. TIN CAN): each a model, each model in 3+ "
                     "(FM %u CHIP %u COMB %u BLOOP %u); kicks %.0f .. %.0f, CAPSULE's %.0f", used[1], used[2], used[3], used[4], klo, khi, cap);
            bad += check(what, ok && klo > cap * 0.4 && khi < cap * 2.5);
            for (l = 0; l < CK_N; l++) {                                 /* every model on every lane: sounds, differs, bounded */
                apply_preset_to(&trk[0], 0);
                trk[0].p[P_E7] = 0;
                trk_note_on(&trk[0], NOTE[l], 110);
                ref[l] = out_rms(120);
                out_rms(900);
                for (mo = 1; mo < CKM_N; mo++) {
                    double r;
                    trk[0].p[P_LN4 + l] = (int16_t)mo;
                    trk_note_on(&trk[0], NOTE[l], 110);
                    r = out_rms(120);
                    out_rms(900);
                    quiet += r < ref[l] * 0.15;
                    wild += r > ref[l] * 6.0 || r > 9000.0;
                    same += r > ref[l] * 0.98 && r < ref[l] * 1.02;
                }
            }
            snprintf(what, sizeof what, "  every MODEL on every lane sounds (%u quiet), stays bounded (%u wild), is not NOISE's (%u alike)",
                     quiet, wild, same);
            bad += check(what, !quiet && !wild && !same);
            apply_preset_to(&trk[0], 0);
            trk[0].p[P_LN4] = 127;                                       /* an older sound: the unused lane levels */
            ok = param_fit(track_desc(&trk[0], P_LN4), 127) == 0 && param_fit(track_desc(&trk[0], P_LN4), 3) == 3 &&
                 ck_model(trk[0].p, CK_KICK) == CKM_NOISE && str_eq(track_desc(&trk[0], P_LN5)->label, "SD MODL");
            bad += check("  an older kit sound (127 in the unused lane levels) loads and plays its lanes as NOISE; MODEL's labels", ok);
            {   /* 8c. 0.9.12: real kits, five all FM (GENESIS's way), five all CHIP (SID's way): their kick low (40..90 Hz
                 * at their strongest), the snare and the open hat ringing longer than ... the closed hat, kicks near CAPSULE's */
                static const char *const RN[10] = {"FM ROCK", "FM JAZZ", "FM FUNK", "FM EIGHT", "FM NINE", "SID ROCK", "SID EIGHT",
                                                   "SID NINE", "SID JAZZ", "SID BREAKS"};
                uint32_t bad_m = 0, bad_f = 0, bad_o = 0;
                double rlo = 1e9, rhi = 0;
                ok = 1;
                for (k = 0; k < 10u; k++) {
                    double ch, oh, z;
                    uint32_t want = k < 5u ? CKM_FM : CKM_CHIP;
                    ok &= str_eq(ENG_CKIT.presets[30u + k].name, RN[k]) && preset_cat(ENGI_CKIT, 30u + k) == CAT_DRUM;
                    apply_preset_to(&trk[0], 30u + k);
                    trk[0].p[P_E7] = 0;
                    for (l = 0; l < CK_N; l++)
                        bad_m += ck_model(trk[0].p, l) != want;
                    trk_note_on(&trk[0], 36, 120);
                    lv[0] = out_rms(150);
                    out_rms(900);
                    trk_note_on(&trk[0], 36, 120);
                    z = out_peak_hz(120, 30.0, 150.0);           /* (the kick's body, over its first 120 ms) */
                    out_rms(900);
                    rlo = lv[0] < rlo ? lv[0] : rlo;
                    rhi = lv[0] > rhi ? lv[0] : rhi;
                    bad_f += z < 40.0 || z > 90.0;
                    trk_note_on(&trk[0], 42, 110);
                    out_rms(60);
                    ch = out_rms(60);
                    out_rms(900);
                    trk_note_on(&trk[0], 46, 110);
                    out_rms(60);
                    oh = out_rms(60);
                    out_rms(1500);
                    bad_o += oh < ch * 1.5;
                }
                snprintf(what, sizeof what, "  ten real kits (FM ROCK .. SID BREAKS): all FM / all CHIP (%u off), kicks 40..90 Hz at their strongest (%u off), "
                         "the open hat rings on (%u not); kicks %.0f .. %.0f", bad_m, bad_f, bad_o, rlo, rhi);
                bad += check(what, ok && !bad_m && !bad_f && !bad_o && rlo > cap * 0.4 && rhi < cap * 2.5);
            }
            trk[0].p[P_LN4] = 0;
        }
        trk[0].p[P_LN7] = 0;
        trk_note_on(&trk[0], 42, 120);                                   /* CH, then OH: one HAT voice (the choke) */
        out_rms(20);
        trk_note_on(&trk[0], 46, 120);
        for (i = 0, k = 0; i < NVOICE; i++)
            k += trk[0].v[i].active && ((uint32_t)trk[0].v[i].s[0] & 3u) == CK_HAT;
        bad += check("  the open hat chokes the closed one (one HAT voice)", k == 1u);
        out_rms(1200);
        ui_power_on();                                                   /* Phantom Tracker: GENESIS ANALOG SID */
        track_select(1);
        go_home();
        frames(50);
        {
            static const uint32_t E[3] = {ENGI_GENESIS, 0u, ENGI_SID};
            uint32_t k;
            ok = 1;
            for (k = 0; k < 3u; k++) {
                set_engine_of(&trk[1], E[k]);
                frames(20);
                apply_preset_to(&trk[1], 0);
                turn(EN_PRESET, 1);
                frames(20);
                ok &= trk[1].eng_req == E[k] && trk[1].preset == 1u && eng_ok(E[k]);
            }
        }
        bad += check("GENESIS, ANALOG and SID back (Phantom Tracker): on HOME PRESETS steps their sounds", ok);
        {   /* 0.9.14 (Ed, on the FM-1: "a large chunk of the presets just return static"): a GENESIS preset loads its
             * operators (GEN_OPS) on every path; without them every operator sat at LVL 127 and the chains were noise */
            uint32_t k, o, f, opsok = 1, worst = 0;
            double wb = 0, bb, lo = 1e9, r;
            ui_power_on();
            song.playing = 0;
            song.master_q12 = 4096;
            track_select(1);
            go_home();
            frames(50);
            set_engine_of(&trk[1], ENGI_GENESIS);
            for (o = 0; o < 4u; o++)
                for (f = 0; f < 5u; f++)
                    opsok &= trk[1].p[P_FM1_ATK + 5u * o + f] == GEN_OPS[0][o][f];
            bad += check("  GENESIS chosen: its first preset's operators (GEN_OPS) loaded", opsok);
            opsok = 1;
            for (k = 0; k < ENG_GENESIS.npresets; k++) {
                apply_preset_to(&trk[1], k);
                frames(20);
                out_rms(30);                                             /* (the audio's side of the load: its fade) */
                for (o = 0; o < 4u; o++)
                    for (f = 0; f < 5u; f++)
                        opsok &= trk[1].p[P_FM1_ATK + 5u * o + f] == GEN_OPS[k][o][f];
                trk_note_on(&trk[1], 57, 100);
                r = out_rms(40);
                bb = out_bright(300);
                if (r < lo) lo = r;
                trk_note_off(&trk[1], 57);
                out_rms(1500);
                if (bb > wb) { wb = bb; worst = k; }
            }
            bad += check("  every GENESIS preset loads its own operators (GEN_OPS)", opsok);
            snprintf(what, sizeof what, "  every GENESIS preset sounds (rms >= %.0f) and is a tone, not static (the brightest %s %.2f; noise ~1.2-2)",
                     lo, ENG_GENESIS.presets[worst].name, wb);
            bad += check(what, wb < 0.7 && lo > 100.0);   /* (0.9.15: the FEEDs 4 dB trimmed, MIX_TRIM) */
        }

        ui_power_on();                                                   /* DEBRIS's tapes, FEED 2 */
        song.playing = 0;
        song.master_q12 = 4096;
        track_select(1);
        go_home();
        frames(50);
        trk[1].p[P_E0] = SIG_N;                         /* (0.10.4: SIG is the EDIT page's: the tape set there) */
        frames(20);
        ui.force = 1;
        frame();
        ok = trk[1].p[P_E0] == SIG_N && str_eq(sig_name(ENGI_DEBRIS, SIG_N), "GROAN") && str_eq(sig_name(ENGI_DEBRIS, SIG_N + 3), "SKYWAVE") &&
             sig_max(ENGI_MORPH) == SIG_N - 1u;
        trk_note_on(&trk[1], 48, 100);
        smp = out_rms(400);
        trk_note_off(&trk[1], 48);
        out_rms(1500);
        snprintf(what, sizeof what, "DEBRIS: SIG 17 GROAN (a tape), it sounds (rms %.0f); MORPH stops at 16", smp);
        bad += check(what, ok && smp > 200.0);
    }

    /* 0.9.15 the levels (Ed: the drums "very quiet in almost every mix", PHANTOM "way too loud"): in the transmission at
     * the default levels, the drums alone over the bass alone and the lead alone (kit x2, the rest 4 dB trimmed: they sat
     * 2 dB under them before); PHANTOM's held note under the bed */
    {
        static const int KITS[3] = {-1, 30, 35};       /* (the power-on kit, FM ROCK, SID ROCK) */
        double r[4], worst = 99.0, ph, bed;
        uint32_t j, k, q;
        for (j = 0; j < 3u; j++) {
            for (k = 0; k < 4u; k++) {
                ui_power_on();
                transmission();
                song.master_q12 = 4096;
                if (KITS[j] >= 0)
                    apply_preset_to(&trk[0], (uint32_t)KITS[j]);
                for (q = 0; q < NTRK; q++)
                    if (q != k)
                        trk[q].p[P_LEVEL] = 0;
                seq_start(); song.playing = 1;
                r[k] = 20 * log10(out_rms(6000) + 1e-9);
                song.playing = 0; seq_stop();
            }
            if (r[0] - r[2] < worst) worst = r[0] - r[2];
            if (r[0] - r[3] < worst) worst = r[0] - r[3];
        }
        snprintf(what, sizeof what, "levels: the drums alone over the bass and the lead alone in the transmission (by %.1f dB at "
                 "least, three kits)", worst);
        bad += check(what, worst > 2.0);
        ui_power_on();
        phantom_init();                                /* (PHANTOM's power-on sound: earlier checks moved it) */
        transmission();
        song.master_q12 = 4096;
        seq_start(); song.playing = 1;
        bed = 20 * log10(out_rms(4000) + 1e-9);
        for (q = 0; q < NTRK; q++)
            trk[q].p[P_LEVEL] = 0;
        ph_keys = 1;
        ph_key_on(0, 62);
        out_rms(300);
        ph = 20 * log10(out_rms(3000) + 1e-9);
        ph_keys = 0;
        song.playing = 0; seq_stop();
        snprintf(what, sizeof what, "  PHANTOM alone (LEVEL %d) %.1f dB, under the transmission's %.1f", ph_trk.p[P_LEVEL], ph, bed);
        printf("%s (Spelunkr: PHANTOM scrapped, not checked)\n", what);
    }

    printf("finish: %s\n", bad ? "FAILED" : "all checks ok");
    return bad != 0;
}
