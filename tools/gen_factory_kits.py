#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Phantom Cosmonaut M8 (CAPTURE, spec 8): the factory sets DERELICT and SHORTWAVE, synthesized offline.

Rendered once (numpy + scipy), so the synthesis can be far heavier than the FM-1 could run live: struck and bowed
modal resonators, plucked strings, a dispersive spring, formant filters, shaped noise and a small synthetic room.
No 808/909 sine-sweep drums and no chip/FM tones (Ed, Nov 2026).

Each set: the GRID's eight kit rows (BD SD CP CH OH TM RS CB) and two grain tapes for GRANULAR.
Deterministic (fixed seeds). 44.1 kHz mono 16-bit WAV masters; the firmware will store them at 16 kHz.

  python3 tools/gen_factory_kits.py OUTDIR                          the 44.1 kHz WAV references
  python3 tools/gen_factory_kits.py --header firmware/src/factory_tables.h
        the firmware's tables: FK_TAPE, the 4 grain tapes for DEBRIS (SIG 17..20), 16384 samples each at 11.025 kHz
        (1.49 s), 8-bit, their ends crossfaded into their starts so a grain wraps without a click. Phantom Tracker
        0.9.7 (Ed: "scrapping our four sampled drum sets"): the sampled hits (FK_HIT / FK_DATA, 66 KB: the kit's SET
        DERELICT / SHORTWAVE, 0.9.2) are no longer written; --sets writes them as before (16 kHz IMA ADPCM)
"""
import sys
import wave
from pathlib import Path

import numpy as np
from scipy import signal

SR = 44100
RNG = np.random.default_rng(0x5EC7)


# ---------------------------------------------------------------- building blocks
def t_axis(dur):
    return np.arange(int(dur * SR)) / SR


def noise(dur, rng=None):
    return (rng or RNG).standard_normal(int(dur * SR))


def env_exp(dur, tau, attack=0.0005):
    t = t_axis(dur)
    a = np.minimum(1.0, t / attack) if attack > 0 else np.ones_like(t)
    return a * np.exp(-t / tau)


def bp(x, lo, hi, order=2):
    sos = signal.butter(order, [lo, hi], btype="band", fs=SR, output="sos")
    return signal.sosfilt(sos, x)


def lp(x, f, order=2):
    return signal.sosfilt(signal.butter(order, f, fs=SR, output="sos"), x)


def hp(x, f, order=2):
    return signal.sosfilt(signal.butter(order, f, btype="high", fs=SR, output="sos"), x)


def reson(x, f, q):
    """a two-pole resonator (constant peak gain), the body of a modal model"""
    w = 2 * np.pi * f / SR
    r = np.exp(-w / (2 * q))
    b = [1 - r, 0, 0]
    a = [1, -2 * r * np.cos(w), r * r]
    return signal.lfilter(b, a, x)


def modal(exc, modes):
    """exc through a bank of resonators: modes = [(freq, decay_s, gain)]"""
    out = np.zeros_like(exc)
    for f, dec, g in modes:
        if f >= SR / 2 * 0.95:
            continue
        q = np.pi * f * dec            # a mode decaying to 1/e in dec seconds
        out += g * reson(exc, f, q)
    return out


def mallet(dur, hardness):
    """a struck excitation: a short raised-cosine pulse; harder = shorter, brighter"""
    n = max(4, int(SR * (0.004 - 0.0035 * hardness)))
    x = np.zeros(int(dur * SR))
    x[:n] = 0.5 - 0.5 * np.cos(2 * np.pi * np.arange(n) / n)
    return x


def room(x, size=0.6, decay=0.5, mix=0.25, seed=1):
    """a small synthetic room: decaying filtered noise as an impulse response"""
    rng = np.random.default_rng(seed)
    n = int(SR * decay * 1.5)
    ir = rng.standard_normal(n) * np.exp(-np.arange(n) / (SR * decay / 6.9))
    ir = lp(ir, 2500 + 4000 * (1 - size))
    ir[: int(SR * 0.008 * size)] = 0             # pre-delay
    ir /= np.sqrt(np.sum(ir ** 2))
    x = x * np.minimum(1, np.linspace(len(x) / (SR * 0.25), 0, len(x)))   # (the source faded over its last 250 ms:
    dry = np.concatenate([x, np.zeros(n)])                                #  a ringing mode never cut off: no click)
    wet = fit(signal.fftconvolve(x, ir), len(dry) / SR)[: len(dry)]
    wet = np.concatenate([wet, np.zeros(len(dry) - len(wet))])
    return dry * (1 - mix) + wet * mix


def fit(x, dur):
    n = int(dur * SR)
    return x[:n] if len(x) >= n else np.concatenate([x, np.zeros(n - len(x))])


def fade(x, ms_in=0.5, ms_out=12):
    x = x.copy()
    a, b = int(SR * ms_in / 1000), int(SR * ms_out / 1000)
    if a:
        x[:a] *= np.linspace(0, 1, a)
    if b:
        x[-b:] *= np.linspace(1, 0, b)
    return x


def trim(x, floor_db=-66):
    """cut the tail where it stays under floor_db of its peak, then a short fade"""
    pk = np.abs(x).max() or 1
    above = np.where(np.abs(x) > pk * 10 ** (floor_db / 20))[0]
    end = min(len(x), (above[-1] if len(above) else len(x)) + int(0.02 * SR))
    return fade(x[:end], 0, 8)


def norm(x, peak_db=-1.0):
    pk = np.abs(x).max() or 1
    return x / pk * 10 ** (peak_db / 20)


def soft(x, drive):
    return np.tanh(x * drive) / np.tanh(drive)


# ---------------------------------------------------------------- DERELICT: the inside of a dead station
def d_hull():
    """a felt mallet on a huge steel bulkhead. The fundamental (58 Hz) is felt, not heard on a small speaker, so the
    strike's clang (the panel's mid modes), the upper partials and a little saturation carry it"""
    dur = 1.6
    t = t_axis(dur)
    exc = lp(mallet(dur, 0.25), 1500) + 0.03 * lp(noise(dur) * env_exp(dur, 0.006), 2500)
    f0 = 58.0
    ratios = [1.0, 1.06, 1.94, 2.31, 3.17, 3.84, 4.9, 6.3, 7.9, 9.6, 12.1]   # a plate's: close pairs that beat
    modes = [(f0 * r, 1.1 / (1 + 0.35 * i), 1.0 / (1 + 0.35 * i)) for i, r in enumerate(ratios)]
    body = modal(exc, modes)
    clang = modal(mallet(dur, 0.6), [(390, 0.07, 1.0), (735, 0.05, 0.8), (1160, 0.035, 0.6), (1720, 0.02, 0.4)])
    x = norm(body) + 0.45 * norm(clang)
    x = x * (1 + 0.12 * np.sin(2 * np.pi * 3.1 * t))     # the slow wobble of a big panel
    return room(soft(norm(x) * 1.6, 2.4), size=0.9, decay=1.0, mix=0.16)


def d_rivet():
    dur = 0.9
    exc = np.zeros(int(dur * SR))
    for i in range(7):                               # a rivet gun's burst, each impact quieter
        at = int(SR * (0.0 + i * 0.021 + RNG.uniform(-0.002, 0.002)))
        exc[at:at + 40] += (0.8 ** i) * RNG.standard_normal(40)
    plate = [(f, 0.12 + 0.2 * RNG.random(), 1 / (1 + 0.15 * i)) for i, f in
             enumerate([620, 893, 1310, 1677, 2240, 2987, 3610, 4420, 5390])]
    body = modal(hp(exc, 300), plate)
    rattle = bp(noise(dur), 1500, 6000) * env_exp(dur, 0.12) * (np.abs(np.sin(2 * np.pi * 37 * t_axis(dur))) ** 4)
    return room(soft(norm(body) + 0.35 * norm(rattle), 2.0), size=0.5, decay=0.6, mix=0.15)


def d_relay():
    dur = 0.25
    x = np.zeros(int(dur * SR))
    for at, g in ((0.0, 1.0), (0.011, 0.6), (0.047, 0.35)):      # pull in, contact bounce, release
        i = int(at * SR)
        x[i:i + 12] += g * RNG.standard_normal(12)
    clack = modal(x, [(2900, 0.012, 1.0), (4650, 0.008, 0.7), (7200, 0.005, 0.5), (1150, 0.02, 0.4)])
    thunk = lp(x, 250) * 3
    return soft(norm(clack) + 0.4 * norm(modal(thunk, [(180, 0.03, 1.0)])), 1.3)


def d_valve():
    dur = 1.1
    t = t_axis(dur)
    env = np.minimum(1, t / 0.015) * np.exp(-t / 0.35)
    env *= np.where(t < 0.62, 1.0, np.exp(-(t - 0.62) / 0.02))    # the valve shutting: choked
    hiss = hp(noise(dur), 2000) * env
    whistle = reson(noise(dur) * env, 3400 + 500 * np.exp(-t / 0.2).mean(), 40)  # a faint pitched edge
    pop = modal(mallet(dur, 0.8), [(820, 0.03, 1.0)])
    return soft(norm(hiss) + 0.25 * norm(whistle) + 0.3 * norm(pop), 1.2)


def d_pipe():
    dur = 1.0
    exc = mallet(dur, 0.7) + 0.05 * bp(noise(dur) * env_exp(dur, 0.004), 1000, 5000)
    f0 = 330.0
    modes = [(f0 * n * (1 + 0.004 * n * n), 0.45 / n ** 0.6, 1 / n) for n in (1, 2, 3, 4, 5, 6)]
    x = modal(exc, modes)
    return room(norm(x), size=0.6, decay=0.7, mix=0.2)


def d_cable():
    """a slack cable plucked: Karplus-Strong with its pitch sagging as it rings"""
    dur = 1.6
    n = int(dur * SR)
    f_start, f_end = 98.0, 84.0
    buf_len = int(SR / f_end) + 4
    rng = np.random.default_rng(7)
    line = lp(rng.standard_normal(buf_len), 3000)
    out = np.zeros(n)
    pos = 0.0
    hist = np.zeros(buf_len * 4)
    hist[:buf_len] = line
    w = buf_len
    for i in range(n):
        f = f_end + (f_start - f_end) * np.exp(-i / (SR * 0.25))
        d = SR / f
        r = w - d
        i0 = int(np.floor(r)); fr = r - i0
        y = hist[i0 % len(hist)] * (1 - fr) + hist[(i0 + 1) % len(hist)] * fr
        y1 = hist[(i0 - 1) % len(hist)] * (1 - fr) + hist[i0 % len(hist)] * fr
        v = 0.996 * 0.5 * (y + y1)
        hist[w % len(hist)] = v
        out[i] = v
        w += 1
    out = hp(out, 60)
    buzz = soft(out * 3, 2.5) * 0.15                                   # the cable slapping its anchor
    return room(norm(out + buzz), size=0.7, decay=0.9, mix=0.2)


def d_spring():
    """a spring tank kicked: each trip along the spring a falling chirp ("pew"), smeared wider every round trip
    (the spring's dispersion), the trips 33 ms apart"""
    dur = 1.4
    t = t_axis(dur)
    out = np.zeros_like(t)
    for k in range(12):
        t0 = 0.004 + k * 0.033
        tt = t - t0
        on = tt >= 0
        tau = 0.010 * (1 + 0.7 * k)
        f = 180 + 3600 * np.exp(-np.maximum(tt, 0) / tau)
        ph = np.cumsum(f * on) / SR
        env = on * np.exp(-np.maximum(tt, 0) / (0.035 * (1 + 0.5 * k))) * np.minimum(1, np.maximum(tt, 0) / 0.0008)
        out += (0.62 ** k) * (-1) ** k * np.sin(2 * np.pi * ph) * env
    kick = modal(mallet(dur, 0.9), [(95, 0.05, 1.0), (240, 0.03, 0.4)])   # the tank knocked
    x = bp(out, 150, 6000) + 0.5 * norm(kick)
    return room(norm(soft(x, 1.4)), size=0.4, decay=0.5, mix=0.12)


def d_servo():
    """a small motor: gear-tooth clicks speeding up, then stopping"""
    dur = 0.32
    t = t_axis(dur)
    rate = 90 + 900 * (t / 0.2) ** 1.5
    rate[t > 0.2] = 0
    ph = np.cumsum(rate) / SR
    clicks = np.diff(np.floor(ph), prepend=0) > 0
    exc = clicks.astype(float) * (0.6 + 0.4 * RNG.random(len(t)))
    body = modal(exc, [(1800, 0.01, 1.0), (3100, 0.006, 0.6), (650, 0.02, 0.5)])
    whine = bp(noise(dur), 900, 2600) * np.clip(rate / 1000, 0, 1) * 0.15
    stop = modal(mallet(dur, 0.9)[::-1][::-1] * (t > 0.2) * (t < 0.2006), [(420, 0.04, 1.0)])
    return norm(body + whine + 0.6 * norm(stop))


def d_groan():
    """bowed sheet metal: stick-slip friction (a jittered pulse train) through plate modes, slowly creaking"""
    dur = 3.0
    t = t_axis(dur)
    f_bow = 71 + 6 * np.sin(2 * np.pi * 0.35 * t) + 3 * np.sin(2 * np.pi * 1.7 * t)
    ph = np.cumsum(f_bow * (1 + 0.02 * lp(noise(dur), 30))) / SR
    slip = (np.diff(np.floor(ph), prepend=0) > 0).astype(float)
    exc = signal.lfilter([1], [1, -0.97], slip) * (0.6 + 0.4 * lp(np.abs(noise(dur)), 8))
    rng = np.random.default_rng(11)
    plate = [(f, 0.8 + rng.random(), 1 / (1 + 0.3 * i)) for i, f in
             enumerate(sorted(rng.uniform(70, 1800, 22)))]
    x = modal(hp(exc, 40), plate)
    env = np.minimum(1, t / 0.4) * np.minimum(1, (dur - t) / 0.5)
    return room(norm(x * env), size=0.9, decay=1.8, mix=0.25)[: int(dur * SR)]


def d_vent():
    dur = 3.0
    t = t_axis(dur)
    air = lp(noise(dur), 900) + 0.3 * bp(noise(dur), 1200, 4000)
    air *= 1 + 0.25 * np.sin(2 * np.pi * 0.6 * t) + 0.1 * lp(noise(dur), 3)
    hum = sum(g * np.sin(2 * np.pi * f * t + p) for f, g, p in ((60, 0.5, 0), (120, 0.35, 1), (180, 0.15, 2),
                                                                 (240, 0.08, 0.5)))
    rattle = bp(noise(dur), 300, 900) * (np.abs(np.sin(2 * np.pi * 12.5 * t)) ** 8) * 0.25
    x = norm(air) + 0.35 * norm(hum) + rattle
    return fade(norm(x), 300, 300)


# ---------------------------------------------------------------- SHORTWAVE: a radio left on in the dark
def s_sonar():
    """a deep muffled thump with a faint pinging echo: the thud (52 Hz) under a short woody knock that a small
    speaker can carry, saturated a little for harmonics; the ping's return louder than before"""
    dur = 1.3
    t = t_axis(dur)
    thump = lp(mallet(dur, 0.1), 200)
    body = modal(thump, [(52, 0.22, 1.0), (69, 0.10, 0.5), (104, 0.06, 0.3)])
    knock = modal(mallet(dur, 0.55), [(190, 0.035, 1.0), (430, 0.02, 0.6), (880, 0.012, 0.3)])
    hit = soft(norm(body) * 1.2 + 1.0 * norm(knock), 2.2)
    ping = np.sin(2 * np.pi * 1180 * t) * env_exp(dur, 0.09, 0.002)
    ping = np.concatenate([np.zeros(int(0.17 * SR)), ping])[: len(t)]    # its return
    return room(norm(hit) + 0.3 * ping, size=1.0, decay=1.0, mix=0.18)


def s_squelch():
    dur = 0.4
    t = t_axis(dur)
    crack = (RNG.random(len(t)) > 0.985) * RNG.standard_normal(len(t)) * 3
    st = bp(noise(dur) + crack, 350, 3200, 3)
    gate = np.where(t < 0.13, np.minimum(1, t / 0.002), 0.0)
    tail = bp(noise(dur), 900, 2600) * (t >= 0.13) * np.exp(-(t - 0.13) / 0.012)   # the squelch's close
    return soft(norm(st * gate) + 0.5 * norm(tail), 1.6)


def s_geiger():
    dur = 0.06
    x = np.zeros(int(dur * SR))
    x[0:6] = [1, -0.8, 0.6, -0.3, 0.15, -0.05]
    return norm(modal(x, [(2600, 0.004, 1.0), (6100, 0.002, 0.7), (900, 0.006, 0.3)]))


def s_tape():
    dur = 0.9
    t = t_axis(dur)
    env = np.sin(np.pi * np.clip(t / 0.75, 0, 1)) ** 1.5
    hiss = hp(noise(dur), 1500) * env
    drop = 1 - 0.85 * np.exp(-((t - 0.38) / 0.035) ** 2)                # a dropout
    dull = lp(hiss, 2500)
    mixw = np.exp(-((t - 0.38) / 0.05) ** 2)
    x = (hiss * (1 - mixw) + dull * mixw) * drop
    return fade(norm(x), 2, 20)


def s_chime():
    """a struck glass rod: a free bar's modes (1, 2.756, 5.404, 8.933), long and pure"""
    dur = 2.6
    f0 = 1046.5
    exc = mallet(dur, 0.85)
    x = modal(exc, [(f0, 1.8, 1.0), (f0 * 2.756, 0.9, 0.45), (f0 * 5.404, 0.4, 0.25), (f0 * 8.933, 0.2, 0.12)])
    return room(norm(x), size=0.8, decay=1.6, mix=0.3)


def s_ping():
    dur = 0.7
    t = t_axis(dur)
    f = 1500 - 400 * (1 - np.exp(-t / 0.03))
    x = np.sin(2 * np.pi * np.cumsum(f) / SR) * env_exp(dur, 0.06, 0.001)
    return room(norm(x), size=0.9, decay=0.9, mix=0.3)


def s_dial():
    dur = 0.3
    t = t_axis(dur)
    x = np.sin(2 * np.pi * 350 * t) + np.sin(2 * np.pi * 440 * t)
    x *= np.minimum(1, t / 0.004) * np.where(t < 0.16, 1, np.exp(-(t - 0.16) / 0.01))
    x = soft(x * 0.6, 2.2)                                                # a phone line's grit
    x = bp(x, 300, 3400, 3) + 0.03 * bp(noise(dur), 300, 3400)
    return fade(norm(x), 1, 15)


def s_tune():
    """tuning across a station: a heterodyne whistle sweeping down through static"""
    dur = 0.45
    t = t_axis(dur)
    f = 2800 * np.exp(-t / 0.12) + 380
    whistle = np.sin(2 * np.pi * np.cumsum(f) / SR)
    static = bp(noise(dur), 400, 4000)
    env = np.minimum(1, t / 0.01) * np.exp(-t / 0.18)
    voice = bp(noise(dur), 500, 1200) * (0.5 + 0.5 * np.sin(2 * np.pi * 7 * t))   # a station half-heard
    x = (0.6 * whistle + 0.5 * norm(static) + 0.3 * norm(voice) * np.exp(-((t - 0.22) / 0.05) ** 2)) * env
    return fade(norm(x), 1, 20)


def s_heterodyne():
    dur = 3.0
    t = t_axis(dur)
    drift = 640 + 220 * np.sin(2 * np.pi * 0.11 * t) + 25 * np.sin(2 * np.pi * 0.9 * t)
    a = np.sin(2 * np.pi * np.cumsum(drift) / SR)
    b = np.sin(2 * np.pi * np.cumsum(drift * 1.0 + 3.3) / SR)            # a second carrier beating against it
    fadein = 0.5 + 0.5 * np.sin(2 * np.pi * 0.27 * t)
    static = bp(noise(dur), 300, 3500) * 0.25
    x = (0.5 * a + 0.35 * b) * (0.4 + 0.6 * fadein) + static
    return fade(norm(soft(x, 1.3)), 300, 300)


def s_ionosphere():
    """a breathy, voice-like hum: noise and a low glottal pulse through moving vowel formants"""
    dur = 3.0
    t = t_axis(dur)
    f0 = 108 + 4 * np.sin(2 * np.pi * 0.4 * t)
    ph = np.cumsum(f0) / SR
    pulse = signal.lfilter([1], [1, -0.95], (np.diff(np.floor(ph), prepend=0) > 0).astype(float))
    src = 0.35 * norm(pulse) + norm(noise(dur))
    m = 0.5 + 0.5 * np.sin(2 * np.pi * 0.23 * t)                         # "oo" -> "ah" -> "oo"
    out = np.zeros_like(t)
    seg = 2048
    for s0 in range(0, len(t), seg):                                    # formants moved block by block
        mm = m[s0]
        f1, f2, f3 = 300 + 450 * mm, 870 + 350 * mm, 2240 + 200 * mm
        blk = src[max(0, s0 - 512): s0 + seg]
        y = 1.0 * reson(blk, f1, 9) + 0.6 * reson(blk, f2, 11) + 0.25 * reson(blk, f3, 14)
        out[s0: s0 + seg] = y[-len(out[s0: s0 + seg]):]
    swell = 0.3 + 0.7 * (0.5 + 0.5 * np.sin(2 * np.pi * 0.31 * t + 1.0)) ** 2
    x = out * swell + 0.15 * bp(noise(dur), 2000, 6000) * swell
    return fade(norm(room(x, size=1.0, decay=1.5, mix=0.3)[: len(t)]), 300, 300)


# the GRID's kit rows in order: BD SD CP CH OH TM RS CB (GM 36 38 39 42 46 45 37 56), then the two grain tapes
SETS = {
    "derelict": [("01-hull", d_hull), ("02-rivet", d_rivet), ("03-pipe", d_pipe), ("04-relay", d_relay),
                 ("05-valve", d_valve), ("06-cable", d_cable), ("07-spring", d_spring), ("08-servo", d_servo),
                 ("09-grain-groan", d_groan), ("10-grain-vent", d_vent)],
    "shortwave": [("01-sonar", s_sonar), ("02-squelch", s_squelch), ("03-chime", s_chime), ("04-geiger", s_geiger),
                  ("05-tape", s_tape), ("06-ping", s_ping), ("07-dial", s_dial), ("08-tune", s_tune),
                  ("09-grain-heterodyne", s_heterodyne), ("10-grain-ionosphere", s_ionosphere)],
}


def make(name, fn):
    """one sound, its noise seeded by its name: the references and the firmware's tables are the same sound"""
    global RNG
    RNG = np.random.default_rng(int.from_bytes(name.encode()[:8].ljust(8, b"_"), "little") ^ 0x5EC7)
    return fn()


def write(path, x):
    y = np.clip(np.round(x * 32767), -32768, 32767).astype("<i2")
    with wave.open(str(path), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes(y.tobytes())


HIT_RATE, TAPE_RATE, TAPE_N, TAPE_XF = 16000, 11025, 16384, 1024
# per row: the length kept (s) and the level against the others (the kit's balance: hats and percs under the drums)
FK_CAP = {
    "derelict": [0.75, 0.55, 0.6, 0.2, 0.62, 0.8, 0.6, 0.32],
    "shortwave": [0.6, 0.24, 0.9, 0.06, 0.7, 0.6, 0.3, 0.45],
}
FK_GAIN = [1.0, 0.92, 0.8, 0.62, 0.62, 0.86, 0.72, 0.72]
TAPE_NAMES = ["GROAN", "VENT", "HETERODYNE", "SKYWAVE"]   # (SKYWAVE: SHORTWAVE's IONOSPHERE tape; SIG 08 has the name)


def header(path, sets=False):
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    from sampleio import ima_encode, IMA_STEP
    data, hits, lines = bytearray(), [], []
    for s in (("derelict", "shortwave") if sets else ()):
        for i, (tag, fn) in enumerate(SETS[s][:8]):
            x = make(f"{s}-{tag}", fn)
            x = x - np.mean(x)
            x = norm(x, -1.0)
            cap = FK_CAP[s][i]
            n = int(cap * SR)
            x = x[:n] if len(x) >= n else np.concatenate([x, np.zeros(n - len(x))])
            f = int(min(0.12, cap * 0.35) * SR)                   # the end faded out
            x[-f:] *= np.linspace(1, 0, f) ** 2
            y = signal.resample_poly(x, 160, 441)               # 44.1 -> 16 kHz
            y = y / (np.abs(y).max() or 1) * 10 ** (-1 / 20) * FK_GAIN[i]
            q = [int(v) for v in np.clip(np.round(y * 32767), -32768, 32767)]
            adp, _ = ima_encode(q, 0)
            hits.append((len(data), len(q), f"{s.upper()} {tag}"))
            data += adp
    tapes = []
    for s in ("derelict", "shortwave"):
        for tag, fn in SETS[s][8:]:
            x = norm(make(f"{s}-{tag}", fn), -1.0)
            y = signal.resample_poly(x, 1, 4)                     # 44.1 -> 11.025 kHz, exactly
            a = int(0.45 * TAPE_RATE)                             # (past the fade-in)
            seg = y[a: a + TAPE_N + TAPE_XF].copy()
            w = np.linspace(0, 1, TAPE_XF)                        # the loop: the tail crossfaded into the head
            seg[:TAPE_XF] = seg[:TAPE_XF] * w + seg[TAPE_N: TAPE_N + TAPE_XF] * (1 - w)
            seg = seg[:TAPE_N]
            seg = seg / (np.abs(seg).max() or 1) * 0.97
            tapes.append(np.clip(np.round(seg * 127), -127, 127).astype(int))
    o = []
    o.append("/* SPDX-License-Identifier: GPL-3.0-only")
    if sets:
        o.append(" * GENERATED by tools/gen_factory_kits.py --header --sets: do not edit. Phantom Cosmonaut's factory sets DERELICT")
        o.append(" * and SHORTWAVE (M8, spec 8): the kit's SET (eng_cycle.c) and DEBRIS's tapes (eng_morph.c). */")
    else:
        o.append(" * GENERATED by tools/gen_factory_kits.py --header: do not edit. DEBRIS's four tapes, SIG 17..20 (eng_morph.c;")
        o.append(" * the sampled drum sets DERELICT and SHORTWAVE, 0.9.2, scrapped in Phantom Tracker 0.9.7: --sets writes them) */")
    o.append("#ifndef PC_FACTORY_TABLES_H\n#define PC_FACTORY_TABLES_H 1\n")
    if sets:
        o.append(f"#define FK_NSET 2                     /* the sampled sets: DERELICT, SHORTWAVE (SET 1, 2; 0 is the NOISE synth) */")
        o.append(f"#define FK_RATE {HIT_RATE}u                /* the hits' rate */")
    o.append(f"#define FK_NTAPE {len(tapes)}\n#define FK_TAPE_N {TAPE_N}u             /* a tape's samples (a power of 2: the pointer's top bits) */")
    o.append(f"#define FK_TAPE_RATE {TAPE_RATE}u")
    if sets:
        o.append('static const char *const FK_SET_NAME[FK_NSET + 1] = {"NOISE", "DERELICT", "SHORTWAVE"};')
    o.append("static const char *const FK_TAPE_NAME[FK_NTAPE] = {" + ", ".join(f'"{n}"' for n in TAPE_NAMES) + "};")
    if sets:
        o.append("static const int16_t FK_STEP[89] = {" + ", ".join(str(v) for v in IMA_STEP) + "};")
        o.append("typedef struct { uint32_t off, n; } fk_hit_t;   /* off: bytes into FK_DATA (two samples a byte, low nibble first) */")
        o.append("static const fk_hit_t FK_HIT[FK_NSET * 8] = {")
        for off, n, name in hits:
            o.append(f"    {{{off}u, {n}u}},   /* {name}: {n / HIT_RATE:.3f} s */")
        o.append("};")
        o.append(f"static const uint8_t FK_DATA[{len(data)}] = {{")
        for i in range(0, len(data), 24):
            o.append("    " + ", ".join(str(b) for b in data[i:i + 24]) + ",")
        o.append("};")
    o.append(f"static const int8_t FK_TAPE[FK_NTAPE][FK_TAPE_N] = {{")
    for k, tp in enumerate(tapes):
        o.append(f"    {{   /* SIG {17 + k} {TAPE_NAMES[k]} */")
        for i in range(0, TAPE_N, 32):
            o.append("        " + ", ".join(str(v) for v in tp[i:i + 32]) + ",")
        o.append("    },")
    o.append("};\n#endif")
    Path(path).write_text("\n".join(o) + "\n")
    print(f"{path}: hits {len(data)} B ({sum(h[1] for h in hits) / HIT_RATE:.2f} s), tapes {len(tapes) * TAPE_N} B")


def main():
    if len(sys.argv) > 2 and sys.argv[1] == "--header":
        header(sys.argv[2], "--sets" in sys.argv[3:])
        return
    out = Path(sys.argv[1] if len(sys.argv) > 1 else "build/factory/wav")
    out.mkdir(parents=True, exist_ok=True)
    for name, items in SETS.items():
        for tag, fn in items:
            x = make(f"{name}-{tag}", fn)
            x = x - np.mean(x)
            x = norm(trim(x) if "grain" not in tag else x, -1.0)
            write(out / f"{name}-{tag}.wav", x)
            print(f"  {name}-{tag}.wav  {len(x) / SR:.2f} s")


if __name__ == "__main__":
    main()
