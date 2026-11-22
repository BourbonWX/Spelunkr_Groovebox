#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Phantom Cosmonaut: DEBRIS, second pass (Ed: "less harsh ... most of that just sounds like different types of
static"). The same idea, folded grains, tuned for tone instead of texture:

  - the frames band-limited to the note (no harmonic above Nyquist) and the fold run 4x oversampled: no aliasing,
    which was most of the static in the first pass (8-bit level 0 read at every pitch, then folded up to x16)
  - grains in phase with the note (a wavetable read, not a random start), Hann windows, four overlapping: a smooth
    sum, no buzz; each grain a few cents off and panned its own way: a chorus, not a storm
  - FOLD's range soft: sin(pi/2 g x) with g 1 .. 4 (the first pass went to 16); FOLD 0 is the clean signal
  - TONE after the fold (a gentle low-pass), SHIMMER: some grains an octave up, quieter

  python3 tools/debris_soft.py OUTDIR
"""
import sys
import wave
from pathlib import Path

import numpy as np
from scipy import signal

sys.path.insert(0, str(Path(__file__).resolve().parent))
import debris_proto as d1  # noqa: E402  (the loaders)

SR = 44100
OS = 4
FS = SR * OS
ROOT = Path(__file__).resolve().parent.parent


def bandlimit(frames, f0):
    """each 256-sample frame keeps the harmonics under 18 kHz at f0"""
    nh = int(min(128, 18000 / f0))
    sp = np.fft.rfft(frames, axis=1)
    sp[:, nh + 1:] = 0
    sp[:, 0] = 0
    return np.fft.irfft(sp, 256, axis=1)


def adsr(n, a, dcy, s, r, gate):
    t = np.arange(n) / FS
    e = np.where(t < a, t / max(a, 1e-4), s + (1 - s) * np.exp(-(t - a) / max(dcy, 1e-4)))
    g = min(int(gate * FS), n - 1)
    return np.where(t >= gate, e[g] * np.exp(-(t - gate) / max(r, 1e-4)), e)


def K(**k):
    base = dict(scan=0.2, grain=0.12, fold=0.0, fenv=0.0, scatter=0.2, detune=5.0, spread=0.6, shimmer=0.0,
                tone=0.6, vib=0.0)
    base.update(k)
    return base


def voice(src, midi, gate, P, env=(0.01, 0.3, 0.8, 0.3), seed=0, tail=None):
    """one note, stereo (2, n) at 44.1 kHz. P(t) -> the knobs (fold, scan .. 0..1; grain in seconds)"""
    rng = np.random.default_rng(seed)
    a, dcy, s, r = env
    n = int((gate + (tail if tail is not None else r * 5)) * FS)
    e = adsr(n, a, dcy, s, r, gate)
    f0 = 440 * 2 ** ((midi - 69) / 12)
    tape = src["kind"] == "tape"
    if tape:
        data = src["data"]
        L = len(data)
        data = signal.sosfiltfilt(signal.butter(4, 3500, fs=11025, output="sos"), data)
    else:
        frames = bandlimit(src["data"].reshape(8, 256), f0)
        frames /= np.abs(frames).max()
    t = np.arange(n) / FS
    q0 = P(0.0)
    vib = 1 + q0["vib"] * 0.004 * np.sin(2 * np.pi * 5.2 * t) * np.clip(t / 0.6, 0, 1)
    if tape:
        phase = np.cumsum(np.full(n, (11025 / FS) * f0 / 261.63) * vib)
    else:
        phase = np.cumsum(np.full(n, f0 / FS) * vib)          # in cycles
    out = np.zeros((2, n))
    hop_n = 4
    tg = 0.0
    k = 0
    while tg < n / FS:
        q = P(tg)
        glen = q["grain"]
        gn = int(glen * FS)
        st = int(tg * FS)
        end = min(n, st + gn)
        m = end - st
        if m > 8:
            det = 2 ** (q["detune"] * (rng.random() * 2 - 1) / 1200)
            if rng.random() < q["shimmer"]:
                det *= 2
                gain = 0.45
            else:
                gain = 1.0
            ph = phase[st] + (phase[st:end] - phase[st]) * det
            if tape:
                start = (q["scan"] * 0.85 + q["scatter"] * 0.08 * (rng.random() - 0.5)) * L
                pos = start + ph - phase[st]
                i0 = np.floor(pos).astype(int)
                fr = pos - i0
                x = data[i0 % L] * (1 - fr) + data[(i0 + 1) % L] * fr
            else:
                fpos = np.clip(q["scan"] * 7 + q["scatter"] * 1.5 * rng.standard_normal(), 0, 7)
                fa = int(np.floor(fpos))
                fb = min(7, fa + 1)
                ff = fpos - fa
                tab = frames[fa] * (1 - ff) + frames[fb] * ff
                p = (ph % 1.0) * 256
                i0 = np.floor(p).astype(int)
                fr = p - i0
                x = tab[i0 % 256] * (1 - fr) + tab[(i0 + 1) % 256] * fr
            fd = np.clip(q["fold"] + q["fenv"] * e[st], 0, 1)
            fd = np.clip(fd * (1 + q["scatter"] * 0.25 * (rng.random() * 2 - 1)), 0, 1)
            g = 1 + 3 * fd
            wet = np.clip(fd * 3, 0, 1)
            y = (1 - wet) * x + wet * np.sin(np.pi / 2 * g * x) * 0.8
            w = np.hanning(gn)[:m]
            pan = 0.5 + q["spread"] * 0.5 * (rng.random() * 2 - 1)
            out[0, st:end] += y * w * gain * np.cos(pan * np.pi / 2)
            out[1, st:end] += y * w * gain * np.sin(pan * np.pi / 2)
        tg += glen / hop_n
        k += 1
    q = P(0.0)
    cut = 900 * 2 ** (q["tone"] * 4.2)                        # 0.9 .. 16 kHz
    sos = signal.butter(2, min(cut, 20000), fs=FS, output="sos")
    out = signal.sosfilt(sos, out * e, axis=1)
    return signal.resample_poly(out, 1, OS, axis=1) * 0.5


def mix(parts, n):
    out = np.zeros((2, n))
    for at, x, g in parts:
        s = int(at * SR)
        if s >= n:
            continue
        m = min(x.shape[-1], n - s)
        out[:, s:s + m] += (x[..., :m] if x.ndim == 2 else np.vstack([x[:m], x[:m]])) * g
    return out


def reverb(x, wet=0.28, decay=2.6):
    m = int(SR * decay)
    out = x * (1 - wet * 0.5)
    for ch, seed in ((0, 11), (1, 12)):
        rng = np.random.default_rng(seed)
        ir = rng.standard_normal(m) * np.exp(-np.arange(m) / (SR * decay / 6.9))
        ir = signal.sosfilt(signal.butter(2, 3200, fs=SR, output="sos"), ir)
        ir[: int(0.012 * SR)] *= np.linspace(0, 1, int(0.012 * SR))
        ir /= np.sqrt((ir ** 2).sum())
        out[ch] += signal.fftconvolve(x[ch], ir)[: x.shape[1]] * wet
    return out


def norm(x, db=-1.5):
    x = x - x.mean(axis=-1, keepdims=True)
    return x / (np.abs(x).max() or 1) * 10 ** (db / 20)


def fade(x, s=0.4):
    m = int(s * SR)
    x[..., -m:] *= np.linspace(1, 0, m) ** 2
    return x


def write(path, x):
    y = np.clip(np.round(x.T * 32767), -32768, 32767).astype("<i2")
    with wave.open(str(path), "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes(y.tobytes())


def main():
    out = Path(sys.argv[1] if len(sys.argv) > 1 else "build/debris-soft")
    out.mkdir(parents=True, exist_ok=True)
    S = {**d1.load_signals(), **d1.load_tapes()}
    ex = {}
    st16 = 60 / 120 / 4
    bar = 16 * st16
    Dm, Bb, C, Am = (50, 53, 57, 62), (46, 50, 53, 58), (48, 52, 55, 60), (45, 48, 52, 57)
    prog = (Dm, Bb, C, Am)

    # 1. glass pad: COLD START, the fold breathing slowly, grains drifting through the frames
    parts = []
    for b, ch in enumerate(prog * 2):
        for k, m_ in enumerate(ch):
            parts.append((b * bar, voice(S["COLD START"], m_, bar * 0.98, lambda t, b=b: K(
                scan=0.15 + 0.5 * ((b * bar + t) / (8 * bar)), grain=0.14, fold=0.22 + 0.1 * np.sin(2 * np.pi * 0.15 * (b * bar + t)),
                scatter=0.35, detune=7, spread=0.8, shimmer=0.12, tone=0.55), env=(0.5, 1, 1, 0.9), seed=b * 4 + k, tail=2.5), 0.7))
    ex["01-glass-pad"] = mix(parts, int(8 * bar * SR + 2.5 * SR))

    # 2. what FOLD does now: one held D on CARRIER, FOLD 0 -> 0.7 over 11 s (clean, warm, hollow, glassy)
    dur = 11.0
    ex["02-fold-as-colour"] = sum(voice(S["CARRIER"], m_, dur, lambda t: K(
        scan=0.3, grain=0.16, fold=0.7 * min(1, t / dur), scatter=0.15, detune=4, spread=0.5, tone=0.7),
        env=(0.6, 1, 1, 1.2), seed=k, tail=3) for k, m_ in enumerate((38, 50)))

    # 3. a round plucked bass: SPUTNIK, the fold opens on the strike and settles (FENV)
    line = [(0, 0), (3, 0), (6, 12), (8, 0), (10, 3), (12, 7), (14, 5)]
    roots = (38, 34, 36, 33)
    parts = []
    for b in range(4):
        for i, (s16, d_) in enumerate(line):
            parts.append((b * bar + s16 * st16, voice(S["SPUTNIK"], roots[b] + d_, st16 * 1.5, lambda t: K(
                scan=0.2, grain=0.09, fold=0.04, fenv=0.45, scatter=0.1, detune=3, spread=0.15, tone=0.45),
                env=(0.003, 0.22, 0.35, 0.15), seed=b * 10 + i, tail=0.6), 1.0))
    ex["03-round-pluck-bass"] = mix(parts, int(4 * bar * SR + SR))

    # 4. a music box: high, short, the fold on each strike turns a soft wave into a bell
    arp = [74, 77, 81, 86, 84, 81, 77, 79, 76, 72, 74, 69] * 3
    parts = []
    for i, m_ in enumerate(arp):
        parts.append((i * st16 * 2, voice(S["CARRIER"], m_, st16, lambda t: K(
            scan=0.1, grain=0.1, fold=0.05, fenv=0.6, scatter=0.2, detune=3, spread=0.9, tone=0.75),
            env=(0.002, 0.35, 0.0, 0.5), seed=i, tail=1.6), 0.55))
    ex["04-music-box"] = mix(parts, int(len(arp) * st16 * 2 * SR + 2 * SR))

    # 5. a slow drone: DARK SIDE, D and A, SCAN through the whole SIGNAL, long grains, a little shimmer
    dur = 13.0
    ex["05-slow-drone"] = sum(voice(S["DARK SIDE"], m_, dur, lambda t: K(
        scan=min(1, t / dur), grain=0.28, fold=0.18 + 0.12 * (t / dur), scatter=0.4, detune=9, spread=1.0,
        shimmer=0.2, tone=0.5), env=(2.0, 1, 1, 2.0), seed=k, tail=4) for k, m_ in enumerate((38, 45, 50, 57)))

    # 6. a lead with a halo: SOLAR WIND's soft frames, vibrato, some grains an octave up
    mel = [(0, 69, 3), (3, 72, 1), (4, 74, 4), (8, 72, 2), (10, 69, 2), (12, 67, 2), (14, 65, 2),
           (16, 64, 3), (19, 65, 1), (20, 67, 4), (24, 69, 8)]
    parts = []
    for i, (s16, m_, l) in enumerate(mel):
        parts.append((s16 * st16 * 2, voice(S["SOLAR WIND"], m_, l * st16 * 2 * 0.96, lambda t: K(
            scan=0.18, grain=0.12, fold=0.2, fenv=0.1, scatter=0.2, detune=5, spread=0.5, shimmer=0.25, tone=0.6,
            vib=1.0), env=(0.06, 0.5, 0.85, 0.35), seed=i, tail=1.5), 1.0))
    ex["06-halo-lead"] = mix(parts, int(32 * st16 * 2 * SR + 2 * SR))

    # 7. a ghost choir, softly: the SKYWAVE tape, long grains, dark, barely folded
    dur = 10.0
    ex["07-ghost-choir"] = sum(voice(S["SKYWAVE"], m_, dur, lambda t: K(
        scan=0.1 + 0.4 * (t / dur), grain=0.3, fold=0.08, scatter=0.3, detune=8, spread=0.9, tone=0.3),
        env=(1.5, 1, 1, 2.0), seed=k, tail=3.5) for k, m_ in enumerate((50, 57, 62, 65)))

    # 8. together: DERELICT's soft hits, the pluck bass, the glass pad, the music box
    n = int(8 * bar * SR + 2.5 * SR)
    hit = {}
    for nm in ("01-hull", "04-relay", "05-valve", "02-rivet"):
        w = wave.open(str(ROOT / f"build/factory/wav/derelict-{nm}.wav"))
        h = np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16) / 32768
        hit[nm] = signal.sosfilt(signal.butter(2, 6000, fs=SR, output="sos"), h)
    drums = []
    for b in range(8):
        b0 = b * bar
        for s16 in (0, 10):
            drums.append((b0 + s16 * st16, hit["01-hull"][: int(0.45 * SR)], 0.8))
        drums.append((b0 + 8 * st16, hit["02-rivet"], 0.35))
        for s16 in range(2, 16, 4):
            drums.append((b0 + s16 * st16, hit["04-relay"], 0.16))
        if b % 2:
            drums.append((b0 + 14 * st16, hit["05-valve"], 0.2))
    bass, pad, box = [], [], []
    for b in range(8):
        for i, (s16, d_) in enumerate(line):
            bass.append((b * bar + s16 * st16, voice(S["SPUTNIK"], roots[b % 4] + d_, st16 * 1.5, lambda t: K(
                scan=0.2, grain=0.09, fold=0.04, fenv=0.45, scatter=0.1, detune=3, spread=0.15, tone=0.45),
                env=(0.003, 0.22, 0.35, 0.15), seed=b * 10 + i, tail=0.6), 1.0))
        for k, m_ in enumerate(prog[b % 4]):
            pad.append((b * bar, voice(S["COLD START"], m_ + 12, bar * 0.98, lambda t: K(
                scan=0.3, grain=0.14, fold=0.2, scatter=0.35, detune=7, spread=0.9, shimmer=0.1, tone=0.5),
                env=(0.4, 1, 1, 0.8), seed=b * 5 + k, tail=2.0), 1.0))
        if b >= 4:
            for j, s16 in enumerate((0, 3, 6, 10, 13)):
                m_ = (prog[b % 4][j % 4] + 24)
                box.append((b * bar + s16 * st16, voice(S["CARRIER"], m_, st16, lambda t: K(
                    scan=0.1, grain=0.1, fold=0.05, fenv=0.6, scatter=0.2, detune=3, spread=0.9, tone=0.75),
                    env=(0.002, 0.3, 0.0, 0.45), seed=b * 7 + j, tail=1.4), 1.0))
    dr = mix(drums, n)
    ex["08-together"] = dr * 0.8 + norm(mix(bass, n)) * 0.5 + norm(mix(pad, n)) * 0.3 + norm(mix(box, n)) * 0.18

    for name, x in ex.items():
        y = norm(fade(reverb(x, 0.18 if name.startswith("08") else 0.3)))
        write(out / f"debris-soft-{name}.wav", y)
        print(f"  debris-soft-{name}.wav  {y.shape[1] / SR:.1f} s")


if __name__ == "__main__":
    main()
