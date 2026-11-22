/* SPDX-License-Identifier: GPL-3.0-only
 * Dungeon Tracker 1.0: MENU > GAME > RANDOM SONG (firmware/src/boot_song.c) through ui_test.c's harness: the real UI,
 * engines, mix. (1.0's first builds rolled such a song at every power-on; now the power-on keeps the default song.)
 *   build/host/boot_song_test [OUT_DIR]    (tests/run_rondo.sh; OUT_DIR: WAVs of six rolls, 16 bars of the verse each)
 * 1. 300 rolls (Spelunkr: A..F, 2 or 4 bars, an arrangement of 6..16 sections): THE PARTY within its points, a ROGUE in it, the tracks in class order; every track a sound of its
 *    class's own list (a ROGUE a kit: FMKIT's or a SAMPLE kit) and a 16-step part with notes in their registers; the
 *    tempo 70..150 (Spelunkr's styles).
 * 2. they differ: at least 280 of the 300 songs are distinct.
 * 3. the melodic notes belong to one seven-note scale (one key) per song.
 * 4. the dungeon: the four adventurers join, at L1; HOME says PRESS PLAY until PLAY.
 * 5. settled (as the screens use it): the autosave sees it, pattern A's copy is the song, nothing dirty.
 * 6. heard: 50 songs, 8 bars each: never full scale, never silent for a bar, no voice left after the stop.
 * 7. the MENU: GAME's last row RANDOM SONG, OCT+ rolls one (the arrangement and the name cleared, as NEW), SAVE held
 *    undoes it: the song before back whole, its party too. */
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"
#include "../firmware/src/boot_song.c"

static int fails;
static void okb(const char *what, int ok) { fails += check(what, ok); }
static char what[256];

static uint32_t song_hash(void)
{
    uint32_t h = 2166136261u, k;
    for (k = 0; k < NTRK; k++) {
        h = as_mix(h, trk[k].step, sizeof trk[k].step);
        h = as_mix(h, &trk[k].eng_req, 1);
        h = as_mix(h, &trk[k].preset, 1);
    }
    return as_mix(h, &song.g[G_BPM], 2);
}
static void power_on_song(uint32_t seed)
{
    ui_power_on();
    song.master_q12 = 4096;
    random_song_make(seed);
    boot_song_settle();
    frames(2);                                            /* (the engine switches: on the audio side) */
}
static uint32_t sounding(void)
{
    uint32_t k, i, n = 0;
    for (k = 0; k < NTRK; k++)
        for (i = 0; i < NVOICE; i++)
            n += trk[k].v[i].active;
    return n;
}

int main(int argc, char **argv)
{
    static uint32_t seen[300];
    uint32_t s, k, i, bad = 0, distinct = 0, offkey = 0, kits[2] = {0, 0}, engs = 0, bnew[4] = {0, 0, 0, 0};
    (void)offkey;
    char *badwhy = what;

    /* 1., 2., 3. */
    for (s = 0; s < 300u; s++) {
        uint16_t pcs = 0;
        uint8_t c[NTRK];
        uint32_t nr = 0, p, any[NTRK] = {0, 0, 0, 0};
        power_on_song(0x9E3779B9u * (s + 1u));
        seen[s] = song_hash();
        for (k = 0; k < NTRK; k++) {
            c[k] = (uint8_t)trk_cls(k);
            nr += CLS_ROLE[c[k]] == ROLE_DRUMS;
            bad += c[k] != k;                             /* (Phantom Cosmonaut: the roles fixed, T1..T4) */
        }
        bad += !nr;
        {   /* Spelunkr (item 3): six patterns (A..F: intro verse chorus break build, the extra), 2 or 4 bars all; a song of
             * 6..16 sections from the intro, each a pattern or "-" per track, at least one chorus, 1..16 repeats */
            uint32_t r, chorus = 0;
            bad += bank.used != 0x3Fu || bank.cur != 1u || chain_config.count < 6u || chain_config.count > CHAIN_ROWS ||
                   chain_config.row[0].slot[1] != 0u || (rsg.len != 32u && rsg.len != 64u);
            for (r = 0; r < chain_config.count; r++) {
                const chain_row_t *row = &chain_config.row[r];
                uint32_t on = 0;
                bad += row->repeat < 1u || row->repeat > 16u;
                for (k = 0; k < NTRK; k++) {
                    bad += row->slot[k] != CHAIN_SILENT && chain_pat(row->slot[k]) >= 6u;
                    on += row->slot[k] != CHAIN_SILENT;
                    chorus += chain_pat(row->slot[k]) == RS_CHORUS;
                }
                bad += on < 2u;                            /* (a drop leaves out up to three: chords and bass stay) */
            }
            bad += !chorus;
        }
        for (k = 0; k < NTRK; k++) {
            uint32_t e = trk[k].eng_req % NENGINES, cat = preset_cat(e, trk[k].preset);
            engs |= 1u << e;
            bad += !cls_allows(c[k], cat);
            if (CLS_ROLE[c[k]] == ROLE_DRUMS) {
                bad += cat != CAT_DRUM || !ENG_KIT(e);   /* (GEN KIT) */
                kits[e != ENGI_CKIT]++;
            }
            for (p = 0; p < 6u; p++) {                    /* (F a LIFT: a tone up, its own key: not counted for the key) */
                const step_t *st0 = bank_pat[p].step[k];
                bad += bank_pat[p].timing[k][0] != rsg.len;   /* (P_SLEN) */
                for (i = 0; i < rsg.len; i++) {
                    const step_t *st = &st0[i];
                    uint32_t j;
                    if (st->time != ST_NOTE)
                        continue;
                    any[k]++;
                    if (CLS_ROLE[c[k]] == ROLE_DRUMS) {
                        bad += ENG_KIT(e) ? st->n != 0u || !st->hit : !st->n || st->hit;
                        continue;
                    }
                    for (j = 0; j < st->n; j++) {
                        uint32_t n = st->note[j];
                        bad += c[k] == CLS_CLERIC ? n < 48u || n > 104u : CLS_ROLE[c[k]] == ROLE_BASS ? n < 16u || n > 64u :
                               n < 60u || n > 100u;
                        if (p != RS_EXTRA || rsg.fx != FX_LIFT) pcs |= (uint16_t)(1u << (n % 12u));
                    }
                }
            }
            bad += !any[k];
            bad += memcmp(trk[k].step, bank_pat[1].step[k], sizeof trk[k].step) != 0;   /* (the tracks: the verse) */
        }
        bad += song.g[G_BPM] < 70 || song.g[G_BPM] > 150;   /* (Spelunkr: by the style, 70..150) */
        {   /* 0.10: CHORDS' B side on, panned apart; PERC's two lanes both play; the verse's pans per step (locks) */
            uint32_t l1 = 0, l2 = 0, pans = 0;
            for (p = 0; p < 4u; p++)
                for (i = 0; i < 32u; i++) {
                    l1 |= bank_pat[p].step[PERC_TRK][i].hit & 1u;
                    l2 |= bank_pat[p].step[PERC_TRK][i].hit & 2u;
                }
            for (i = 0; i < bank_pat[1].motion.count; i++)
                pans += (bank_pat[1].motion.event[i].param & MOTION_LOCK) && MOTION_ID(&bank_pat[1].motion.event[i]) == P_PAN;
            bnew[0] += b_on(&trk[1]) && bsr[0].s.pn == -trk[1].p[P_PAN] && bsr[0].s.pn > 0;
            bnew[1] += b_on(&trk[2]) + b_on(&trk[3]);
            bnew[2] += l1 && l2;
            bnew[3] += pans >= 4u;
        }
        {   /* one key: the pitch classes fit some rotation of a seven-note mode (minor's, its rotations: dorian, major) */
            static const uint16_t NAT = 0x0AD5;            /* C D E F G A B: the diatonic set (every mode here is one) */
            uint32_t r, fit = 0;
            for (r = 0; r < 12u; r++) {
                uint16_t rot = (uint16_t)(((NAT << r) | (NAT >> (12u - r))) & 0xFFFu);
                fit |= (pcs & ~rot) == 0;
            }
            offkey += !fit;
            if (!fit && getenv("BS_DBG")) {
                printf("  off: song %u pcs %03X classes %u %u %u %u\n", s, pcs, c[0], c[1], c[2], c[3]);
                for (k = 0; k < NTRK; k++) for (p = 0; p < 4u; p++) for (i = 0; i < 32u; i++) {
                    const step_t *st = &bank_pat[p].step[k][i];
                    if (st->time == ST_NOTE && CLS_ROLE[c[k]] != ROLE_DRUMS) {
                        uint32_t j;
                        printf("    T%u %c%02u:", k + 1u, 'A' + p, i);
                        for (j = 0; j < st->n; j++) printf(" %u", st->note[j]);
                        printf("\n");
                    }
                }
            }
        }
    }
    for (s = 0; s < 300u; s++) {
        uint32_t j, dup = 0;
        for (j = 0; j < s; j++)
            dup |= seen[j] == seen[s];
        distinct += !dup;
    }
    for (k = 0, i = 0; k < NENGINES; k++)
        i += (engs >> k) & 1u;
    snprintf(badwhy, sizeof what, "300 rolls: the fixed roles (T1 drums .. T4 lead); their sounds; PATTERN BANK A..D (intro verse "
             "chorus break, 32 steps) in key and register; SONG A B C B C D C; on B (%u off; %u GEN KIT kits, %u other; %u engines)", bad, kits[0], kits[1], i);
    okb(what, !bad && kits[0] && !kits[1] && i >= NENG_SHOWN);   /* (Phantom Cosmonaut: MOD KIT retired, every kit GEN KIT's) */
    snprintf(what, sizeof what, "  0.10: CHORDS' B side panned apart %u of 300, BASS / LEAD B sides %u, PERC's two lanes %u, "
             "the verse's per-step pans %u", bnew[0], bnew[1], bnew[2], bnew[3]);
    okb(what, bnew[0] == 300u && bnew[1] > 150u && bnew[1] < 450u && bnew[2] == 300u && bnew[3] >= 270u);   /* (Spelunkr: a kick can take a panned hat's step) */
    snprintf(what, sizeof what, "  they differ: %u of 300 distinct", distinct);
    okb(what, distinct >= 280u);
    snprintf(what, sizeof what, "  one key each: the melodic notes in a seven-note scale (%u songs off)", offkey);
    okb(what, !offkey);

    /* 5. settled */
    power_on_song(777u);
    okb("settled: the autosave sees the power-on state, pattern A's copy is the song, nothing dirty",
        as.saved == autosave_sig() && bank_same(bank.cur) && !bank.dirty);

    /* 6. heard */
    {
        uint32_t over = 0, quiet = 0, hung = 0, n;
        for (s = 0; s < 50u; s++) {
            int32_t o[2 * CTL];
            uint32_t bar, bars = 8, per = FS * 60u / (uint32_t)song.g[G_BPM] * 4u;
            power_on_song(0xC0FFEEu + s * 7919u);
            per = FS * 60u / (uint32_t)song.g[G_BPM] * 4u;
            seq_start();
            song.playing = 1;
            for (bar = 0; bar < bars; bar++) {
                double e = 0;
                int32_t pk = 0;
                for (n = 0; n < per; n += CTL) {
                    mix_block(o, CTL);
                    for (i = 0; i < 2u * CTL; i++) {
                        e += (double)o[i] * o[i];
                        pk = abs(o[i]) > pk ? abs(o[i]) : pk;
                    }
                }
                over += pk >= 32767;
                quiet += bar && e / (2.0 * per) < 100.0 * 100.0;   /* (under -50 dB for a whole bar) */
            }
            song.playing = 0;
            seq_stop();
            for (n = 0; n < FS * 6u; n += CTL)
                mix_block(o, CTL);
            hung += sounding() != 0u;
        }
        snprintf(what, sizeof what, "50 songs heard, 8 bars each: full scale %u, a silent bar %u, voices left after 6 s %u", over, quiet, hung);
        okb(what, !over && !quiet && !hung);
    }

    /* 7. the MENU */
    {
        uint32_t before, rolled, again;
        uint8_t cls_before[NTRK];
        ui_power_on();
        song.playing = 0;
        bank_reset();
        project_save_as(4, "MY SONG");                     /* (a song of the user's, named; the bank: its pattern only) */
        before = song_hash();
        for (k = 0; k < NTRK; k++) cls_before[k] = (uint8_t)trk_cls(k);
        okb("MENU > SYSTEM: RANDOM SONG, the tab's last row, an action (OCT+)", MI_TAB[MI_RANDOM] == MTAB_SYSTEM &&
            MI_RANDOM == MI_COUNT - 1u && mtab_rows(MTAB_SYSTEM) <= MTAB_ROWS && str_eq(MI_NAME[MI_RANDOM], "RANDOM SONG") && MI_ACTION(MI_RANDOM) &&
            !menu_valued(MI_RANDOM));
        go_home(); frames(50);
        hold(B_HOME); frames(100);
        ui.menu_sel = MI_RANDOM;
        press(B_OCTUP); frames(50);
        rolled = song_hash();
        okb("  OCT+: a song rolled: patterns A..D, SONG's sections, on B (\"RANDOM SONG\", the name cleared), the MENU up",
            rolled != before && ui.menu && msg_is("RANDOM SONG") && !proj_name[0] && trk[0].p[P_SLEN] == 32 &&
            bank.used == 0x3Fu && bank.cur == 1u && chain_config.count >= 6u);
        press(B_OCTUP); frames(50);
        again = song_hash();
        okb("  OCT+ again: another roll at once (nothing changed since the last)", again != rolled && msg_is("RANDOM SONG"));
        hold(B_HOME); frames(100);
        hold(B_SAVE); frames(50);
        for (k = 0, i = 0; k < NTRK; k++) i += trk_cls(k) != cls_before[k];
        okb("  SAVE held: undone, the song before back whole (its party, its pattern in A), the rolled patterns gone",
            song_hash() == before && !i && bank.cur == 0u && bank.used == 1u);
        /* the user's patterns in the bank: the first OCT+ warns, the second rolls */
        {
            uint32_t f = motion_guard();
            bank_take(2);                                  /* (C: a pattern of the user's) */
            motion_unguard(f);
        }
        hold(B_HOME); frames(100);
        ui.menu_sel = MI_RANDOM;
        press(B_OCTUP); frames(50);
        okb("  with patterns of the user's in the bank: the first OCT+ says REPLACES PATTERNS A-F, nothing rolled",
            song_hash() == before && msg_is("REPLACES PATTERNS A-F"));
        press(B_OCTUP); frames(50);
        okb("  .. the second (within 4 s) rolls", song_hash() != before && msg_is("RANDOM SONG") && bank.used == 0x3Fu);
        hold(B_HOME); frames(100);
    }

    if (argc > 1) {                                       /* four rolls as WAVs */
        uint32_t d;
        for (d = 0; d < 6u; d++) {
            char path[512];
            FILE *w;
            int32_t o[2 * CTL];
            uint32_t n, total;
            power_on_song(0xD06u + d * 104729u);
            total = FS * 60u / (uint32_t)song.g[G_BPM] * 4u * 16u;   /* (0.10: 16 bars of the verse) */
            snprintf(path, sizeof path, "%s/random_song_%u.wav", argv[1], d + 1u);
            if (!(w = fopen(path, "wb")))
                break;
            wav_hdr(w, total);
            seq_start();
            song.playing = 1;
            for (n = 0; n < total; n += CTL) {
                mix_block(o, CTL);
                for (i = 0; i < CTL; i++)
                    wav_put(w, o[2 * i], o[2 * i + 1]);
            }
            song.playing = 0;
            seq_stop();
            fclose(w);
            printf("boot_song: %s  %d BPM  DRUMS %s  PERC %s %s / %s %s", path, song.g[G_BPM], CK_PRESETS[trk[0].preset].name,
                   N_PC_KIT[trk[PERC_TRK].p[PC_KIT(0)]], N_PC_SND[pc_snd(trk[PERC_TRK].p, 0)],
                   N_PC_KIT[trk[PERC_TRK].p[PC_KIT(1)]], N_PC_SND[pc_snd(trk[PERC_TRK].p, 1)]);
            for (k = 1; k <= B_N; k++) {
                const engine_t *e = ENGINES[trk[k].eng_req % NENGINES];
                printf("  F%u %s %s", fd_pos(k) + 1u, e->name, e->presets[trk[k].preset % e->npresets].name);
                if (b_on(&trk[k])) {
                    const engine_t *eb = ENGINES[bsr[k - 1u].s.engine % NENGINES];
                    printf(" + B %s %s (%+d)", eb->name, eb->presets[bsr[k - 1u].s.preset % eb->npresets].name, bsr[k - 1u].s.tr);
                }
            }
            printf("\n");
        }
    }
    printf("boot_song: %s\n", fails ? "FAILED" : "all checks ok");
    return fails != 0;
}
