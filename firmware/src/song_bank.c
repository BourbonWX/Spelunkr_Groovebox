/* SPDX-License-Identifier: GPL-3.0-only */
/* RONDO: the PATTERN BANK, QY70 style. Six patterns A..F, each the four tracks' steps (CHANCE RATCH NUDGE in them),
 * their LEN DIV SWING GATE and QUANTIZE, and the automation, locks and TRACKER commands (the whole motion store); the
 * sounds are the tracks' own, shared by every pattern (as SONG's sections always were).
 *   the current pattern (bank.cur): the tracks' own steps and motion, the one played and edited (so every page edits
 *     it); its copy in bank_pat[] is as of its last store: a switch away, a song played, the idle store (project.c)
 *   ENV held (ui_layer.c LAYER_ENV): F3 G3 A3 B3 C4 D4 are A..F. A key with a pattern: it next. Stopped, at once;
 *     playing, at the end of this pattern (bank_tick: the end of the loop of its longest track, bank_clock), the new
 *     one from its step 1 with all four tracks together; pressed again (or the current one's key): back to staying.
 *     An empty key: the current pattern copied into it, and it is the current one from then on (A done, B starts as
 *     A). OCT+ held: the current one copied into the key, staying here; OCT- held: the key cleared (not the current
 *     one's); OCT- tapped alone: the next one forgotten
 *   the flash: each pattern its own A/B object pair in the top of user sample slot 3 (storage.c), written stopped and
 *     idle (project.c bank_poll; an erase silences the audio); which pattern was current rides in the autosave
 *   SONG (song_chain.c): a section's tracks name these patterns A..F (chain_pat); PLAY SONG stores the current one
 *     first (project.c chain_prepare)
 * The ISR switches (bank_tick, bank_start); the main loop stores, queues and clears with the interrupts off. */
#if RONDO_LAYOUT
static uint32_t step_samples(const track_t *t, uint32_t period, uint32_t idx);
static void ui_message(const char *m);
static void ui_say(const char *a, const char *b);

/* the tracks' pattern as it is now == pattern p's copy (no store needed) */
static int bank_same(uint32_t p)
{
    const chain_pattern_t *c = &bank_pat[p % NBANK];
    uint32_t k;
    for (k = 0; k < NTRK; k++) {
        const track_t *t = &trk[k];
        if (memcmp(c->step[k], t->step, sizeof c->step[k]) || memcmp(c->timing[k], &t->p[P_SLEN], sizeof c->timing[k]) ||
            c->qnt[k] != (t->p[P_SQNT] != 0))
            return 0;
    }
    return !memcmp(&c->motion, &motion, sizeof motion) && !memcmp(&c->fxp, &fxp, sizeof fxp);
}
/* the tracks' pattern -> pattern p's copy (interrupts off, or the ISR) */
static void bank_take(uint32_t p)
{
    chain_pattern_t *c = &bank_pat[p % NBANK];
    uint32_t k;
    for (k = 0; k < NTRK; k++) {
        const track_t *t = &trk[k];
        memcpy(c->step[k], t->step, sizeof c->step[k]);
        memcpy(c->timing[k], &t->p[P_SLEN], sizeof c->timing[k]);
        c->qnt[k] = (uint8_t)(t->p[P_SQNT] != 0);
    }
    c->motion = motion;
    c->fxp = fxp;                                       /* (Phantom Cosmonaut: the FX PAD's prints) */
    bank.used |= (uint8_t)(1u << (p % NBANK));
}
/* RECORD THE ARRANGEMENT (GLO > SONG, REC tapped: armed; PLAY: recording): every pattern played becomes a section of
 * SONG, all four tracks on it, REPS its whole loops (over 16: more sections), the song recorded over; STOP ends it
 * (the last pattern at least once). The ISR logs {pattern, loops} at each switch and at STOP; the main loop writes
 * the sections (bank_poll: chain_config is the main loop's) */
#define ARR_N 16u
static struct {
    volatile uint8_t armed, on, fresh, done, full;
    volatile uint8_t n_in, n_out;
    uint8_t pat;
    uint16_t loops;
    struct { uint8_t pat; uint16_t loops; } ev[ARR_N];
} arr;
static void arr_push(uint32_t p, uint32_t loops)       /* ISR */
{
    if ((uint8_t)(arr.n_in - arr.n_out) < ARR_N) {
        arr.ev[arr.n_in % ARR_N].pat = (uint8_t)p;
        arr.ev[arr.n_in % ARR_N].loops = (uint16_t)loops;
        RING_PUBLISH();
        arr.n_in++;
    }
}
/* MIDI: a Program Change on channel 16 picks the pattern, programs 1..6 (0..5) A..F, as ENV + its key (bank_poll) */
#define BANK_PC_CH 15u
static volatile uint8_t bank_pc;                        /* the pattern + 1 a Program Change asked for, 0 none */
static void bank_midi_pc(uint32_t ch, uint32_t prog) { if (ch == BANK_PC_CH && prog < NBANK) bank_pc = (uint8_t)(prog + 1u); }
/* the current pattern stored when it changed (interrupts off, or the ISR); 1 = it had */
static int bank_keep(void)
{
    if (bank_same(bank.cur))
        return 0;
    bank_take(bank.cur);
    bank.dirty |= (uint8_t)(1u << bank.cur);
    return 1;
}
/* pattern p's copy -> the tracks (their notes let go, their automation back to the sound's values first) */
static void bank_give(uint32_t p)
{
    const chain_pattern_t *c = &bank_pat[p % NBANK];
    uint32_t k;
    for (k = 0; k < NTRK; k++) {
        seq_release(&trk[k]);
        motion_restore(&trk[k]);
    }
    motion = c->motion;
    fxp = c->fxp;
    for (k = 0; k < NTRK; k++) {
        track_t *t = &trk[k];
        memcpy(t->step, c->step[k], sizeof t->step);
        memcpy(&t->p[P_SLEN], c->timing[k], sizeof c->timing[k]);
        t->p[P_SQNT] = c->qnt[k];
    }
}
/* the track whose loop ends the current pattern: the longest loop (LEN x DIV; the first of a tie) */
static uint32_t bank_clock(void)
{
    uint32_t k, best = 0, most = 0;
    for (k = 0; k < NTRK; k++) {
        uint32_t n = pat_loop(trk[k].p[P_SLEN], trk[k].p[P_SDIV]);
        if (n > most) {
            most = n;
            best = k;
        }
    }
    return best;
}
/* to pattern p (one with a pattern, not the current one): this one stored, p given, playing: every track from its
 * step 1 together (as a SONG section starts). The ISR (bank_tick, bank_start) or the main loop, interrupts off */
static __attribute__((noinline)) void bank_switch(uint32_t p)
{
    uint32_t k;
    bank.queue = CHAIN_OFF;
    if (p >= NBANK || p == bank.cur || !((bank.used >> p) & 1u))
        return;
    bank_keep();
    bank_give(p);
    bank.cur = (uint8_t)p;
    bank.gen++;
    for (k = 0; k < NTRK; k++) {
        track_t *t = &trk[k];
        t->seq_idx = (uint16_t)(t->p[P_SLEN] - 1);
        t->seq_pos = 0x7FFFFFFFu;
        t->rh_n = t->rskip_n = 0;
        t->rat_left = 0;
        t->nd_late = t->nd_early = ND_NONE;
        t->nd_fired = 0;
    }
}
/* ISR, each block before the tracks: the queued pattern at the end of this one's loop (its clock track's last step
 * over: the new one's step 1 where that would have started, bank.carry) */
static void bank_tick(uint32_t n)
{
    const track_t *t = &trk[bank.clk % NTRK];
    uint32_t length, carry, old;
    bank.carry = 0;
    if ((bank.queue >= NBANK && !arr.on) || chain.running || !song.playing || t->seq_pos >= 0x7FFFFFFFu ||
        t->seq_idx + 1u != (uint32_t)t->p[P_SLEN])
        return;
    length = step_samples(t, div_samples((uint32_t)t->p[P_SDIV]), t->seq_idx);
    if (t->seq_pos + n < length)
        return;
    if (arr.on)                                         /* (recording: a whole loop of this pattern) */
        arr.loops++;
    if (bank.queue >= NBANK)
        return;
    carry = t->seq_pos + n - length;
    old = bank.cur;
    bank_switch(bank.queue);
    bank.carry = carry;
    if (arr.on && bank.cur != old) {
        arr_push(old, arr.loops);
        arr.loops = 0;
    }
}
/* ISR, PLAY: a pattern queued plays from the start (a song starting: none) */
static void bank_start(void)
{
    bank.carry = 0;
    if (chain.armed)
        bank.queue = CHAIN_OFF;
    else if (bank.queue < NBANK)
        bank_switch(bank.queue);
    if (arr.armed && !chain.armed) {                    /* (RECORD THE ARRANGEMENT: from here) */
        arr.armed = 0;
        arr.n_in = arr.n_out = 0;
        arr.loops = 0;
        arr.fresh = 1;
        arr.on = 1;
    }
}
/* ISR, STOP: the recording ends with the pattern playing (its loops, at least one) */
static void bank_stop(void)
{
    if (!arr.on)
        return;
    arr_push(bank.cur, arr.loops ? arr.loops : 1u);
    arr.on = 0;
    arr.done = 1;
}
/* main loop: the logged patterns -> SONG's sections (bank_poll) */
static void chain_defaults(chain_config_t *c);
static void arr_drain(void)
{
    if (arr.fresh) {
        arr.fresh = 0;
        chain_defaults(&chain_config);
        arr.full = 0;
    }
    while (arr.n_out != arr.n_in) {
        uint32_t p = arr.ev[arr.n_out % ARR_N].pat, n = arr.ev[arr.n_out % ARR_N].loops;
        arr.n_out++;
        while (n && !arr.full) {
            uint32_t r = n > 16u ? 16u : n;
            if (chain_config.count >= CHAIN_ROWS) {
                arr.full = 1;
                arr.on = 0;
                ui_message("ORBIT FULL: 16 SECTIONS");
                break;
            }
            chain_config.row[chain_config.count] = chain_row_of(chain_val(p), r);
            chain_config.count++;
            n -= r;
        }
    }
    if (arr.done) {
        char b[4];
        arr.done = 0;
        fmt_int(b, (int32_t)chain_config.count);
        if (!arr.full)
            ui_say(b, chain_config.count == 1u ? " SECTION RECORDED" : " SECTIONS RECORDED");
    }
}
/* GLO > SONG, REC tapped: armed / disarmed (recording: stopped by STOP) */
static void arr_toggle(void)
{
    if (arr.on) {
        ui_message("STOP ENDS THE RECORDING");
        return;
    }
    arr.armed = (uint8_t)!arr.armed;
    ui_message(arr.armed ? "ARRANGE REC: PLAY, ENV KEYS" : "ARRANGE REC OFF");
}

/* ---- the main loop: ENV held's keys (ui_layer.c) ---- */
static int chain_busy(void);
static int bank_has(uint32_t p) { return p < NBANK && ((bank.used >> p) & 1u); }
static void bank_name(char *b, uint32_t p) { b[0] = (char)('A' + p % NBANK); b[1] = 0; }
#if SPK_NU
/* the Spelunkr Groovebox (Ed, Nov 24: "copy 1 of the 6 patterns by accident, pressing the undo button could undo
 * that"): UNDO (felucca_web.c web_undo) takes back the last pattern copy or clear when it was the last undoable thing
 * done (bank_ulast; a sound loaded, a row tool, LEAD's take clear it). kind 1: pattern p's contents replaced (OCT+ held
 * + a key: copied into; OCT- held + a key: cleared); kind 2: p made as a copy of the current one (an empty pattern's
 * key, the page's pattern blocks) and the current one since: undone, p is gone and old is the current one again. UNDO
 * again: redo */
static struct { uint8_t ok, kind, p, old, used; chain_pattern_t pat; } bundo;
static uint8_t bank_ulast;
static void bank_snap(uint32_t kind, uint32_t p)
{
    bundo.ok = 1;
    bundo.kind = (uint8_t)kind;
    bundo.p = (uint8_t)p;
    bundo.old = bank.cur;
    bundo.used = (uint8_t)bank_has(p);
    bundo.pat = bank_pat[p % NBANK];
    bank_ulast = 1;
    lead_ulast = 0;
}
static int bank_undo(void)                              /* main: 1 done */
{
    uint32_t p = bundo.p, f;
    char m[24] = "UNDO: PATTERN A";
    if (!bundo.ok || !bank_ulast || chain_busy() || p >= NBANK)
        return 0;
    f = motion_guard();                                 /* (the web build: the audio runs between these calls, not in them) */
    if (bundo.kind == 1) {                              /* its contents swapped with the copy */
        chain_pattern_t now = bank_pat[p];
        uint8_t used = (uint8_t)bank_has(p);
        bank_pat[p] = bundo.pat;
        bank.used = (uint8_t)(bundo.used ? bank.used | 1u << p : bank.used & ~(1u << p));
        bundo.pat = now;
        bundo.used = used;
        if (bank.queue == p) bank.queue = CHAIN_OFF;
        if (p == bank.cur)
            bank_give(p);                               /* (the one playing: its tracks as it was) */
    } else if (bank.cur == p && bank_has(bundo.old)) {  /* made: back to the one it came from, p gone */
        bank_switch(bundo.old);                         /* (p's last state stored first: kept for the redo) */
        bundo.pat = bank_pat[p];
        memset(&bank_pat[p], 0, sizeof bank_pat[p]);
        bank.used &= (uint8_t)~(1u << p);
        str_cpy(m, "UNDO: NO PATTERN A", sizeof m);
        m[17] = (char)('A' + p);
        p = NBANK;
    } else if (!bank_has(p) && bank.cur == bundo.old) { /* the redo: p back, the current one */
        bank_pat[p] = bundo.pat;
        bank.used |= (uint8_t)(1u << p);
        bank_switch(p);
        str_cpy(m, "REDO: PATTERN A", sizeof m);
    } else {                                            /* (something else changed them since: no undo) */
        bundo.ok = 0;
        motion_unguard(f);
        return 0;
    }
    bank.dirty |= (uint8_t)(1u << bundo.p | 1u << bundo.old);
    motion_unguard(f);
    if (p < NBANK)
        m[14] = (char)('A' + p);
    ui_message(m);
    return 1;
}
#define BANK_SNAP(kind, p) bank_snap(kind, p)
/* (Ed, Nov 24: "hold the corresponding key of the pattern you want to copy and then clicking the key associated to the
 * spot you want to copy it to. Just clicking an empty spot should generate an empty pattern") A pattern key acts when it
 * is let go (bank_hold_poll), unless another pattern key was pressed while it was held: then that one got a copy of it
 * (bank_press). An empty one tapped: a new empty pattern there (its tracks' lengths as the current one's), the current
 * one (stopped) or the next (playing). OCT+ / OCT- held with a key: as before (the current one copied there / cleared) */
static void step_clear(step_t *st);                    /* (ui.c) */
static int bank_key(uint32_t p);
static int bank_new_empty(uint32_t p)
{
    chain_pattern_t *c = &bank_pat[p % NBANK];
    char b[2];
    uint32_t k, i, f;
    if (p >= NBANK || bank_has(p))
        return 1;
    if (chain_busy()) {
        ui_message("ORBIT PLAYING");
        return 1;
    }
    bank_name(b, p);
    f = motion_guard();
    BANK_SNAP(song.playing ? 1u : 2u, p);               /* (playing: it's only queued; stopped: the current one now) */
    bank_keep();
    bank_take(p);                                       /* the current one's shape (lengths, speed, swing) .. */
    for (k = 0; k < NTRK; k++)                          /* .. with no steps, no automation, no prints */
        for (i = 0; i < NSTEP; i++)
            step_clear(&c->step[k][i]);
    memset(&c->motion, 0, sizeof c->motion);
    memset(&c->fxp, 0, sizeof c->fxp);
    bank.dirty |= (uint8_t)(1u << p);
    if (song.playing) {
        bank.clk = (uint8_t)bank_clock();
        RING_PUBLISH();
        bank.queue = (uint8_t)p;
        ui_say("NEW EMPTY NEXT: ", b);
    } else {
        bank_switch(p);
        ui_say("NEW EMPTY PATTERN ", b);
    }
    motion_unguard(f);
    return 0;
}
static int bank_copy_from(uint32_t src, uint32_t dst)  /* pattern src copied into dst (either may be the current one) */
{
    char b[8] = "A TO A";
    uint32_t f;
    if (src >= NBANK || dst >= NBANK || src == dst)
        return 1;
    if (chain_busy()) {
        ui_message("ORBIT PLAYING");
        return 1;
    }
    if (!bank_has(src) && src != bank.cur) {
        bank_name(b, src);
        ui_say(b, " IS EMPTY");
        return 1;
    }
    f = motion_guard();
    bank_keep();                                        /* (the current one's latest edits first) */
    BANK_SNAP(1u, dst);
    bank_pat[dst] = bank_pat[src];
    bank.used |= (uint8_t)(1u << dst);
    bank.dirty |= (uint8_t)(1u << dst);
    if (dst == bank.cur)
        bank_give(dst);                                 /* (copied onto the one playing: its tracks now) */
    if (bank.queue == dst) bank.queue = CHAIN_OFF;
    motion_unguard(f);
    b[0] = (char)('A' + src);
    b[5] = (char)('A' + dst);
    ui_say("COPIED ", b);
    return 0;
}
static uint8_t bk_src = 0xFFu, bk_did;
static void bank_press(uint32_t p)                      /* a pattern key down (no OCT held) */
{
    if (bk_src < NBANK && bk_src != p) {
        bank_copy_from(bk_src, p);
        bk_did = 1;
        return;
    }
    bk_src = (uint8_t)p;
    bk_did = 0;
}
static int bank_hold_poll(uint32_t held)                /* main, every pass: held = the pattern keys down (bit p); 1 acted */
{
    uint32_t p = bk_src;
    if (p >= NBANK || ((held >> p) & 1u))
        return 0;
    bk_src = 0xFFu;
    if (bk_did)
        return 0;
    if (!bank_has(p) && p != bank.cur)
        bank_new_empty(p);
    else
        bank_key(p);
    return 1;
}
#else
#define BANK_SNAP(kind, p) ((void)0)
#endif
/* a white key A..F: go there (stopped), next (playing), back to staying (the queued one, the current one); an empty
 * one: the current pattern copied in, the current one from now. 0 done, 1 refused */
static int bank_key(uint32_t p)
{
    char a[2], b[2];
    if (p >= NBANK)
        return 1;
    if (chain_busy()) {
        ui_message("ORBIT PLAYING");
        return 1;
    }
    bank_name(a, bank.cur);
    bank_name(b, p);
    if (p == bank.cur || p == bank.queue) {
        if (bank.queue < NBANK) {
            bank.queue = CHAIN_OFF;
            ui_say("STAYING ON ", a);
        } else
            ui_say("PLAYING ", a);
        return 0;
    }
#if SPK_NU
    if (!bank_has(p))                                    /* (the web, the box: an empty one is a new empty pattern) */
        return bank_new_empty(p);
#endif
    if (!bank_has(p)) {                                  /* an empty key: a copy of this one, the current one now */
        uint32_t f = motion_guard();
        BANK_SNAP(2u, p);
        bank_keep();
        bank_take(p);
        bank.dirty |= (uint8_t)(1u << p);
        if (bank.queue < NBANK) bank.queue = CHAIN_OFF;
        bank.cur = (uint8_t)p;
        bank.gen++;
        motion_unguard(f);
        ui_say("NEW PATTERN ", b);
        return 0;
    }
    if (song.playing) {
        bank.clk = (uint8_t)bank_clock();
        RING_PUBLISH();
        bank.queue = (uint8_t)p;
        ui_say("NEXT: ", b);
        return 0;
    }
    uint32_t f = motion_guard();
    bank_switch(p);
    motion_unguard(f);
    ui_say("FREQ ", b);
    return 0;
}
/* OCT+ held + a key: the current pattern copied into it (it stays the current one) */
static int bank_copy(uint32_t p)
{
    char b[8] = "A TO A";
    if (p >= NBANK || p == bank.cur) {
        ui_message(p == bank.cur ? "THAT ONE IS PLAYING" : "NO PATTERN THERE");
        return 1;
    }
    if (chain_busy()) {
        ui_message("ORBIT PLAYING");
        return 1;
    }
    uint32_t f = motion_guard();
    BANK_SNAP(1u, p);
    bank_take(p);
    bank.dirty |= (uint8_t)(1u << p);
    if (bank.queue == p) bank.queue = CHAIN_OFF;
    motion_unguard(f);
    b[0] = (char)('A' + bank.cur);
    b[5] = (char)('A' + p);
    ui_say("COPIED ", b);
    return 0;
}
/* OCT- held + a key: pattern p cleared (an empty key; not the current one's: SEQ / REC CLEAR its tracks) */
static int bank_clear(uint32_t p)
{
    char b[2];
    if (p >= NBANK)
        return 1;
    if (p == bank.cur) {
        ui_message("THAT ONE IS PLAYING");
        return 1;
    }
    if (chain_busy()) {
        ui_message("ORBIT PLAYING");
        return 1;
    }
    bank_name(b, p);
    if (!bank_has(p)) {
        ui_say(b, " IS EMPTY");
        return 1;
    }
    uint32_t f = motion_guard();
    BANK_SNAP(1u, p);
    bank.used &= (uint8_t)~(1u << p);
    bank.dirty |= (uint8_t)(1u << p);
    if (bank.queue == p) bank.queue = CHAIN_OFF;
    memset(&bank_pat[p], 0, sizeof bank_pat[p]);
    motion_unguard(f);
    ui_say("CLEARED ", b);
    return 0;
}
/* OCT- tapped in the layer: the queued one forgotten (1: there was one) */
static int bank_unqueue(void)
{
    char a[2];
    if (bank.queue >= NBANK)
        return 0;
    bank.queue = CHAIN_OFF;
    bank_name(a, bank.cur);
    ui_say("STAYING ON ", a);
    return 1;
}
/* pattern p's track k as SONG and ENV's map draw it: one loop in 1/48 quarters (project.c len48_of), its picture
 * (pat_density: 2 bits a cell, *n cells); the current pattern from the tracks (a song playing: its stored copy, the
 * same). 0: an empty key */
static uint32_t pat_density(const step_t *st, int32_t slen, uint8_t *n);
static uint32_t len48_of(int32_t len, int32_t div);
static uint32_t bank_info(uint32_t p, uint32_t k, uint32_t *den, uint8_t *n)
{
    const chain_pattern_t *c = &bank_pat[p % NBANK];
    int live = p == bank.cur && !chain.running;
    const step_t *st = live ? trk[k % NTRK].step : c->step[k % NTRK];
    int32_t len = live ? trk[k % NTRK].p[P_SLEN] : c->timing[k % NTRK][0];
    int32_t div = live ? trk[k % NTRK].p[P_SDIV] : c->timing[k % NTRK][1];
    *den = 0;
    *n = 0;
    if (!bank_has(p))
        return 0;
    *den = pat_density(st, len, n);
    return len48_of(len, div);
}
/* power-on and the host tests: an empty bank, A current */
static void bank_reset(void)
{
    memset(bank_pat, 0, sizeof bank_pat);
    memset((void *)&bank, 0, sizeof bank);
    bank.queue = CHAIN_OFF;
    bank.used = 1;
}
#endif
