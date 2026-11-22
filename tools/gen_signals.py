#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Phantom Cosmonaut: the 16 SIGNALs, the wavetables of the MORPH engine (firmware/src/eng_morph.c), written to
firmware/src/signals_tables.h.

Each signal is a bank of eight frames (single cycles) that SCAN moves through, cross-fading neighbours; each bank
sweeps somewhere on purpose from its first frame to its last. All are designed here from maths (spec section 4: "mostly
script-designed"): additive spectra, pulses, formants, sync, combs, phase distortion, folding, a few with fixed
pseudo-random harmonics for noise. Neighbouring frames share their harmonics' phases, so a cross-fade never cancels.

The format is Dungeon Tracker 1.5's WAVE 32 (its tools/gen_wt32.py): every frame band-limited and kept at seven levels (a mipmap) of 256, 128,
128, 64, 32, 16 and 8 samples with harmonics 1..120, 60, 30, 15, 7, 3, 1, each followed by a copy of its first sample
(the interpolation's guard); the engine reads the fullest level whose top harmonic stays under ~11.5 kHz, so no note
aliases. A frame's levels share one gain (its 256-sample peak at -1 dBFS), so a note keeps its level from one size to
the next. Samples are 8-bit (the budget: 16 x 8 x 639 B = 81.8 KB of the spec's 96 KB): the PPG's cold digital edge,
the rounding error a fixed pattern under -45 dB.
    python3 tools/gen_signals.py          (needs numpy; writes firmware/src/signals_tables.h)"""
import math
import pathlib
import numpy as np

SIZES = [256, 128, 128, 64, 32, 16, 8]
HARM = [120, 60, 30, 15, 7, 3, 1]
NH = 120
NF = 8
OVER = 4096                      # time-domain shapes are drawn at this resolution, then band-limited
N = np.arange(1, NH + 1)         # harmonic numbers


def rng(seed):
    return np.random.default_rng(seed)


def from_wave(w):
    """complex harmonics 1..120 of a time-domain cycle"""
    X = np.fft.rfft(np.asarray(w, dtype=float)) / (len(w) / 2)
    return X[1:NH + 1]


def additive(amps, phases=None):
    """harmonics from amplitudes, sine phase (or the phases given)"""
    a = np.asarray(amps, dtype=float)[:NH]
    a = np.concatenate([a, np.zeros(NH - len(a))])
    ph = np.zeros(NH) if phases is None else np.asarray(phases, dtype=float)[:NH]
    return a * np.exp(1j * (ph - math.pi / 2))


X = np.arange(OVER) / OVER


def sweep(a, b, k):
    """frame k's place between a and b (0..7 -> a..b)"""
    return a + (b - a) * k / (NF - 1)


# ---------------------------------------------------------------------------------------------------- the signals
def s_carrier():
    """a clean carrier opening up: a pure sine, then a saw's harmonics let in more and more"""
    out = [additive([1])]
    for c in (0.8, 1.6, 3, 6, 12, 30):
        out.append(additive(np.exp(-(N - 1) / c) / N))
    out.append(additive(1 / N))
    return out


def s_pulsar():
    """a pulse narrowing from square to a thin tick"""
    return [from_wave(np.where(X < w, 1.0, -1.0)) for w in (0.5, 0.4, 0.31, 0.23, 0.16, 0.1, 0.06, 0.03)]


def s_telemetry():
    """a sine read by an ever coarser converter: 64 levels down to 2 (a square)"""
    s = np.sin(2 * math.pi * X)
    out = []
    for q in (128, 24, 12, 8, 6, 4, 3, 2):
        out.append(from_wave(np.round(s * (q / 2 - 0.5) + 0.0001) / (q / 2 - 0.5) if q > 2 else np.sign(s)))
    return out


def s_vostok():
    """a voice in a helmet: vowels U O A E I and back, formants over a 110 Hz pulse"""
    f0 = 110.0
    V = [(300, 870, 2240), (360, 800, 2400), (570, 840, 2410), (730, 1090, 2440), (660, 1700, 2400),
         (530, 1840, 2480), (390, 1990, 2550), (270, 2290, 3010)]
    out = []
    for f1, f2, f3 in V:
        a = []
        for k in N:
            f = f0 * k
            g = (1 / (1 + ((f - f1) / 90) ** 2) + 0.6 / (1 + ((f - f2) / 120) ** 2) + 0.35 / (1 + ((f - f3) / 160) ** 2))
            a.append(g / math.sqrt(k))
        out.append(additive(a))
    return out


def s_cold_start():
    """an organ warming up: drawbars pulled out one after another"""
    REG = [{1: 1}, {1: 1, 2: .5}, {1: 1, 2: .6, 3: .4}, {1: 1, 2: .6, 3: .5, 4: .4}, {1: 1, 2: .7, 3: .5, 4: .5, 6: .3},
           {1: 1, 2: .7, 3: .6, 4: .5, 6: .4, 8: .3}, {1: .8, 2: .8, 3: .6, 4: .6, 5: .3, 6: .5, 8: .4, 10: .2},
           {1: .7, 2: .8, 3: .7, 4: .7, 5: .4, 6: .6, 8: .6, 10: .3, 12: .3, 16: .2}]
    out = []
    for r in REG:
        a = np.zeros(NH)
        for h, v in r.items():
            a[h - 1] = v
        out.append(additive(a))
    return out


def s_orbit_decay():
    """a slave oscillator hard-synced to the carrier, its ratio climbing: the classic tearing sweep"""
    out = []
    for r in (1.0, 1.4, 1.9, 2.5, 3.2, 4.1, 5.3, 7.0):
        out.append(from_wave(1 - 2 * ((X * r) % 1.0)))
    return out


def s_solar_wind():
    """a band of noise drifting up through the spectrum, over a faint fundamental (fixed random phases)"""
    R = rng(7)
    ph, mag = R.uniform(0, 2 * math.pi, NH), R.uniform(0.4, 1.0, NH)
    out = []
    for k in range(NF):
        c = math.exp(sweep(math.log(2), math.log(70), k))
        a = mag * np.exp(-((np.log(N) - math.log(c)) ** 2) / 0.35) / np.sqrt(N)
        a[0] += 0.35
        out.append(additive(a, ph))
    return out


def s_ionosphere():
    """a saw through a comb whose notches sweep (the shimmer of a signal bouncing off the sky)"""
    out = []
    for k in range(NF):
        d = sweep(0.03, 0.43, k)
        out.append(additive(np.abs(np.cos(math.pi * N * d)) / N))
    return out


def s_dead_star():
    """a cold glassy cluster of partials climbing away from a faint fundamental"""
    out = []
    for k in range(NF):
        n0 = sweep(4, 52, k)
        a = np.exp(-((N - n0) ** 2) / (6 + n0 / 2)) / (1 + n0 / 30)
        a[0] += 0.3
        out.append(additive(a))
    return out


def s_static():
    """a saw buried under more and more radio static (random harmonics, fixed per frame set)"""
    R = rng(10)
    ph, mag = R.uniform(0, 2 * math.pi, NH), R.uniform(0.0, 1.0, NH)
    out = []
    for k in range(NF):
        m = sweep(0.0, 1.0, k)
        a = (1 - m) / N + m * mag / np.sqrt(N) * 0.8
        out.append(additive(a, ph * m))
    return out


def s_hull_groan():
    """a low saw through two resonances wandering in the lower mids: metal under stress"""
    f0 = 55.0
    out = []
    for k in range(NF):
        r1, r2 = sweep(180, 520, k), sweep(900, 400, k)
        a = []
        for h in N:
            f = f0 * h
            a.append((1 / h) * (0.25 + 2.0 / (1 + ((f - r1) / 40) ** 2) + 1.2 / (1 + ((f - r2) / 60) ** 2)))
        out.append(additive(a))
    return out


def s_numbers():
    """a numbers station's tones: spectra switched on and off by binary patterns, PPG style, opening up"""
    out = []
    for k in range(NF):
        pat = [0b1011, 0b1101, 0b10110, 0b11001, 0b101011, 0b110101, 0b1011011, 0b1110101][k]
        a = np.array([(1.0 if (pat >> ((h - 1) % pat.bit_length())) & 1 else 0.0) / math.sqrt(h) for h in N])
        a[0] = 1.0
        out.append(additive(a * np.exp(-N / sweep(6, 90, k))))   # (and opening up as it goes)
    return out


def s_last_words():
    """breath in a voice: vowel formants with a hiss that grows over them (fixed random phases)"""
    R = rng(13)
    ph, hiss = R.uniform(0, 2 * math.pi, NH), R.uniform(0.2, 1.0, NH)
    f0 = 130.0
    V = [(730, 1090, 2440), (570, 840, 2410), (440, 1020, 2240), (300, 870, 2240), (390, 1990, 2550),
         (530, 1840, 2480), (660, 1700, 2400), (490, 1350, 1690)]
    out = []
    for k, (f1, f2, f3) in enumerate(V):
        m = sweep(0.05, 0.7, k)
        a = []
        for i, h in enumerate(N):
            f = f0 * h
            g = (1 / (1 + ((f - f1) / 80) ** 2) + 0.7 / (1 + ((f - f2) / 110) ** 2) + 0.4 / (1 + ((f - f3) / 150) ** 2))
            a.append((1 - m) * g / math.sqrt(h) + m * hiss[i] * 0.35 / math.sqrt(h))
        out.append(additive(a, ph * m))
    return out


def s_sputnik():
    """the beacon: a sine growing a ring-modulated twin, then thin pulses ringing at the 6th"""
    out = []
    for k in range(NF):
        if k < 4:
            r = 2 + k                                          # sine x sine (ratio r): partials r-1 and r+1
            a = np.zeros(NH)
            a[0] = 1 - k * 0.2
            a[r - 2] += 0.5 + k * 0.1
            a[r] += 0.5 + k * 0.1
            out.append(additive(a))
        else:
            w = (0.25, 0.14, 0.08, 0.04)[k - 4]
            env = np.where(X < w, np.sin(math.pi * X / w), 0.0)
            out.append(from_wave(env * np.sin(2 * math.pi * 6 * X) + 0.3 * np.sin(2 * math.pi * X)))
    return out


def s_relay():
    """phase distortion (the CZ's resonance): a cosine read faster and faster in its first part"""
    out = []
    for k in range(NF):
        d = sweep(0.5, 0.03, k)                             # the knee: 0.5 = a plain cosine
        p = np.where(X < d, X / d * 0.5, 0.5 + (X - d) / (1 - d) * 0.5)
        out.append(from_wave(-np.cos(2 * math.pi * p * (1 + k * 0.6)) * (1 - X)))
    return out


def s_dark_side():
    """a sine through a wavefolder driven harder and harder: the far side of the moon"""
    s = np.sin(2 * math.pi * X)
    return [from_wave(np.sin(g * math.pi / 2 * s)) for g in (1.0, 1.6, 2.2, 2.9, 3.7, 4.6, 5.7, 7.0)]


SIGNALS = [("CARRIER", s_carrier), ("PULSAR", s_pulsar), ("TELEMETRY", s_telemetry), ("VOSTOK", s_vostok),
           ("COLD START", s_cold_start), ("ORBIT DECAY", s_orbit_decay), ("SOLAR WIND", s_solar_wind),
           ("IONOSPHERE", s_ionosphere), ("DEAD STAR", s_dead_star), ("STATIC", s_static),
           ("HULL GROAN", s_hull_groan), ("NUMBERS", s_numbers), ("LAST WORDS", s_last_words),
           ("SPUTNIK", s_sputnik), ("RELAY", s_relay), ("DARK SIDE", s_dark_side)]


def frame_levels(Xh):
    """the levels of a frame from its harmonics; one gain for all (the 256's peak at -1 dBFS)"""
    waves = []
    for n, h in zip(SIZES, HARM):
        spec = np.zeros(n // 2 + 1, dtype=complex)
        spec[1:h + 1] = Xh[:h]
        waves.append(np.fft.irfft(spec * (n / 2), n))
    g = 0.89 / max(1e-9, np.max(np.abs(waves[0])))
    return [np.clip(np.round(w * g * 127), -127, 127).astype(int) for w in waves]


def main():
    assert len(SIGNALS) == 16 and all(len(n) <= 12 for n, _ in SIGNALS)
    rows = []
    for name, gen in SIGNALS:
        frames = gen()
        assert len(frames) == NF, name
        for f in frames:
            data = []
            for w in frame_levels(f):
                data += list(w) + [int(w[0])]
            rows.append(data)
    per = sum(s + 1 for s in SIZES)
    out = ["/* SPDX-License-Identifier: GPL-3.0-only */",
           "/* generated by tools/gen_signals.py: the 16 SIGNALs of the MORPH engine (eng_morph.c), 8 frames each, 8-bit */",
           f"#define SIG_N {len(SIGNALS)}",
           f"#define SIG_NF {NF}",
           f"#define SIG_FRAME {per}             /* a frame's seven levels, each with its guard sample */",
           "#define SIG_NL 7",
           "static const uint16_t SIG_OFS[SIG_NL] = {" + ", ".join(str(sum(s + 1 for s in SIZES[:i])) for i in range(7)) + "};",
           "static const uint8_t SIG_BITS[SIG_NL] = {" + ", ".join(str(int(math.log2(s))) for s in SIZES) + "};",
           "static const uint8_t SIG_HARM[SIG_NL] = {" + ", ".join(str(h) for h in HARM) + "};",
           "static const char *const SIG_NAME[SIG_N + 1] = {" + ", ".join(f'"{n}"' for n, _ in SIGNALS) + ", 0};",
           f"static const int8_t SIG_TAB[SIG_N * SIG_NF][SIG_FRAME] = {{"]
    for s, (name, _) in enumerate(SIGNALS):
        out.append(f"    /* {s + 1:02d} {name} */")
        for f in range(NF):
            out.append("    {" + ",".join(str(v) for v in rows[s * NF + f]) + "},")
    out.append("};")
    dest = pathlib.Path(__file__).resolve().parent.parent / "firmware" / "src" / "signals_tables.h"
    dest.write_text("\n".join(out) + "\n")
    print(f"signals: {len(SIGNALS)} x {NF} frames x {per} samples -> {dest} ({len(SIGNALS) * NF * per} B)")


if __name__ == "__main__":
    main()
