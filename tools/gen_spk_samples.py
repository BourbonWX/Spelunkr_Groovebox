#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Spelunkr Groovebox: the sampled sounds (Ed, Nov 18 2026: "take drums samples where we can and really lean into
that aesthetic": vaporwave, chiptune, lofi beats).

Two steps:

  gen_spk_samples.py --prepare VCSL_DIR
      makes the sounds once into assets/samples-spk/ (22.05 kHz mono 16-bit WAVs and spk.json: roots and loops).
      VCSL_DIR: a checkout of github.com/sgossner/VCSL (Versilian Community Sample Library, CC0) holding the files
      named below (a sparse or blobless clone is enough). The chip and drum-machine kits are synthesized here.

  gen_spk_samples.py OUT/felucca_samples.h
      the headers (tools/build.py), from assets/samples-spk/ alone:
        felucca_samples.h  the SAMPLE engine's sets, zones and presets (eng_sample.c)
        spk_kits.h         beside it: the KIT engine's sampled sets (eng_cycle.c, a lane's MODEL past BLOOP), the
                           GRID's eight rows each (BD SD CP CH OH TM RS CB)
      IMA ADPCM (4 bit) at 22.05 kHz, as the FM-1's own sample sets.

The kits (KITS):
  CRATE      VCSL's acoustic kit through an SP-1200's path: 26 kHz, 12 bit, pitched down, tape
  BEDROOM    the soft VCSL kit on a worn cassette: brushes of the rope snare, the hat's pedal, a cymbal for the open hat
  VAPOR 808  an 808 synthesized (bridged-T kick, the six square-wave metals, the clap's four bursts), warmed on tape
  LINN       VCSL's kit as an early-80s drum machine stored it: 28 kHz 8-bit mu-law, tight, a little room
  GATED      big 80s snare, toms and clap: VCSL's through a long room, gated
  NES        the 2A03: the triangle's dropping kick, the 15-bit LFSR noise (and its short metallic mode), pulses
  GAMEBOY    the DMG: the 4-bit wave channel's kick, 7- and 15-bit noise, 64 Hz envelope steps
  SNES       VCSL's kit as a 16-bit console played it: 11 kHz BRR blocks, Gaussian interpolation, the echo
The instruments (INSTS): UPRIGHT (piano), FM PIANO (the TX81Z's), VIBES, MARIMBA, GLOCK, KALIMBA, SAX (looped),
GLASS (wine glasses, looped). PRESETS: the SAMPLE engine's factory sounds on them (WOW, AGE: eng_sample.c).
"""
import hashlib
import json
import re
import sys
import wave
from pathlib import Path

import numpy as np
from scipy import signal

sys.path.insert(0, str(Path(__file__).resolve().parent))
from sampleio import IMA_STEP, detect_hz, hz_to_midi, ima_encode, key_split, read_any_wav  # noqa: E402

SRC = Path(__file__).resolve().parents[1]
PREP = SRC / "assets" / "samples-spk"
CACHE = SRC / "build" / "gen_spk_samples.cache"
SR = 44100                       # working rate
TR = 22050                       # stored rate

# ---------------------------------------------------------------- VCSL files (CC0)
D_BD = "Membranophones/Struck Membranophones/Bass Drum 1/"
D_SN = "Membranophones/Struck Membranophones/Snare Drum, Modern 1/"
D_RS = "Membranophones/Struck Membranophones/Snare Drum, Rope Tension/"
D_HH = "Idiophones/Struck Idiophones/Hi-Hat Cymbal/"
D_TM = "Membranophones/Struck Membranophones/Tom 1/"
V = {
    "kick": D_BD + "BDrumNew_hit_v5_rr1_Sum.wav",
    "kick_soft": D_BD + "BDrumNew_hit_v3_rr1_Sum.wav",
    "snare": D_SN + "Snare2_HitSN_v6_rr1_Mid.wav",
    "snare_stick": D_SN + "Snare2_stick_v1_rr1_Mid.wav",
    "rope": D_RS + "Low/RopeSnare_low_ns_Main_vl2_rr1.wav",
    "side": D_RS + "RopeSnare_sidestick_Main_vl2_rr1.wav",
    "clap": "Idiophones/Struck Idiophones/Claps/Clap_rr2.wav",
    "clap_solo": "Idiophones/Struck Idiophones/Claps/SoloClap_vl3.wav",
    "hat": D_HH + "HiHat_HitC_v3_rr1_Mid.wav",
    "hat_pedal": D_HH + "HiHat_Close_rr1_Mid.wav",
    "hat_open": D_HH + "HiHat_HitO_rr1_Mid.wav",
    "cymbal": "Idiophones/Struck Idiophones/Suspended Cymbal 1/susCymb1_hit_stick_mp1.wav",
    "tom": D_TM + "Stick/TomH_HitS_v3_rr1_Mid.wav",
    "tom_mallet": D_TM + "Mallet/TomH_HitM_v3_rr1_Mid.wav",
    "cowbell": "Idiophones/Struck Idiophones/Cowbells/Cowbell1_Normal_v2_rr1_Mid.wav",
    "tamb": "Idiophones/Struck Idiophones/Tambourine 1/Tamb1_Hit_v2_rr1_Mid.wav",
}
INSTS = [   # name, VCSL folder, file filter, kind, seconds kept, loop region (s) for "loop"
    ("UPRIGHT", "Chordophones/Zithers/Upright Piano, Knight/Sustains", r"Player_vl2_rr1_", "oneshot", 2.6, None),
    ("FM PIANO", "Electrophones/TX81Z/FM Piano", r"_vl2", "oneshot", 2.2, None),
    ("VIBES", "Idiophones/Struck Idiophones/Vibraphone/Soft Mallets", r"_v2_", "oneshot", 2.6, None),
    ("MARIMBA", "Idiophones/Struck Idiophones/Marimba", r"_med_", "oneshot", 1.2, None),
    ("GLOCK", "Idiophones/Struck Idiophones/Glockenspiel", r"_medium_", "oneshot", 1.8, None),
    ("KALIMBA", "Idiophones/Plucked Idiophones/Kalimba, Kenya", r"", "oneshot", 1.8, None),
    ("SAX", "Aerophones/Reed Aerophones/Tenor Saxophone/Non-Vibrato", r"_(C|E|G#)\d_vl2_", "loop", 1.6, (0.45, 1.5)),
    ("GLASS", "Idiophones/Friction Idiophones/Wine Glasses/Sustains/Slow", r"", "loop", 3.0, (1.1, 2.9)),
]
SHOWN = {"FM PIANO": "FM EP"}   # a set's name on the screen where its own is longer than a card's value (7)
ROWS = ["BD", "SD", "CP", "CH", "OH", "TM", "RS", "CB"]
ROW_CAP = [0.9, 0.55, 0.6, 0.16, 0.75, 0.8, 0.25, 0.45]       # seconds kept, unless a kit says
ROW_GAIN = [1.0, 0.92, 0.8, 0.6, 0.6, 0.86, 0.72, 0.7]          # the kit's balance

# SAMPLE's presets: name, set, {E values}, (ATK DEC SUS REL), (DIST CHORUS DELAY REVERB), mono, category letter.
# E0 SET, E1 TUNE, E2 BITS, E3 LOOP, E4 CUT, E5 WOW, E6 DRV, E7 AGE
PRESETS = [
    ("LOFI PIANO", "UPRIGHT", dict(cut=92, wow=22, age=40), (0, 127, 127, 62), (0, 0, 10, 30), 0, "K"),
    ("MALL EP", "FM PIANO", dict(cut=104, wow=40, age=18), (0, 127, 127, 70), (0, 40, 22, 44), 0, "K"),
    ("DREAM VIBES", "VIBES", dict(cut=100, wow=52, age=20), (2, 127, 127, 80), (0, 20, 30, 60), 0, "K"),
    ("SMOOTH SAX", "SAX", dict(cut=112, wow=12), (6, 127, 127, 48), (0, 0, 20, 40), 1, "L"),
    ("GLASS PAD", "GLASS", dict(wow=30, age=10), (64, 127, 127, 100), (0, 30, 30, 80), 0, "P"),
    ("KALIMBA", "KALIMBA", dict(wow=10), (0, 127, 127, 70), (0, 0, 20, 34), 0, "U"),
    ("MUSIC BOX", "GLOCK", dict(bits=40, cut=104, wow=26, age=34), (0, 127, 127, 74), (0, 0, 24, 50), 0, "U"),
    ("WORN TAPE", "UPRIGHT", dict(cut=70, wow=88, age=100, drv=20), (0, 127, 127, 64), (0, 0, 0, 34), 0, "K"),
    ("MARIMBA", "MARIMBA", dict(), (0, 127, 127, 60), (0, 0, 10, 24), 0, "U"),
    ("UPRIGHT", "UPRIGHT", dict(), (0, 127, 127, 70), (0, 0, 0, 20), 0, "K"),
    ("FM PIANO", "FM PIANO", dict(), (0, 127, 127, 70), (0, 0, 0, 20), 0, "K"),
    ("VIBES", "VIBES", dict(), (0, 127, 127, 80), (0, 0, 0, 30), 0, "U"),
    ("GLOCK", "GLOCK", dict(), (0, 127, 127, 74), (0, 0, 10, 30), 0, "U"),
    ("NIGHT SAX", "SAX", dict(cut=72, wow=40, age=52), (8, 127, 127, 60), (0, 0, 30, 60), 1, "L"),
    ("8BIT KEYS", "FM PIANO", dict(bits=80, cut=118), (0, 127, 127, 50), (0, 0, 16, 20), 0, "K"),
    ("VHS VIBES", "VIBES", dict(cut=78, wow=96, age=72), (10, 127, 127, 90), (0, 30, 20, 70), 0, "P"),
]


# ---------------------------------------------------------------- DSP helpers
def t_axis(dur):
    return np.arange(int(dur * SR)) / SR


def lp(x, f, order=2):
    return signal.sosfilt(signal.butter(order, min(f, SR * 0.45), fs=SR, output="sos"), x)


def hp(x, f, order=2):
    return signal.sosfilt(signal.butter(order, f, btype="high", fs=SR, output="sos"), x)


def bp(x, lo, hi, order=2):
    return signal.sosfilt(signal.butter(order, [lo, min(hi, SR * 0.45)], btype="band", fs=SR, output="sos"), x)


def shelf_lo(x, f, db):
    """a low shelf: the band under f raised by db (a first-order split)"""
    lo = lp(x, f, 1)
    return x + lo * (10 ** (db / 20) - 1)


def norm(x, peak_db=-1.0):
    pk = np.abs(x).max() or 1
    return x / pk * 10 ** (peak_db / 20)


def fit(x, dur):
    n = int(dur * SR)
    return x[:n] if len(x) >= n else np.concatenate([x, np.zeros(n - len(x))])


def fade_end(x, dur):
    f = min(len(x), int(dur * SR))
    if f > 1:
        x = x.copy()
        x[-f:] *= np.cos(np.linspace(0, np.pi / 2, f)) ** 2
    return x


def start(x, rel=0.03):
    """the hit from its onset (a millisecond before), the leading silence cut"""
    pk = np.abs(x).max() or 1
    i = int(np.argmax(np.abs(x) > pk * rel))
    return x[max(0, i - SR // 1000):]


def tape(x, drive=1.4):
    """tape: a soft saturation, the head bump, the top rolled off"""
    y = np.tanh(drive * x / (np.abs(x).max() or 1)) / np.tanh(drive)
    return lp(shelf_lo(y, 110, 2.5), 11000, 1)


def varispeed(x, semis):
    """played faster or slower (the pitch and the length together), as a sampler's tune"""
    if not semis:
        return x
    r = 2 ** (semis / 12)
    n = int(len(x) / r)
    return np.interp(np.arange(n) * r, np.arange(len(x)), x)


def crush(x, rate, bits, hold_out=True):
    """sampled again at `rate` without a filter (its aliasing), `bits` deep, held back up to SR"""
    pos = np.arange(int(len(x) * rate / SR)) * SR / rate
    y = x[np.minimum(pos.astype(int), len(x) - 1)]
    q = 2 ** (bits - 1)
    y = np.round(y / (np.abs(y).max() or 1) * (q - 1)) / (q - 1)
    if not hold_out:
        return y
    idx = np.minimum((np.arange(len(x)) * rate / SR).astype(int), len(y) - 1)
    return y[idx]


def sp1200(x, semis=-1):
    y = crush(varispeed(x, semis), 26040, 12)
    return lp(y, 9500, 2)


def mulaw8(x, rate=28000):
    """an early drum machine's store: 8-bit mu-law at `rate`"""
    x = x / (np.abs(x).max() or 1)
    pos = np.arange(int(len(x) * rate / SR)) * SR / rate
    y = np.interp(pos, np.arange(len(x)), x)
    mu = 255.0
    c = np.sign(y) * np.log1p(mu * np.abs(y)) / np.log1p(mu)
    c = np.round(c * 127) / 127
    y = np.sign(c) * ((1 + mu) ** np.abs(c) - 1) / mu
    out = np.interp(np.arange(len(x)) * rate / SR, np.arange(len(y)), y)
    return lp(out, rate * 0.42, 4)


def room(dur, decay, seed, bright=6000):
    rng = np.random.default_rng(seed)
    t = t_axis(dur)
    ir = rng.standard_normal(len(t)) * np.exp(-t / decay)
    ir = lp(ir, bright, 1)
    ir[0] = 0
    return ir / np.sqrt(np.sum(ir ** 2))


def verb(x, wet, dur, decay, seed, bright=6000):
    w = signal.fftconvolve(x, room(dur, decay, seed, bright))[:len(x) + int(dur * SR)]
    y = np.concatenate([x, np.zeros(len(w) - len(x))])
    return y + wet * w * (np.abs(x).max() / (np.abs(w).max() or 1))


def gate(x, t_open, fade=0.012):
    n, f = int(t_open * SR), int(fade * SR)
    x = x[:n + f].copy()
    x[n:] *= np.linspace(1, 0, len(x) - n)
    return x


def echo(x, delay, fb, taps=5, cut=3500):
    """the SNES's echo: a delay fed back through a low-pass"""
    d = int(delay * SR)
    y = np.concatenate([x, np.zeros(d * taps)])
    e = np.zeros_like(y)
    e[:len(x)] = x
    out = y.copy()
    g = fb
    for k in range(1, taps + 1):
        e = lp(np.roll(e, d), cut, 1)
        e[:d * k] = 0
        out += e * g
        g *= fb
    return out


def brr(x, rate=11025):
    """the SNES's BRR: blocks of 16 samples, 4 bits each under the block's own shift, at `rate`; played back
    through the Gaussian interpolation (a soft low-pass)"""
    pos = np.arange(int(len(x) * rate / SR)) * SR / rate
    y = np.interp(pos, np.arange(len(x)), lp(x, rate * 0.45, 4))
    y = y / (np.abs(y).max() or 1) * 0.99
    out = np.zeros_like(y)
    for i in range(0, len(y), 16):
        b = y[i:i + 16]
        m = np.abs(b).max()
        sh = max(1, int(np.ceil(np.log2(m * 32768 / 7)))) if m > 0 else 1
        out[i:i + 16] = np.clip(np.round(b * 32768 / 2 ** sh), -8, 7) * 2 ** sh / 32768
    up = np.interp(np.arange(int(len(out) * SR / rate)) * rate / SR, np.arange(len(out)), out)
    g = signal.windows.gaussian(int(SR / rate * 4) | 1, SR / rate * 0.6)
    return np.convolve(up, g / g.sum(), "same")


# ---------------------------------------------------------------- the 808
def sq(f, t):
    return np.sign(np.sin(2 * np.pi * f * t))


def k808_kick():
    t = t_axis(1.3)
    f = 49 + 80 * np.exp(-t / 0.02)
    ph = 2 * np.pi * np.cumsum(f) / SR
    x = np.sin(ph) * np.exp(-t / 0.55)
    x[: int(0.002 * SR)] += np.linspace(0.6, 0, int(0.002 * SR))
    return np.tanh(1.6 * x)


def k808_snare(rng):
    t = t_axis(0.6)
    body = np.sin(2 * np.pi * 182 * t) * np.exp(-t / 0.07) + 0.6 * np.sin(2 * np.pi * 334 * t) * np.exp(-t / 0.045)
    sn = hp(rng.standard_normal(len(t)), 1800) * np.exp(-t / 0.11)
    return 0.7 * body + 0.6 * sn


def k808_clap(rng):
    t = t_axis(0.7)
    n = bp(rng.standard_normal(len(t)), 850, 2400)
    env = np.zeros_like(t)
    for k, o in enumerate((0.0, 0.011, 0.022, 0.031)):
        m = t >= o
        env[m] += np.exp(-(t[m] - o) / (0.004 if k < 3 else 0.14)) * (1 if k < 3 else 0.9)
    return n * env


METAL = [205.3, 304.4, 369.6, 522.7, 540.0, 800.0]


def k808_hat(open_):
    t = t_axis(0.9 if open_ else 0.25)
    m = sum(sq(f, t) for f in METAL)
    m = hp(bp(m, 6800, 11500, 2), 7000)
    return m * np.exp(-t / (0.26 if open_ else 0.022))


def k808_tom(rng):
    t = t_axis(0.9)
    f = 118 * (1 + 0.35 * np.exp(-t / 0.04))
    x = np.sin(2 * np.pi * np.cumsum(f) / SR) * np.exp(-t / 0.24)
    return x + 0.04 * lp(rng.standard_normal(len(t)), 3000) * np.exp(-t / 0.02)


def k808_rim(rng):
    t = t_axis(0.2)
    x = np.sin(2 * np.pi * 1690 * t) * np.exp(-t / 0.009) + 0.6 * np.sin(2 * np.pi * 480 * t) * np.exp(-t / 0.006)
    return x + 0.3 * hp(rng.standard_normal(len(t)), 2500) * np.exp(-t / 0.003)


def k808_cowbell():
    t = t_axis(0.5)
    x = bp(sq(540, t) + sq(800, t), 420, 2600)
    return x * (0.6 * np.exp(-t / 0.05) + 0.4 * np.exp(-t / 0.28))


# ---------------------------------------------------------------- the 2A03 (NES)
CPU = 1789773
FRAME = SR // 60                  # an envelope step (the frame counter's quarter frames are finer: 240 Hz)
NES_NOISE = [4, 8, 16, 32, 64, 96, 128, 160, 202, 254, 380, 508, 762, 1016, 2034, 4068]


def lfsr(n_out, clock, short=False, bits=15, seed=1):
    """an LFSR noise channel clocked at `clock` Hz, as heard at SR (held between clocks): 0/1"""
    n_clk = min(int(n_out * clock / SR) + 2, 4_000_000)
    reg, out = seed, np.empty(n_clk, dtype=np.int8)
    tap = 6 if short else 1                       # the 2A03's short mode: bit 0 ^ bit 6 (93 steps, metallic)
    for i in range(n_clk):
        fb = (reg ^ (reg >> tap)) & 1
        reg = (reg >> 1) | (fb << 14)
        if bits == 7:                             # the DMG's 7-bit width: the feedback into bit 6 too
            reg = (reg & ~0x40) | (fb << 6)
        out[i] = 1 - (reg & 1)
    idx = np.minimum((np.arange(n_out) * clock / SR).astype(int), n_clk - 1)
    return out[idx].astype(float)


def mix_in(dst, src, g=1.0):
    """src added onto the start of dst (as long as both reach)"""
    n = min(len(dst), len(src))
    dst[:n] += src[:n] * g
    return dst


def steps(levels, step=FRAME):
    """a 4-bit volume envelope, a level per step (0..15) -> per sample"""
    return np.repeat(np.asarray(levels, float) / 15.0, step)


def nes_tri(freqs, step=FRAME):
    """the triangle: 32 steps of 4 bits, its frequency per step (no volume: on or off)"""
    f = np.repeat(np.asarray(freqs, float), step)
    ph = np.cumsum(f) / SR
    k = (ph * 32).astype(int) % 32
    lev = np.where(k < 16, 15 - k, k - 16)
    return (lev / 7.5 - 1.0) * (f > 0)


def nes_pulse(freq, duty, vol):
    v = steps(vol, FRAME // 2)
    t = np.arange(len(v)) / SR
    return ((t * freq) % 1.0 < duty).astype(float) * 2 - 1, v


def nes_noise(period_i, vol, short=False, step=FRAME // 2):
    v = steps(vol, step)
    clock = CPU / NES_NOISE[period_i]
    return (lfsr(len(v), clock, short) * 2 - 1) * v


def nes_out(x):
    return lp(hp(hp(x, 90, 1), 440, 1) * 0.6 + hp(x, 90, 1) * 0.4, 14000, 1)


def nes_kit():
    f_drop = [220 * (55 / 220) ** (k / 5) for k in range(6)] + [55] * 6
    bd = nes_tri(f_drop, FRAME // 2)
    mix_in(bd, nes_noise(9, [15], step=FRAME // 3), 0.5)
    sn = nes_noise(6, [15, 13, 11, 9, 8, 7, 6, 5, 4, 3, 2, 2, 1, 1])
    mix_in(sn, nes_tri([190, 150, 120], FRAME // 2), 0.7)
    cp = nes_noise(5, [15, 4, 14, 4, 13, 9, 7, 6, 5, 4, 3, 2, 2, 1, 1, 1], step=FRAME // 3)
    ch = nes_noise(1, [12, 7, 3, 1], short=True, step=FRAME // 4)
    oh = nes_noise(1, [12, 12, 11, 10, 9, 8, 8, 7, 6, 6, 5, 4, 4, 3, 3, 2, 2, 1, 1, 1])
    tm = nes_tri([200 * (95 / 200) ** (k / 9) for k in range(10)] + [95] * 8, FRAME // 2)
    p, v = nes_pulse(1320, 0.125, [15, 10, 5])
    rs = p * v
    mix_in(rs, nes_noise(3, [12], step=FRAME // 4), 0.5)
    p1, v1 = nes_pulse(587, 0.25, [14, 13, 11, 10, 8, 7, 6, 5, 4, 3, 2, 2, 1, 1])
    p2, _ = nes_pulse(880, 0.25, [14] * 14)
    n = min(len(p1), len(p2), len(v1))
    cb = (p1[:n] + p2[:n] * 0.8) * v1[:n]
    return [nes_out(x) for x in (bd, sn, cp, ch, oh, tm, rs, cb)]


# ---------------------------------------------------------------- the DMG (Game Boy)
GB_ENV = SR // 64


def gb_noise(r, s, vol, width7=False, step=GB_ENV):
    v = steps(vol, step)
    div = 0.5 if r == 0 else r
    clock = 524288 / div / 2 ** (s + 1)
    return (lfsr(len(v), clock, bits=7 if width7 else 15) * 2 - 1) * v


def gb_wave(freqs, step, table=None):
    tbl = np.array(table if table is not None else
                   [8, 10, 12, 13, 14, 15, 15, 15, 14, 13, 12, 10, 8, 6, 4, 3, 2, 1, 0, 0, 0, 1, 2, 3, 4, 6, 7, 8, 8, 8, 8, 8])
    f = np.repeat(np.asarray(freqs, float), step)
    ph = np.cumsum(f) / SR
    return (tbl[(ph * 32).astype(int) % 32] / 7.5 - 1.0)


def gb_pulse(freqs, duty, vol, step=GB_ENV):
    v = steps(vol, step)
    f = np.repeat(np.asarray(freqs, float), step)[: len(v)]
    f = np.concatenate([f, np.full(len(v) - len(f), f[-1])])
    ph = np.cumsum(f) / SR
    return ((ph % 1.0) < duty).astype(float) * 2 - 1, v


def gb_out(x):
    return lp(hp(x, 60, 1), 12000, 1)


def gb_kit():
    st = SR // 128
    bd = gb_wave([170 * (42 / 170) ** (k / 14) for k in range(15)] + [42] * 20, st)
    bd *= np.repeat(np.linspace(1, 0.0, 35) ** 0.6, st)[: len(bd)]
    sn = gb_noise(0, 3, list(range(15, 0, -1)))
    mix_in(sn, gb_wave([210, 180, 150, 120], st), 0.6)
    cp = gb_noise(1, 4, [15, 3, 14, 3, 13, 10, 8, 6, 5, 4, 3, 2, 1], step=GB_ENV // 2)
    ch = gb_noise(0, 1, [10, 6, 3, 1], width7=True, step=GB_ENV // 2)
    oh = gb_noise(0, 1, [10, 10, 9, 9, 8, 8, 7, 7, 6, 6, 5, 5, 4, 4, 3, 3, 2, 2, 1, 1], step=GB_ENV * 3 // 2)
    p, v = gb_pulse([220 * (110 / 220) ** (k / 12) for k in range(13)], 0.5, list(range(15, 0, -1)), step=GB_ENV)
    tm = p * v
    p, v = gb_pulse([1500], 0.125, [15, 8, 3], step=GB_ENV // 2)
    rs = p * v
    p1, v1 = gb_pulse([540], 0.5, [13, 12, 10, 9, 7, 6, 5, 4, 3, 2, 1])
    p2, _ = gb_pulse([800], 0.5, [13] * 11)
    n = min(len(p1), len(p2), len(v1))
    cb = (p1[:n] + p2[:n]) * 0.5 * v1[:n]
    return [gb_out(x) for x in (bd, sn, cp, ch, oh, tm, rs, cb)]


# ---------------------------------------------------------------- the kits
def vcsl(root, key):
    sr, x = read_any_wav(Path(root) / V[key])
    x = np.asarray(x, float)
    if sr != SR:
        x = signal.resample_poly(x, SR, sr)
    return start(x - np.mean(x))


def kits(root):
    """name -> (8 hits at SR, caps or None)"""
    rng = np.random.default_rng(0x5BC)
    k = {}
    s = lambda key: vcsl(root, key)  # noqa: E731
    # CRATE: SP-1200, a semitone down, on tape
    k["CRATE"] = ([tape(sp1200(s(key), semis), 1.6) for key, semis in
                   (("kick", -2), ("snare", -1), ("clap", -1), ("hat", 0), ("hat_open", -1), ("tom", -2), ("side", -1),
                    ("tamb", 0))], None)
    # BEDROOM: soft hits on a worn cassette
    k["BEDROOM"] = ([tape(lp(varispeed(s(key), semis), 5200, 2), 2.2) for key, semis in
                     (("kick_soft", -2), ("rope", -2), ("clap_solo", -1), ("hat_pedal", 0), ("cymbal", -3),
                      ("tom_mallet", -3), ("snare_stick", -2), ("cowbell", -5))],
                    [0.9, 0.5, 0.5, 0.2, 1.0, 0.8, 0.25, 0.35])
    # VAPOR 808
    k["VAPOR 808"] = ([tape(x, 1.2) for x in (k808_kick(), k808_snare(rng), k808_clap(rng), k808_hat(False),
                                              k808_hat(True), k808_tom(rng), k808_rim(rng), k808_cowbell())],
                      [1.1, 0.45, 0.55, 0.18, 0.8, 0.8, 0.2, 0.5])
    # LINN: 8-bit mu-law, tight, a small room
    k["LINN"] = ([verb(mulaw8(hp(s(key), 30) * (1.0 if key != "kick" else 1.2)), 0.12, 0.25, 0.05, 3)
                  for key in ("kick", "snare", "clap", "hat", "hat_open", "tom", "side", "cowbell")],
                 [0.5, 0.45, 0.45, 0.14, 0.6, 0.6, 0.2, 0.35])
    # GATED: the big room, gated
    kick = s("kick")
    k["GATED"] = ([gate(verb(kick, 0.3, 0.6, 0.18, 11), 0.28),
                   gate(verb(s("snare"), 1.1, 1.2, 0.35, 12, 7000), 0.34),
                   gate(verb(s("clap"), 0.9, 1.0, 0.3, 13, 6500), 0.28),
                   s("hat"), s("hat_open"),
                   gate(verb(varispeed(s("tom_mallet"), -3), 1.0, 1.2, 0.35, 14, 5000), 0.42),
                   verb(s("side"), 0.4, 0.4, 0.1, 15), s("tamb")],
                  [0.33, 0.37, 0.31, 0.16, 0.7, 0.45, 0.3, 0.4])
    k["NES"] = (nes_kit(), [0.25, 0.3, 0.4, 0.08, 0.45, 0.4, 0.08, 0.3])
    k["GAMEBOY"] = (gb_kit(), [0.3, 0.3, 0.3, 0.06, 0.5, 0.25, 0.06, 0.2])
    # SNES: BRR at 11 kHz, the echo
    k["SNES"] = ([echo(brr(s(key)), 0.112, 0.32 if key not in ("kick", "hat") else 0.15) for key in
                  ("kick", "snare", "clap", "hat", "hat_open", "tom", "side", "cowbell")],
                 [0.7, 0.75, 0.7, 0.2, 0.85, 0.85, 0.4, 0.5])
    return k


# ---------------------------------------------------------------- the instruments
NOTE = {"C": 0, "C#": 1, "D": 2, "D#": 3, "E": 4, "F": 5, "F#": 6, "G": 7, "G#": 8, "A": 9, "A#": 10, "B": 11}


def name_midi(stem):
    """the note in a VCSL file name (C3 = MIDI 60 in most of the library; the octave is checked by ear below)"""
    m = re.search(r"(?<![A-Za-z])([A-G]#?)(-?\d)(?!\d)", stem)
    return 12 * (int(m.group(2)) + 2) + NOTE[m.group(1)] if m else None


def find_loop(x, f0, a, b):
    """a loop from about a to b (s) a whole number of periods long, where the waveform before its end matches the
    one before its start best; its end crossfaded into the start -> (x cut after the loop, ls, le)"""
    P = SR / f0
    ls = int(a * SR)
    m = max(1, round((b - a) * SR / P))
    W = int(max(3 * P, 256))
    ref = x[ls - W:ls]
    best, le = -2.0, ls + int(m * P)
    for c in range(ls + int(m * P) - int(P), ls + int(m * P) + int(P) + 1):
        if c + 1 > len(x):
            break
        seg = x[c - W + 1:c + 1]
        r = float(np.dot(seg, ref) / (np.linalg.norm(seg) * np.linalg.norm(ref) + 1e-12))
        if r > best:
            best, le = r, c
    X = min(int(0.08 * SR), le - ls - 1, ls)
    y = x[:le + 1].copy()
    w = np.linspace(0, 1, X)
    y[le - X + 1:le + 1] = y[le - X + 1:le + 1] * (1 - w) + x[ls - X:ls] * w
    return y, ls, le, best


def insts(root):
    """name -> (kind, [(x at SR, root16, ls, le)])"""
    out = {}
    for name, folder, filt, kind, keep, region in INSTS:
        files = sorted(p for p in (Path(root) / folder).glob("*.wav") if re.search(filt, p.name))
        got, seen = [], set()
        for p in files:
            nm = name_midi(p.stem)
            if nm is None or nm in seen:
                continue
            seen.add(nm)
            sr, x = read_any_wav(p)
            x = np.asarray(x, float)
            if sr != SR:
                x = signal.resample_poly(x, SR, sr)
            x = start(x - np.mean(x), 0.02)
            hz = detect_hz(list(x[: int(0.8 * SR)]), SR) if len(x) > SR // 4 else 0
            got.append((nm, hz, x))
        offs = [round((hz_to_midi(hz) - nm) / 12) * 12 for nm, hz, _ in got if hz]
        off = int(np.median(offs)) if offs else 0
        zones = []
        for nm, hz, x in got:
            midi = nm + off
            cents = 0.0
            if hz:
                d = hz_to_midi(hz) - midi
                cents = d if abs(d) < 0.5 else 0.0
            f0 = 440 * 2 ** ((midi + cents - 69) / 12)
            if kind == "loop":
                x = fit(x, keep + 0.2)
                y, ls, le, q = find_loop(x, f0, *region)
                zones.append((y, int(round((midi + cents) * 16)), ls, le))
            else:
                zones.append((fade_end(fit(x, keep), min(0.4, keep * 0.3)), int(round((midi + cents) * 16)), 0, 0))
        g = max(np.abs(z[0]).max() for z in zones) or 1
        out[name] = (kind, sorted(((z[0] / g * 0.89, *z[1:]) for z in zones), key=lambda z: z[1]))
        print(f"  {name}: {len(zones)} zones, octave {off:+d}, roots " +
              " ".join(str(z[1] // 16) for z in out[name][1]))
    return out


# ---------------------------------------------------------------- files
def to_tr(x):
    return signal.resample_poly(x, 1, 2)


def write_wav(path, x):
    path.parent.mkdir(parents=True, exist_ok=True)
    y = np.clip(np.round(x * 32767), -32768, 32767).astype("<i2")
    with wave.open(str(path), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(TR)
        w.writeframes(y.tobytes())


def read_wav16(path):
    with wave.open(str(path), "rb") as w:
        return np.frombuffer(w.readframes(w.getnframes()), "<i2").astype(int).tolist()


def prepare(root):
    man = {"kits": [], "insts": []}
    for name, (hits, caps) in kits(root).items():
        rows = []
        for i, x in enumerate(hits):
            cap = (caps or ROW_CAP)[i]
            x = fade_end(fit(x - np.mean(x), cap), min(0.1, cap * 0.35))
            y = norm(to_tr(x), -1.0) * ROW_GAIN[i]
            f = f"kits/{name}/{i}-{ROWS[i]}.wav"
            write_wav(PREP / f, y)
            rows.append(f)
        man["kits"].append({"name": name, "rows": rows})
        print(f"  kit {name}")
    for name, (kind, zones) in insts(root).items():
        zl = []
        for k, (x, root16, ls, le) in enumerate(zones):
            f = f"insts/{name}/{k:02d}-{root16 // 16}.wav"
            y = to_tr(x)
            if kind == "loop":                          # the loop points at the stored rate, the loop's end the last
                ls2, le2 = ls // 2, le // 2
                y = y[:le2 + 1]
            else:
                ls2 = le2 = len(y) - 1
            write_wav(PREP / f, y)
            zl.append({"file": f, "root16": root16, "ls": ls2, "le": le2, "looped": kind == "loop"})
        man["insts"].append({"name": name, "kind": kind, "zones": zl})
    (PREP / "spk.json").write_text(json.dumps(man, indent=1) + "\n")
    (PREP / "SOURCES.txt").write_text(
        "Spelunkr Groovebox sounds (tools/gen_spk_samples.py --prepare).\n"
        "Recorded instruments and the acoustic kits: Versilian Community Sample Library (VCSL), Versilian Studios\n"
        "and contributors, CC0 1.0 (https://github.com/sgossner/VCSL), processed here (aged, crushed, looped).\n"
        "VAPOR 808, NES and GAMEBOY: synthesized by the script (GPL-3.0 like the rest of the source).\n")


# ---------------------------------------------------------------- the headers
def input_key():
    h = hashlib.sha256()
    for p in [Path(__file__), Path(__file__).with_name("sampleio.py"), PREP / "spk.json",
              *sorted(PREP.glob("*/*/*.wav"))]:
        h.update(str(p.name).encode() + b"\0" + p.read_bytes())
    return h.hexdigest()


def c_bytes(name, blob):
    L = [f"static const uint8_t {name}[{max(1, len(blob))}] = {{"]
    for i in range(0, len(blob), 40):
        L.append(",".join(map(str, blob[i:i + 40])) + ",")
    if not blob:
        L.append("0,")
    L.append("};")
    return L


def header_kits(man):
    data, hits = bytearray(), []
    for kit in man["kits"]:
        for f in kit["rows"]:
            q = read_wav16(PREP / f)
            enc, _ = ima_encode(q, 0)
            hits.append((len(data), len(q), f"{kit['name']} {Path(f).stem}"))
            data += enc
            if len(data) & 1:
                data.append(0)
    names = [k["name"] for k in man["kits"]]
    L = ["/* GENERATED by tools/gen_spk_samples.py: do not edit. The KIT engine's sampled sets (eng_cycle.c: a lane's",
         " * MODEL past BLOOP plays set MODEL - CKM_N + 1), the GRID's eight rows each (BD SD CP CH OH TM RS CB) */",
         "#ifndef SPK_KITS_H", "#define SPK_KITS_H 1",
         f"#define FK_NSET {len(names)}", f"#define FK_RATE {TR}u",
         "static const char *const FK_SET_NAME[FK_NSET] = {" + ", ".join(f'"{n}"' for n in names) + "};",
         "#define FK_SET_NAMES_INIT " + ", ".join(f'"{n}"' for n in names),
         "static const int16_t FK_STEP[89] = {" + ", ".join(map(str, IMA_STEP)) + "};",
         "typedef struct { uint32_t off, n; } fk_hit_t;   /* off: bytes into FK_DATA (two samples a byte, low nibble first) */",
         "static const fk_hit_t FK_HIT[FK_NSET * 8] = {"]
    for off, n, nm in hits:
        L.append(f"    {{{off}u, {n}u}},   /* {nm}: {n / TR:.3f} s */")
    L.append("};")
    L += c_bytes("FK_DATA", data)
    L.append("#endif")
    return L, len(data)


def header_samples(man):
    blob, zones, sets = bytearray(), [], []
    rate = int(round(TR / 44100 * 65536))
    for ins in man["insts"]:
        z0 = len(zones)
        ents = []
        for z in ins["zones"]:
            q = read_wav16(PREP / z["file"])
            ls = z["ls"] if z["looped"] else len(q)
            enc, st = ima_encode(q, ls)
            off = len(blob)
            blob += enc
            if len(blob) & 1:
                blob.append(0)
            ents.append(dict(off=off, n=len(q), ls=z["ls"] if z["looped"] else len(q),
                             le=z["le"] if z["looped"] else len(q), root16=z["root16"], pred=st[0], idx=st[1],
                             looped=z["looped"]))
        for e, (lo, hi) in zip(ents, key_split([e["root16"] // 16 for e in ents])):
            e["lo"], e["hi"] = lo, hi
        zones += ents
        sets.append((ins["name"], z0, len(ents)))
    sidx = {n: i for i, (n, _, _) in enumerate(sets)}
    L = ["/* GENERATED by tools/gen_spk_samples.py: do not edit. The SAMPLE engine's sets (VCSL, CC0, aged; eng_sample.c) */",
         "#pragma once", "#include <stdint.h>", ""]
    L += c_bytes("SMP_DATA", blob)
    L.append("static const smp_zone_t SMP_ZONES[] = {")
    for e in zones:
        L.append(f"    {{{e['off']}, {e['n']}, {e['ls']}, {e['le']}, {rate}, {e['root16']}, {e['pred']}, {e['idx']}, "
                 f"{e['lo']}, {e['hi']}, {1 if e['looped'] else 0}}},")
    L.append("};")
    L.append("static const smp_set_t SMP_SETS[] = {")
    L += [f'    {{"{n}", {z0}, {nz}}},' for n, z0, nz in sets]
    L.append("};")
    L.append(f"#define SMP_NSETS {len(sets)}")
    L.append(f"#define SMP_NPRESETS {len(PRESETS)}")
    L.append("static const preset_t SMP_PRESET_TABLE[] = {")
    for name, st, ev, env, fx, mono, _ in PRESETS:
        loop = 1 if any(i["kind"] == "loop" for i in man["insts"] if i["name"] == st) else 0
        e = [sidx[st], ev.get("tune", 0), ev.get("bits", 0), loop, ev.get("cut", 127), ev.get("wow", 0),
             ev.get("drv", 0), ev.get("age", 0)]
        L.append(f'    {{"{name}", {{{", ".join(map(str, e))}}}, {{{", ".join(map(str, env))}}}, 0, {mono}, '
                 f'FX({", ".join(map(str, fx))})}},')
    L.append("};")
    L.append('#define SMP_PRESET_CATS "' + "".join(p[6] for p in PRESETS) + '"   /* category.c: a letter a preset */')
    L.append("static const uint8_t SMP_SET_ORIG[] = {" + ", ".join(str(i) for i in range(len(sets))) + "};")
    L.append("#define SMP_NALIAS 0")
    names = ", ".join(f'"{SHOWN.get(n, n)}"' for n, _, _ in sets)   # (the card's value: 7 characters)
    L.append("#define SMP_SET_NAMES_INIT " + names)
    L.append("static const char *const SMP_SET_NAMES[] = {" + names + "};")
    L.append("#define SLC_BREAK_INIT {0}")
    L.append("#define SLC_PIANO_INIT {0}")
    return L, len(blob), len(zones), len(sets)


def main(out):
    out = Path(out)
    kout = out.with_name("spk_kits.h")
    man_p = PREP / "spk.json"
    if not man_p.exists():
        raise SystemExit("spk samples: no assets/samples-spk (run gen_spk_samples.py --prepare VCSL_DIR)")
    key = input_key()
    try:
        ck, summary, a, b = CACHE.read_text().split("\n\x00\n")
        if ck == key:
            out.write_text(a)
            kout.write_text(b)
            print(summary + " (cached)")
            return
    except (OSError, ValueError):
        pass
    man = json.loads(man_p.read_text())
    ls, nb, nz, ns = header_samples(man)
    lk, kb = header_kits(man)
    a, b = "\n".join(ls) + "\n", "\n".join(lk) + "\n"
    out.write_text(a)
    kout.write_text(b)
    summary = (f"spk samples: SAMPLE {ns} sets, {nz} zones, {nb} B; KIT {len(man['kits'])} sets, {kb} B "
               f"(IMA ADPCM, {TR} Hz)")
    CACHE.parent.mkdir(parents=True, exist_ok=True)
    CACHE.write_text("\n\x00\n".join([key, summary, a, b]))
    print(summary)


if __name__ == "__main__":
    if sys.argv[1:2] == ["--prepare"] and len(sys.argv) == 3:
        prepare(sys.argv[2])
    elif len(sys.argv) == 2:
        main(sys.argv[1])
    else:
        sys.exit(__doc__)
