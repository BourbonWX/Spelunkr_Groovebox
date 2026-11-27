/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* SAMPLE: IMA ADPCM sample sets. */
static const char *const N_ONOFF_E[] = {"OFF", "ON"};
/* IMA ADPCM sample sets (tools/gen_samples.py), native rates, zones across
 * the keyboard, loops with the ADPCM state stored at the loop start.
 * SET is P_E0 (EDIT 1, KNOB 1). SET 4 was PERC, the GM drum kit: retired, an alias of PIANO; its sounds
 * load as the DRUM engine (core.h drum_from_perc). */
typedef struct {
    uint32_t off, n, ls, le;     /* byte offset, samples, loop start / end (sample index) */
    uint32_t rate;               /* source rate / 44100, Q16 */
    int16_t root16;              /* root pitch in 1/16 semitone */
    int16_t pred;                /* ADPCM state at the loop start */
    uint8_t idx, lo, hi, looped;
} smp_zone_t;
typedef struct {
    const char *name;
    uint16_t z0, nz;
} smp_set_t;
#include "felucca_samples.h"
#if defined(SMP_PERC_SLOT) && SMP_PERC_SLOT != SMP_SET_PERC
#error "tools/gen_samples.py PERC_SLOT != core.h SMP_SET_PERC"
#endif

static const int16_t IMA_STEP[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97,
    107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796,
    876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428,
    4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350,
    22385, 24623, 27086, 29794, 32767};
static const int8_t IMA_IDX[8] = {-1, -1, -1, -1, 2, 4, 6, 8};
/* 2^(i/192), Q16: pitch ratios in 1/16 semitones, d16 >= -3072 (16 octaves down) */
static uint32_t pow2_q16(int32_t d16)
{
    uint32_t u = (uint32_t)(d16 + 192 * 16), oct = u / 192u;     /* no loop for negative d16 */
    uint32_t r = pitch_inc(1600 + u % 192u) / (pitch_inc(1600) >> 16);   /* 2^(d/192) via the pitch table */
    return oct >= 16u ? r << (oct - 16u) : r >> (16u - oct);
}

/* ---- user sample slots (loaded from the web editor into flash, see web/EDITOR_PROTOCOL.md)
 * 3 slots of 80 KiB at flash 0xA0000.. (Felucca data region), read through the plain XIP
 * window. Slot = header (magic, count, name, data length, CRC32) + up to 16 zones in the
 * smp_zone_t layout (off relative to the slot's data at +512) + IMA ADPCM data. */
#include "../hal/fm1_xip.h"   /* relative: hostsim includes this file too */
#if SPK_NU                                          /* Spelunkr's web build: the user samples (core.h), whole then CHOP */
#define SMP_USER_SLOTS (2 * SPK_NU)
#define SMP_UZ 32u                                  /* (a CHOP: up to SPK_NSLICE zones) */
#else
#define SMP_USER_SLOTS 3
#define SMP_UZ 16u
#endif
#define SMP_USER_BASE 0xA0000u
#define SMP_USER_SIZE 0x14000u
#define SMP_USER_DATA 512u
#define SMP_USER_MAGIC 0x504D5346u                  /* "FSMP" */
/* RONDO: slot 3 (USR3) keeps its first 32 KiB; the 48 above are the PATTERN BANK's (storage.c ST_BANK_BASE) */
#define SMP_USER_TOP (RONDO_LAYOUT ? 0x8000u : SMP_USER_SIZE)
/* Dungeon Tracker: USR2's flash holds the song slots (song_store.c), 1.3 USR3's too: no sample there, nothing
 * written there (USR1 keeps its 80 KiB) */
#define SMP_USER_GONE(k) (RONDO_LAYOUT && ((k) == 1u || (k) == 2u))
static uint32_t smp_user_size(uint32_t k) { return SMP_USER_GONE(k) ? 0u : k == 2u ? SMP_USER_TOP : SMP_USER_SIZE; }
static int smp_user_ok(uint32_t k) { return k < SMP_USER_SLOTS && smp_user_size(k) > SMP_USER_DATA; }
#define SMP_NALL (SMP_NSETS + SMP_USER_SLOTS)
typedef struct {
    uint32_t magic;
    uint16_t version;
    uint8_t nz, rsv;
    char name[8];
    uint32_t data_len, crc, rsv2[2];
    smp_zone_t zone[16];
} smp_user_hdr_t;                                   /* 32 + 16 x 28 = 480 B, data at +512 */
static smp_zone_t usr_zone[SMP_USER_SLOTS][SMP_UZ];  /* RAM copy, off rebased onto SMP_DATA */
static uint8_t usr_nz[SMP_USER_SLOTS];
#if SPK_NU
static smp_zone_t usr_rzone[SPK_NU][SMP_UZ];        /* REV: each chop turned (felucca_web.c web_spk_commit), the zone's
                                                     * slot field 2 x SPK_NU + k (smp_zone) */
static uint8_t usr_rnz[SPK_NU];
#endif
#if SPK_NU
_Static_assert(SPK_NU == 8 && SPK_NSLICE <= SMP_UZ, "SMP_ALL_NAMES names 8 user samples");
static const char *const SMP_ALL_NAMES[SMP_NALL] = {SMP_SET_NAMES_INIT, spk_name[0], spk_name[1], spk_name[2], spk_name[3],
    spk_name[4], spk_name[5], spk_name[6], spk_name[7], spk_chop[0], spk_chop[1], spk_chop[2], spk_chop[3], spk_chop[4],
    spk_chop[5], spk_chop[6], spk_chop[7]};
#else
static const char *const SMP_ALL_NAMES[SMP_NALL] = {SMP_SET_NAMES_INIT, "USR1", "USR2", "USR3"};
#endif

#ifndef SMP_USER_XIP                                /* host tests: a RAM image of the slots */
#define SMP_USER_XIP(k) fm1_xip_ptr(SMP_USER_BASE + (k) * SMP_USER_SIZE)
#endif
static const uint8_t *smp_user_xip(uint32_t k) { return SMP_USER_XIP(k); }
#if FELUCCA_SLICE
static void slc_user_scan(uint32_t k, int valid);   /* eng_slice.c: SLICE's slice table of the slot */
#else
#define slc_user_scan(k, valid) ((void)0)
#endif
static uint32_t smp_user_gen;                       /* + 1 per slot scan (GRAIN: its seek index follows uploads) */

/* (re)read slot k from flash: valid header -> zones usable; call after boot and after an upload
 * (main loop: SLICE scans the slot's audio here). The zones are checked in a copy: an unusable slot leaves
 * every zone of it empty (n 0: a voice still on it ends), never a half-checked one */
static void smp_user_scan(uint32_t k)
{
    const smp_user_hdr_t *h = (const smp_user_hdr_t *)smp_user_xip(k);
    smp_zone_t zt[16];
    uint32_t i, base, nz = 0;
    smp_user_gen++;
#if SPK_NU
    (void)h; (void)zt; (void)i; (void)base; (void)nz;
    return;                                         /* (the web's user samples come from the page: felucca_web.c) */
#endif
    usr_nz[k] = 0;
    slc_user_scan(k, 0);
    if (smp_user_ok(k) && h->magic == SMP_USER_MAGIC && h->version == 1 && h->nz && h->nz <= 16u &&
        h->data_len <= smp_user_size(k) - SMP_USER_DATA) {
        base = (uint32_t)(uintptr_t)(smp_user_xip(k) + SMP_USER_DATA) - (uint32_t)(uintptr_t)SMP_DATA;
        for (nz = h->nz, i = 0; i < nz; i++) {
            smp_zone_t *z = &zt[i];
            *z = h->zone[i];
            if (z->off > h->data_len || z->n > 2u * SMP_USER_SIZE || z->off + (z->n + 1u) / 2u > h->data_len ||
                z->idx > 88u || !z->rate || z->rate > 4u << 16 || z->ls > z->le || z->le >= z->n || z->lo > z->hi) {
                nz = 0;                             /* zone outside the data or malformed: slot unusable */
                break;
            }
            z->off += base;
        }
    }
    for (i = 0; i < SMP_UZ; i++)
        usr_zone[k][i] = i < nz ? zt[i] : (smp_zone_t){0};
    if (!nz)
        return;
    usr_nz[k] = (uint8_t)nz;
    slc_user_scan(k, 1);
}

/* A missing sample (SAMPLE, GRAIN, SLICE: an empty or invalid user slot, a set this build has no data for) plays
 * this instead, like a hardware sampler with its sample gone: a plain sine at the note's pitch (+ d16, 1/16
 * semitones), at half the peak of a full sample, through the engine's one-pole low-pass (lp Q15, state *y) and the
 * voice's amp (the ADSR: no click at the start, phase 0, or the end). *ph its phase. The UI says so once
 * (engines.c snd_missing, ui_input.c sample_notice). */
static void smp_sine(int32_t *out, uint32_t n, const vmod_t *m, int32_t d16, int32_t lp, uint32_t *ph, int32_t *y)
{
    uint32_t i, p = *ph, inc = pitch_inc((uint32_t)clamp(m->pitch16 + d16, 0, 2047));
    int32_t s = *y;
    for (i = 0; i < n; i++, p += inc) {
        s += mulq15((sine_i(p) >> 1) - s, lp);
        out[i] += voice_amp(s, m, i) << 1;
    }
    *ph = p;
    *y = s;
}
/* SET / SRC si (0..SMP_NALL - 1) has no sample data: an empty or invalid user slot, a built-in set without data */
static int smp_set_missing(uint32_t si)
{
    if (si < SMP_NSETS)
        return !SMP_ZONES[SMP_SETS[si].z0].n;
    return !usr_nz[(si - SMP_NSETS) % SMP_USER_SLOTS];
}

/* zone index in a voice: < 0x8000 built-in, else 0x8000 | slot << 5 | zone */
static inline const smp_zone_t *smp_zone(uint32_t zi)
{
#if SPK_NU
    if (zi >= 0x8000u && ((zi >> 5) & 31u) >= 2u * SPK_NU)   /* (a chop turned: REV) */
        return &usr_rzone[(((zi >> 5) & 31u) - 2u * SPK_NU) % SPK_NU][zi & 31u];
#endif
    return zi < 0x8000u ? &SMP_ZONES[zi] : &usr_zone[((zi >> 5) & 31u) % SMP_USER_SLOTS][zi & 31u];
}

/* voice: ph[0] position (samples), ph[1] fraction Q16, s[0] predictor, s[1] step
 * index, s[2] previous sample, s[3] current sample, s[4] zone, s[5] step Q16 */
static inline int32_t sample_next(const smp_zone_t *z, voice_t *v, int loop)
{
    uint32_t pos = v->ph[0], b = SMP_DATA[z->off + (pos >> 1)];
    uint32_t code = (pos & 1u) ? (b >> 4) : (b & 15u);
    int32_t step = IMA_STEP[(uint32_t)v->s[1] <= 88u ? v->s[1] : 88], vd = step >> 3;
    if (code & 4u)
        vd += step;
    if (code & 2u)
        vd += step >> 1;
    if (code & 1u)
        vd += step >> 2;
    v->s[0] = clamp(v->s[0] + ((code & 8u) ? -vd : vd), -32768, 32767);
    v->s[1] = clamp(v->s[1] + IMA_IDX[code & 7u], 0, 88);
    pos++;
    if (pos > z->le && z->looped && loop) {
        pos = z->ls;
        v->s[0] = z->pred;
        v->s[1] = z->idx;
    }
    v->ph[0] = pos;
    return v->s[0];
}

/* Spelunkr (Nov 27 2026): a CHOP track's BANK B, a second sample's chops on the octave above (C4..B4): its slot in
 * P_LN3 (a DRUM lane's level, unused by SAMPLE; 127, the default, or anything past the slots: none; felucca_web.c
 * web_spk_use sets it). A voice playing it has SMP_ZI_BANKB in its zone (bit 10: smp_zone reads bits 5..9 and 0..4) */
#define SPK_BANK 12u                                /* the chops a bank holds: one octave */
#define SMP_ZI_BANKB 0x400u
#if SPK_NU
static uint32_t smp_bank_b(const track_t *t) { return t->p[P_LN3] >= 0 && t->p[P_LN3] < (int16_t)SPK_NU ? (uint32_t)t->p[P_LN3] : 0xFFu; }
static int smp_is_chop(const track_t *t) { return (uint32_t)t->p[P_E0] % SMP_NALL >= SMP_NSETS + SPK_NU; }
/* a chop bank's effects (Nov 27): BANK A in the SAMPLE's own TUNE, BITS, DRV and P_LN0 (REV), BANK B in P_LN4..7
 * (PITCH + 64, REV, CRUSH, SAT; web_spk_use sets them when the bank is made: a value from before reads as off) */
typedef struct { int16_t pitch, rev, crush, sat; } smp_fx_t;
static void smp_fx(const track_t *t, uint32_t bank, smp_fx_t *f)
{
    const int16_t *p = t->p;
    if (!bank) {
        f->pitch = (int16_t)clamp(p[P_E1], -24, 24);
        f->rev = p[P_LN0] == 1;
        f->crush = (int16_t)clamp(p[P_E2], 0, 127);
        f->sat = (int16_t)clamp(p[P_E6], 0, 127);
    } else {
        f->pitch = (int16_t)(p[P_LN4] >= 52 && p[P_LN4] <= 76 ? p[P_LN4] - 64 : 0);
        f->rev = p[P_LN5] == 1;
        f->crush = (int16_t)(p[P_LN6] >= 0 && p[P_LN6] <= 120 ? p[P_LN6] : 0);
        f->sat = (int16_t)(p[P_LN7] >= 0 && p[P_LN7] <= 120 ? p[P_LN7] : 0);
    }
}
static volatile uint32_t spk_struck[NTRK];          /* the chops each track started since the page last looked (felucca_web.c
                                                     * web_chops_on: the PADS view lights a short hit too) */
#endif
static void sample_note_on(track_t *t, voice_t *v)
{
    uint32_t si = (uint32_t)t->p[P_E0] % SMP_NALL, i, zi = 0xFFFFu, width = 128u;
    if (si < SMP_NSETS) {                           /* a built-in set: its zones split the keyboard */
        const smp_set_t *set = &SMP_SETS[si];
        for (i = 0; i < set->nz; i++)
            if (v->note >= SMP_ZONES[set->z0 + i].lo && v->note <= SMP_ZONES[set->z0 + i].hi) {
                zi = set->z0 + i;
                break;
            }
        v->s[4] = (int32_t)(zi == 0xFFFFu ? set->z0 : zi);
    } else {                                        /* user slot: silent if empty; the narrowest zone plays */
        uint32_t k = si - SMP_NSETS, note = v->note, bank = 0;
#if SPK_NU
        if (k >= SPK_NU && note >= SPK_CHOP_KEY + SPK_BANK && smp_bank_b(t) < SPK_NU) {   /* BANK B: C4..B4, the second
                                                     * sample's chops (an octave down onto its zones, played at pitch) */
            k = SPK_NU + smp_bank_b(t);
            note -= SPK_BANK;
            bank = SMP_ZI_BANKB;
        }
#endif
        for (i = 0; i < usr_nz[k]; i++) {
            const smp_zone_t *z = &usr_zone[k][i];
            if (note >= z->lo && note <= z->hi && z->hi - z->lo <= width) {
                zi = 0x8000u | bank | k << 5 | i;
                width = z->hi - z->lo;
            }
        }
#if SPK_NU
        if (zi != 0xFFFFu && k >= SPK_NU && usr_rnz[k - SPK_NU] > (zi & 31u)) {   /* REV: the bank's chops turned */
            smp_fx_t f;
            smp_fx(t, bank ? 1u : 0u, &f);
            if (f.rev)
                zi = 0x8000u | bank | (2u * SPK_NU + k - SPK_NU) << 5 | (zi & 31u);
        }
#endif
        v->s[4] = (int32_t)(zi == 0xFFFFu ? 0 : zi);
#if SPK_NU
        if (zi != 0xFFFFu && k >= SPK_NU && t >= trk && t < trk + NTRK)
            spk_struck[t - trk] |= 1u << ((zi & 31u) + (bank ? SPK_BANK : 0u));
#endif
    }
    v->ph[0] = 0;
    v->ph[1] = 0;
    v->s[0] = 0;
    v->s[1] = 0;
    v->s[2] = v->s[3] = 0;
    v->s[6] = zi == 0xFFFFu;     /* 1 = sample ended (one-shot); a kit key with no sound stays silent */
    v->s[7] = 0;                 /* lo-pass state */
    v->s[5] = 0;                 /* AGE: the held sample (high 16 bits), the hold's count (low 8) */
    v->ph[2] = (v->age + 1u) * 2654435761u ^ (uint32_t)v->note * 40503u;   /* WOW's phase: each note its own */
    if (smp_set_missing(si))
        v->s[6] = SPK_NU && si >= SMP_NSETS ? 1 : 2;   /* 2 = no sample data: the sine (smp_sine; ph[0] its phase); (Spelunkr:
                                                  * a user sample not here, a shared song's, is silent) */
}

static void sample_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    const smp_zone_t *z = smp_zone((uint32_t)v->s[4]);
    uint32_t i, frac = v->ph[1];
    /* Spelunkr: WOW (E5) the tape's warble, a slow wow and a faster flutter on the pitch, up to +-60 cents; AGE (E7)
     * a worn tape or a cheap sampler: hiss under the note, the rate held (down to 11 kHz, its aliasing) and the top
     * dulled. Both 0: the sample as it was */
    int32_t wow = clamp(p[P_E5], 0, 127), age = clamp(p[P_E7], 0, 127), wd = 0;
    uint32_t hold = 1u + (uint32_t)age * 3u / 127u, hc = (uint32_t)v->s[5] & 255u, rn;
    int32_t held = v->s[5] >> 16, hiss = age * age >> 4;          /* (0 .. 1008 of 32767: -30 dB at the top) */
    if (wow) {
        uint32_t ph = v->ph[2];
        wd = (sine_i(ph) * wow * 10 + sine_i(ph * 11u + 0x40000000u) * wow * 3) >> 22;   /* 1/16 st: up to ~+-13 */
        v->ph[2] = ph + 53568u * n;                                 /* 0.55 Hz (the flutter 11 times that) */
    }
    int32_t tune = p[P_E1], bits = p[P_E2], drv = p[P_E6];
#if SPK_NU
    if (((uint32_t)v->s[4] & 0x8000u) && (((uint32_t)v->s[4] >> 5) & 31u) >= SPK_NU) {   /* a chop: its bank's effects. PITCH
                                                     * is the rate (an SP-303's: lower is slower, higher faster); CRUSH
                                                     * the bits and the rate held; SAT the drive */
        smp_fx_t f;
        smp_fx(t, (uint32_t)v->s[4] & SMP_ZI_BANKB ? 1u : 0u, &f);
        tune = f.pitch;
        bits = f.crush;
        drv = f.sat;
        if (f.crush) {
            uint32_t ch = 1u + (uint32_t)f.crush * 5u / 120u;
            if (ch > hold)
                hold = ch;
        }
    }
#endif
    int32_t d16 = clamp(m->pitch16 + tune * 16 - z->root16 + wd - ((uint32_t)v->s[4] & SMP_ZI_BANKB ? 16 * SPK_BANK : 0),
                        -1536, 576);   /* <= 3 octaves up: bounded decode load */
    uint32_t r = pow2_q16(d16), stepq = (r >> 8) * (z->rate >> 8);   /* Q16 samples per output */
    int32_t lp = 4000 + ((clamp((p[P_E4] << 8) + m->cutoff - age * 48, 0, 127 << 8) * 28767) >> 15);
    int32_t sh = bits / 10;
    rn = v->ph[2] ^ (uint32_t)v->s[7] * 2654435761u;
    if (v->s[6] == 2) {                               /* no sample data: the sine at the note's pitch + TUNE */
        smp_sine(out, n, m, p[P_E1] * 16, lp, &v->ph[0], &v->s[7]);
        return;
    }
    if (v->s[6] || !z->n) {
        v->active = 0;
        return;
    }
    if (v->ph[0] == 0 && v->ph[1] == 0 && v->s[3] == 0)
        v->s[3] = sample_next(z, v, p[P_E3]);         /* prime the interpolator */
    for (i = 0; i < n; i++) {
        int32_t s;
        frac += stepq;
        while (frac >= 65536u) {
            frac -= 65536u;
            v->s[2] = v->s[3];
            if (v->ph[0] >= z->n) {                   /* one-shot reached its end */
                v->s[6] = 1;
                v->s[3] = 0;
                if (frac >= 65536u) {                /* past the last sample-to-zero interval */
                    v->s[2] = 0;
                    frac &= 65535u;
                }
                break;
            }
            v->s[3] = sample_next(z, v, p[P_E3]);
        }
        s = v->s[2] + (((v->s[3] - v->s[2]) * (int32_t)(frac >> 1)) >> 15);
        if (sh)                                       /* BITS: 0 = clean, up to 12 bits removed */
            s = (s >> sh) << sh;
        if (drv)
            s = softclip(s + (((s >> 2) * (drv * 150)) >> 11));   /* = s * drv * 600 / 32768, no overflow */
        if (age || hold > 1u) {                       /* AGE: the hiss, then the rate held (CRUSH: held, no hiss) */
            if (age) {
                rn = rn * 1664525u + 1013904223u;
                s += ((int32_t)rn >> 16) * hiss >> 15;
            }
            if (hc == 0u)
                held = clamp(s, -32768, 32767);
            hc = hc + 1u >= hold ? 0u : hc + 1u;
            s = held;
        }
        v->s[7] += mulq15(s - v->s[7], lp);           /* gentle tone control (CUT) */
        out[i] += voice_amp(v->s[7], m, i) << 1;
        if (v->s[6])
            break;
    }
    v->ph[1] = frac;
    v->s[5] = (int32_t)((uint32_t)(held & 0xFFFF) << 16 | (hc & 255u));
}

#if SPK_NU
/* Spelunkr: a user sample's CHOP plays slice i on key C3 + i exactly (felucca_web.c web_spk_commit: one zone a key).
 * Its keys are its own map, as a kit's (Ed, Nov 27 2026: a chop that wouldn't play from the keys): the octave buttons
 * move a whole keyboard of chops (C3..B3 BANK A, C4..B4 BANK B: sample_note_on), but no scale snap (KEY's "keys stay in key"), no
 * TRANSPOSE and no WHITE walk move a key onto another slice or off them all. The whole sample (a synth) and the
 * built-in sets keep the keys as ever */
static int32_t smp_keys(const track_t *t, uint32_t k)
{
    int32_t n;
    if (!smp_is_chop(t))
        return -1;
    n = (int32_t)SPK_CHOP_KEY + (int32_t)k + 12 * (int32_t)song.octave;   /* (BANK B: OCT+ from C3, or the keys past B3) */
    return n < 0 ? 0 : n > 127 ? 127 : n;
}
#endif
/* ONE-SHOT (Ed, Nov 27: "I like the one shot style"): a chop plays to its end mark whatever the key does, as an MPC's
 * pad: the ADSR is not its envelope (the slice's own fades are, spk_samples.js toInt16), the voice ends where the slice
 * does (sample_render). The whole sample and the built-in sets keep their ADSR */
#if SPK_NU
static int32_t smp_amp(track_t *t, voice_t *v, int32_t adsr)
{
    (void)t;
    if (!v->active)
        return 0;
    if (!((uint32_t)v->s[4] & 0x8000u) || (((uint32_t)v->s[4] >> 5) & 31u) < SPK_NU)
        return adsr;
    v->env = 1 << 24;                                 /* (the release never fades it) */
    return 32767;
}
#endif
static const engine_t ENG_SAMPLE = {
    .name = "SAMPLE",
    .page_title = {"SET", "TAPE"},
    .edit = {
        {"SET", F_ENUM, 0, (SPK_NU ? SMP_NALL : SMP_NSETS) - 1, 0, SMP_ALL_NAMES, 0},   /* (Spelunkr: the built-in sets,
                                                                     * the user samples, their CHOPs; no FM-1 slots) */
        {"TUNE", F_SEMI, -24, 24, 0, 0, 0},
        {"BITS", F_INT, 0, 127, 0, 0, 0},
        {"LOOP", F_ENUM, 0, 1, 1, N_ONOFF_E, 0},
        {"CUT", F_INT, 0, 127, 127, 0, 0},
        {"WOW", F_PCT, 0, 127, 0, 0, 0},
        {"DRV", F_PCT, 0, 127, 0, 0, 0},
        {"AGE", F_PCT, 0, 127, 0, 0, 0},
    },
    .presets = SMP_PRESET_TABLE,
    .npresets = SMP_NPRESETS,
    .note_on = sample_note_on,
    .render = sample_render,
#if SPK_NU
    .keys = smp_keys,
    .amp = smp_amp,
#endif
    .knob = {P_E0, P_E4, P_E5, P_E7},             /* Spelunkr: SET, CUT, WOW, AGE */
    .sampled = 1,
};
