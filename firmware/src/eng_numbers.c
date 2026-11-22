/* SPDX-License-Identifier: GPL-3.0-only
 * Phantom Cosmonaut 0.9.4: NUMBERS (27), a numbers-station voice (Ed, Nov 2026: VOX "not landing", one phoneme a note;
 * "if I wanted to make it say 'I'm in outer space' using the tracker"; on the prototype: "YES, MAN! ... that creepy
 * vocal vibe I wanted"). A note speaks a whole word, at the note's pitch, through a radio.
 *
 * The words (numbers_tables.h, tools/gen_numbers.py; the prototype tools/numbers_proto.py): 100 of them, each Flite's
 * voice coded as the Speak & Spell coded speech, LPC-10: every 25 ms at 8 kHz ten reflection coefficients (the tract's
 * shape), a loudness, voiced or not, the word's own pitch movement; 10.5 KB in all. Played here as the TMS5220 did: the
 * frames read in order (SPEED: faster, slower, the pitch kept), interpolated, a ten-stage lattice at 8 kHz excited by
 * the note's pitch (HUMAN 0: a short chirp each period, the robot; 127: a glottal pulse, a breath, the word's own rise
 * and fall, a little jitter) or noise where the word hisses; de-emphasised; then linearly up to 44.1 kHz and the radio.
 *   E0 WORD (the word a note speaks in WORD mode: lock it per step on the GRID, or PRESETS on HOME)
 *   E1 RADIO (300 Hz .. 3 kHz, drive, static under a squelch, crackle)  E2 SPEED (-64..63: x0.5 .. x2)  E3 HUMAN
 *   E4 MODE: WORD (each note its WORD) or PHRASE (each note the next word of PHRASE, from the first again when the
 *            FEED's pattern starts over)  E5 PHRASE (16 built in)  E6 DROP (the signal drops out)  E7 TAIL (a squelch
 *            burst after each word)
 * State: per voice in nm_x (a FEED's two, PHANTOM's), the phrase's cursor per part in nm_p. Mono (a voice speaks one
 * word at a time: the next one cuts it, as a keyed radio) */
#include "numbers_tables.h"
#define NM_FR 200u                                     /* samples of 8 kHz a frame (25 ms) */

typedef struct {
    uint32_t bit, fpos, ph8, acc, rng, cnt;            /* the next frame's bit; the read position (frames, Q16); the
                                                        * excitation's phase (8 kHz); 8 kHz in 44.1 kHz (Q16); noise;
                                                        * samples since the period began */
    int32_t b[11], y8p, y8, de, hp, lp, sq, tail, drop; /* the lattice; the last two 8 kHz samples; de-emphasis; the
                                                        * radio's high- and low-pass; the squelch's follower; the tail
                                                        * burst left (samples); a dropout left (samples) */
    int32_t gA, gB, per, dcb, agc, gq, ehp;            /* (ehp: the human pulse's last sample, its tilt undone) */                     /* the two frames' excitation levels; the period (samples); the
                                                        * DC blocker's follower; the AGC's envelope */
    int16_t kA[10], kB[10];                            /* .. their shapes, Q15 */
    int8_t sA, sB;                                     /* .. their pitch movement (1.5 semitone steps) */
    uint8_t vA, vB, word, fi, nfr, done;               /* voiced; the word; frames decoded; its frames; finished */
} nm_x_t;
static nm_x_t nm_x[(NPART + 1u) * 2u] __attribute__((section(".pool")));
#ifndef NM_KNEE
/* the output stage (0.9.4, Ed: "can't really hear what the voice is saying ... pretty muffled"): tuned by a speech
 * recogniser's word accuracy over the 16 phrases (tools/numbers_clarity.py) against the loudness in the mix
 * (notes094_test): driven hard into the knee it was loud but muffled */
#define NM_AGC_T 64000                                  /* the AGC's level (the voice's mean |y| after the band) */
#define NM_AGC_A 12                                     /* .. its attack (93 ms) and release (370 ms): a phrase's level, */
#define NM_AGC_R 14                                     /* not a syllable's (fast, it flattened the words' own shape) */
#define NM_AGC_G 16                                     /* .. its most gain */
#define NM_KNEE 32000                                   /* the knee (the peaks mostly pass) */
#define NM_OUT 48                                       /* the output (x 6: peaks to ~290k; fx.c mix_part and phantom.c
                                                         * pre-shift / clamp for it; past it the master's limiter takes
                                                         * the rest back) */
#endif
#ifndef NM_HISS_GAP
#define NM_HISS_GAP 20000                               /* RADIO's static between the words .. */
#define NM_HISS_DUCK 19000                              /* .. ducked under the voice (-26 dB: it masked the words) */
#endif
static struct { uint8_t cur, last, phr, said; } nm_p[NPART + 1u];   /* PHRASE: the next word; the step the FEED was
                                                                   * on; the phrase; the word spoken last (the UI's) */

static uint32_t nm_part(const track_t *t)
{
    uintptr_t a = (uintptr_t)t;
    return a >= (uintptr_t)&trk[0] && a < (uintptr_t)&trk[NPART] ? (uint32_t)(t - trk) : NPART;
}
static nm_x_t *nm_ext(const track_t *t, const voice_t *v)
{
    return &nm_x[nm_part(t) * 2u + (uint32_t)(v - t->v) % 2u];
}

/* 2^(st16 / 192) in Q16 (st16: 1/16 semitone), -4 .. +4 octaves */
static const uint32_t NM_SEMI[13] = {65536, 69433, 73562, 77936, 82570, 87480, 92682, 98193, 104032, 110218, 116772,
                                     123715, 131072};
static uint32_t nm_ratio(int32_t st16)
{
    int32_t o, k;
    uint32_t r;
    st16 = clamp(st16, -4 * 192, 4 * 192 - 1) + 4 * 192;
    o = (st16 * 2731) >> 19;                            /* / 192 */
    k = st16 - o * 192;
    r = NM_SEMI[k >> 4] + (((NM_SEMI[(k >> 4) + 1] - NM_SEMI[k >> 4]) * (uint32_t)(k & 15)) >> 4);
    return o >= 4 ? r << (o - 4) : r >> (4 - o);
}
static uint32_t nm_isqrt(uint32_t x)
{
    uint32_t r = 0, b = 1u << 30;
    while (b > x) b >>= 2;
    while (b) {
        if (x >= r + b) { x -= r + b; r = (r >> 1) + b; }
        else r >>= 1;
        b >>= 2;
    }
    return r;
}
static uint32_t nm_get(uint32_t *bit, uint32_t n)       /* n bits from NW_BITS, most significant first */
{
    uint32_t v = 0;
    while (n--) {
        uint32_t i = *bit >> 3;
        v = v << 1 | (i < NW_BYTES ? (NW_BITS[i] >> (7u - (*bit & 7u))) & 1u : 0u);
        (*bit)++;
    }
    return v;
}
/* the next frame into slot B (A takes B's); past the word's end: silence */
#define NM_HE 10u                                       /* (the codes of the energy, the pitch movement) */
#define NM_HP 11u
/* coefficient i's index (or NM_HE / NM_HP's value), its canonical Huffman code read (numbers_tables.h NW_HN / NW_HO / NW_HS) */
static uint32_t nm_k(uint32_t *bit, uint32_t i)
{
    uint32_t code = 0, first = 0, at = NW_HO[i], l;
    for (l = 0; l < 15u; l++) {
        uint32_t c = NW_HN[i][l];
        code |= nm_get(bit, 1);
        if (code - first < c)
            return NW_HS[at + code - first];
        at += c;
        first = (first + c) << 1;
        code <<= 1;
    }
    return 0;                                           /* (never: a broken stream) */
}
static void nm_frame(nm_x_t *x)
{
    static const uint8_t KB[10] = {6, 6, 5, 5, 4, 4, 4, 4, 3, 3};
    uint32_t i, e;
    int32_t p = 32768;
    x->gA = x->gB; x->sA = x->sB; x->vA = x->vB;
    for (i = 0; i < 10u; i++)
        x->kA[i] = x->kB[i];
    if (x->fi >= x->nfr) {                              /* the word is over */
        x->gB = 0;
        x->fi++;
        return;
    }
    x->fi++;
    e = nm_k(&x->bit, NM_HE);
    if (!e) {                                           /* a silent frame: the shape held, no sound */
        x->gB = 0;
        return;
    }
    if (nm_get(&x->bit, 1)) {                           /* a repeat: the shape held, a new loudness and pitch */
        x->sB = (int8_t)(((int32_t)nm_k(&x->bit, NM_HP) ^ 8) - 8);
    } else {
        x->vB = (uint8_t)nm_get(&x->bit, 1);
        x->sB = (int8_t)(((int32_t)nm_k(&x->bit, NM_HP) ^ 8) - 8);
        for (i = 0; i < 10u; i++) {                     /* (all ten, voiced or not: 0.9.4's clarity) */
            uint32_t q = nm_k(&x->bit, i);
            x->kB[i] = KB[i] == 6u ? NW_KQ6[q] : KB[i] == 5u ? NW_KQ5[q] : KB[i] == 4u ? NW_KQ4[q] : NW_KQ3[q];
        }
    }
    for (i = 0; i < 10u; i++)                           /* the filter's gain undone: prod (1 - k^2) */
        p = (int32_t)(((int64_t)p * (32768 - ((x->kB[i] * x->kB[i]) >> 15))) >> 15);
    x->gB = (int32_t)(((uint64_t)NW_AMP[e] * nm_isqrt((uint32_t)p << 15)) >> 15);
}
/* word w from its start */
/* KIT (0.9.4, Ed: "more like a drum kit where we can have all possible words in 1 preset"): a key a word, 27 a page,
 * OCT the page (0 .. 3, round: OCT- from the first is the last); the note it plays NM_KBASE + the word, so the GRID, REC
 * and MIDI IN carry the word as a drum kit's note carries its drum. The pitch: KNOB 1 PITCH (E0, a note), the bend,
 * LFO and envelope's pitch on top (the glide not: it would slide between words) */
#define NM_KIT 2
#define NM_KBASE 20u
static uint32_t nm_page(void) { return (uint32_t)((song.octave % 4 + 4) % 4); }
/* word w in three letters (b holds 4: the GRID's): its first three but for NW_ABX's */
static void nm_abbr(uint32_t w, char *b)
{
    uint32_t i;
    const char *n = NW_NAME[w % NW_N];
    for (i = 0; i < 3u; i++)
        b[i] = n[i] && (i == 0u || n[i - 1u]) ? n[i] : ' ';
    b[3] = 0;
    for (i = 0; i < NW_NABX; i++)
        if (NW_ABX[i].w == w) {
            b[0] = NW_ABX[i].a[0];
            b[1] = NW_ABX[i].a[1];
            b[2] = NW_ABX[i].a[2];
        }
}
static int32_t nm_keys(const track_t *t, uint32_t k)
{
    uint32_t w = nm_page() * NW_KPAGE + k;
    if (t->p[P_E4] != NM_KIT)
        return -1;                                      /* (WORD, PHRASE: the keys are notes) */
    return w < NW_N ? (int32_t)(NM_KBASE + w) : 255;      /* (seq.c KB_SILENT: past the 100, a silent key) */
}
static uint32_t nm_kit_inc(const track_t *t, const voice_t *v, const vmod_t *m)
{
    int32_t p16 = clamp(t->p[P_E0], 0, 99) * 16 + (int32_t)m->pitch16 - (int32_t)v->pitch_cur;
    return pitch_inc((uint32_t)clamp(p16, 0, 2047)) >> 8;
}
static const param_desc_t NM_PITCH = {"PITCH", F_NOTE, 0, 99, 45, 0, 0};
static const param_desc_t *nm_desc(const struct track *t, uint32_t k)
{
    return k == 0u && t->p[P_E4] == NM_KIT ? &NM_PITCH : 0;   /* KIT: KNOB 1 the pitch (WORD's the note's then) */
}
static void nm_word(nm_x_t *x, uint32_t w)
{
    x->word = (uint8_t)(w % NW_N);
    x->bit = (NW_OFF[x->word] * 8u);
    x->nfr = NW_NF[x->word];
    x->fi = 0;
    x->fpos = 0;
    x->done = 0;
    x->gB = 0;
    x->vB = 0;
    x->sB = 0;
    nm_frame(x);                                        /* (B: the first frame; A: silence, then the word) */
    nm_frame(x);
}

static void nm_note_on(track_t *t, voice_t *v)
{
    nm_x_t *x = nm_ext(t, v);
    const int16_t *p = t->p;
    uint32_t part = nm_part(t), w;
    if (!v->env && !v->env_out) {                       /* from rest */
        uint32_t i;
        for (i = 0; i < 11u; i++)
            x->b[i] = 0;
        x->y8p = x->y8 = x->de = x->hp = x->lp = x->sq = x->dcb = 0;
        x->agc = NM_AGC_T;
        x->ehp = 0;

        x->gq = 4096;
        x->ph8 = 0;
        x->acc = 0;
        x->drop = 0;
        x->rng = 0x6D2B79F5u ^ ((uint32_t)v->note << 8) ^ (v->age * 2654435761u);
    }
    if (p[P_E4] == 1) {                                 /* PHRASE: the next word; from the first when the FEED starts over */
        uint32_t ph = (uint32_t)clamp(p[P_E5], 0, NW_NPHR - 1), idx = part < NPART ? t->seq_idx : 0u;
        if (nm_p[part].phr != ph || (part < NPART && song.playing && idx < nm_p[part].last))
            nm_p[part].cur = 0;
        nm_p[part].phr = (uint8_t)ph;
        nm_p[part].last = (uint8_t)idx;
        if (nm_p[part].cur >= 8u || NW_PHRASE[ph][nm_p[part].cur] == 255u)
            nm_p[part].cur = 0;
        w = NW_PHRASE[ph][nm_p[part].cur++];
    } else if (p[P_E4] == NM_KIT) {                     /* KIT: the note is the word (a key, a word: nm_keys) */
        if (v->note < NM_KBASE || v->note >= NM_KBASE + NW_N) {
            x->done = 1;                                /* (past the 100: nothing) */
            x->nfr = 0;
            x->gA = x->gB = 0;
            return;
        }
        w = v->note - NM_KBASE;
    } else
        w = (uint32_t)clamp(p[P_E0], 0, NW_N - 1);
    nm_word(x, w);
    nm_p[part].said = (uint8_t)w;
    x->agc = NM_AGC_T / 4;                              /* (each word's level from x4: the gap's gain had run up;
                                                         * reset, the words came out clearer than without) */
    x->gq = 4096 * 4;
    x->tail = 0;
}

/* the UI (ui_graph.c graph_numbers): word w's loudness a frame (0..31), up to n frames; returns its frames */
static uint32_t nm_contour(uint32_t w, uint8_t *e, uint32_t n)
{
    uint32_t bit = (NW_OFF[w % NW_N] * 8u), nf = NW_NF[w % NW_N], i, j;
    for (i = 0; i < nf && i < n; i++) {
        e[i] = (uint8_t)nm_k(&bit, NM_HE);
        if (!e[i])
            continue;
        if (nm_get(&bit, 1)) {
            nm_k(&bit, NM_HP);
            continue;
        }
        nm_get(&bit, 1);
        nm_k(&bit, NM_HP);
        for (j = 0; j < 10u; j++)
            nm_k(&bit, j);
    }
    return nf < n ? nf : n;
}
/* .. the frame voice v of t is on now (0xFF: silent) */
static uint32_t nm_now(const track_t *t)
{
    const nm_x_t *x = &nm_x[nm_part(t) * 2u];
    uint32_t k;
    for (k = 0; k < 2u; k++, x++)
        if (t->v[k].active && x->fi && x->fi <= x->nfr)
            return x->fi - 1u;
    return 0xFFu;
}

static void nm_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    static const int16_t CH[8] = {0, 26214, -19661, 16384, -13107, 9830, -6554, 3277};   /* the robot's chirp, Q15 */
    const int16_t *p = t->p;
    nm_x_t *x = nm_ext(t, v);
    int32_t radio = clamp(p[P_E1], 0, 127) * 258, human = clamp(p[P_E3] + ((m->shape - (64 << 8)) >> 8), 0, 127) * 258;
    int32_t drop = clamp(p[P_E6], 0, 127), tail = clamp(p[P_E7], 0, 127);
    uint32_t i, sp = nm_ratio(clamp(p[P_E2], -64, 63) * 3), finc = (sp + NM_FR / 2u) / NM_FR;   /* (SPEED: an octave
                                                         * of ratio each way: x0.5 .. x2; the frames a sample, Q16) */
    uint32_t inc8 = (t->p[P_E4] == NM_KIT ? nm_kit_inc(t, v, m) : m->inc >> 8) * 1411u;   /* the note at 8 kHz:
                                                         * x 44100 / 8000 (Q8); KIT: PITCH's */
    int32_t lpk = 14000 - ((radio * 5600) >> 15), drive = 32768 + radio;   /* the radio's low-pass
                                                         * (3 .. 1.8 kHz), its drive (to x2: the band's level made up,
                                                         * the knee does the rest) */
    if (n > CTL)
        n = CTL;
    for (i = 0; i < n; i++) {
        int32_t s, y;
        x->acc += 11889u;                               /* 8000 / 44100 in Q16 */
        if (x->acc >= 65536u) {                         /* a sample of 8 kHz */
            int32_t t16, g, e, k[10], f, st;
            uint32_t j, fr, voiced, step;
            x->acc -= 65536u;
            x->fpos += finc;
            fr = x->fpos >> 16;
            while (fr + 2u > x->fi && x->fi <= x->nfr + 1u)   /* (the frames read ahead: A the one playing, B the next) */
                nm_frame(x);
            if (x->fi > x->nfr + 1u && fr + 1u >= x->fi) {
                if (!x->done && tail)
                    x->tail = 5292;                     /* (the word over: the squelch's burst, 120 ms) */
                x->done = 1;
            }
            t16 = (int32_t)(x->fpos & 0xFFFFu);
            g = x->done ? 0 : x->gA + (int32_t)(((int64_t)(x->gB - x->gA) * t16) >> 16);
            for (j = 0; j < 10u; j++)
                k[j] = x->kA[j] + (((x->kB[j] - x->kA[j]) * (t16 >> 1)) >> 15);   /* (t16 / 2: no overflow) */
            voiced = t16 < 32768 ? x->vA : x->vB;
            st = x->sA * 24 + (((x->sB - x->sA) * 24 * t16) >> 16);   /* (1/16 semitones) */
            step = (uint32_t)(((uint64_t)inc8 * nm_ratio((st * human) >> 15)) >> 16);
            x->rng = x->rng * 1664525u + 1013904223u;
            if (voiced) {
                uint32_t was = x->ph8;
                int32_t er, eh;
                x->ph8 += step + (uint32_t)((int32_t)(step >> 9) * (((int32_t)(x->rng >> 24) - 128) * human >> 15) >> 7);
                if (x->ph8 < was) {                     /* a new period */
                    x->cnt = 0;
                    x->per = step ? (int32_t)(0xFFFFFFFFu / step) : 64;
                } else
                    x->cnt++;
                er = x->cnt < 8u ? (CH[x->cnt] * (int32_t)nm_isqrt((uint32_t)x->per * 41216u)) >> 8 : 0;   /* unit
                                                         * power: x sqrt(per / 1.59) (sum CH^2 = 1.59) */
                {   /* the glottal pulse's derivative (Rosenberg, open 0.6), unit power (x 2.58) */
                    uint32_t ph = x->ph8 >> 16;         /* 0..65535 of the period */
                    if (ph < 25953u)                    /* (0.66 x 0.6) opening */
                        eh = (sine_i(ph * 82750u) * 42271) >> 15;   /* sin (pi ph / 0.396) x 0.5 x 2.58 */
                    else if (ph < 39322u)               /* closing, to 0.6 */
                        eh = -(int32_t)(((int64_t)sine_i((ph - 25953u) * 80299u) * 84541) >> 15);
                    else
                        eh = 0;
                }
                {   /* the pulse's own tilt undone (the frames carry the voice's tone already: a dark pulse on them
                     * muffled HUMAN), its level kept */
                    int32_t d = eh - ((x->ehp * 22938) >> 15);   /* (1 - 0.7 z^-1, x 1.22) */
                    x->ehp = eh;
                    eh = (int32_t)(((int64_t)d * 40000) >> 15);
                }
                e = er + (int32_t)(((int64_t)(eh - er) * human) >> 15);
                e += (((int32_t)(x->rng >> 16) - 32768) * (human >> 5)) >> 15;   /* (a breath under the human voice;
                                                         * halved in 0.9.4: louder, it hid the words) */
            } else
                e = (int32_t)(((int64_t)((int32_t)(x->rng >> 16) - 32768) * 56756) >> 15);   /* noise, unit power */
            f = (int32_t)(((int64_t)e * g) >> 15);
            for (j = 10u; j-- > 0u;) {                  /* the lattice, all-pole */
                f -= (int32_t)(((int64_t)k[j] * x->b[j]) >> 15);
                x->b[j + 1u] = x->b[j] + (int32_t)(((int64_t)k[j] * f) >> 15);
            }
            x->b[0] = f;
            x->de = f + (int32_t)(((int64_t)x->de * 30720) >> 15);   /* de-emphasis (0.9375) */
            x->y8p = x->y8;
            x->dcb += (x->de - x->dcb) >> 7;           /* (a DC blocker, ~10 Hz) */
            x->y8 = clamp(x->de - x->dcb, -(1 << 24), 1 << 24);

            if (drop && x->drop <= 0 && (x->fpos & 0xFFFFu) < finc &&   /* DROP: at a frame's start, now and then */
                (int32_t)((x->rng >> 8) & 1023u) < drop * 2)
                x->drop = (int32_t)(200u + ((x->rng >> 4) & 511u));   /* (25 .. 90 ms) */
            if (x->drop > 0)
                x->drop--;
        }
        y = x->y8p + (int32_t)(((int64_t)(x->y8 - x->y8p) * (int32_t)x->acc) >> 16);
        /* the radio: band-limited, driven, static under a squelch that opens with the voice, crackle */
        x->hp += (int32_t)(((int64_t)(y - x->hp) * 1399) >> 15);   /* (300 Hz) */
        {
            int32_t bl = y - x->hp;
            x->lp += (int32_t)(((int64_t)(bl - x->lp) * lpk) >> 15);
            y += (int32_t)(((int64_t)(((int32_t)(((int64_t)x->lp * drive) >> 15)) - y) * radio) >> 15);
        }
        {   /* the AGC (Ed, 0.9.4: "I can't hear the voice synth at all" in a car): a radio's, after the band, so it
             * levels what is heard: the words brought up to one level (8 ms on, 60 ms off at 44.1 kHz, up to x32,
             * the gain worked out every 8 samples), the peaks then the knee's */
            int32_t a = y < 0 ? -y : y;
            if (!x->done)                               /* (the word over: the gain held, the tail not pumped up) */
                x->agc += a > x->agc ? (a - x->agc) >> NM_AGC_A : -((x->agc - a) >> NM_AGC_R);
            if (!(i & 7u))
                x->gq = (int32_t)(((uint32_t)NM_AGC_T << 12) /
                                  (uint32_t)(x->agc > NM_AGC_T / NM_AGC_G ? x->agc : NM_AGC_T / NM_AGC_G));

            y = (int32_t)(((int64_t)y * x->gq) >> 12);
            a = y < 0 ? -y : y;                         /* (the squelch follows the voice as heard: after the AGC) */
            x->sq += (a - x->sq) >> 9;
        }
        s = soft_knee(clamp(y >> 2, -200000, 200000), NM_KNEE);
        if (radio) {
            int32_t nz = (int32_t)((x->rng >> 16) & 0xFFFFu) - 32768, gate = clamp(x->sq >> 1, 0, 32767);
            x->rng = x->rng * 22695477u + 1u;
            /* (0.9.4: a squelch's way round: the hiss in the gaps, ducked under the voice; it rose with the voice
             * before, and masked the words) */
            s += (int32_t)(((int64_t)nz * (radio >> 4) * (NM_HISS_GAP - ((gate * NM_HISS_DUCK) >> 15))) >> 30);
            if (((x->rng >> 8) & 0xFFFFu) < (uint32_t)(radio >> 10))
                s += nz >> 1;                           /* (crackle) */
        }
        if (x->tail > 0) {                              /* TAIL: the squelch after the word */
            int32_t nz = (int32_t)((x->rng >> 16) & 0xFFFFu) - 32768;
            x->rng = x->rng * 22695477u + 1u;
            s += (int32_t)(((int64_t)nz * tail * x->tail) >> 22);
            x->tail--;
        }
        if (x->drop > 0)
            s >>= 4;                                    /* (a dropout: the signal fades under) */
        out[i] += (voice_amp(s, m, i) * NM_OUT) >> 3;  /* (speech, levelled by the AGC, then x NM_OUT / 8: as loud
                                                         * in the mix as a lead: notes094_test) */
    }
}

/* {WORD, RADIO, SPEED, HUMAN, MODE, PHRASE, DROP, TAIL} */
static const preset_t NUMBERS_PRESETS[] = {
    {"STATION", {43, 110, -4, 15, NM_KIT, 14, 0, 70}, {0, 60, 127, 12}, 0, 1, FX(0, 0, 20, 40), PAT(4)},
    {"OUTER SPACE", {0, 45, -12, 95, 1, 0, 0, 0}, {2, 60, 127, 30}, 0, 1, FX(0, 20, 30, 60), PAT(4)},
    {"GROUND CTRL", {0, 60, 0, 55, 1, 1, 0, 40}, {0, 60, 127, 16}, 0, 1, FX(0, 0, 25, 40), PAT(4)},
    {"HEAR ME", {0, 30, -20, 110, 1, 2, 10, 0}, {4, 60, 127, 40}, 0, 1, FX(0, 30, 40, 80), PAT(4)},
    {"MAYDAY", {0, 105, 6, 20, 1, 8, 25, 60}, {0, 60, 127, 10}, 0, 1, FX(20, 0, 20, 30), PAT(4)},
    {"GHOST", {0, 25, -30, 127, 1, 9, 15, 0}, {6, 60, 127, 60}, 0, 1, FX(0, 40, 50, 110), PAT(4)},
    {"SPEAK SPELL", {48, 0, 0, 0, NM_KIT, 14, 0, 0}, {0, 60, 127, 8}, 0, 1, FX(0, 0, 0, 0), PAT(4)},
    {"DEAD AIR", {0, 127, 0, 35, 1, 4, 70, 100}, {0, 60, 127, 12}, 0, 1, FX(0, 0, 30, 60), PAT(4)},
};
static const char *const N_NM_MODE[4] = {"WORD", "PHRASE", "KIT", 0};
/* a word is a hit (0.9.4: a step's gate is a 16th, the word half a second): the voice is the word's, its tail and the
 * lattice's ring (engine_t.ownenv: no ADSR, no velocity); it ends there, or when the next word cuts it */
static int nm_done(track_t *t, voice_t *v)
{
    const nm_x_t *x = nm_ext(t, v);
    return x->done && x->tail <= 0 && x->y8 < 256 && x->y8 > -256;
}
static const engine_t ENG_NUMBERS = {
    .name = "NUMBERS",
    .page_title = {"VOICE", "RADIO"},
    .edit = {
        {"WORD", F_ENUM, 0, NW_N - 1, 7, NW_NAME, 0},
        {"RADIO", F_PCT, 0, 127, 70, 0, 0},
        {"SPEED", F_BIPCT, -64, 63, 0, 0, 0},
        {"HUMAN", F_PCT, 0, 127, 40, 0, 0},
        {"MODE", F_ENUM, 0, 2, 2, N_NM_MODE, 0},
        {"PHRASE", F_ENUM, 0, NW_NPHR - 1, 0, NW_PHRASE_NAME, 0},
        {"DROP", F_PCT, 0, 127, 0, 0, 0},
        {"TAIL", F_PCT, 0, 127, 40, 0, 0},
    },
    .presets = NUMBERS_PRESETS,
    .npresets = NELEM(NUMBERS_PRESETS),
    .note_on = nm_note_on,
    .render = nm_render,
    .knob = {P_E0, P_E1, P_E2, P_E3},                     /* HOME: WORD (KIT: PITCH) RADIO SPEED HUMAN */
    .poly = 1,
    .keys = nm_keys,
    .desc = nm_desc,
    .ownenv = 1,
    .done = nm_done,
};
