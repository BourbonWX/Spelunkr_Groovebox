/* SPDX-License-Identifier: GPL-3.0-only
 * The Spelunkr Groovebox (the web build and the box, SPK_NU): LEAD, Phantom Tracker's PHANTOM (phantom.c) grown past
 * the FM-1 (Ed, Nov 24 2026: "I love the way that voice sounds on top of the styles of music this program creates ...
 * broaden that track and engine"; his picks: record it, more sounds).
 *
 * RECORDED. LEAD focused (TRACK past the last track) with REC armed while the music plays: what the keys play on it goes
 * into a lane of notes, one lane a pattern (A..F; in a SONG, the pattern its row names first), as times in 1/16 of a
 * step of the clock track (song_bank.c bank.clk, the pattern's loop), so swing and tempo carry it. Takes overdub; a
 * note held over a loop's end or a pattern's change is closed where it was played. Played back by lead_tick (the ISR,
 * after the tracks) through PHANTOM's keys 27.. (phantom.c PH_LIVE), whether LEAD is focused or not, under the player's
 * own keys. CLEAR (web_lead_clear: the row tools' CLEAR with LEAD focused) empties this pattern's lane; UNDO (web_undo)
 * brings back the lanes as they were before the last take or clear. A NEW / RANDOM song clears every lane.
 * Kept in the storage object OBJ_LEAD (storage.c: the web build's flash at 0xA0000, the FM-1's user sample slot 0 that
 * the web never writes): its sound and its lanes; the page's SAVE file (the flash) and song codes (worklet.js id 7)
 * carry it, the bounce clones it with the device.
 *
 * MORE SOUNDS. LEAD's list (SOUNDS / PRESETS on it): MORPH's 16 SIGNALs (the theremin: legato, its own soft envelope,
 * as PHANTOM always was), then FM's, SAMPLE's and GENESIS's factory sounds (lead_list). Those play a new note each key
 * (gliding there with GLIDE up), on the sound's own ATTACK DECAY SUSTAIN RELEASE (GENESIS: its operators' envelopes),
 * with VIBRATO; the knobs: the engine's own four, then GLIDE VIBRATO REVERB LEVEL (phantom.c ph_param). */
#ifndef SPK_LEAD_C
#define SPK_LEAD_C 1
#if SPK_NU

#define LEAD_PATS NBANK                                 /* A..F */
#define LEAD_PER 150u                                   /* notes on + off, a pattern (6 x 150 x 4 bytes: the object) */
#define LEAD_SUB 16                                     /* a step's sixteenths */
#define LEAD_MAGIC 0x4441454Cu                          /* "LEAD" */
#define LEAD_HDR 48u
typedef struct { uint16_t t; uint8_t note, on; } lead_ev_t;
static lead_ev_t lead_ev[LEAD_PATS][LEAD_PER], lead_uev[LEAD_PATS][LEAD_PER];
static uint16_t lead_n[LEAD_PATS], lead_un[LEAD_PATS];
static uint8_t lead_uok;                                /* an undo copy (lead_ulast: phantom.c) */
static volatile uint8_t lead_dirty;                     /* (ISR or main) the object to save */
static uint32_t lead_dirty_ms;
static int32_t lead_prev = -1;                          /* ISR: the clock's position after the last block, -1 none */
static uint8_t lead_pat_now = 0xFFu, lead_take;         /* ISR: the lane playing; a take running */
static uint8_t lead_held[128];                          /* ISR: a recorded note held: its lane + 1 */
static uint8_t lead_pl[PH_NKEY - PH_LIVE];              /* ISR: the playback keys' notes + 1 */

/* ---- the sounds */
static const uint8_t LEAD_ENG[] = {ENGI_PFM, ENGI_SAMPLE, ENGI_GENESIS};
static uint32_t lead_list_n(uint32_t i) { return ENGINES[LEAD_ENG[i]]->npresets; }
static uint32_t ph_voice_n(void)
{
    uint32_t n = SIG_N, i;
    for (i = 0; i < NELEM(LEAD_ENG); i++)
        n += lead_list_n(i);
    return n;
}
/* sound k of the list: its engine and preset (MORPH: the SIGNAL) */
static void lead_of(uint32_t k, uint32_t *eng, uint32_t *pi)
{
    uint32_t i;
    if (k < SIG_N) {
        *eng = ENGI_MORPH;
        *pi = k;
        return;
    }
    k -= SIG_N;
    for (i = 0; i < NELEM(LEAD_ENG); i++) {
        if (k < lead_list_n(i)) {
            *eng = LEAD_ENG[i];
            *pi = k;
            return;
        }
        k -= lead_list_n(i);
    }
    *eng = ENGI_MORPH;
    *pi = 0;
}
static int lead_theremin(void) { return ph_trk.eng_req == ENGI_MORPH; }
static uint32_t ph_voice_at(void) { return lead_snd < ph_voice_n() ? lead_snd : 0u; }
static const char *ph_voice_name(void)
{
    uint32_t e, pi;
    lead_of(ph_voice_at(), &e, &pi);
    return e == ENGI_MORPH ? SIG_NAME[clamp(ph_trk.p[P_E0], 0, SIG_N - 1)] : ENGINES[e]->presets[pi].name;
}
/* sound k: MORPH as PHANTOM's SIGNAL (GLIDE VIBRATO kept; from another sound: the theremin's); else the preset's values,
 * envelope and sends (GENESIS: its operators), GLIDE and VIBRATO off at first (a key, a bell, a sax) */
static void ph_voice(uint32_t k)
{
    uint32_t e, pi, i, was = ph_trk.eng_req;
    const preset_t *pr;
    k = k < ph_voice_n() ? k : 0u;
    lead_of(k, &e, &pi);
    fm1_irq_off();
    lead_snd = k;
    if (e == ENGI_MORPH) {
        ph_signal(pi);
        if (was != ENGI_MORPH) {
            ph_trk.p[PH_GLIDE] = PH_GLIDE_D.def;
            ph_trk.p[PH_VIB] = PH_VIB_D.def;
            ph_trk.p[P_REV] = 50;
            ph_trk.p[P_DLY] = 24;
        }
    } else {
        static const uint8_t FX_DEF[4] = {0, 24, 28, 36};
        pr = &ENGINES[e]->presets[pi];
        for (i = 0; i < 8u; i++)
            ph_trk.p[P_E0 + i] = (int16_t)pr->e[i];
        ph_trk.p[P_ATK] = pr->env[0];
        ph_trk.p[P_DEC] = pr->env[1];
        ph_trk.p[P_SUS] = pr->env[2];
        ph_trk.p[P_REL] = pr->env[3];
        for (i = 0; i < 4u; i++)
            ph_trk.p[P_DIST + i] = (int16_t)(pr->fx[i] ? pr->fx[i] - 1 : FX_DEF[i]);
        if (e == ENGI_GENESIS)
            eng_own_values(ENGI_GENESIS, pi, ph_trk.p);
        if (was == ENGI_MORPH) {
            ph_trk.p[PH_GLIDE] = 0;
            ph_trk.p[PH_VIB] = 0;
        }
        ph_trk.eng_req = (uint8_t)e;
    }
    ph_trk.v[0].active = 0;                             /* (the next key starts the new sound from rest) */
    ph.env = 0;
    fm1_irq_on();
    lead_dirty = 1;
}

/* ---- the lanes */
static const track_t *lead_clk(void) { return &trk[bank.clk % NTRK]; }
static uint32_t lead_loop(void) { return (uint32_t)clamp(lead_clk()->p[P_SLEN], 1, NSTEP) * LEAD_SUB; }
static int32_t lead_pos(void)                           /* the clock track's place: its step x 16 + the sixteenth */
{
    const track_t *t = lead_clk();
    uint32_t len, sub;
    if (t->seq_pos >= 0x7FFFFFFFu)
        return -1;
    len = step_samples(t, div_samples((uint32_t)t->p[P_SDIV]), t->seq_idx);
    sub = len ? (uint32_t)((uint64_t)t->seq_pos * LEAD_SUB / len) : 0u;
    return (int32_t)(t->seq_idx * LEAD_SUB + (sub < LEAD_SUB ? sub : LEAD_SUB - 1u));
}
static uint32_t lead_pat(void)                          /* the lane playing: the pattern, or in a SONG its row's first */
{
    uint32_t k, v;
    if (chain.running) {
        if (chain.row >= chain_config.count)
            return LEAD_PATS;
        for (k = 0; k < NTRK; k++)
            if ((v = chain_pat(chain_config.row[chain.row].slot[FD_AT[k]])) < LEAD_PATS)
                return v;
        return LEAD_PATS;
    }
    return bank.cur < LEAD_PATS ? bank.cur : 0u;
}
static int lead_recording(void)
{
    return ph_focus && song.playing && ((song.rec >> (song.sel % NTRK)) & 1u) && !seq_counting();
}
static void lead_put(uint32_t p, uint32_t t, uint32_t note, int on)   /* (ISR) in time order, after the same time's */
{
    uint32_t i;
    lead_ev_t *L = lead_ev[p];
    if (p >= LEAD_PATS || lead_n[p] >= LEAD_PER)
        return;
    for (i = lead_n[p]; i && L[i - 1u].t > t; i--)
        L[i] = L[i - 1u];
    L[i].t = (uint16_t)t;
    L[i].note = (uint8_t)note;
    L[i].on = (uint8_t)(on != 0);
    lead_n[p]++;
    lead_dirty = 1;
}
static void lead_snap(void)                             /* the lanes as they are: UNDO's */
{
    memcpy(lead_uev, lead_ev, sizeof lead_ev);
    memcpy(lead_un, lead_n, sizeof lead_n);
    lead_uok = lead_ulast = 1;
    bank_ulast = 0;
}
static void lead_rec_end(void)                          /* (ISR) the take over: its notes still held closed here */
{
    uint32_t nt;
    int32_t pos = lead_pos();
    if (!lead_take)
        return;
    lead_take = 0;
    for (nt = 0; nt < 128u; nt++)
        if (lead_held[nt]) {
            uint32_t p = lead_held[nt] - 1u, lp = lead_loop();
            lead_put(p, pos >= 0 && (uint32_t)pos < lp ? (uint32_t)pos : lp - 1u, nt, 0);
            lead_held[nt] = 0;
        }
}
static void lead_rec_key(uint32_t note, int on)        /* (ISR, phantom.c) a player's key on LEAD */
{
    uint32_t p, lp;
    int32_t pos;
    note &= 127u;
    if (!lead_recording()) {
        if (!on)
            lead_held[note] = 0;
        return;
    }
    p = lead_pat();
    pos = lead_pos();
    lp = lead_loop();
    if (p >= LEAD_PATS || pos < 0)
        return;
    if (!lead_take) {
        lead_snap();
        lead_take = 1;
    }
    if (on) {
        if (lead_held[note])                            /* (the same note again without its end: closed first) */
            lead_put(lead_held[note] - 1u, (uint32_t)pos < lp ? (uint32_t)pos : lp - 1u, note, 0);
        if (lead_n[p] + 2u > LEAD_PER)                  /* (no room for its end too: not taken) */
            return;
        lead_put(p, (uint32_t)pos, note, 1);
        lead_held[note] = (uint8_t)(p + 1u);
    } else if (lead_held[note]) {
        uint32_t hp = lead_held[note] - 1u;
        lead_put(hp, hp == p && (uint32_t)pos < lp ? (uint32_t)pos : lp - 1u, note, 0);   /* (another lane: at its end) */
        lead_held[note] = 0;
    }
}
static void lead_play(uint32_t note, int on)            /* (ISR) a recorded note, through the keys PH_LIVE.. */
{
    uint32_t i;
    for (i = 0; i < NELEM(lead_pl); i++)
        if (lead_pl[i] == note + 1u) {
            ph_key_off(PH_LIVE + i);
            lead_pl[i] = 0;
            break;
        }
    if (!on)
        return;
    for (i = 0; i < NELEM(lead_pl) && lead_pl[i]; i++)
        ;
    if (i == NELEM(lead_pl))
        return;
    lead_pl[i] = (uint8_t)(note + 1u);
    ph_key_on(PH_LIVE + i, note);
}
static void lead_release(void)                          /* (ISR) the lane's notes let go */
{
    uint32_t i;
    for (i = 0; i < NELEM(lead_pl); i++)
        if (lead_pl[i]) {
            ph_key_off(PH_LIVE + i);
            lead_pl[i] = 0;
        }
}
static void lead_fire(uint32_t p, int32_t a, int32_t b) /* (ISR) the lane's notes in (a, b] */
{
    uint32_t i;
    for (i = 0; i < lead_n[p]; i++) {
        int32_t t = lead_ev[p][i].t;
        if (t > b)
            break;
        if (t > a)
            lead_play(lead_ev[p][i].note, lead_ev[p][i].on);
    }
}
static void lead_tick(void)                             /* ISR (seq.c, every block, after the tracks) */
{
    uint32_t p, lp;
    int32_t pos;
    if (!song.playing) {
        lead_rec_end();
        if (lead_prev >= 0) {
            lead_release();
            lead_prev = -1;
        }
        lead_pat_now = 0xFFu;
        return;
    }
    if (!lead_recording())
        lead_rec_end();
    pos = lead_pos();
    if (pos < 0)
        return;
    p = lead_pat();
    lp = lead_loop();
    if (p != lead_pat_now) {                            /* (another pattern: from its start) */
        lead_release();
        lead_pat_now = (uint8_t)p;
        lead_prev = -1;
    }
    if (p < LEAD_PATS) {
        if (pos >= lead_prev)
            lead_fire(p, lead_prev, pos);
        else {                                          /* (round the loop's end) */
            lead_fire(p, lead_prev, (int32_t)lp);
            lead_fire(p, -1, pos);
        }
    }
    lead_prev = pos;
}
/* main: CLEAR this pattern's lane (undoable) */
static uint32_t lead_clear(void)
{
    uint32_t p = lead_pat() < LEAD_PATS ? lead_pat() : (bank.cur < LEAD_PATS ? bank.cur : 0u);
    fm1_irq_off();
    lead_snap();
    lead_n[p] = 0;
    lead_release();
    fm1_irq_on();
    lead_dirty = 1;
    return p;
}
static void lead_clear_all(void)                        /* main: a new song */
{
    fm1_irq_off();
    memset(lead_n, 0, sizeof lead_n);
    memset(lead_held, 0, sizeof lead_held);
    lead_take = 0;
    lead_release();
    lead_uok = lead_ulast = 0;
    fm1_irq_on();
    lead_dirty = 1;
}
static int lead_undo(void)                              /* main: UNDO, when the last undoable thing was LEAD's; again: redo */
{
    static lead_ev_t tmp[LEAD_PATS][LEAD_PER];
    static uint16_t tn[LEAD_PATS];
    if (!lead_uok || !lead_ulast)
        return 0;
    fm1_irq_off();
    memcpy(tmp, lead_ev, sizeof tmp);
    memcpy(tn, lead_n, sizeof tn);
    memcpy(lead_ev, lead_uev, sizeof lead_ev);
    memcpy(lead_n, lead_un, sizeof lead_n);
    memcpy(lead_uev, tmp, sizeof tmp);
    memcpy(lead_un, tn, sizeof tn);
    lead_release();
    fm1_irq_on();
    lead_dirty = 1;
    return 1;
}
static uint32_t lead_count(uint32_t p)                  /* the notes in a lane */
{
    uint32_t i, n = 0;
    for (i = 0; p < LEAD_PATS && i < lead_n[p]; i++)
        n += lead_ev[p][i].on;
    return n;
}

/* ---- kept: the storage object (LEAD_HDR bytes, then the lanes' counts and events, 4 bytes each) */
static uint8_t lead_obj[ST_PAYLOAD_MAX];
static uint32_t lead_pack(void)
{
    uint8_t *b = lead_obj;
    uint32_t i, p, o;
    memset(b, 0, LEAD_HDR);
    b[0] = (uint8_t)LEAD_MAGIC; b[1] = (uint8_t)(LEAD_MAGIC >> 8); b[2] = (uint8_t)(LEAD_MAGIC >> 16); b[3] = (uint8_t)(LEAD_MAGIC >> 24);
    b[4] = 1;                                           /* the version */
    b[5] = (uint8_t)ph_voice_at();
    for (i = 0; i < 8u; i++) {
        b[6 + 2u * i] = (uint8_t)ph_trk.p[P_E0 + i];
        b[7 + 2u * i] = (uint8_t)((uint16_t)ph_trk.p[P_E0 + i] >> 8);
    }
    {
        static const uint8_t K[] = {P_ATK, P_DEC, P_SUS, P_REL, P_DIST, P_CHOR, P_DLY, P_REV, P_LEVEL};
        for (i = 0; i < NELEM(K); i++)
            b[22 + i] = (uint8_t)clamp(ph_trk.p[K[i]], 0, 127);
        b[31] = (uint8_t)clamp(ph_trk.p[PH_GLIDE], 0, 127);
        b[32] = (uint8_t)clamp(ph_trk.p[PH_VIB], 0, 127);
    }
    o = LEAD_HDR;
    for (p = 0; p < LEAD_PATS; p++) {
        b[o++] = (uint8_t)lead_n[p];
        b[o++] = (uint8_t)(lead_n[p] >> 8);
    }
    for (p = 0; p < LEAD_PATS; p++)
        for (i = 0; i < lead_n[p]; i++) {
            b[o++] = (uint8_t)lead_ev[p][i].t;
            b[o++] = (uint8_t)(lead_ev[p][i].t >> 8);
            b[o++] = lead_ev[p][i].note;
            b[o++] = lead_ev[p][i].on;
        }
    return o;
}
/* 1: an object with LEAD in it (its sound and lanes now); 0: none or not one (empty lanes, the sound as it is) */
static int lead_unpack(const uint8_t *b, uint32_t n)
{
    uint32_t i, p, o = LEAD_HDR, cnt[LEAD_PATS], total = 0;
    memset(lead_n, 0, sizeof lead_n);
    if (n < LEAD_HDR + 2u * LEAD_PATS || (b[0] | b[1] << 8 | b[2] << 16 | (uint32_t)b[3] << 24) != LEAD_MAGIC || b[4] != 1u)
        return 0;
    for (p = 0; p < LEAD_PATS; p++) {
        cnt[p] = b[o] | b[o + 1u] << 8;
        o += 2u;
        if (cnt[p] > LEAD_PER)
            return 0;
        total += cnt[p];
    }
    if (o + total * 4u > n)
        return 0;
    ph_voice(b[5]);
    for (i = 0; i < 8u; i++)
        ph_trk.p[P_E0 + i] = (int16_t)(b[6 + 2u * i] | b[7 + 2u * i] << 8);
    {
        static const uint8_t K[] = {P_ATK, P_DEC, P_SUS, P_REL, P_DIST, P_CHOR, P_DLY, P_REV, P_LEVEL};
        for (i = 0; i < NELEM(K); i++)
            ph_trk.p[K[i]] = b[22 + i];
        ph_trk.p[PH_GLIDE] = b[31];
        ph_trk.p[PH_VIB] = b[32];
    }
    for (p = 0; p < LEAD_PATS; p++)
        for (i = 0; i < cnt[p]; i++, o += 4u) {
            lead_ev[p][i].t = (uint16_t)(b[o] | b[o + 1u] << 8);
            lead_ev[p][i].note = b[o + 2u] & 127u;
            lead_ev[p][i].on = b[o + 3u] & 1u;
        }
    for (p = 0; p < LEAD_PATS; p++)
        lead_n[p] = (uint16_t)cnt[p];
    return 1;
}
static void lead_boot(void)                             /* main, at power-on (after the autosave's PHANTOM) */
{
    int32_t n = st_load(OBJ_LEAD, lead_obj, sizeof lead_obj);
    gen_xt = &ph_trk;                                   /* (GENESIS: LEAD's voice its memory, eng_genesis.c) */
    if (n <= 0 || !lead_unpack(lead_obj, (uint32_t)n)) {
        memset(lead_n, 0, sizeof lead_n);
        lead_snd = (uint32_t)clamp(ph_trk.p[P_E0], 0, SIG_N - 1);   /* (the autosave's SIGNAL) */
    }
    lead_dirty = 0;
}
static void lead_poll(void)                             /* main, a frame: saved 1.5 s after it changed, not mid-take */
{
    if (!lead_dirty) {
        lead_dirty_ms = fm1_ms;
        return;
    }
    if (lead_take || fm1_ms - lead_dirty_ms < 1500u)
        return;
    lead_dirty = 0;
    (void)st_save(OBJ_LEAD, lead_obj, lead_pack());
}
#endif
#endif
