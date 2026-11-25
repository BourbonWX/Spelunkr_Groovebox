/* SPDX-License-Identifier: GPL-3.0-only
 * Phantom Cosmonaut M3: PHANTOM, the jam (spec section 4). The FEEDs loop what you built; PHANTOM is what you play over
 * them on the 27 keys, live, and nothing of it is ever recorded: its notes go into no FEED, no pattern, no song, even
 * with REC armed. Its sound is kept with the project (proj: phantom_pack, 5 bytes).
 *
 * One voice of its own, outside the FEEDs' tracks and their voice budget (it never takes a FEED's voice, never stops
 * the loop), a theremin by default:
 *   - monophonic and legato: a key held over the last one slides there with no new attack; let go, it slides back to
 *     the one still held; the last key let go releases it. A new phrase glides from wherever the last one ended
 *   - GLIDE always on (its time, 0 a jump .. ~0.5 s), a soft attack (80 ms) and release (~0.35 s), not velocity-led
 *   - VIBRATO (its depth, to +-0.6 semitone at 5.5 Hz) creeping in from 0.25 s after a fresh attack, full by ~0.9 s
 *   - the MORPH engine on a SIGNAL of its own (PRESETS while it is focused), shaped by the same contort knobs: SCAN WARP
 *     FOLD CLARITY on its first page, GLIDE VIBRATO SUB SIG on its second (EDIT flips them)
 * Its sound lives in ph_trk (a track_t outside trk[], so no FEED's arrays see it): MORPH's eight values in P_E0..P_E7,
 * GLIDE in P_GLIDE, VIBRATO in P_LD_PIT (slots this voice reads itself), the send levels as a track's. Rendered by the
 * audio ISR after the parts, into the dry mix and the delay and reverb sends (fx.c mix_block, as the fanfare).
 * The keys: seq.c keyboard_block hands them here (ph_key_on / ph_key_off) while ph_keys: PHANTOM focused on ALGORITHM
 * (ui_input.c; Ed, Nov 2026: otherwise they play the selected FEED). Its pitch follows the focused FEED's scale keyboard (QNT ROOT SCALE TRN). */
#ifndef PC_PHANTOM_C
#define PC_PHANTOM_C 1

static track_t ph_trk;                                 /* PHANTOM's sound and its one voice (v[0]) */
/* the Spelunkr Groovebox (web, SPK_NU): PHANTOM is LEAD (Ed, Nov 24: "broaden that track and engine"): recorded into the
 * song (lead.c: a lane of notes a pattern, played back through the keys 27.. here) and more sounds than MORPH's
 * SIGNALs (FM's, SAMPLE's, GENESIS's presets: lead.c lead_sound). The FM-1's PHANTOM is unchanged (SPK_NU 0) */
#define PH_LIVE 27u                                    /* the keys 0..26 are the player's; 27.. the recorded lane's */
#if SPK_NU
#define PH_NKEY (PH_LIVE + 13u)
static void lead_rec_key(uint32_t note, int on);       /* lead.c */
static void lead_tick(void);
static uint32_t lead_snd;                              /* LEAD's sound: 0..SIG_N - 1 MORPH's SIGNALs, then lead.c's list */
static uint8_t lead_ulast;                             /* the last undoable thing was LEAD's (lead.c; ui.c load_begin clears) */
static int lead_theremin(void);                        /* MORPH: the theremin's envelope and legato; else lead.c's ADSR */
static int lead_recording(void);                       /* REC armed on LEAD, playing: its keys go into the lane */
#else
#define PH_NKEY PH_LIVE
#endif
static volatile uint8_t ph_keys;                       /* main: the keys play PHANTOM (= ph_focus, ui_input.c) */
static uint8_t ph_focus;                               /* main: ALGORITHM's fifth stop: KNOB 1..4, PRESETS on PHANTOM */
static uint8_t ph_page;                                /* main: its knobs: 0 SCAN WARP FOLD CLARITY, 1 GLIDE VIBRATO SUB LEVEL
                                                        * (0.9.15: LEVEL in SIG's place; PRESETS still steps SIG) */
static struct {
    uint8_t stack[8], n;                               /* ISR: the keys held, the newest last */
    uint8_t gate, started;                             /* a key held; a note ever played (the glide's start) */
    int32_t env;                                       /* Q15 */
    int32_t cur, tgt;                                  /* the pitch, Q8 of a 1/16 semitone */
    uint32_t vph;                                      /* the vibrato's phase */
    uint32_t held;                                     /* blocks since the fresh attack (the vibrato's delay) */
    uint8_t key[PH_NKEY];                              /* the note each key started, 0 none */
    uint8_t stage;                                     /* (LEAD, not the theremin: 0 attack, 1 decay / sustain) */
} ph;
static int32_t ph_buf[CTL];

#define PH_GLIDE P_GLIDE                               /* (ph_trk's slots this voice reads itself) */
#define PH_VIB P_LD_PIT
#define PH_ATK_STEP 298                                /* Q15 a block: 80 ms to full */
#define PH_VIB_INC 17140000u                           /* 5.5 Hz a block (2^32 / 1378.1 blocks a second x 5.5) */
#define PH_VIB_DELAY 345u                              /* blocks: 0.25 s */
#define PH_VIB_RISE 900u                               /* blocks: then 0.65 s to full */

static const param_desc_t PH_GLIDE_D = {"GLIDE", F_PCT, 0, 127, 56, 0, 0};
static const param_desc_t PH_VIB_D = {"VIBRATO", F_PCT, 0, 127, 40, 0, 0};
#ifndef PH_LEVEL_DEF
#define PH_LEVEL_DEF 86                                /* 0.9.15 (Ed: "the phantom theremin track is way too loud, it over
                                                        * takes everything"): 10 dB under 0.9.14's 106, and the FEEDs' 4 dB
                                                        * trim (fx.c MIX_TRIM): ~2 dB under the default song's bed */
#endif
static const param_desc_t PH_LEVEL_D = {"LEVEL", F_DB, 0, 127, PH_LEVEL_DEF, 0, 0};

/* the signal k's values (MORPH's factory sound k: SIG, SCAN .. SUB), leaving GLIDE, VIBRATO and the envelope alone */
static void ph_signal(uint32_t k)
{
    const preset_t *pr = &ENG_MORPH.presets[k % SIG_N];
    uint32_t i;
    for (i = 0; i < 8u; i++)
        ph_trk.p[P_E0 + i] = pr->e[i];
    ph_trk.eng_req = (uint8_t)ENGI_MORPH;               /* (the ISR switches: phantom_render) */
}
/* PRESETS on PHANTOM: the 16 SIGNALs; its sound's place on that line, its name */
#if SPK_NU
static uint32_t ph_voice_at(void);                     /* lead.c: LEAD's whole list */
static void ph_voice(uint32_t k);
static const char *ph_voice_name(void);
static uint32_t ph_voice_n(void);
#else
static uint32_t ph_voice_at(void) { return (uint32_t)clamp(ph_trk.p[P_E0], 0, SIG_N - 1); }
static void ph_voice(uint32_t k) { ph_signal(k); }
static const char *ph_voice_name(void) { return SIG_NAME[clamp(ph_trk.p[P_E0], 0, SIG_N - 1)]; }
static uint32_t ph_voice_n(void) { return SIG_N; }
#endif
/* power-on and a project without PHANTOM: the theremin. CARRIER near its sine, the low-pass a little closed */
static void phantom_init(void)
{
    uint32_t i;
    memset(&ph_trk, 0, sizeof ph_trk);
    memset(&ph, 0, sizeof ph);
    ph_focus = ph_page = 0;
    for (i = 0; i < P_E0; i++)
        ph_trk.p[i] = TP[i].def;
    ph_trk.eng_req = ph_trk.engine = (uint8_t)ENGI_MORPH;
    ph_signal(0);
    ph_trk.p[P_E1] = 6;                                /* SCAN: barely past the sine */
    ph_trk.p[P_E4] = -18;                              /* CLARITY: soft */
    ph_trk.p[P_E5] = 10;
    ph_trk.p[P_E6] = 0;
    ph_trk.p[PH_GLIDE] = PH_GLIDE_D.def;
    ph_trk.p[PH_VIB] = PH_VIB_D.def;
    ph_trk.p[P_LEVEL] = PH_LEVEL_D.def;               /* (0.9.15: with the FEEDs, not over them; EDIT's KNOB 4 turns it) */
    ph_trk.p[P_REV] = 50;                              /* airy */
    ph_trk.p[P_DLY] = 24;
    ph_trk.p[P_CHOR] = 0;
}

/* ISR (keyboard_block): a key down, its note; a key up */
static void phantom_note(uint32_t note)
{
    ph.tgt = (int32_t)note * 16 * 256;
    if (!ph.started) {                                 /* the very first note: no slide in */
        ph.cur = ph.tgt;
        ph.started = 1;
    }
#if SPK_NU
    if (!lead_theremin()) {                            /* LEAD's other sounds: every key a new note (gliding there if
                                                        * GLIDE is up), its sample or operators struck again */
        voice_t *v = &ph_trk.v[0];
        v->note = (uint8_t)note;
        v->vel = 100;
        v->pitch16 = v->pitch_cur = (int16_t)clamp((ph.cur >> 8), 0, 2047);
        v->gate = 1;
        ph_trk.engine = ph_trk.eng_req;
        ENGINES[eng_idx(ph_trk.engine)]->note_on(&ph_trk, v);
        v->active = 1;
        ph.gate = 1;
        ph.held = 0;
        ph.stage = 0;
        return;
    }
#endif
    if (!ph.gate) {                                    /* a fresh phrase: attack from where the last one faded to */
        if (!ph_trk.v[0].active || ph.env == 0) {
            ph_trk.engine = ph_trk.eng_req;
            ENGINES[eng_idx(ph_trk.engine)]->note_on(&ph_trk, &ph_trk.v[0]);
        }
        ph_trk.v[0].active = 1;
        ph.gate = 1;
        ph.held = 0;
    }
}
/* MENU > MIDI > PHANTOM CH: the channel 0..15 its keys go out on to MIDI OUT (seq.c ph_midi_on), 16 OFF; the UI
 * keeps it as the setting says (menu_items.c ph_mch, every pass) */
static uint8_t ph_och = 5u;                            /* (0.10: channel 6; FEED 5, PERC, is on 5) */
static void ph_key_on(uint32_t k, uint32_t note)
{
    uint32_t i, j = 0;
    if (k >= PH_NKEY)
        return;
#if SPK_NU
    if (k < PH_LIVE)
        lead_rec_key(note, 1);                         /* (recording: into LEAD's lane) */
#endif
    if (!ph.n && k < PH_LIVE) {                         /* M6: a PHANTOM accent (a phrase's first note, not a legato
                                                         * slide): the screen breaks up a moment, HOME's starfield
                                                         * sends a shockwave (gfx.c, ui_graph.c graph_stars) */
        jit_kick(1u);
        star_kick();
    }
    ph.key[k] = (uint8_t)(note + 1u);
    for (i = 0; i < ph.n; i++)                         /* (the same note again: to the top) */
        if (ph.stack[i] != note)
            ph.stack[j++] = ph.stack[i];
    ph.n = (uint8_t)j;
    if (ph.n >= 8u) {
        for (i = 1; i < 8u; i++)
            ph.stack[i - 1u] = ph.stack[i];
        ph.n = 7;
    }
    ph.stack[ph.n++] = (uint8_t)note;
    phantom_note(note);
}
static void ph_key_off(uint32_t k)
{
    uint32_t i, j = 0, note;
    if (k >= PH_NKEY || !ph.key[k])
        return;
    note = ph.key[k] - 1u;
    ph.key[k] = 0;
#if SPK_NU
    if (k < PH_LIVE)
        lead_rec_key(note, 0);
#endif
    for (i = 0; i < PH_NKEY; i++)                      /* (another key still holds the same note) */
        if (ph.key[i] == note + 1u)
            return;
    for (i = 0; i < ph.n; i++)
        if (ph.stack[i] != note)
            ph.stack[j++] = ph.stack[i];
    ph.n = (uint8_t)j;
    if (ph.n)
        ph.tgt = (int32_t)ph.stack[ph.n - 1u] * 16 * 256;   /* legato back to the one still held */
    else {
        ph.gate = 0;                                   /* the release */
        ph_trk.v[0].gate = 0;                          /* (an engine with its own envelopes: they release too) */
    }
}
static void ph_all_off(void)                           /* ISR: every key let go at once (power, a layer) */
{
    memset(ph.key, 0, sizeof ph.key);
    ph.n = 0;
    ph.gate = 0;
    ph_trk.v[0].gate = 0;
}

/* ISR (fx.c mix_block): n samples into the dry mix and the delay and reverb sends */
static void phantom_render(int32_t *l, int32_t *r, int32_t *sd, int32_t *sv, uint32_t n)
{
    voice_t *v = &ph_trk.v[0];
    vmod_t m;
    uint32_t i;
    int32_t e0 = ph.env, g, p, vib, fine, lvl, d, rv, tune, tune_fine;
    if (!v->active)
        return;
#if SPK_NU
    if (!lead_theremin()) {                            /* LEAD's other sounds: the sound's own ATTACK DECAY SUSTAIN
                                                        * RELEASE (an engine with its own envelopes: held full, they shape it) */
        const engine_t *en = ENGINES[eng_idx(ph_trk.eng_req)];
        int32_t a = clamp(ph_trk.p[P_ATK], 0, 127), dc = clamp(ph_trk.p[P_DEC], 0, 127), su = clamp(ph_trk.p[P_SUS], 0, 127) * 258,
                rl = clamp(ph_trk.p[P_REL], 0, 127);
        if (en->ownenv) {
            a = dc = 0;
            su = 32767;
        }
        if (ph.gate && ph.stage == 0) {
            ph.env += a ? 32767 / (1 + a * a / 8) + 1 : 32767;
            if (ph.env >= 32767) {
                ph.env = 32767;
                ph.stage = 1;
            }
        } else if (ph.gate) {
            ph.env -= (ph.env - su) / (1 + dc * dc / 16);
        } else {
            int32_t f = ph.env / (1 + rl * rl / 32) + 2;
            ph.env = ph.env > f ? ph.env - f : 0;
        }
    } else
#endif
    if (ph.gate)                                       /* the envelope: a soft linear rise, a long fall */
        ph.env = ph.env + PH_ATK_STEP < 32767 ? ph.env + PH_ATK_STEP : 32767;
    else
        ph.env = ph.env - (ph.env >> 7) - 4 > 0 ? ph.env - (ph.env >> 7) - 4 : 0;
    if (!ph.gate && !ph.env && !e0) {
        v->active = 0;
        return;
    }
    if (ph_trk.engine != ph_trk.eng_req) {               /* (an engine change: the new one from rest) */
        ph_trk.engine = ph_trk.eng_req;
        ENGINES[eng_idx(ph_trk.engine)]->note_on(&ph_trk, v);
    }
    g = clamp(ph_trk.p[PH_GLIDE], 0, 127);             /* GLIDE: a time constant of 1 .. ~670 blocks */
    {
        int32_t tau = 1 + g * g / 24, dd = ph.tgt - ph.cur, step = (int32_t)(((int64_t)dd * (65536 / tau)) >> 16);
        if (!step && dd)
            step = dd > 0 ? 1 : -1;
        ph.cur += step;
    }
    if (ph.gate && ph.held < 0xFFFFu)
        ph.held++;
    {   /* VIBRATO: depth to ~10 1/16 semitones, after its delay, rising */
        int32_t depth = clamp(ph_trk.p[PH_VIB], 0, 127) * 20, fade;
        fade = ph.held <= PH_VIB_DELAY ? 0 : ph.held >= PH_VIB_DELAY + PH_VIB_RISE ? 32767
             : (int32_t)((ph.held - PH_VIB_DELAY) * 32767u / PH_VIB_RISE);
        ph.vph += PH_VIB_INC;
        vib = (int32_t)(((int64_t)sine_i(ph.vph) * depth >> 15) * fade >> 15);   /* Q8 of a 1/16 semitone */
    }
    tune = song.g[G_TUNE] >= 0 ? song.g[G_TUNE] * 16 / 100 : -((-song.g[G_TUNE] * 16 + 99) / 100);
    tune_fine = (song.g[G_TUNE] * 16 - tune * 100) * 2367 / 16000;
    p = ph.cur + vib;
    m.pitch16 = clamp((p >> 8) + tune, 0, 2047);
    m.inc = pitch_inc((uint32_t)m.pitch16);
    fine = (p & 255) * 2367 / 40960 + tune_fine;       /* (the rest below 1/16 semitone, 1/4096) */
    m.fine = fine;
    if (fine)
        m.inc += (uint32_t)((int32_t)(m.inc >> 12) * fine);
    m.amp0 = e0;
    m.amp1 = ph.env;
    m.envq15 = ph.env;
    m.cutoff = 0;
    m.shape = 64 << 8;
    v->pitch_cur = m.pitch16;
#if SPK_NU
    v->pitch16 = m.pitch16;
#endif
    for (i = 0; i < n; i++)
        ph_buf[i] = 0;
    ENGINES[eng_idx(ph_trk.engine)]->render(&ph_trk, v, ph_buf, n, &m);
    POLISH_TRACK(NTRK, ph_buf, 0, n);                   /* (LEAD: the page's POLISH compressor) */
    lvl = (LEVEL_Q12[ph_trk.p[P_LEVEL] & 127] * MIX_TRIM) >> 12;   /* (0.9.15: as a FEED's, fx.c feed_lvl) */
    d = ph_trk.p[P_DLY] * 258;
    rv = ph_trk.p[P_REV] * 258;
    {
        int32_t xmax = 0x7FFFFFFF / ((d > rv ? d : rv) | 1);   /* sends: mulq15 would overflow (as mix_part's) */
        int32_t pk = ph_trk.peak;
        for (i = 0; i < n; i++) {
            int32_t x = ((ph_buf[i] >> 2) * lvl) >> 10, xs = clamp(x, -xmax, xmax), a = x < 0 ? -x : x;
            if (a > pk)
                pk = a;                                 /* (0.10: the strip's PHANTOM meter) */
            l[i] += x;
            r[i] += x;
            if (d)
                sd[i] += mulq15(xs, d);
            if (rv)
                sv[i] += mulq15(xs, rv);
        }
        ph_trk.peak = pk;
    }
}

/* the project's 5 bytes (project.c PROJ_PH_OFF): b[0] 0x80 (a PHANTOM saved: a byte every project before left 0) | SIG
 * (4 bits) | SUB / 18 (3 bits, 0..6) << 4; b[1..4] LE: SCAN WARP FOLD CLARITY + 64 (6 bits each, the value / 2), GLIDE
 * and VIBRATO (4 bits each, / 8). Speaking: SUB's field 7 (SUB 126 and 127 were 7 before: such a project loads
 * speaking; 0.9.3's singing loads speaking too), the phrase in SIG's; RADIO, SPEED + 64, HUMAN, TAIL 5 bits each
 * (/ 4), DROP 2 bits (/ 42), then GLIDE and VIBRATO as MORPH's */
static uint32_t ph_q2(int32_t v)                       /* 0..127 to the nearest of 0 42 85 127 (compares: a shift and
                                                         * mask here compiles to 0xFFC00000, the ROM's address: build.py) */
{
    return v >= 106 ? 3u : v >= 64 ? 2u : v >= 21 ? 1u : 0u;
}
static void phantom_pack(uint8_t *b)
{
    const int16_t *p = ph_trk.p;
    uint32_t v;
    {
        v = (uint32_t)(clamp(p[P_E1], 0, 127) >> 1) | (uint32_t)(clamp(p[P_E2], 0, 127) >> 1) << 6 |
            (uint32_t)(clamp(p[P_E3], 0, 127) >> 1) << 12 | (uint32_t)(clamp(p[P_E4] + 64, 0, 127) >> 1) << 18;
        b[0] = (uint8_t)(0x80u | (uint32_t)clamp(p[P_E0], 0, 15) | (uint32_t)clamp(clamp(p[P_E7], 0, 127) / 18, 0, 6) << 4);
    }
    v |= (uint32_t)(clamp(p[PH_GLIDE], 0, 127) >> 3) << 24 | (uint32_t)(clamp(p[PH_VIB], 0, 127) >> 3) << 28;
    b[1] = (uint8_t)v; b[2] = (uint8_t)(v >> 8); b[3] = (uint8_t)(v >> 16); b[4] = (uint8_t)(v >> 24);
}
/* its LEVEL in the project (project.c PROJ_PHLVL_OFF, 0.9.15): LEVEL + 1; 0 (a project before): the default */
static uint8_t phantom_pack_level(void) { return (uint8_t)(clamp(ph_trk.p[P_LEVEL], 0, 127) + 1); }
static void phantom_unpack_level(uint8_t b) { ph_trk.p[P_LEVEL] = (int16_t)(b ? (int32_t)b - 1 : (int32_t)PH_LEVEL_D.def); }
/* 1: a PHANTOM was saved and is now the sound (its SIGNAL's values, then the saved ones); 0: none, nothing changed */
static int phantom_unpack(const uint8_t *b)
{
    uint32_t v = (uint32_t)b[1] | (uint32_t)b[2] << 8 | (uint32_t)b[3] << 16 | (uint32_t)b[4] << 24;
    if (!(b[0] & 0x80u))
        return 0;
    if ((b[0] & 0x70u) == 0x70u) {                      /* a 0.9.4 project's speaking PHANTOM: the theremin's SIGNAL */
        ph_signal(0);
        ph_trk.p[PH_GLIDE] = (int16_t)(((v >> 24) & 15u) << 3);
        ph_trk.p[PH_VIB] = (int16_t)(((v >> 28) & 15u) << 3);
        return 1;
    }
    ph_signal(b[0] & 15u);
    ph_trk.p[P_E7] = (int16_t)(((b[0] >> 4) & 7u) * 18u);
    ph_trk.p[P_E1] = (int16_t)((v & 63u) << 1);
    ph_trk.p[P_E2] = (int16_t)(((v >> 6) & 63u) << 1);
    ph_trk.p[P_E3] = (int16_t)(((v >> 12) & 63u) << 1);
    ph_trk.p[P_E4] = (int16_t)((int32_t)(((v >> 18) & 63u) << 1) - 64);
    ph_trk.p[PH_GLIDE] = (int16_t)(((v >> 24) & 15u) << 3);
    ph_trk.p[PH_VIB] = (int16_t)(((v >> 28) & 15u) << 3);
    return 1;
}

/* main: PHANTOM's knob k on its page: the value it shows and turns, its descriptor */
static const param_desc_t *ph_param(uint32_t k, int16_t **vp)
{
    static const uint8_t ID[2][4] = {{P_E1, P_E2, P_E3, P_E4}, {PH_GLIDE, PH_VIB, P_E7, P_LEVEL}};
    uint32_t id = ID[ph_page & 1u][k & 3u];
#if SPK_NU
    if (ph_trk.eng_req != ENGI_MORPH) {               /* LEAD's other sounds: the engine's own HOME knobs, then GLIDE
                                                        * VIBRATO REVERB LEVEL */
        const engine_t *en = ENGINES[eng_idx(ph_trk.eng_req)];
        id = ph_page & 1u ? (k & 3u) == 2u ? (uint32_t)P_REV : ID[1][k & 3u] : en->knob[k & 3u];
        *vp = &ph_trk.p[id];
        return id == PH_GLIDE ? &PH_GLIDE_D : id == PH_VIB ? &PH_VIB_D : id == P_LEVEL ? &PH_LEVEL_D
             : id >= P_E0 && id < P_E0 + 8u ? &en->edit[id - P_E0] : &TP[id];
    }
#endif
    *vp = &ph_trk.p[id];
    return id == PH_GLIDE ? &PH_GLIDE_D : id == PH_VIB ? &PH_VIB_D : id == P_LEVEL ? &PH_LEVEL_D : &ENG_MORPH.edit[id - P_E0];
}
#endif
