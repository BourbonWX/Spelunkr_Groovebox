/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 RONDO contributors. Built on Felucca (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* RONDO's two engines: CYCLE KIT (engine 14) and CYCLE TONE (engine 15). Both are small operator-FM voices with
 * macro controls in the spirit of a "machine" groovebox: a few knobs per sound (PITCH, DECAY, COLOR, SWEEP, SHAPE)
 * instead of a full operator editor. All fixed point, as Felucca's other engines (engines/src/dsp.c).
 *
 * ------------------------------------------------------------------------------------------------ CYCLE KIT ---
 * Four drum voices, one voice each, playing at once: KICK, SNARE, HAT, PERC. Every voice has its own six values,
 * stored in the track's parameters that only Felucca's retired DIGITAL engine used (P_FMk_*, k = the voice), and
 * shown on EDIT's pages KICK, SNARE, HAT, PERC (PITCH DECAY COLOR SWEEP), SHAPE (one per voice) and LANES (the
 * voice levels, P_LN0..P_LN3):
 *   PITCH  P_FMk_ATK    +-24 semitones around the voice's designed pitch (64)
 *   DECAY  P_FMk_DEC    4 ms .. 4 s (the time it takes to fall 60 dB)
 *   COLOR  P_FMk_SUS    the FM index (KICK, PERC: with its own faster envelope), the snare's body and noise
 *                       brightness, the hat's metal
 *   SWEEP  P_FMk_REL    the pitch drop after the hit (KICK up to 4 octaves, SNARE and PERC up to 2, HAT none)
 *   SHAPE  P_FMk_LEVEL  KICK: drive into a soft clip (sine -> square-ish), SNARE: body <-> noise, HAT: the metal's
 *                       inharmonic ratio (8 steps), PERC: the operators' ratio (8 steps)
 *   LEVEL  P_LNk        square law, 100 % = full
 * EDIT 1 / EDIT 2 (P_E0..P_E7) act on the whole kit: TUNE (+-12 semitones), DECAY (x 1/4 .. x 4), COLOR (+-64),
 * OPEN (the open hat's length over the closed one), CLICK (a short transient on KICK SNARE PERC), VELO (velocity
 * sensitivity, 0 = every hit full), DRV (a soft clip on every hit) and VARI (a little random pitch and decay per hit).
 * All of them, the lanes' values included, can be parameter-locked per step (SEQ: hold a step, turn a knob).
 *
 * Notes: the General MIDI drum map, as Felucca's DRUM: 35 36 KICK; 38 40 SNARE; 42 44 closed HAT, 46 open HAT,
 * cymbals 49 51 52 55 57 59 a long open HAT; toms, rim, clap, cowbell, congas, claves, .. PERC at their own pitch.
 * So the step grid's eight rows (BD SD CP CH OH TM RS CB) all play, on the four voices, and a QY70 or any GM drum
 * part plays it as it is. A voice plays one hit at a time: a new hit on it starts over (the closed hat chokes the
 * open one by itself).
 *
 * ----------------------------------------------------------------------------------------------- CYCLE TONE ---
 * Two operators and a sub: a modulator (RATIO of the note, with FEEDBACK) into a carrier, the carrier optionally
 * doubled a few cents up (STACK) and a sine an octave down (SUB). COLOR is the FM index, CNTR how fast the index
 * falls after the note starts and MSUS where it rests (the timbre's own envelope, Model-machine style), SWEEP a
 * pitch blip at the start (up or, below 0, from under). The host's ADSR is the amplitude, so pads, chords, basses and
 * leads all come from the one engine; ENV DEST SHP / LFO DEST SHP and the matrix's SHAPE move COLOR. */
#include "cycle_tables.h"


static inline int32_t cyc_kb(int32_t i) { return CYC_KB[clamp(i, 0, CYC_NKB - 1)]; }
static inline int32_t cyc_mul30(int32_t a, int32_t k) { return (int32_t)(((int64_t)a * k) >> 30); }
/* an index amount 0..127 as the phase offset of a Q15 modulator sample (x 32768 = the offset in 2^32 of a turn):
 * square law, up to 2.9 radians */
static inline int32_t cyc_idx(int32_t c)
{
    c = clamp(c, 0, 127);
    return (c * c * 15) >> 2;
}
/* a Q15 sine of phase ph, phase-modulated by a Q15 sample md at index k (cyc_idx) */
static inline int32_t cyc_pm(uint32_t ph, int32_t md, int32_t k) { return sine_i(ph + (uint32_t)(md * k)); }

/* the operators' ratios, Q8 */
static const uint16_t CYC_RATIO[16] = {128, 256, 384, 512, 640, 768, 896, 1024, 1280, 1536, 1792, 2048,
                                       361, 707, 804, 1329};
static const char *const N_CYC_RATIO[] = {"0.5", "1", "1.5", "2", "2.5", "3", "3.5", "4", "5", "6", "7", "8",
                                          "1.41", "2.76", "3.14", "5.19", 0};

/* ===================================================================================================== KIT === */
enum { CK_KICK, CK_SNARE, CK_HAT, CK_PERC, CK_N };
#define CK_SILENT (1 << 16)                       /* the amplitude envelope's end: -84 dB of Q30 */
#define CK_CLICK_LEN 160u                         /* the CLICK transient, samples (3.6 ms) */
static const uint8_t CK_BASE[CK_N] = {31, 52, 79, 62};   /* the designed pitches: G1 49 Hz, E3, G5, D4 */
static const uint16_t CK_HAT_RATIO[8] = {361, 443, 617, 707, 804, 947, 1178, 1329};   /* 1.41 .. 5.19 */
static const uint16_t CK_PERC_RATIO[8] = {256, 512, 768, 384, 707, 896, 128, 1178};   /* 1 2 3 1.5 2.76 3.5 .5 4.6 */
static const char *const N_CK_LANE[CK_N] = {"KICK", "SNARE", "HAT", "PERC"};
static const char *const N_CK_ABBR[CK_N] = {"BD", "SD", "HH", "PC"};

/* the parameters of voice l */
#define CK_PITCH(l) (P_FM1_ATK + 5u * (l))
#define CK_DECAY(l) (P_FM1_DEC + 5u * (l))
#define CK_COLOR(l) (P_FM1_SUS + 5u * (l))
#define CK_SWEEP(l) (P_FM1_REL + 5u * (l))
#define CK_SHAPE(l) (P_FM1_LEVEL + 5u * (l))

/* General MIDI notes 35..81 -> {voice, semitones from its designed pitch, decay index offset (12 = x2; 120 an open
 * hat, 121 a cymbal: OPEN's length)} */
static const int8_t CK_GM[47][3] = {
    {CK_KICK, -2, 0}, {CK_KICK, 0, 0}, {CK_PERC, 10, -24}, {CK_SNARE, 0, 0}, {CK_PERC, 5, -6},        /* 35 36 37 38 39 */
    {CK_SNARE, 2, -6}, {CK_PERC, -14, 6}, {CK_HAT, 0, 0}, {CK_PERC, -11, 6}, {CK_HAT, -2, -6},         /* 40 .. 44 */
    {CK_PERC, -7, 6}, {CK_HAT, 0, 120}, {CK_PERC, -4, 6}, {CK_PERC, -2, 6}, {CK_HAT, -5, 121},         /* 45 .. 49 */
    {CK_PERC, 1, 6}, {CK_HAT, 3, 121}, {CK_HAT, -7, 121}, {CK_PERC, 17, 0}, {CK_HAT, 5, -12},          /* 50 .. 54 */
    {CK_HAT, -3, 121}, {CK_PERC, 7, -6}, {CK_HAT, -6, 121}, {CK_PERC, -9, 18}, {CK_HAT, 2, 121},       /* 55 .. 59 */
    {CK_PERC, 9, -12}, {CK_PERC, 4, -12}, {CK_PERC, 5, -18}, {CK_PERC, 2, -6}, {CK_PERC, -3, -6},      /* 60 .. 64 */
    {CK_PERC, 0, 0}, {CK_PERC, -5, 0}, {CK_PERC, 14, -12}, {CK_PERC, 11, -12}, {CK_HAT, 7, -12},       /* 65 .. 69 */
    {CK_HAT, 9, -18}, {CK_PERC, 19, 6}, {CK_PERC, 17, 12}, {CK_PERC, 12, -24}, {CK_PERC, 8, -24},      /* 70 .. 74 */
    {CK_PERC, 15, -24}, {CK_PERC, 13, -18}, {CK_PERC, 10, -18}, {CK_PERC, 6, -6}, {CK_PERC, 3, -6},    /* 75 .. 79 */
    {CK_PERC, 21, -18}, {CK_PERC, 21, 18},                                                             /* 80 81 */
};
/* the GM row of a note (other notes as their place in the octave from 36) */
static const int8_t *ck_gm(uint32_t note)
{
    uint32_t n = note >= 35u && note <= 81u ? note : 36u + (note + 120u - 36u) % 12u;
    return CK_GM[n - 35u];
}
static uint32_t ck_lane_of(uint32_t note) { return (uint32_t)ck_gm(note)[0]; }

/* 0.10: PERC (trk[PERC_TRK], the kit engine always): two slots, PERC 1 (notes below 48, C3 = 36 its own pitch) and
 * PERC 2 (48 and up, C4 = 48), each any drum sound of any kit: a kit (0..39) and one of the PC_NSND sounds (a GM note:
 * its voice, pitch and length, as on the kit). The slot's five values, LEVEL and MODEL are the source kit's for that
 * voice (pc_source), in the kit's lanes 0 and 1; the sources in lanes 2 / 3's PITCH (the kit) and DECAY (the sound
 * + 1, 0 the default: CLAP, SHAKER). A voice's s[0]: the slot (bits 0-1), the voice it plays XOR the slot (2-3), the
 * GM note (8-14) that picks the hat's / cymbal's flavour (the FM model); a kit's hit has s[0] = its lane as before */
#define PC_NSND 16u
static const uint8_t PC_GM[PC_NSND] = {36, 38, 37, 39, 42, 44, 46, 49, 51, 53, 45, 50, 56, 75, 63, 70};
static const char *const N_PC_SND[PC_NSND + 1u] = {"KICK", "SNARE", "RIM", "CLAP", "CL HAT", "PD HAT", "OP HAT",
                                                    "CRASH", "RIDE", "BELL", "LO TOM", "HI TOM", "COWBELL", "CLAVE",
                                                    "CONGA", "SHAKER", 0};
static const uint8_t PC_DEF[2] = {3u, 15u};
#define PC_KIT(sl) (P_FM3_ATK + 5u * (sl))              /* (lanes 2 / 3's PITCH: the slot's kit) */
#define PC_SND(sl) (P_FM3_DEC + 5u * (sl))              /* (lanes 2 / 3's DECAY: its sound + 1) */
static int trk_is_perc(const track_t *t) { return t == &trk[PERC_TRK]; }
static uint32_t pc_snd(const int16_t *p, uint32_t sl)
{
    int32_t v = p[PC_SND(sl & 1u)];
    return v >= 1 && v <= (int32_t)PC_NSND ? (uint32_t)v - 1u : PC_DEF[sl & 1u];
}
static uint32_t pc_slot(uint32_t note) { return drum_lane(note) & 1u; }   /* (the roll's lane 1 KICK: PERC 1, lane 2 SNARE:
                                                                            * PERC 2; others by their lane's parity) */
/* PERC's lanes on the roll: 1 and 2 its two sounds, the others none */
static const char *pc_lane_name(const track_t *t, uint32_t l, int abbr)
{
    static const char *const AB[PC_NSND] = {"BD", "SD", "RS", "CP", "CH", "PH", "OH", "CR", "RD", "BL", "LT", "HT", "CB",
                                            "CL", "CG", "SH"};
    static const char *const NM[PC_NSND] = {"KICK", "SNARE", "RIM", "CLAP", "HATCL", "HATPD", "HATOP", "CRASH", "RIDE",
                                            "BELL", "LOTOM", "HITOM", "COWBL", "CLAVE", "CONGA", "SHAKR"};
    if (l > 1u)
        return "-";
    return (abbr ? AB : NM)[pc_snd(t->p, l)];
}

/* a voice's state: s[0] its voice (lane), s[1] the amplitude envelope (Q30), s[2] the pitch envelope (Q30),
 * s[3] the index envelope (Q30; HAT: the feedback's last sample), s[4] the noise, s[5] a filter, s[6] the hit's
 * pitch offset (1/16 semitone), s[7] its decay offset (index steps); ph[2] the samples since the hit (HAT: its
 * second operator) */
static voice_t *ck_reuse(track_t *t, uint32_t note)
{
    uint32_t i;
    if (trk_is_perc(t)) {                               /* (0.10: PERC, its slot's voice) */
        for (i = 0; i < NVOICE; i++)
            if (t->v[i].active && ((uint32_t)t->v[i].s[0] & 3u) == pc_slot(note))
                return &t->v[i];
        return 0;
    }
    uint32_t lane = ck_lane_of(note);
    for (i = 0; i < NVOICE; i++)
        if (t->v[i].active && (uint32_t)t->v[i].s[0] == lane)
            return &t->v[i];
    return 0;
}

/* the velocity gain the VELO knob leaves of velocity vel, over the host's own (amp1 = this x vel), Q15 */
static int32_t ck_velo(const int16_t *p, uint32_t vel)
{
    int32_t v = vel ? (int32_t)vel : 1, target = 127 - (clamp(p[P_E5], 0, 127) * (127 - v)) / 127;
    return target * 32767 / v;
}

/* Phantom Cosmonaut: the NOISE kit (Ed, Nov 2026; GEN KIT's controls, map and lanes kept, its FM voices replaced):
 * no samples, no operators. KICK a sine swept down from far above (SWEEP how far), COLOR a knock (the sine bent on
 * itself), SHAPE drive; SNARE two sines (a fifth apart) under a burst of noise, COLOR its brightness, SHAPE body <-> noise; HAT noise
 * held at a rate (the pitch: grit; SHAPE drops the rate further, bits too), COLOR the high-pass; PERC a sine rung
 * against another at SHAPE's ratio (COLOR how much: a tom to a clank on the hull); CLICK noise on any attack. The noise is a
 * one-line generator (an LCG). State: s[1] the amplitude, s[2] the pitch envelope, s[3] a second filter, s[4] the
 * noise, s[5] a filter, s[6] the hit's pitch offset, s[7] its decay offset; ph[0], ph[1] the oscillators, ph[2] the
 * samples since the hit */
/* Phantom Tracker 0.9.10: the comb lines of a part, VULT's (eng_vult.c: VLT_POLY voices) or, on a kit, its COMB
 * voices' (four lanes of CKC_N each in the same bytes); a part plays one engine at a time */
#define VLT_POLY 3u
#define VLT_N 512u                                      /* VULT's comb line: half a period down to 43 Hz (F1) */
static int16_t vlt_buf[NPART - 1u][VLT_POLY][VLT_N] __attribute__((section(".pool")));   /* (zeroed at boot: main.c) */
static int16_t pc_comb[2u * 384u] __attribute__((section(".pool")));   /* 0.10: PERC's (kits only: its two slots' COMB lines,
                                                                     * not a VULT part's three: 1.5 KB kept) */
/* part p's kit COMB line for lane / slot l (CKC_N samples) */
#define CK_COMB_LINE(p, l) ((p) == PERC_TRK ? pc_comb + ((l) & 1u) * CKC_N : &vlt_buf[(p) % (NPART - 1u)][0][0] + (l) * CKC_N)
#ifndef CK_GAIN
#define CK_GAIN 32                                      /* the kit's output, x / 16 (0.9.15, Ed: "all of our drums need to be
                                                         * increased in volume"): +6 dB; the other sounds 4 dB under it in
                                                         * the mix (fx.c MIX_TRIM), the drums 10 dB up on them */
#endif
#define CKC_N 384u                                      /* a kit lane's: (VLT_POLY x VLT_N) / 4 */
typedef char ckc_fits[CKC_N * CK_N <= VLT_POLY * VLT_N ? 1 : -1];
/* (VULT's, eng_vult.c, here for the kit's COMB voices too) */
static inline int32_t vlt_sat(int32_t x)                /* Saturate_soft: 16 tanh(x / 16), Q16 */
{
    return softclip(clamp(x >> 5, -(1 << 24), 1 << 24)) << 5;
}
static uint32_t vlt_isqrt(uint32_t x)                   /* floor(sqrt(x)) */
{
    uint32_t r = 0, b = 1u << 30;
    while (b > x)
        b >>= 2;
    while (b) {
        if (x >= r + b) {
            x -= r + b;
            r = (r >> 1) + b;
        } else {
            r >>= 1;
        }
        b >>= 2;
    }
    return r;
}

/* Phantom Tracker 0.9.10 (Ed: "engine flavoured voices plus kits that use them"): each lane's MODEL, in the DRUM lane
 * levels the kit never used (P_LN4 + lane: KICK SNARE HAT PERC). 0 (and anything past 4: 127 from older sounds) the
 * NOISE synthesis below; 1 FM (two operators, GENESIS / FM's), 2 CHIP (SID's drums: noise frames, then a stepped
 * pulse), 3 COMB (VULT's resonant comb, pinged), 4 BLOOP (GLORP's vowel wah on a hit). The lane's five values keep
 * their places and take each model's meaning (ck_model_render) */
enum { CKM_NOISE, CKM_FM, CKM_CHIP, CKM_COMB, CKM_BLOOP, CKM_N };
/* Spelunkr: past BLOOP a lane plays a sampled set (spk_kits.h, tools/gen_spk_samples.py: CRATE .. SNES): MODEL
 * CKM_N + s is set s + 1, each lane its own (a sampled kick under an FM snare, ..) */
#include "spk_kits.h"
#define CKM_ALL (CKM_N + FK_NSET + SPK_NU)                 /* the MODEL values (+ the user samples' kits, core.h) */
#if SPK_NU
static const char *const N_CK_MODEL[] = {"NOISE", "FM", "CHIP", "COMB", "BLOOP", FK_SET_NAMES_INIT, spk_name[0], spk_name[1],
                                         spk_name[2], spk_name[3], spk_name[4], spk_name[5], spk_name[6], spk_name[7], 0};
_Static_assert(SPK_NU == 8, "N_CK_MODEL names 8 user kits");
#else
static const char *const N_CK_MODEL[] = {"NOISE", "FM", "CHIP", "COMB", "BLOOP", FK_SET_NAMES_INIT, 0};
#endif
static uint32_t ck_model(const int16_t *p, uint32_t lane)
{
    int32_t m = p[P_LN4 + (lane & 3u)];
    return m >= 1 && m < CKM_N ? (uint32_t)m : CKM_NOISE;
}

static inline int32_t nk_noise(uint32_t *r)
{
    *r = *r * 1664525u + 1013904223u;
    return (int32_t)*r >> 16;
}

/* ---- Phantom Cosmonaut M8: the kit's SET (Ed, Nov 2026: "bake Derelict and Shortwave into the firmware").
 * SET 0 is the NOISE synthesis above; 1 DERELICT and 2 SHORTWAVE play the factory hits (factory_tables.h: 16 kHz
 * IMA ADPCM, generated by tools/gen_factory_kits.py --header), one for each of the GRID's eight rows, through the
 * same four voices (so CH and OH still choke each other, the percs share PERC), mutes, LEVEL, VELO, DRV and VARI.
 * On a set: PITCH and TUNE the playback rate; DECAY (the voice's, the kit's) a fade over the hit, at 120 and above
 * none; OPEN the open hat's; COLOR the crunch (bits and the rate held), RIDE on top; SWEEP, SHAPE and CLICK rest.
 * SET lives in P_LN7, a DRUM lane level the kit never used: a sound from before (127 there) reads as NOISE.
 * Phantom Tracker 0.9.7 (Ed: "scrapping our four sampled drum sets", for room): the sets are gone, built only when
 * factory_tables.h carries them (FK_NSET); a sound with a SET plays the synthesis, and the kits 16..19 that were the
 * sets' are synthesized kits now (CK_KITS).
 * Voice state on a set: s[0] the lane, s[1] the amplitude (Q30, the fade), s[2] the decoder's predictor, s[3] its
 * step index, s[4] the hit (0..15) | 0x100 an open hat, s[5] the previous sample (interpolation), s[6] the pitch
 * offset (1/16 st), s[7] the crunch's held sample; ph[0] the samples decoded, ph[1] the fraction (Q16), ph[2] the
 * crunch's count */
#include "factory_tables.h"                                /* (DEBRIS's tapes; the sets only with --sets) */
#ifdef FK_NSET                                           /* (Phantom Tracker 0.9.7: the sets scrapped, factory_tables.h
                                                          * has only DEBRIS's tapes: tools/gen_factory_kits.py --sets) */
static const int8_t FK_IDX[8] = {-1, -1, -1, -1, 2, 4, 6, 8};
#define FK_MARK 0x5A5A0000                               /* s[4] of a voice playing a hit (the synthesis keeps its noise
                                                          * there): a voice keeps its kind while it rings, SET changed or not */
#define FK_IS(v) (((uint32_t)(v)->s[4] & 0xFFFF0000u) == (uint32_t)FK_MARK)
#if FK_NSET * 8 + SPK_NU * 8 > 256
#error "a hit's index is 8 bits (fk_note_on's s[4])"
#endif
static uint32_t fk_set(const int16_t *p, uint32_t lane)   /* the set lane `lane` plays (1..), 0 = a model's synthesis */
{
    int32_t m = p[P_LN4 + (lane & 3u)];
    return m >= CKM_N && m < CKM_ALL ? (uint32_t)(m - CKM_N + 1) : 0u;
}
/* the row a GM note plays on a set (0..7: BD SD CP CH OH TM RS CB), -1: the lane's own row at the note's GM pitch */
static int32_t fk_row(uint32_t note)
{
    switch (note) {
    case 35: case 36: return 0;
    case 38: case 40: return 1;
    case 39: return 2;
    case 42: case 44: return 3;
    case 46: return 4;
    case 45: return 5;
    case 37: return 6;
    case 56: return 7;
    default: return -1;
    }
}
/* hit k's data and length: the sets', then the user samples' slices (core.h; length 0: silent) */
static inline const uint8_t *fk_hit(uint32_t k, uint32_t *n)
{
    if (k < FK_NSET * 8u) {
        *n = FK_HIT[k].n;
        return FK_DATA + FK_HIT[k].off;
    }
#if SPK_NU
    if ((k -= FK_NSET * 8u) < SPK_NU * 8u) {
        *n = spk_hit_d[k] ? spk_hit_n[k] : 0u;
        return spk_hit_d[k];
    }
#endif
    *n = 0;
    return FK_DATA;
}
/* the next sample of the hit (IMA ADPCM, low nibble first), 0 past its end */
static inline int32_t fk_next(voice_t *v, const uint8_t *d, uint32_t hn)
{
    uint32_t pos = v->ph[0], b, code;
    int32_t step, vd;
    if (pos >= hn)
        return 0;
    b = d[pos >> 1];
    code = (pos & 1u) ? (b >> 4) : (b & 15u);
    step = FK_STEP[(uint32_t)v->s[3] <= 88u ? v->s[3] : 88];
    vd = step >> 3;
    if (code & 4u) vd += step;
    if (code & 2u) vd += step >> 1;
    if (code & 1u) vd += step >> 2;
    v->s[2] = clamp(v->s[2] + ((code & 8u) ? -vd : vd), -32768, 32767);
    v->s[3] = clamp(v->s[3] + FK_IDX[code & 7u], 0, 88);
    v->ph[0] = pos + 1u;
    return v->s[2];
}
static void fk_note_on(track_t *t, voice_t *v, uint32_t set, uint32_t note)
{
    const int8_t *g = ck_gm(note);
    int32_t row = fk_row(note), lane = g[0], vari = clamp(t->p[P_E7], 0, 127);
    uint32_t r = (v->age + 1u) * 2654435761u ^ (uint32_t)note * 40503u;
    int32_t ofs = 0;
    if (row < 0) {                                        /* a note off the eight rows: its lane's row, at its GM pitch */
        row = lane == CK_KICK ? 0 : lane == CK_SNARE ? 1 : lane == CK_HAT ? (g[2] >= 120 ? 4 : 3) : 5;
        ofs = (g[1] - (lane == CK_PERC ? -7 : 0)) * 16;   /* (the toms' row is 45's, GM -7) */
    }
    v->s[4] = FK_MARK | (int32_t)((set - 1u) * 8u + (uint32_t)row) | (row == 4 || g[2] >= 120 ? 0x100 : 0);
    v->s[2] = 0;
    v->s[3] = 0;
    v->s[5] = 0;
    v->s[6] = ofs + (vari ? (int32_t)((r >> 8) % (uint32_t)(2 * vari + 1)) - vari : 0) / 8;
    v->s[7] = 0;
    v->ph[0] = 0;
    v->ph[1] = 0;
    v->ph[2] = 0;
}
/* the playback rate for a pitch offset (1/16 st), Q16 samples of the hit an output sample: FK_RATE / FS x 2^(d/192) */
static uint32_t fk_inc(int32_t d16)
{
    uint32_t r = pitch_inc((uint32_t)(1600 + clamp(d16, -480, 432))) / (pitch_inc(1600) >> 16);   /* 2^(d/192), Q16 */
    return (uint32_t)(((uint64_t)r * (FK_RATE * 65536u / FS)) >> 16);
}
static void fk_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    uint32_t lane = (uint32_t)v->s[0] & 3u, i, pi = (uint32_t)(t - trk) % NPART, hit = (uint32_t)v->s[4] & 0xFFu, hn;
    const uint8_t *hd = fk_hit(hit, &hn);
    int32_t col = clamp(p[CK_COLOR(lane)] + p[P_E2] + mk_x[3] * 2 / 5, 0, 127);    /* the crunch (RIDE) */
    int32_t dv = ((v->s[4] & 0x100) ? 63 + clamp(p[P_E3], 0, 127) : p[CK_DECAY(lane)]) + p[P_E1] * 2;
    int32_t a0 = v->s[1], a1 = dv >= 120 ? a0 : cyc_mul30(a0, cyc_kb(clamp(dv, 0, 119) * 120 / 127 + 12));
    int32_t lv = clamp(p[P_LN0 + lane], 0, 127), drv = clamp(p[P_E6], 0, 127), g = 0, mk = 0, da;
    int32_t mute = (int32_t)((drum_mute[pi] >> lane) & 1u), y[CTL];
    uint32_t inc = fk_inc((p[CK_PITCH(lane)] - 64) * 6 + p[P_E0] * 3 + v->s[6]), frac = v->ph[1];
    uint32_t sh = (uint32_t)col * 11u / 127u, hold = 1u + (uint32_t)col * 7u / 127u, hc = v->ph[2];
    int32_t prev = v->s[5], held = v->s[7];
    vmod_t ml;
    if (n > CTL)
        n = CTL;
    if (lv < 127) {                                       /* the voice's LEVEL, as the synthesis */
        int32_t gl = lv * lv * 2 + (lv * lv >> 6);
        ml = *m;
        ml.amp0 = (m->amp0 >> 4) * gl >> 11;
        ml.amp1 = (m->amp1 >> 4) * gl >> 11;
        m = &ml;
    }
    da = (a1 - a0) / (int32_t)n;
    for (i = 0; i < n; i++) {
        int32_t cur = v->s[2], s;
        frac += inc;
        while (frac >= 65536u) {                          /* the hit's own samples passed by this output sample */
            prev = cur;
            cur = fk_next(v, hd, hn);
            frac -= 65536u;
        }
        s = prev + (int32_t)(((int64_t)(cur - prev) * (int32_t)frac) >> 16);
        if (sh) {                                         /* COLOR: the crunch, the rate held and the bits dropped */
            if (hc == 0u)
                held = (s >> sh) << sh;
            hc = hc + 1u >= hold ? 0u : hc + 1u;
            s = held;
        }
        y[i] = (s * ((a0 + da * (int32_t)i) >> 15)) >> 15;
    }
    v->s[5] = prev;
    v->s[7] = held;
    v->ph[1] = frac;
    v->ph[2] = hc;
    v->s[1] = v->ph[0] >= hn ? 0 : a1;                   /* (past its end: let go, ck_amp) */
    if (mute)
        return;
    if (drv) {                                            /* DRV, as the synthesis */
        g = 4096 + drv * 3 * 4096 / 127;
        mk = (int32_t)((19661u << 15) / (uint32_t)softclip((19661 * g) >> 12));
    }
    for (i = 0; i < n; i++) {
        int32_t s = y[i];
        if (g)
            s = (softclip((s * g) >> 12) * mk) >> 15;
        out[i] += (voice_amp(soft_knee(s, 30000), m, i) * CK_GAIN) >> 4;   /* (Spelunkr: as loud as the synthesis) */
    }
}

#endif

/* the four engine-flavoured models (ck_model): voice v of lane `lane` fills y[0..n) at the hit's amplitude a0 (Q30,
 * falling by da a sample), the block's phase increment inc (the pitch, SWEEP's drop on it), COLOR col and SHAPE shp
 * (0..127); the voice's state through the pointers (ck_render's locals: ph0 ph1, f2 = s[3], flt = s[5], nz = s[4]) and
 * ph2 (the samples since the hit, at the block's start). Per model:
 *   FM     a carrier and a modulator at SHAPE's ratio (the hat's inharmonic eight, else 1 2 3 1.5 2.76 3.5 0.5 4.6);
 *          COLOR the index, which falls ~3x faster than the hit (f2, Q30); the snare's noise under it as NOISE's. The
 *          hat lane: feedback FM by the note (a hat, a ride, a crash: the CKM_FM case)
 *   CHIP   SID's drums: SHAPE's first frames (10 ms each) noise, then the tone, its pitch stepped a frame at a time
 *          (f2: the held increment, ph1 the samples to the next frame); COLOR triangle (low) .. a narrowing pulse;
 *          32 volume steps, 7 bits. The hat: noise clocked at its pitch, SHAPE past the middle a short loop (metal)
 *   COMB   VULT's resonant comb (eng_vult.c, Vult's Rescomb) on a burst of noise: half the period of the pitch, COLOR
 *          the ring (feedback 0.6 .. 0.99), SHAPE the tone (left hollow, right bright); the kick struck by one period of
 *          a sine at its pitch (ph0, ph1 the period's end), the others by 3 ms of noise; its line CKC_N samples of the
 *          part's vlt_buf (f2 the write position, flt the comb's output); the snare keeps a little noise (its wires)
 *   BLOOP  GLORP's mouth on a hit: a glottal pulse through a resonant band-pass that opens from OO towards COLOR's
 *          height (600 Hz .. 3 kHz) and closes twice as fast as the hit; SHAPE the resonance (Q 4 .. 20); SWEEP the dip
 *          (f2 the filter's low, flt its band) */
static void ck_model_render(uint32_t model, uint32_t lane, uint32_t sl, uint32_t note, track_t *t, int32_t *y, uint32_t n, uint32_t inc,
                            int32_t a0, int32_t da, int32_t col, int32_t shp, int32_t didx, uint32_t ph2, uint32_t *pph0,
                            uint32_t *pph1, int32_t *pf2, int32_t *pflt, uint32_t *pnz)
{
    uint32_t i, ph0 = *pph0, ph1 = *pph1, nz = *pnz;
    int32_t f2 = *pf2, flt = *pflt;
    switch (model) {
    case CKM_FM:
        if (lane == CK_HAT) {                             /* 0.9.12 (Ed: "the rhythmic dings ... sound so similar"): the hats
                                                           * and cymbals, the YM2612's way: the modulator fed back on itself
                                                           * (into noise as it rises), the carrier at SHAPE's inharmonic
                                                           * ratio below it, a first-difference noise for the sizzle. The
                                                           * note picks the recipe: a hat (42 44 46) bright and noisy, its
                                                           * index falling with the hit; a ride (51 59) a cleaner ping over a
                                                           * little wash, its index falling slowly; a crash (49 52 55 57)
                                                           * near-full feedback and noise, the index held: a wash. The noise
                                                           * also bends the carrier's phase (kn): metal that never settles
                                                           * into a ding. COLOR how much metal; flt the modulator's last
                                                           * sample */
            uint32_t minc = (inc >> 8) * CK_HAT_RATIO[shp >> 4];
            int ride = note == 51u || note == 59u, crash = note == 49u || note == 52u || note == 55u || note == 57u;
            int32_t kfb = crash ? 56000 + col * 50 : ride ? 40000 + col * 70 : 52000 + col * 80;   /* (feedback 2.5 .. 3 radians:
                                                       * chaos, noise to the end; a ride's ~2: a ping on the edge of it) */
            int32_t ki = crash ? 30000 + col * 150 : ride ? 9000 + col * 130 : 20000 + col * 200;
            int32_t nm = (crash ? 9000 : ride ? 3000 : 5000) + col * 60, kn = crash ? 40000 : ride ? 9000 : 30000 + col * 100, ka, k1;   /* (kn: the
                                                       * noise's own phase modulation of the carrier: the shimmer) */
            if (!ph2)
                f2 = 1 << 30;
            ka = f2 >> 15;
            f2 = crash ? f2 : cyc_mul30(f2, cyc_kb(ride ? didx + 12 : didx));   /* (a ride's ping outlasts its wash) */
            k1 = f2 >> 15;
            for (i = 0; i < n; i++) {
                int32_t a = (a0 + da * (int32_t)i) >> 15, e = ka + (((k1 - ka) * (int32_t)i) >> 5), k = (ki * e) >> 15;
                int32_t mo = sine_i(ph1 + (uint32_t)(flt * kfb)), sg, w0 = (int32_t)nz >> 16, w = nk_noise(&nz);
                flt = mo;
                sg = sine_i(ph0 + (uint32_t)(mo * k) + (uint32_t)(w * kn));
                ph0 += inc;
                ph1 += minc;
                sg = (sg * (32767 - nm) + clamp((w - w0) >> 1, -32767, 32767) * nm) >> 15;
                y[i] = (clamp(sg, -32767, 32767) * a) >> 15;
            }
            break;
        }
        {
        uint32_t minc = (inc >> 8) * CK_PERC_RATIO[shp >> 4];
        int32_t ki, ka, nm = lane == CK_SNARE ? 14000 : 0, kq = 2000 + col * 180;
        if (!ph2)
            f2 = 1 << 30;                                 /* (the index from the top at the hit) */
        ka = f2 >> 15;
        f2 = cyc_mul30(f2, cyc_kb(didx - 18));
        ki = col * col * 4;                               /* (up to ~3 radians) */
        for (i = 0; i < n; i++) {
            int32_t a = (a0 + da * (int32_t)i) >> 15, k = (ki * (ka + ((((f2 >> 15) - ka) * (int32_t)i) >> 5))) >> 15;
            int32_t mo = sine_i(ph1), sg = sine_i(ph0 + (uint32_t)(mo * k));
            ph0 += inc;
            ph1 += minc;
            if (nm) {                                     /* (the snare's wires: high-passed noise) */
                int32_t w = nk_noise(&nz);
                flt += (((w - flt) >> 1) * (32767 - kq)) >> 14;
                sg = (sg * (32767 - nm) + clamp(w - flt, -32767, 32767) * nm) >> 15;
            }
            y[i] = (clamp(sg, -32767, 32767) * a) >> 15;
        }
        }
        break;
    case CKM_CHIP: {
        uint32_t nfr = lane == CK_SNARE ? (uint32_t)shp >> 4 : (uint32_t)shp >> 5, pw;
        pw = col <= 42 ? 0u : 0x80000000u - (uint32_t)(col - 42) * 22000000u;   /* (0: the triangle) */
        if (!ph2) {
            f2 = (int32_t)inc;
            ph1 = 441u;
        }
        for (i = 0; i < n; i++) {
            int32_t a = ((a0 + da * (int32_t)i) >> 15) & ~0x3FF, sg;   /* (32 volume steps) */
            uint32_t fr = (ph2 + i) / 441u;
            if (lane == CK_HAT) {                         /* noise clocked at the pitch (x 8); SHAPE: a short loop */
                uint32_t c = ph0;
                ph0 += inc << 3;
                if (ph0 < c)
                    flt = shp > 63 ? (int32_t)((nz = (nz * 75u + 74u) % 127u) * 516u) - 32767 : nk_noise(&nz);
                sg = flt;
            } else if (fr < nfr) {                        /* the first frames: noise */
                sg = nk_noise(&nz);
            } else {
                if (!--ph1) {                             /* a new frame: the pitch steps */
                    f2 = (int32_t)inc;
                    ph1 = 441u;
                }
                ph0 += (uint32_t)f2;
                sg = pw ? osc_pulse(ph0, (uint32_t)f2, pw) * 3 / 2 : osc_tri(ph0) * 3 / 4;   /* (the pulse band-limited) */
            }
            y[i] = (((clamp(sg, -32767, 32767) * a) >> 15) >> 9) << 9;   /* (7 bits) */
        }
        break;
    }
    case CKM_COMB: {
        int16_t *line = CK_COMB_LINE((uint32_t)(t - trk) % NPART, sl);   /* (the slot's line) */
        int32_t fb = 39321 + col * 205, tn = clamp((shp - 64) * 2, -127, 127), st;   /* (feedback 0.6 .. 0.997, Q16) */
        uint32_t per = (inc >> 8) ? 0x80000000u / (inc >> 8) : (CKC_N - 2u) << 8, wp = (uint32_t)f2 % CKC_N;
        if (per > ((CKC_N - 2u) << 8))
            per = (CKC_N - 2u) << 8;
        if (per < (2u << 8))
            per = 2u << 8;
        st = (int32_t)vlt_isqrt((uint32_t)(tn < 0 ? -tn : tn) * 33818640u);   /* Rescomb.toneCurve: sqrt(|t|), Q16 */
        st = tn < 0 ? -st : st;
        for (i = 0; i < n; i++) {
            int32_t a = (a0 + da * (int32_t)i) >> 15, x = 0, si, d, fr;
            uint32_t r, t1, t2, k = ph2 + i;
            if (lane == CK_KICK) {                        /* the kick's strike: one period of a sine at its pitch */
                if (!ph1) {
                    uint32_t c = ph0;
                    ph0 += inc;
                    x = sine_i(c);
                    ph1 = ph0 < c;
                }
            } else if (k < 132u) {                        /* the others': 3 ms of falling noise */
                x = (nk_noise(&nz) * (int32_t)(132u - k)) / 132;
            }
            if (lane == CK_SNARE)
                x += nk_noise(&nz) >> 3;                  /* (the snare's wires, under the ring) */
            si = vlt_sat(((x << 1) + (int32_t)(((int64_t)flt * fb) >> 16)));   /* (Q16: the input and the feedback) */
            wp = wp + 1u >= CKC_N ? 0u : wp + 1u;
            line[wp] = (int16_t)clamp(si >> 2, -32768, 32767);
            r = ((wp << 8) + (CKC_N << 8) - per) % (CKC_N << 8);
            t1 = r >> 8;
            t2 = t1 + 1u >= CKC_N ? 0u : t1 + 1u;
            fr = (int32_t)(r & 255u);
            d = (line[t1] + (((line[t2] - line[t1]) * fr) >> 8)) << 2;
            flt = (x << 1) + (int32_t)(((int64_t)st * d) >> 16);
            y[i] = (clamp(vlt_sat(flt) >> 1, -32767, 32767) * a) >> 15;
        }
        f2 = (int32_t)wp;
        break;
    }
    default: {                                            /* CKM_BLOOP */
        int32_t ao = a0 >> 15, o = (ao * ao) >> 15, fq, q, fc, dq, lo = f2, bp = flt;
        o = ph2 < 88u ? o * (int32_t)ph2 / 88 : o;        /* (it opens in 2 ms, closes at twice the hit's speed) */
        fq = 250 + (((350 + col * 22) * o) >> 15);
        q = 4 + shp / 8;
        fc = sine_i((uint32_t)clamp(fq, 80, 7000) * 48696u) >> 2;   /* 2 sin(pi f / FS), Q12 */
        dq = 4096 / q;
        for (i = 0; i < n; i++) {
            int32_t a = (a0 + da * (int32_t)i) >> 15, g = 0, x, h;
            if (ph0 < 1717986918u) {                      /* GLORP's glottal pulse */
                int32_t sn = sine_i(ph0 + (ph0 >> 2));
                g = (sn * sn) >> 15;
            }
            ph0 += inc;
            x = (g - 8192) >> 3;
            h = x - lo - ((dq * bp) >> 12);
            bp = clamp(bp + ((fc * h) >> 12), -250000, 250000);
            lo = clamp(lo + ((fc * bp) >> 12), -250000, 250000);
            y[i] = (softclip(clamp(bp * 20 + lo * (lane == CK_KICK ? 16 : 6), -(1 << 24), 1 << 24)) * a) >> 15;   /* (the low: its body) */
        }
        f2 = lo;
        flt = bp;
        break;
    }
    }
    *pph0 = ph0;
    *pph1 = ph1;
    *pf2 = f2;
    *pflt = flt;
    *pnz = nz;
}

static void ck_note_on(track_t *t, voice_t *v)
{
    const int8_t *g = ck_gm(v->note);
    const int16_t *p = t->p;
    uint32_t lane = (uint32_t)g[0], r = (v->age + 1u) * 2654435761u ^ (uint32_t)v->note * 40503u, gmn = 0, sl;
    int32_t dofs = g[2], vari = clamp(p[P_E7], 0, 127), semi = g[1];
    if (trk_is_perc(t)) {                                 /* 0.10: PERC: the slot's sound, the key its pitch */
        sl = pc_slot(v->note);
        gmn = PC_GM[pc_snd(p, sl)];
        g = ck_gm(gmn);
        dofs = g[2];
        semi = g[1] + (int32_t)v->note - (sl ? 48 : 36);
        lane = sl;                                        /* (the slot: its values, its line, its mute) */
    }
    if (dofs >= 120)                                      /* an open hat (120) or a cymbal (121): OPEN's length */
        dofs = 12 + clamp(p[P_E3], 0, 127) * 48 / 127 + (dofs == 121 ? 12 : 0);   /* x2 .. x32 */
    v->s[0] = (int32_t)(lane | ((((uint32_t)g[0] ^ lane) & 3u) << 2) | (gmn << 8));
    if (lane == CK_KICK && t == &trk[0])
        duck_kick();                                      /* (the INSERT's DUCK on the other tracks: core.h) */
    v->s[1] = 1 << 30;
    v->s[2] = 1 << 30;
    v->s[3] = 0;
    v->s[4] = (int32_t)(r | 1u);                         /* (the noise) */
    v->s[5] = 0;
    v->s[6] = semi * 16 + (vari ? (int32_t)((r >> 8) % (uint32_t)(2 * vari + 1)) - vari : 0) / 8;
    v->s[7] = dofs + (vari ? (int32_t)((r >> 20) % (uint32_t)(vari / 8 + 1)) - vari / 16 : 0);
    v->ph[0] = 0;
    v->ph[1] = 0;
    v->ph[2] = 0;
    if (ck_model(p, lane) == CKM_COMB && (uintptr_t)t >= (uintptr_t)&trk[0] && (uintptr_t)t < (uintptr_t)&trk[NPART]) {
        int16_t *line = CK_COMB_LINE((uint32_t)(t - trk), lane);   /* (a COMB hit rings from silence) */
        uint32_t k;
        for (k = 0; k < CKC_N; k++)
            line[k] = 0;
    }
    v->env = 1 << 24;                                     /* the ADSR held at full: a hit, not a note */
    v->env_out = mulq15(ck_velo(p, v->vel), v->vel * 258);   /* (no ramp up from 0) */
#ifdef FK_NSET
    if (fk_set(p, lane))                                  /* a sampled set: its hit, not the synthesis */
        fk_note_on(t, v, fk_set(p, lane), gmn ? gmn : v->note);
#endif
}

/* the voice's amplitude is the hit (s[1], in the render); the ADSR only keeps it alive. Ends it when it has rung
 * out, or when another voice took its lane */
static int32_t ck_amp(track_t *t, voice_t *v, int32_t adsr)
{
    (void)adsr;
    if (!v->active)
        return 0;
    if (v->s[1] < CK_SILENT) {
        v->active = v->gate = 0;
        v->stage = 0;
        v->env = 0;
        return 0;
    }
    v->env = 1 << 24;
    return ck_velo(t->p, v->vel);
}

static void ck_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
#ifdef FK_NSET
    if (FK_IS(v)) {                                       /* a factory hit (M8) */
        fk_render(t, v, out, n, m);
        return;
    }
#endif
    uint32_t sl = (uint32_t)v->s[0] & 3u, lane = (sl ^ ((uint32_t)v->s[0] >> 2)) & 3u, i, pi = (uint32_t)(t - trk) % NPART,
             model = ck_model(p, sl), rnote = ((uint32_t)v->s[0] >> 8) & 127u;   /* (0.10: sl the values' slot, lane the
                                                           * voice it plays: the same on a kit, PERC's sound's) */
    int32_t a0 = v->s[1], pe0 = v->s[2], f2 = v->s[3], flt = v->s[5];
    uint32_t ph0 = v->ph[0], ph1 = v->ph[1], ph2 = v->ph[2], nz = (uint32_t)v->s[4] | 1u;
    int32_t col = clamp(p[CK_COLOR(sl)] + p[P_E2] + mk_x[3] * 2 / 5, 0, 127), shp = clamp(p[CK_SHAPE(sl)], 0, 127);   /* (RIDE) */
    int32_t didx = p[CK_DECAY(sl)] * 120 / 127 + 12 + p[P_E1] * 24 / 64 + v->s[7];
    int32_t a1 = cyc_mul30(a0, cyc_kb(didx)), pe1, p16, sw16, lv = clamp(p[P_LN0 + sl], 0, 127);
    int32_t click = lane == CK_HAT ? 0 : clamp(p[P_E4], 0, 127), drv = clamp(p[P_E6], 0, 127), g = 0, mk = 0;
    int32_t y[CTL], da, mute = (drum_mute[pi] >> sl) & 1u;
    uint32_t inc;
    vmod_t ml;
    if (n > CTL)
        n = CTL;
    /* pitch: the voice's, the kit's TUNE, the hit's, then SWEEP's drop on top (the pitch envelope's own speed) */
    sw16 = lane == CK_KICK ? p[CK_SWEEP(sl)] * 768 / 127 : lane == CK_HAT && !model ? 0 : p[CK_SWEEP(sl)] * 384 / 127;
    pe1 = cyc_mul30(pe0, cyc_kb((lane == CK_KICK ? 48 : lane == CK_SNARE ? 48 : 54) + (p[CK_DECAY(sl)] - 64) / 6));
    p16 = (int32_t)CK_BASE[lane] * 16 + (p[CK_PITCH(sl)] - 64) * 6 + p[P_E0] * 3 + v->s[6];
    inc = pitch_inc((uint32_t)clamp(p16 + (int32_t)(((int64_t)sw16 * pe0) >> 30), 0, 2047));
    if (lv < 127) {                                       /* the voice's LEVEL on the block's amplitude ramp */
        int32_t gl = lv * lv * 2 + (lv * lv >> 6);
        ml = *m;
        ml.amp0 = (m->amp0 >> 4) * gl >> 11;
        ml.amp1 = (m->amp1 >> 4) * gl >> 11;
        m = &ml;
    }
    da = (a1 - a0) / (int32_t)n;
    if (model)                                            /* 0.9.10: an engine-flavoured voice */
        ck_model_render(model, lane, sl, rnote ? rnote : v->note, t, y, n, inc, a0, da, col, shp, didx, ph2, &ph0, &ph1, &f2, &flt, &nz);
    else
    switch (lane) {
    case CK_KICK: {                                       /* a sine swept down; COLOR bends it on itself (a knock, an
                                                           * edge); SHAPE drive */
        int32_t sg = 4096 + shp * 229, nk = 0, kc = col * col * 2;
        if (shp)
            nk = (int32_t)((32767u << 15) / (uint32_t)softclip((32767 * sg) >> 12));
        for (i = 0; i < n; i++) {
            int32_t a = (a0 + da * (int32_t)i) >> 15, s = sine_i(ph0 + (uint32_t)(sine_i(ph0) * kc));
            ph0 += inc;
            if (shp)
                s = (softclip((s * sg) >> 12) * nk) >> 15;
            y[i] = (clamp(s, -32767, 32767) * a) >> 15;
        }
        break;
    }
    case CK_SNARE: {                                      /* two sines a fifth apart (the body, falling fast) under noise
                                                           * through a one-pole high-pass (COLOR its corner) */
        int32_t nm = shp * 258, ab = (int32_t)(((int64_t)a0 * (a0 >> 15)) >> 30), kq = 2000 + col * 180;
        uint32_t minc = (inc >> 8) * 384u;
        for (i = 0; i < n; i++) {
            int32_t a = (a0 + da * (int32_t)i) >> 15, w = nk_noise(&nz), body, hp;
            int32_t abi = ab + (((a * a) >> 15) - ab) * (int32_t)i / (int32_t)n;
            body = (sine_i(ph0) + (sine_i(ph1) >> 1)) * 2 / 3;
            ph0 += inc;
            ph1 += minc;
            flt += (((w - flt) >> 1) * (32767 - kq)) >> 14;
            hp = w - flt;
            y[i] = (((body * abi) >> 15) * (32767 - nm) + ((clamp(hp, -32767, 32767) * a) >> 15) * nm) >> 15;
        }
        break;
    }
    case CK_HAT: {                                        /* noise held at the pitch's rate (SHAPE: slower, fewer bits),
                                                           * a high-pass (COLOR: up) */
        uint32_t hold = 1u + (uint32_t)shp / 16u, sh = (uint32_t)shp / 16u;
        int32_t kq = 4000 + (127 - col) * 200;
        uint32_t step = inc << 3;                          /* (the hold's clock, 8 x the pitch: PITCH moves the grit) */
        for (i = 0; i < n; i++) {
            int32_t a = (a0 + da * (int32_t)i) >> 15, hp;
            uint32_t c = ph1;
            ph1 += step;
            if (ph1 < c || ++ph0 >= hold) {               /* (a clock: a new noise value) */
                ph0 = 0;
                f2 = (nk_noise(&nz) >> sh) << sh;
            }
            flt += (((f2 - flt) >> 1) * (32767 - kq)) >> 14;
            hp = f2 - flt;
            y[i] = (clamp(hp, -32767, 32767) * a) >> 15;
        }
        break;
    }
    default: {                                            /* PERC: a sine rung against another at SHAPE's ratio (COLOR
                                                           * how much) */
        uint32_t minc = (inc >> 8) * CK_PERC_RATIO[shp >> 4];
        int32_t rm = col * 258;
        for (i = 0; i < n; i++) {
            int32_t a = (a0 + da * (int32_t)i) >> 15, c = sine_i(ph0), r = sine_i(ph1), s;
            ph0 += inc;
            ph1 += minc;
            s = c + ((((c * r) >> 15) - c) * rm >> 15);
            y[i] = (clamp(s, -32767, 32767) * a) >> 15;
        }
        break;
    }
    }
    if (click && ph2 < CK_CLICK_LEN) {                    /* CLICK: a falling burst of noise at the hit */
        for (i = 0; i < n && ph2 + i < CK_CLICK_LEN; i++) {
            int32_t r = (int32_t)(CK_CLICK_LEN - ph2 - i), w = nk_noise(&nz);
            y[i] += ((w * click) >> 8) * r / (int32_t)CK_CLICK_LEN * r / (int32_t)CK_CLICK_LEN;
        }
    }
    ph2 = ph2 + n < 65536u ? ph2 + n : 65536u;            /* (the click counter, held at its end) */
    v->s[1] = a1;
    v->s[2] = pe1;
    v->s[3] = f2;
    v->s[4] = (int32_t)nz;
    v->s[5] = flt;
    v->ph[0] = ph0;
    v->ph[1] = ph1;
    v->ph[2] = ph2;
    if (mute)                                             /* a muted voice rings on unheard (the EDIT layer's keys) */
        return;
    if (drv) {                                            /* DRV: x1..x4 into the soft clip, the level kept */
        g = 4096 + drv * 3 * 4096 / 127;
        mk = (int32_t)((19661u << 15) / (uint32_t)softclip((19661 * g) >> 12));
    }
    for (i = 0; i < n; i++) {
        int32_t s = y[i];
        if (g)
            s = (softclip((s * g) >> 12) * mk) >> 15;
        s = s >= 0 ? s & ~127 : -((-s) & ~127);           /* (9 bits: the kit's grain) */
        out[i] += (voice_amp(soft_knee(s, 30000), m, i) * CK_GAIN) >> 4;   /* (0.9.15: louder, CK_GAIN) */
    }
}

/* the keys play the GM map, the first C (key 7) the kick, as DRUM's */
static int32_t ck_keys(const track_t *t, uint32_t k)
{
    if (trk_is_perc(t))                                   /* 0.10: PERC: F3 .. F4 its first sound, F#4 .. G5 its second */
        return k < 13u ? 36 : 38;
    return clamp(29 + 12 * song.octave + (int32_t)k, 0, 127);
}

/* the kits: the engine's values in the preset; each voice's six in CK_KITS (ck_kit_load) */
typedef struct { int8_t v[CK_N][7]; } ck_kit_t;          /* PITCH DECAY COLOR SWEEP SHAPE LEVEL MODEL (0.9.10; 0 NOISE) */
static const ck_kit_t CK_KITS[] = {
    {{{64, 82, 38, 72, 24, 127}, {64, 58, 52, 30, 78, 112}, {64, 36, 80, 0, 40, 92}, {64, 54, 58, 22, 20, 100}}},  /* CYCLE */
    {{{58, 100, 12, 80, 0, 127}, {66, 56, 30, 34, 70, 108}, {68, 30, 64, 0, 16, 88}, {60, 62, 34, 26, 96, 100}}},  /* 808ISH */
    {{{66, 74, 46, 76, 60, 127}, {62, 60, 60, 36, 90, 115}, {62, 40, 96, 0, 56, 96}, {70, 50, 70, 16, 64, 96}}},   /* 909ISH */
    {{{60, 88, 70, 92, 96, 120}, {56, 64, 80, 52, 50, 112}, {56, 34, 110, 0, 90, 90}, {52, 70, 96, 40, 32, 104}}}, /* GRIT */
    {{{72, 66, 90, 48, 30, 118}, {76, 50, 98, 20, 40, 100}, {80, 44, 127, 0, 127, 88}, {84, 74, 110, 8, 72, 108}}}, /* METAL */
    {{{56, 96, 20, 60, 10, 127}, {60, 70, 24, 24, 100, 96}, {60, 26, 56, 0, 8, 80}, {56, 80, 40, 30, 48, 110}}},    /* DUB */
    {{{60, 90, 30, 70, 30, 127}, {60, 62, 40, 30, 95, 114}, {62, 32, 60, 0, 30, 84}, {72, 40, 50, 10, 20, 96}}},  /* BOOM BAP */
    {{{62, 84, 50, 78, 50, 127}, {66, 52, 76, 20, 110, 104}, {70, 28, 110, 0, 72, 92}, {64, 60, 80, 20, 64, 98}}},  /* TECHNO KIT */
    {{{56, 104, 16, 90, 0, 127}, {70, 48, 46, 40, 60, 110}, {72, 30, 90, 0, 40, 90}, {66, 54, 40, 40, 80, 100}}},  /* ELECTRO KIT */
    {{{58, 80, 24, 60, 40, 124}, {58, 58, 30, 24, 85, 108}, {58, 34, 40, 0, 20, 80}, {60, 52, 30, 20, 40, 96}}},  /* DUSTY KIT */
    {{{54, 96, 24, 56, 10, 127}, {56, 70, 30, 46, 40, 106}, {60, 40, 70, 0, 30, 80}, {50, 80, 40, 50, 48, 118}}},  /* TRIBAL KIT */
    {{{66, 60, 40, 70, 20, 127}, {74, 36, 70, 10, 110, 96}, {76, 20, 100, 0, 100, 88}, {80, 34, 90, 8, 112, 104}}},  /* MINIMAL KIT */
    {{{60, 86, 90, 96, 110, 124}, {54, 66, 100, 60, 70, 116}, {54, 40, 127, 0, 120, 94}, {48, 74, 110, 46, 24, 108}}},  /* FORGE KIT */
    {{{70, 64, 60, 100, 40, 124}, {80, 44, 90, 60, 100, 108}, {84, 24, 120, 0, 64, 86}, {88, 40, 100, 64, 90, 104}}},  /* CHIP KIT 2 */
    {{{54, 100, 20, 64, 20, 127}, {52, 74, 28, 40, 80, 110}, {56, 40, 50, 0, 50, 82}, {46, 90, 30, 60, 56, 112}}},  /* CAVE KIT */
    {{{64, 78, 36, 70, 30, 127}, {64, 56, 60, 34, 96, 110}, {66, 44, 96, 0, 48, 96}, {72, 50, 60, 30, 88, 100}}},  /* DISCO KIT */
    /* Phantom Tracker 0.9.7: the four in the sampled sets' places (Ed's "glitch hop meets chillwave"), synthesized */
    {{{58, 80, 40, 80, 60, 127}, {66, 50, 70, 30, 100, 112}, {74, 22, 100, 0, 60, 90}, {70, 40, 90, 40, 100, 100}}},  /* GLITCH HOP */
    {{{56, 96, 10, 60, 0, 124}, {60, 70, 20, 30, 50, 104}, {62, 36, 20, 0, 20, 80}, {58, 60, 20, 30, 30, 92}}},      /* CHILLWAVE */
    {{{58, 84, 30, 60, 30, 127}, {58, 60, 40, 20, 80, 106}, {60, 30, 40, 0, 30, 82}, {56, 50, 40, 20, 40, 96}}},     /* TAPE DECK */
    {{{62, 70, 60, 100, 80, 124}, {72, 40, 100, 50, 110, 106}, {80, 18, 120, 0, 100, 88}, {84, 36, 110, 70, 110, 104}}},   /* WONK */
    /* Phantom Tracker 0.9.10 (Ed: "engine flavoured voices plus kits that use them ... 10 usable but also unique
     * bespoke kits"): the lanes' MODELs, 1 FM 2 CHIP 3 COMB 4 BLOOP (0 NOISE) */
    {{{64, 80, 50, 90, 0, 127, 1}, {64, 56, 60, 30, 64, 90, 1}, {64, 30, 90, 0, 80, 60, 1}, {64, 60, 70, 10, 80, 80, 1}}},   /* SIXTEEN BIT */
    {{{64, 92, 30, 110, 40, 127, 2}, {64, 56, 70, 60, 64, 84, 2}, {64, 26, 40, 0, 90, 70, 2}, {64, 54, 30, 70, 0, 104, 2}}},   /* BREADBIN */
    {{{70, 84, 115, 80, 16, 127, 3}, {64, 54, 50, 20, 100, 100, 3}, {64, 24, 70, 0, 127, 70, 3}, {64, 64, 100, 0, 40, 108, 3}}},   /* RESONATOR */
    {{{64, 80, 20, 70, 60, 127, 4}, {64, 50, 90, 30, 90, 84, 4}, {64, 20, 127, 0, 40, 72, 4}, {64, 50, 70, 50, 110, 84, 4}}},   /* POND */
    {{{58, 80, 40, 80, 60, 127, 0}, {66, 48, 80, 30, 64, 90, 1}, {64, 22, 0, 0, 100, 70, 2}, {70, 48, 110, 0, 100, 108, 3}}},   /* GLITCHWAVE */
    {{{60, 96, 20, 60, 0, 124, 1}, {60, 70, 20, 30, 50, 104, 0}, {62, 34, 40, 0, 48, 52, 1}, {60, 64, 50, 40, 80, 76, 4}}},   /* VAPOR FM */
    {{{62, 88, 36, 120, 20, 127, 2}, {68, 46, 90, 80, 48, 88, 2}, {70, 18, 0, 0, 40, 70, 2}, {70, 40, 90, 30, 112, 100, 1}}},   /* ARCADE */
    {{{56, 96, 10, 60, 0, 124, 0}, {66, 48, 100, 40, 100, 84, 4}, {64, 30, 60, 0, 30, 84, 0}, {58, 60, 80, 70, 120, 80, 4}}},   /* FROGBEAT */
    {{{62, 78, 70, 90, 0, 127, 1}, {60, 56, 90, 20, 10, 90, 3}, {66, 36, 127, 0, 112, 54, 1}, {66, 70, 120, 0, 30, 108, 3}}},   /* FOUNDRY */
    {{{60, 92, 30, 90, 0, 127, 2}, {66, 40, 60, 10, 120, 108, 3}, {64, 20, 0, 0, 20, 72, 2}, {68, 40, 60, 30, 60, 100, 4}}},   /* TIN CAN */
    /* Phantom Tracker 0.9.12 (Ed: "five more genesis kits that try to emulate actual drum kits and five more SID kits
     * that do the same"): real kits on FM, then on CHIP; a kick ~65 Hz, a snare ~190 Hz, toms on PERC around 200 Hz */
    {{{80, 82, 50, 14, 0, 127, 1}, {72, 74, 70, 10, 64, 110, 1}, {112, 54, 110, 0, 80, 72, 1}, {44, 86, 30, 12, 0, 110, 1}}},   /* FM ROCK */
    {{{84, 80, 30, 6, 0, 110, 1}, {76, 78, 40, 4, 80, 96, 1}, {76, 64, 50, 0, 16, 68, 1}, {56, 84, 24, 6, 0, 100, 1}}},   /* FM JAZZ */
    {{{80, 76, 60, 10, 0, 127, 1}, {78, 72, 80, 14, 64, 115, 1}, {124, 40, 120, 0, 112, 74, 1}, {70, 70, 60, 8, 48, 104, 1}}},   /* FM FUNK */
    {{{64, 100, 12, 26, 0, 127, 1}, {74, 60, 50, 20, 32, 100, 1}, {104, 36, 30, 0, 0, 72, 1}, {64, 80, 30, 10, 0, 96, 1}}},   /* FM EIGHT */
    {{{70, 80, 70, 40, 0, 127, 1}, {76, 62, 90, 30, 64, 112, 1}, {127, 40, 127, 0, 96, 72, 1}, {66, 64, 50, 20, 0, 100, 1}}},   /* FM NINE */
    {{{78, 84, 20, 30, 32, 127, 2}, {72, 70, 70, 40, 80, 100, 2}, {100, 52, 0, 0, 30, 72, 2}, {44, 84, 20, 30, 0, 110, 2}}},   /* SID ROCK */
    {{{64, 104, 10, 40, 0, 127, 2}, {74, 60, 60, 40, 64, 96, 2}, {104, 40, 0, 0, 100, 70, 2}, {64, 80, 10, 20, 0, 96, 2}}},   /* SID EIGHT */
    {{{70, 84, 50, 60, 32, 127, 2}, {76, 62, 80, 40, 96, 108, 2}, {108, 40, 0, 0, 20, 74, 2}, {66, 66, 30, 30, 0, 100, 2}}},   /* SID NINE */
    {{{84, 80, 10, 10, 0, 127, 2}, {76, 80, 40, 10, 127, 92, 2}, {96, 58, 0, 0, 110, 64, 2}, {56, 82, 10, 10, 0, 100, 2}}},   /* SID JAZZ */
    {{{70, 80, 20, 40, 32, 127, 2}, {74, 66, 70, 30, 96, 112, 2}, {100, 46, 0, 0, 40, 74, 2}, {68, 66, 60, 20, 0, 100, 2}}},   /* SID BREAKS */
    /* Spelunkr (Ed, Nov 18 2026: "take drums samples where we can"): the sampled sets, every lane on its set
     * (MODEL CKM_N + s: spk_kits.h); DECAY 127, no fade: the hit as it was made */
    {{{64, 127, 0, 0, 0, 127, 5}, {64, 127, 0, 0, 0, 127, 5}, {64, 127, 0, 0, 0, 127, 5}, {64, 127, 0, 0, 0, 127, 5}}},   /* CRATE */
    {{{64, 127, 0, 0, 0, 127, 6}, {64, 127, 0, 0, 0, 127, 6}, {64, 127, 0, 0, 0, 127, 6}, {64, 127, 0, 0, 0, 127, 6}}},   /* BEDROOM */
    {{{64, 127, 0, 0, 0, 127, 7}, {64, 127, 0, 0, 0, 127, 7}, {64, 127, 0, 0, 0, 127, 7}, {64, 127, 0, 0, 0, 127, 7}}},   /* VAPOR 808 */
    {{{64, 127, 0, 0, 0, 127, 8}, {64, 127, 0, 0, 0, 127, 8}, {64, 127, 0, 0, 0, 127, 8}, {64, 127, 0, 0, 0, 127, 8}}},   /* LINN */
    {{{64, 127, 0, 0, 0, 127, 9}, {64, 127, 0, 0, 0, 127, 9}, {64, 127, 0, 0, 0, 127, 9}, {64, 127, 0, 0, 0, 127, 9}}},   /* GATED */
    {{{64, 127, 0, 0, 0, 127, 10}, {64, 127, 0, 0, 0, 127, 10}, {64, 127, 0, 0, 0, 127, 10}, {64, 127, 0, 0, 0, 127, 10}}},   /* NES */
    {{{64, 127, 0, 0, 0, 127, 11}, {64, 127, 0, 0, 0, 127, 11}, {64, 127, 0, 0, 0, 127, 11}, {64, 127, 0, 0, 0, 127, 11}}},   /* GAMEBOY */
    {{{64, 127, 0, 0, 0, 127, 12}, {64, 127, 0, 0, 0, 127, 12}, {64, 127, 0, 0, 0, 127, 12}, {64, 127, 0, 0, 0, 127, 12}}},   /* SNES */
};
/* the SET a kit plays: the synthesis (0.9.7: the sets' kits synthesized too) */
static uint32_t ck_kit_set(uint32_t k) { (void)k; return 0u; }
/* {TUNE, DECAY, COLOR (the three -64..63, 0 = the voices as set), OPEN, CLICK, VELO, DRV, VARI}; every kit suggests
 * the BEAT pattern */
static const preset_t CK_PRESETS[] = {
    {"CAPSULE", {0, 0, 0, 64, 40, 80, 0, 12}, {0, 100, 127, 100}, 0, 0, FX(0, 0, 0, 16), PAT(12)},
    {"LOW ORBIT", {0, 0, 0, 80, 16, 80, 0, 6}, {0, 100, 127, 100}, 0, 0, FX(0, 0, 0, 20), PAT(12)},
    {"HARD DOCK", {0, 0, 0, 56, 72, 80, 24, 8}, {0, 100, 127, 100}, 0, 0, FX(0, 0, 0, 14), PAT(12)},
    {"GRIT", {-4, -4, 0, 64, 90, 70, 80, 20}, {0, 100, 127, 100}, 0, 0, FX(20, 0, 0, 10), PAT(12)},
    {"HULL METAL", {0, 0, 0, 90, 30, 90, 10, 30}, {0, 100, 127, 100}, 0, 0, FX(0, 20, 10, 30), PAT(12)},
    {"DEEP SPACE", {-2, 6, -4, 100, 10, 90, 16, 16}, {0, 100, 127, 100}, 0, 0, FX(0, 0, 40, 40), PAT(12)},
    /* Dungeon Tracker 1.0: more sounds (appended: a song's preset numbers stay) */
    {"BOOM BAP", {0, 0, -6, 50, 30, 90, 10, 10}, {0, 100, 127, 100}, 0, 0, FX(10, 0, 0, 22), PAT(12)},
    {"TELEMETRY", {0, -4, 4, 60, 60, 70, 20, 8}, {0, 100, 127, 100}, 0, 0, FX(10, 0, 10, 18), PAT(12)},
    {"ELECTRO", {0, 0, 0, 70, 20, 85, 0, 6}, {0, 100, 127, 100}, 0, 0, FX(0, 10, 20, 16), PAT(12)},
    {"DUST", {-6, -2, -10, 40, 20, 95, 30, 24}, {0, 100, 127, 100}, 0, 0, FX(20, 0, 0, 24), PAT(12)},
    {"TRIBAL", {0, 4, 0, 50, 30, 85, 0, 30}, {0, 100, 127, 100}, 0, 0, FX(0, 0, 20, 40), PAT(12)},
    {"MINIMAL", {0, -10, 0, 40, 80, 60, 0, 4}, {0, 100, 127, 100}, 0, 0, FX(0, 0, 30, 14), PAT(12)},
    {"FORGE", {-6, 0, 8, 70, 100, 70, 70, 18}, {0, 100, 127, 100}, 0, 0, FX(40, 0, 10, 20), PAT(12)},
    {"BIT KIT", {4, -6, 6, 50, 20, 60, 20, 6}, {0, 100, 127, 100}, 0, 0, FX(0, 0, 20, 10), PAT(12)},
    {"CRATER", {-4, 6, -6, 70, 20, 80, 10, 16}, {0, 100, 127, 100}, 0, 0, FX(0, 10, 30, 70), PAT(12)},
    {"DISCO", {0, 0, 0, 90, 40, 80, 0, 10}, {0, 100, 127, 100}, 0, 0, FX(0, 20, 10, 26), PAT(12)},
    /* Phantom Tracker 0.9.7: in the sampled sets' places (DERELICT SCRAP SHORTWAVE DEAD AIR, scrapped; their numbers
     * kept, so a song's kit stays a kit): glitch hop and chillwave kits, synthesized */
    {"GLITCH HOP", {0, -6, 6, 40, 70, 75, 40, 6}, {0, 100, 127, 100}, 0, 0, FX(10, 0, 10, 14), PAT(12)},
    {"CHILLWAVE", {-2, 4, -8, 60, 10, 90, 0, 14}, {0, 100, 127, 100}, 0, 0, FX(0, 30, 20, 60), PAT(12)},
    {"TAPE DECK", {-4, -2, -12, 50, 20, 95, 30, 24}, {0, 100, 127, 100}, 0, 0, FX(20, 10, 0, 24), PAT(12)},
    {"WONK", {4, -10, 10, 30, 90, 60, 60, 20}, {0, 100, 127, 100}, 0, 0, FX(30, 0, 20, 12), PAT(12)},
    /* Phantom Tracker 0.9.10: kits on the engines' voices (each lane's MODEL in CK_KITS) */
    {"SIXTEEN BIT", {0, 0, 0, 50, 30, 80, 10, 6}, {0, 100, 127, 100}, 0, 0, FX(0, 0, 10, 14), PAT(12)},
    {"BREADBIN", {0, 0, 0, 40, 0, 70, 0, 0}, {0, 100, 127, 100}, 0, 0, FX(0, 0, 10, 8), PAT(12)},
    {"RESONATOR", {0, 0, 0, 60, 10, 85, 0, 8}, {0, 100, 127, 100}, 0, 0, FX(0, 0, 20, 30), PAT(12)},
    {"POND", {0, 0, 0, 50, 0, 75, 20, 10}, {0, 100, 127, 100}, 0, 0, FX(0, 20, 20, 30), PAT(12)},
    {"GLITCHWAVE", {0, -4, 4, 40, 60, 70, 30, 8}, {0, 100, 127, 100}, 0, 0, FX(10, 0, 20, 16), PAT(12)},
    {"VAPOR FM", {-2, 4, -6, 70, 10, 90, 0, 14}, {0, 100, 127, 100}, 0, 0, FX(0, 30, 30, 60), PAT(12)},
    {"ARCADE", {0, -2, 0, 40, 0, 60, 0, 0}, {0, 100, 127, 100}, 0, 0, FX(0, 0, 20, 10), PAT(12)},
    {"FROGBEAT", {0, 0, 0, 50, 20, 80, 20, 10}, {0, 100, 127, 100}, 0, 0, FX(10, 10, 20, 24), PAT(12)},
    {"FOUNDRY", {-2, 0, 4, 70, 50, 80, 40, 12}, {0, 100, 127, 100}, 0, 0, FX(20, 0, 10, 30), PAT(12)},
    {"TIN CAN", {0, -4, 0, 40, 30, 70, 30, 16}, {0, 100, 127, 100}, 0, 0, FX(20, 10, 0, 16), PAT(12)},
    /* Phantom Tracker 0.9.12: real kits, five on FM (GENESIS's way) and five on CHIP (SID's way) */
    {"FM ROCK", {0, 0, 0, 90, 50, 95, 20, 24}, {0, 100, 127, 100}, 0, 0, FX(0, 0, 0, 30), PAT(12)},
    {"FM JAZZ", {0, 0, -6, 100, 10, 110, 0, 40}, {0, 100, 127, 100}, 0, 0, FX(0, 0, 0, 40), PAT(12)},
    {"FM FUNK", {0, -4, 0, 90, 40, 100, 10, 20}, {0, 100, 127, 100}, 0, 0, FX(0, 0, 0, 20), PAT(12)},
    {"FM EIGHT", {0, 0, 0, 95, 10, 85, 0, 4}, {0, 100, 127, 100}, 0, 0, FX(0, 0, 0, 14), PAT(12)},
    {"FM NINE", {0, 0, 0, 100, 70, 85, 20, 4}, {0, 100, 127, 100}, 0, 0, FX(0, 0, 0, 16), PAT(12)},
    {"SID ROCK", {0, 0, 0, 90, 30, 95, 10, 16}, {0, 100, 127, 100}, 0, 0, FX(0, 0, 0, 24), PAT(12)},
    {"SID EIGHT", {0, 0, 0, 60, 0, 85, 0, 4}, {0, 100, 127, 100}, 0, 0, FX(0, 0, 0, 12), PAT(12)},
    {"SID NINE", {0, 0, 0, 70, 50, 85, 20, 4}, {0, 100, 127, 100}, 0, 0, FX(0, 0, 0, 14), PAT(12)},
    {"SID JAZZ", {0, 0, -6, 100, 0, 110, 0, 40}, {0, 100, 127, 100}, 0, 0, FX(0, 0, 0, 36), PAT(12)},
    {"SID BREAKS", {0, 0, 0, 50, 40, 90, 30, 24}, {0, 100, 127, 100}, 0, 0, FX(10, 0, 0, 20), PAT(12)},
    /* Spelunkr: the sampled kits (lofi, vapor drum machines, chiptune), 40.. */
    {"CRATE", {0, 0, 0, 64, 0, 90, 0, 4}, {0, 100, 127, 100}, 0, 0, FX(0, 0, 0, 16), PAT(12)},
    {"BEDROOM", {0, 0, 0, 64, 0, 90, 0, 4}, {0, 100, 127, 100}, 0, 0, FX(0, 10, 0, 22), PAT(12)},
    {"VAPOR 808", {0, 0, 0, 64, 0, 90, 0, 4}, {0, 100, 127, 100}, 0, 0, FX(0, 0, 10, 24), PAT(12)},
    {"LINN", {0, 0, 0, 64, 0, 90, 0, 4}, {0, 100, 127, 100}, 0, 0, FX(0, 0, 0, 12), PAT(12)},
    {"GATED", {0, 0, 0, 64, 0, 90, 0, 4}, {0, 100, 127, 100}, 0, 0, FX(0, 0, 0, 10), PAT(12)},
    {"NES", {0, 0, 0, 64, 0, 90, 0, 0}, {0, 100, 127, 100}, 0, 0, FX(0, 0, 0, 8), PAT(12)},
    {"GAMEBOY", {0, 0, 0, 64, 0, 90, 0, 0}, {0, 100, 127, 100}, 0, 0, FX(0, 0, 0, 8), PAT(12)},
    {"SNES", {0, 0, 0, 64, 0, 90, 0, 4}, {0, 100, 127, 100}, 0, 0, FX(0, 0, 0, 14), PAT(12)},
};
typedef char ck_kits_fit[NELEM(CK_KITS) == NELEM(CK_PRESETS) ? 1 : -1];

/* kit k's voices into the track (a preset load, a clean sound, power-on) */
static void pc_source(track_t *t, uint32_t sl);
static void ck_kit_load(track_t *t, uint32_t k)
{
    const ck_kit_t *K = &CK_KITS[k % NELEM(CK_KITS)];
    uint32_t l;
    if (trk_is_perc(t)) {                                 /* 0.10: PERC: both slots' sounds from kit k */
        for (l = 0; l < 2u; l++) {
            t->p[PC_KIT(l)] = (int16_t)(k % NELEM(CK_KITS));
            t->p[PC_SND(l)] = (int16_t)(pc_snd(t->p, l) + 1u);
            pc_source(t, l);
        }
        return;
    }
    for (l = 0; l < CK_N; l++) {
        t->p[CK_PITCH(l)] = K->v[l][0];
        t->p[CK_DECAY(l)] = K->v[l][1];
        t->p[CK_COLOR(l)] = K->v[l][2];
        t->p[CK_SWEEP(l)] = K->v[l][3];
        t->p[CK_SHAPE(l)] = K->v[l][4];
        t->p[P_LN0 + l] = K->v[l][5];
    }
    for (l = 0; l < CK_N; l++)
        t->p[P_LN4 + l] = K->v[l][6];                     /* the lanes' MODELs (0.9.10: in M8's SET and the unused levels) */
    (void)ck_kit_set;
}

/* 0.10: PERC's slot sl from its source: the kit's values for the voice its sound plays */
static void pc_source(track_t *t, uint32_t sl)
{
    const ck_kit_t *K = &CK_KITS[(uint32_t)clamp(t->p[PC_KIT(sl & 1u)], 0, (int32_t)NELEM(CK_KITS) - 1)];
    uint32_t f, ln = (uint32_t)ck_gm(PC_GM[pc_snd(t->p, sl)])[0];
    sl &= 1u;
    for (f = 0; f < 5u; f++)
        t->p[P_FM1_ATK + 5u * sl + f] = K->v[ln][f];
    t->p[P_LN0 + sl] = K->v[ln][5];
    t->p[P_LN4 + sl] = K->v[ln][6];
}

/* the labels of a KIT track's voice parameters (params.c track_desc): the page names the voice */
static const param_desc_t CK_DESC[6] = {
    {"PITCH", F_INT, 0, 127, 64, 0, 0}, {"DECAY", F_PCT, 0, 127, 64, 0, 0}, {"COLOR", F_PCT, 0, 127, 64, 0, 0},
    {"SWEEP", F_PCT, 0, 127, 0, 0, 0}, {"SHAPE", F_PCT, 0, 127, 0, 0, 0}, {"LEVEL", F_PCT, 0, 127, 127, 0, 0},
};
static const char *const N_CK_SHP_HAT[] = {"1.41", "1.73", "2.41", "2.76", "3.14", "3.70", "4.60", "5.19", 0};
static const char *const N_CK_SHP_PERC[] = {"1", "2", "3", "1.5", "2.76", "3.5", "0.5", "4.6", 0};
static const param_desc_t CK_SHAPE_DESC[CK_N] = {
    {"BD DRV", F_PCT, 0, 127, 0, 0, 0}, {"SD NOIS", F_PCT, 0, 127, 0, 0, 0},
    {"HH RAT", F_INT, 0, 127, 0, N_CK_SHP_HAT, 0}, {"PC RAT", F_INT, 0, 127, 0, N_CK_SHP_PERC, 0},
};
static const param_desc_t CK_MODEL_DESC[CK_N] = {   /* 0.9.10: EDIT > MODEL (P_LN4..P_LN7) */
    {"BD MODL", F_ENUM, 0, CKM_ALL - 1, 0, N_CK_MODEL, 0}, {"SD MODL", F_ENUM, 0, CKM_ALL - 1, 0, N_CK_MODEL, 0},
    {"HH MODL", F_ENUM, 0, CKM_ALL - 1, 0, N_CK_MODEL, 0}, {"PC MODL", F_ENUM, 0, CKM_ALL - 1, 0, N_CK_MODEL, 0},
};
static const param_desc_t CK_LEVEL_DESC[CK_N] = {
    {"KICK", F_PCT, 0, 127, 127, 0, 0}, {"SNARE", F_PCT, 0, 127, 127, 0, 0},
    {"HAT", F_PCT, 0, 127, 127, 0, 0}, {"PERC", F_PCT, 0, 127, 127, 0, 0},
};
/* the descriptor of id on a KIT track, 0 = not one of its own */
static const param_desc_t *ck_param_desc(uint32_t id)
{
    if (id >= P_FM1_ATK && id <= P_FM4_LEVEL) {
        uint32_t k = (id - P_FM1_ATK) % 5u;
        return k == 4u ? &CK_SHAPE_DESC[(id - P_FM1_ATK) / 5u] : &CK_DESC[k];
    }
    if (id >= P_LN0 && id < P_LN0 + CK_N)
        return &CK_LEVEL_DESC[id - P_LN0];
    if (id >= P_LN4 && id < P_LN4 + CK_N)
        return &CK_MODEL_DESC[id - P_LN4];
    return 0;
}

/* 0.10: PERC's own labels (track_desc): SOURCE (1 KIT 1 SOUND 2 KIT 2 SOUND), PERC 1 / 2 (lanes 0 / 1's four), MIX
 * (their SHAPEs, as the voice its sound plays names it, and LEVELs), MODEL */
static const char *const N_PC_KIT[] = {
    "CAPSULE", "LOW ORBIT", "HARD DOCK", "GRIT", "HULL METAL", "DEEP SPACE", "BOOM BAP", "TELEMETRY", "ELECTRO",
    "DUST", "TRIBAL", "MINIMAL", "FORGE", "BIT KIT", "CRATER", "DISCO", "GLITCH HOP", "CHILLWAVE", "TAPE DECK",
    "WONK", "SIXTEEN BIT", "BREADBIN", "RESONATOR", "POND", "GLITCHWAVE", "VAPOR FM", "ARCADE", "FROGBEAT", "FOUNDRY",
    "TIN CAN", "FM ROCK", "FM JAZZ", "FM FUNK", "FM EIGHT", "FM NINE", "SID ROCK", "SID EIGHT", "SID NINE",
    "SID JAZZ", "SID BREAKS", "CRATE", "BEDROOM", "VAPOR 808", "LINN", "GATED", "NES", "GAMEBOY", "SNES", 0};
typedef char pc_kits_named[NELEM(N_PC_KIT) == NELEM(CK_PRESETS) + 1u ? 1 : -1];
static const char *const N_PC_SND1[PC_NSND + 2u] = {"-", "KICK", "SNARE", "RIM", "CLAP", "CL HAT", "PD HAT", "OP HAT",
                                                    "CRASH", "RIDE", "BELL", "LO TOM", "HI TOM", "COWBELL", "CLAVE",
                                                    "CONGA", "SHAKER", 0};   /* (the sound + 1) */
static const param_desc_t PC_SRC_DESC[4] = {
    {"1 KIT", F_ENUM, 0, (int16_t)(NELEM(CK_PRESETS) - 1u), 16, N_PC_KIT, 0},
    {"1 SOUND", F_ENUM, 1, PC_NSND, 4, N_PC_SND1, 0},
    {"2 KIT", F_ENUM, 0, (int16_t)(NELEM(CK_PRESETS) - 1u), 16, N_PC_KIT, 0},
    {"2 SOUND", F_ENUM, 1, PC_NSND, 16, N_PC_SND1, 0},
};
static const param_desc_t PC_SHAPE_DESC[2][CK_N] = {
    {{"1 DRV", F_PCT, 0, 127, 0, 0, 0}, {"1 NOIS", F_PCT, 0, 127, 0, 0, 0},
     {"1 RAT", F_INT, 0, 127, 0, N_CK_SHP_HAT, 0}, {"1 RAT", F_INT, 0, 127, 0, N_CK_SHP_PERC, 0}},
    {{"2 DRV", F_PCT, 0, 127, 0, 0, 0}, {"2 NOIS", F_PCT, 0, 127, 0, 0, 0},
     {"2 RAT", F_INT, 0, 127, 0, N_CK_SHP_HAT, 0}, {"2 RAT", F_INT, 0, 127, 0, N_CK_SHP_PERC, 0}},
};
static const param_desc_t PC_LEVEL_DESC[2] = {{"1 LEVEL", F_PCT, 0, 127, 127, 0, 0}, {"2 LEVEL", F_PCT, 0, 127, 127, 0, 0}};
static const param_desc_t PC_MODEL_DESC[2] = {{"1 MODEL", F_ENUM, 0, CKM_ALL - 1, 0, N_CK_MODEL, 0},
                                              {"2 MODEL", F_ENUM, 0, CKM_ALL - 1, 0, N_CK_MODEL, 0}};
static const param_desc_t *pc_param_desc(const track_t *t, uint32_t id)
{
    uint32_t sl;
    if (id == PC_KIT(0) || id == PC_SND(0) || id == PC_KIT(1) || id == PC_SND(1))
        return &PC_SRC_DESC[(id == PC_KIT(1) || id == PC_SND(1)) * 2u + (id == PC_SND(0) || id == PC_SND(1))];
    if (id >= P_FM1_ATK && id < P_FM3_ATK) {
        sl = (id - P_FM1_ATK) / 5u;
        return (id - P_FM1_ATK) % 5u == 4u ? &PC_SHAPE_DESC[sl][ck_gm(PC_GM[pc_snd(t->p, sl)])[0]]
                                           : &CK_DESC[(id - P_FM1_ATK) % 5u];
    }
    if (id == P_LN0 || id == P_LN1)
        return &PC_LEVEL_DESC[id - P_LN0];
    if (id == P_LN4 || id == P_LN5)
        return &PC_MODEL_DESC[id - P_LN4];
    return ck_param_desc(id);
}

static const engine_t ENG_CKIT = {
    .name = "NOISE",                                      /* (Phantom Cosmonaut: the NOISE kit; GEN KIT, RONDO's CYCLE KIT before) */
    .page_title = {"KIT", "FEEL"},
    .edit = {
        {"TUNE", F_BIPCT, -64, 63, 0, 0, 0},
        {"DECAY", F_BIPCT, -64, 63, 0, 0, 0},
        {"COLOR", F_BIPCT, -64, 63, 0, 0, 0},
        {"OPEN", F_PCT, 0, 127, 64, 0, 0},
        {"CLICK", F_PCT, 0, 127, 40, 0, 0},
        {"VELO", F_PCT, 0, 127, 80, 0, 0},
        {"DRV", F_PCT, 0, 127, 0, 0, 0},
        {"VARI", F_PCT, 0, 127, 12, 0, 0},
    },
    .presets = CK_PRESETS,
    .npresets = NELEM(CK_PRESETS),
    .note_on = ck_note_on,
    .render = ck_render,
    .amp = ck_amp,
    .knob = {P_E1, P_E2, P_E0, P_E4},
    .poly = CK_N,
    .sampled = 1,                /* (no phases kept over a retrigger: every hit starts the same) */
    .oneshot = 1,
    .keys = ck_keys,
};

/* ==================================================================================================== TONE === */
/* state: s[0] the index envelope (Q30), s[1] the pitch envelope (Q30), s[2] the modulator's last sample (feedback),
 * s[3] the DC blocker's (a 3 Hz one-pole, Q15 << 8: full FEEDBACK leaves a little DC) */
static void ct_note_on(track_t *t, voice_t *v)
{
    (void)t;
    v->s[0] = 1 << 30;
    v->s[1] = 1 << 30;
    v->s[2] = 0;
    v->s[3] = 0;
}

static void ct_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    int32_t me0 = v->s[0], pe0 = v->s[1], fbs = v->s[2], dc = v->s[3], ms = clamp(p[P_E3], 0, 127) << 23;
    int32_t me1 = ms + cyc_mul30(me0 - ms, cyc_kb(p[P_E2] * 143 / 127)), pe1 = cyc_mul30(pe0, cyc_kb(60));
    int32_t col = clamp(p[P_E1] + ((m->shape - (64 << 8)) >> 8) + (m->cutoff >> 8) + (v->vel - 96) / 4 + mk_x[3] * 2 / 5, 0, 127);
    int32_t ki = cyc_idx(col), k0 = (int32_t)(((int64_t)ki * (me0 >> 15)) >> 15), k1 = (int32_t)(((int64_t)ki * (me1 >> 15)) >> 15);
    int32_t e4 = clamp(p[P_E4] + mk_x[3] / 4, 0, 127);    /* (the FX layer's RIDE: a little more FDBK) */
    int32_t fb = e4 * e4 * 5 >> 2, stk = p[P_E6] * 258, sub = p[P_E7] * 258, sw = p[P_E5];
    uint32_t inc = m->inc, minc, inc2, i, ph0 = v->ph[0], ph1 = v->ph[1], ph2 = v->ph[2];
    if (sw && pe0 > (1 << 16)) {                          /* SWEEP: the pitch starts away from the note */
        int32_t d16 = sw * 384 / 64;                      /* +-24 semitones */
        inc = cents_inc(clamp(m->pitch16 + (int32_t)(((int64_t)d16 * pe0) >> 30), 0, 2047), 0, m->fine);
    }
    minc = (inc >> 8) * CYC_RATIO[(uint32_t)p[P_E0] & 15u];
    inc2 = inc + (inc >> 12) * (uint32_t)(p[P_E6] * 72 / 127);   /* STACK: up to ~30 cents up */
    if (n > CTL)
        n = CTL;
    for (i = 0; i < n; i++) {
        int32_t k = k0 + (k1 - k0) * (int32_t)i / (int32_t)n, md = cyc_pm(ph1, fbs, fb), s = cyc_pm(ph0, md, k);
        fbs = md;
        ph0 += inc;
        ph1 += minc;
        if (stk) {                                        /* the second carrier (its phase in ph[2]'s high use: */
            int32_t s2 = cyc_pm(ph2, md, k);              /* SUB is then off) */
            ph2 += inc2;
            s = s - ((((s - s2) >> 1) * stk) >> 15);
        } else if (sub) {
            int32_t so = sine_i(ph2);
            ph2 += inc >> 1;
            s = ((s * (32767 - (sub >> 1))) >> 15) + ((so * sub) >> 16);
        }
        dc += ((s << 8) - dc) >> 11;
        out[i] += voice_amp(s - (dc >> 8), m, i);
    }
    v->s[0] = me1;
    v->s[1] = pe1;
    v->s[2] = fbs;
    v->s[3] = dc;
    v->ph[0] = ph0;
    v->ph[1] = ph1;
    v->ph[2] = ph2;
}

/* {RATIO, COLOR, CNTR, MSUS, FDBK, SWEEP, STACK, SUB} */
static const preset_t CT_PRESETS[] = {
    {"FM PAD", {1, 44, 104, 56, 12, 0, 64, 0}, {52, 90, 112, 78}, 0, 0, FX(0, 40, 30, 60), PAT(5)},
    {"CHORD EP", {1, 74, 66, 12, 0, 0, 24, 0}, {0, 84, 52, 46}, 0, 0, FX(0, 30, 20, 36), PAT(6)},
    {"STAB", {3, 84, 44, 0, 30, 4, 0, 0}, {0, 56, 38, 26}, 0, 0, FX(0, 0, 40, 30), PAT(6)},
    {"GLASS PAD", {6, 62, 92, 24, 0, 0, 48, 0}, {24, 100, 76, 84}, 0, 0, FX(0, 30, 40, 70), PAT(5)},
    {"ORGAN", {3, 36, 127, 127, 0, 0, 0, 56}, {0, 0, 127, 16}, 0, 0, FX(0, 50, 0, 30), PAT(6)},
    {"FM BASS", {1, 76, 46, 26, 40, 0, 0, 64}, {0, 60, 92, 14}, 0, 1, FX(10, 0, 0, 0), PAT(2)},
    {"SUB BASS", {1, 22, 34, 0, 0, 0, 0, 110}, {0, 40, 112, 12}, 0, 1, FX(0, 0, 0, 0), PAT(8)},
    {"PLUCK BASS", {3, 92, 32, 0, 20, 6, 0, 44}, {0, 56, 30, 12}, 0, 1, FX(0, 0, 10, 0), PAT(1)},
    {"FM LEAD", {1, 86, 92, 72, 56, 0, 24, 0}, {0, 70, 100, 32}, 0, 0, FX(0, 20, 50, 30), PAT(4)},
    {"SYNC LEAD", {3, 96, 100, 90, 72, 0, 0, 0}, {0, 60, 110, 30}, 0, 0, FX(16, 0, 46, 24), PAT(3)},
    {"BRASS", {1, 72, 70, 60, 20, -8, 32, 0}, {14, 70, 100, 36}, 0, 0, FX(0, 20, 20, 40), PAT(4)},
    {"BELL", {13, 70, 84, 0, 0, 0, 20, 0}, {0, 96, 0, 80}, 0, 0, FX(0, 20, 40, 50), PAT(7)},
    /* Dungeon Tracker 1.0: more sounds (appended: a song's preset numbers stay) */
    {"REESE", {1, 60, 80, 60, 24, 0, 100, 40}, {0, 60, 110, 20}, 0, 1, FX(20, 10, 0, 0), PAT(8)},
    {"DUB BASS", {1, 30, 40, 10, 0, 0, 0, 90}, {0, 50, 100, 16}, 0, 1, FX(0, 0, 20, 10), PAT(2)},
    {"SLAP BASS", {5, 96, 26, 0, 10, 8, 0, 30}, {0, 52, 40, 12}, 0, 1, FX(0, 0, 0, 4), PAT(1)},
    {"GROWL FM", {0, 90, 70, 70, 80, 0, 0, 40}, {0, 60, 100, 18}, 0, 1, FX(30, 0, 0, 0), PAT(8)},
    {"ROLLING", {3, 70, 30, 0, 30, 4, 0, 60}, {0, 48, 30, 10}, 0, 1, FX(10, 0, 10, 0), PAT(2)},
    {"WOOD BASS", {1, 50, 20, 0, 0, 2, 0, 50}, {0, 64, 20, 24}, 0, 1, FX(0, 0, 0, 10), PAT(2)},
    {"ZAP BASS", {1, 84, 20, 0, 40, 30, 0, 40}, {0, 50, 50, 12}, 0, 1, FX(10, 0, 0, 0), PAT(1)},
    {"OCTAVE BASS", {0, 64, 50, 30, 20, 0, 40, 70}, {0, 60, 90, 16}, 0, 1, FX(10, 0, 0, 0), PAT(2)},
    {"WARM PAD", {1, 36, 110, 40, 8, 0, 90, 20}, {70, 90, 110, 90}, 0, 0, FX(0, 50, 20, 70), PAT(5)},
    {"CHOIR FM", {3, 50, 110, 60, 0, 0, 80, 0}, {60, 90, 110, 90}, 0, 0, FX(0, 60, 20, 80), PAT(5)},
    {"DARK PAD", {0, 40, 100, 30, 20, 0, 70, 30}, {80, 96, 110, 96}, 0, 0, FX(0, 40, 30, 80), PAT(5)},
    {"AIR PAD", {7, 30, 110, 40, 0, 0, 100, 0}, {90, 96, 100, 100}, 0, 0, FX(0, 60, 40, 90), PAT(5)},
    {"FM STRINGS", {1, 60, 90, 50, 30, -2, 110, 0}, {50, 90, 110, 80}, 0, 0, FX(0, 60, 20, 60), PAT(5)},
    {"DX PIANO", {3, 80, 60, 10, 0, 0, 10, 0}, {0, 88, 30, 50}, 0, 0, FX(0, 10, 20, 40), PAT(6)},
    {"VIBES", {7, 56, 80, 0, 0, 0, 30, 0}, {0, 92, 0, 70}, 0, 0, FX(0, 40, 30, 50), PAT(7)},
    {"CLAV", {5, 100, 30, 0, 40, 0, 0, 0}, {0, 60, 30, 20}, 0, 0, FX(10, 0, 10, 20), PAT(6)},
    {"ORGAN 2", {1, 50, 127, 100, 30, 0, 40, 80}, {0, 0, 127, 16}, 0, 0, FX(0, 60, 0, 40), PAT(6)},
    {"MUSIC BOX", {11, 60, 70, 0, 0, 0, 0, 0}, {0, 90, 0, 60}, 0, 0, FX(0, 20, 40, 60), PAT(7)},
    {"FM HARP", {3, 64, 50, 0, 0, 0, 20, 0}, {0, 80, 0, 60}, 0, 0, FX(0, 20, 30, 60), PAT(3)},
    {"KOTO", {5, 86, 40, 0, 20, 4, 0, 0}, {0, 74, 0, 40}, 0, 0, FX(0, 0, 30, 40), PAT(3)},
    {"DROP PLUCK", {13, 70, 36, 0, 0, 20, 0, 0}, {0, 64, 0, 40}, 0, 0, FX(0, 10, 40, 40), PAT(13)},
    {"FM MARIMBA", {7, 60, 30, 0, 0, 0, 0, 0}, {0, 80, 0, 50}, 0, 0, FX(0, 0, 10, 40), PAT(3)},
    {"FM SAW LEAD", {1, 100, 100, 90, 64, 0, 0, 0}, {0, 70, 110, 30}, 0, 1, FX(10, 40, 40, 30), PAT(4)},
    {"HOLLOW FM", {3, 70, 100, 90, 20, 0, 0, 30}, {0, 70, 110, 34}, 0, 0, FX(0, 20, 40, 30), PAT(4)},
    {"SOLO HORN", {1, 64, 70, 70, 10, -6, 20, 0}, {20, 70, 100, 40}, 0, 0, FX(0, 20, 30, 40), PAT(4)},
    {"WHISTLE", {1, 10, 100, 10, 0, 0, 0, 0}, {30, 80, 110, 40}, 0, 1, FX(0, 20, 40, 50), PAT(4)},
    {"SCREAMER", {3, 110, 110, 100, 72, 0, 60, 0}, {0, 70, 110, 30}, 0, 1, FX(40, 0, 40, 20), PAT(4)},
    {"GLIDE LEAD", {3, 90, 100, 80, 60, -10, 30, 0}, {0, 70, 110, 30}, 0, 1, FX(10, 10, 50, 30), PAT(4)},
    {"HERO LEAD", {1, 80, 90, 80, 20, 0, 30, 0}, {10, 70, 110, 40}, 0, 0, FX(10, 30, 50, 40), PAT(4)},
    {"SIREN", {1, 80, 100, 80, 50, 63, 0, 0}, {40, 90, 100, 60}, 0, 0, FX(0, 20, 50, 60), PAT(7)},
};

static const engine_t ENG_CTONE = {
    .name = "FMTONE",
    .page_title = {"TONE", "BODY"},
    .edit = {
        {"RATIO", F_ENUM, 0, 15, 1, N_CYC_RATIO, 0},
        {"COLOR", F_PCT, 0, 127, 64, 0, 0},
        {"CNTR", F_PCT, 0, 127, 64, 0, 0},
        {"MSUS", F_PCT, 0, 127, 32, 0, 0},
        {"FDBK", F_PCT, 0, 127, 0, 0, 0},
        {"SWEEP", F_BIPCT, -64, 63, 0, 0, 0},
        {"STACK", F_PCT, 0, 127, 0, 0, 0},
        {"SUB", F_PCT, 0, 127, 0, 0, 0},
    },
    .presets = CT_PRESETS,
    .npresets = NELEM(CT_PRESETS),
    .note_on = ct_note_on,
    .render = ct_render,
    .knob = {P_E1, P_E2, P_E0, P_REL},
};
