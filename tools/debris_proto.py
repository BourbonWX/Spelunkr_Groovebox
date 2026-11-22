#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Phantom Cosmonaut: DEBRIS, GRANULAR and FOLDER as one engine (Ed, Nov 2026: "combine folder and granular into one
... more unique engine"). A prototype on the firmware's own material: the 16 SIGNALs (signals_tables.h, level 0: 256
samples a frame, 8 frames a tape) and the four recorded tapes (factory_tables.h FK_TAPE, 16384 at 11.025 kHz).

Grains read the tape as GRANULAR does (two streams half a grain apart, triangle windows; on a SIGNAL a frame a cycle:
the note's pitch; on a tape its own speed at middle C), and every grain goes through FOLDER's sine fold before its
window, each grain folded a little differently (SCATTER):
  SCAN where the grains read   WARP the grain's size (0.7 .. 180 ms; at the top a continuous signal: FOLDER)
  FOLD the fold (x1 .. x16)    CLARITY left gaps between grains, right static
  EDIT: SCATTER (jumps, and each grain's fold its own), FENV (the envelope moves FOLD), JIT (octave grains), SYM

  python3 tools/debris_proto.py OUTDIR
"""
import re
import sys
import wave
from pathlib import Path

import numpy as np
from scipy import signal

SR = 44100
ROOT = Path(__file__).resolve().parent.parent


def load_signals():
    src = (ROOT / "firmware/src/signals_tables.h").read_text()
    names = re.search(r'SIG_NAME\[SIG_N \+ 1\] = \{(.*?)\};', src).group(1)
    names = re.findall(r'"([^"]+)"', names)
    body = src[src.index("SIG_TAB[SIG_N * SIG_NF][SIG_FRAME]"):]
    frames = re.findall(r"\{([-0-9,\s]+)\}", body)
    tapes = {}
    for k, nm in enumerate(names):
        fr = [np.array([int(x) for x in frames[k * 8 + j].split(",") if x.strip()][:256], float) / 128 for j in range(8)]
        tapes[nm] = dict(data=np.concatenate(fr), kind="signal")
    return tapes


def load_tapes():
    src = (ROOT / "firmware/src/factory_tables.h").read_text()
    names = re.findall(r'"([^"]+)"', re.search(r"FK_TAPE_NAME\[FK_NTAPE\] = \{(.*?)\};", src).group(1))
    body = src[src.index("FK_TAPE[FK_NTAPE][FK_TAPE_N]"):]
    blocks = re.findall(r"\{\s*/\*[^*]*\*/(.*?)\n    \}", body, re.S)
    out = {}
    for nm, b in zip(names, blocks):
        out[nm] = dict(data=np.array([int(x) for x in re.findall(r"-?\d+", b)], float) / 127, kind="tape")
    return out


def fold(x, g, sym):
    """FOLDER's sine fold, sin(pi/2 x g); g 1 and no SYM: the grain itself (GRANULAR)"""
    if g <= 1.01 and abs(sym) < 0.01:
        return x
    return np.sin(np.pi / 2 * g * (x + sym * 0.4)) * 0.6


def adsr(n, a, d, s, r, gate):
    t = np.arange(n) / SR
    e = np.where(t < a, t / max(a, 1e-4), s + (1 - s) * np.exp(-(t - a) / max(d, 1e-4)))
    rel = t >= gate
    e = np.where(rel, e[min(int(gate * SR), n - 1)] * np.exp(-(t - gate) / max(r, 1e-4)), e)
    return e


def note(tape, midi, gate, P, env=(0.01, 0.3, 0.8, 0.3), seed=0, tail=None):
    """one voice: tape (a dict), MIDI note, held gate seconds, P(t) -> the knobs at time t (0..1; CLARITY, SYM -1..1)"""
    rng = np.random.default_rng(seed)
    a, d, s, r = env
    n = int((gate + (tail if tail is not None else r * 5)) * SR)
    e = adsr(n, a, d, s, r, gate)
    f0 = 440 * 2 ** ((midi - 69) / 12)
    data = tape["data"]
    L = len(data)
    rate = 256 * f0 / SR if tape["kind"] == "signal" else (11025 / SR) * f0 / 261.63
    out = np.zeros(n)
    p0 = P(0.0)
    if p0["warp"] >= 0.97:                            # the top of WARP: one continuous read (FOLDER)
        pos = p0["scan"] * 0.875 * L + rate * np.arange(n)
        i0 = np.floor(pos).astype(int)
        fr = pos - i0
        x = data[i0 % L] * (1 - fr) + data[(i0 + 1) % L] * fr
        blk = 64
        for b in range(0, n, blk):
            q = P(b / SR)
            g = 1 + 15 * min(1.0, q["fold"] + q["fenv"] * e[b])
            out[b:b + blk] = fold(x[b:b + blk], g, q["sym"])
        return out * e
    for stream in (0, 1):                              # two grains, half a grain apart (a steady sum of windows)
        t = 0.0 if stream == 0 else None
        while True:
            q = P(t if t is not None else 0.0)
            glen = 0.0007 * (180 / 0.7) ** (q["warp"] / 0.97)
            if t is None:
                t = glen / 2
                continue
            if t >= n / SR:
                break
            gn = max(8, int(glen * SR))
            st = int(t * SR)
            sc = q["scatter"]
            start = (q["scan"] * 0.875 + sc * 0.3 * (rng.random() - 0.5)) * L
            oc = 1.0
            if rng.random() < q["jit"]:
                oc = 2.0 if rng.random() < 0.5 else 0.5
            silent = q["clarity"] < 0 and rng.random() < -q["clarity"] * 0.9
            if not silent:
                pos = start + rate * oc * np.arange(gn)
                i0 = np.floor(pos).astype(int)
                frc = pos - i0
                x = data[i0 % L] * (1 - frc) + data[(i0 + 1) % L] * frc
                fe = q["fold"] + q["fenv"] * e[min(st, n - 1)]
                g = 1 + 15 * max(0.0, min(1.0, fe * (1 + sc * 0.9 * (rng.random() * 2 - 1))))   # each grain its own fold
                y = fold(x, g, q["sym"])
                w = 1 - np.abs(np.linspace(-1, 1, gn))
                end = min(n, st + gn)
                out[st:end] += (y * w)[: end - st]
            t += glen
    if True:
        st_amt = max(0.0, P(0.0)["clarity"])
        if st_amt > 0:
            out += rng.standard_normal(n) * 0.12 * st_amt * np.abs(out).mean() * 4
    return out * e


def K(**k):
    base = dict(scan=0.3, warp=0.6, fold=0.0, clarity=0.0, scatter=0.0, fenv=0.0, jit=0.0, sym=0.0)
    base.update(k)
    return base


def sweep(t, dur, a, b):
    return a + (b - a) * min(1.0, max(0.0, t / dur))


def norm(x, db=-1.0):
    x = x - np.mean(x)
    return x / (np.abs(x).max() or 1) * 10 ** (db / 20)


def write(path, x, stereo=False):
    y = np.clip(np.round(x * 32767), -32768, 32767).astype("<i2")
    with wave.open(str(path), "wb") as w:
        w.setnchannels(2 if stereo else 1)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes(y.tobytes())


def mix(parts, n):
    out = np.zeros(n)
    for at, x, g in parts:
        s = int(at * SR)
        if s >= n:
            continue
        m = min(len(x), n - s)
        out[s:s + m] += x[:m] * g
    return out


def room(x, mix_=0.2, decay=1.2, seed=3):
    rng = np.random.default_rng(seed)
    m = int(SR * decay)
    ir = rng.standard_normal(m) * np.exp(-np.arange(m) / (SR * decay / 6.9))
    ir = signal.sosfilt(signal.butter(2, 4000, fs=SR, output="sos"), ir)
    ir /= np.sqrt((ir ** 2).sum())
    wet = signal.fftconvolve(x, ir)[: len(x)]
    return x * (1 - mix_) + wet * mix_


def main():
    out = Path(sys.argv[1] if len(sys.argv) > 1 else "build/debris")
    out.mkdir(parents=True, exist_ok=True)
    S = {**load_signals(), **load_tapes()}
    print("material:", ", ".join(S))
    ex = {}
    bpm = 120
    st16 = 60 / bpm / 4
    Dm, Bb, C = (50, 53, 57), (46, 50, 53), (48, 52, 55)

    # 1. FOLD on a continuous signal: FOLDER's territory (WARP at the top), a bass riff
    riff = [38, 38, 50, 38, 41, 38, 45, 43] * 8
    parts = []
    for i, m_ in enumerate(riff):
        tt = i * st16 * 2
        parts.append((tt, note(S["HULL GROAN"], m_, st16 * 1.6,
                                lambda t, tt=tt: K(warp=1.0, scan=0.4, fold=sweep(tt + t, 15.5, 0.0, 0.85)),
                                env=(0.003, 0.2, 0.6, 0.08), seed=i), 1.0))
    ex["01-fold-sweep-bass (FOLDER: one continuous signal, FOLD 0 to full)"] = mix(parts, int(16.5 * SR))

    # 2. WARP from continuous down to shards, a held chord, FOLD a little
    dur = 10.0
    ex["02-grain-size-sweep (WARP: continuous, long grains, down to tiny shards)"] = sum(
        note(S["VOSTOK"], m_, dur, lambda t: K(warp=sweep(t, dur, 0.96, 0.05), scan=0.35, fold=0.3, scatter=0.25),
             env=(0.3, 1, 1, 0.6), seed=k) for k, m_ in enumerate(Dm))

    # 3. GRANULAR vs DEBRIS: the same grains, FOLD 0, then FOLD and SCATTER up
    def p3(t):
        on = t > 4.5
        return K(warp=0.55, scan=0.5, fold=0.55 if on else 0.0, scatter=0.6 if on else 0.15)
    ex["03-granular-then-debris (the same grains: FOLD 0 for 4.5 s, then folded and scattered)"] = sum(
        note(S["GROAN"], m_, 9.0, p3, env=(0.4, 1, 1, 0.8), seed=k) for k, m_ in enumerate((57, 62, 65)))

    # 4. SCATTER rising on HETERODYNE: from one steady grain to a storm of different folds
    dur = 9.0
    ex["04-scatter-rising (SCATTER 0 to full: each grain jumps and folds its own way)"] = note(
        S["HETERODYNE"], 60, dur, lambda t: K(warp=0.45, scan=0.4, fold=0.5, scatter=sweep(t, dur, 0.0, 1.0)),
        env=(0.2, 1, 1, 0.5), seed=4)

    # 5. a crumbling bass: FENV, the fold follows the envelope (bright strike, crumbling as it decays), short grains
    line = [(0, 38), (3, 38), (6, 50), (8, 38), (10, 41), (12, 45), (14, 43)] * 4
    parts = []
    for i, (s16, m_) in enumerate(line):
        tt = s16 * st16 + (i // 7) * 16 * st16
        parts.append((tt, note(S["DEAD STAR"], m_, st16 * 1.2,
                                lambda t: K(warp=0.72, scan=0.25, fold=0.05, fenv=0.8, scatter=0.35),
                                env=(0.002, 0.18, 0.15, 0.12), seed=i), 1.0))
    ex["05-crumbling-bass (FENV: a folded strike that crumbles into grains as it decays)"] = mix(parts, int(8.4 * SR))

    # 6. metal rain: tiny grains, hard folds, scatter, octave jitter, gaps
    dur = 8.0
    ex["06-metal-rain (tiny grains, hard folds, gaps, octave jitter)"] = sum(
        note(S["VENT"], m_, dur, lambda t: K(warp=0.12, scan=sweep(t, dur, 0.2, 0.8), fold=0.75, scatter=0.9,
                                                jit=0.35, clarity=-0.5), env=(0.05, 1, 1, 0.8), seed=k)
        for k, m_ in enumerate((62, 69, 74)))

    # 7. a snarling lead: continuous, SYM off-centre, a phrase
    mel = [(0, 62, 2), (2, 65, 2), (4, 69, 3), (7, 67, 1), (8, 65, 4), (12, 64, 2), (14, 62, 6)]
    parts = []
    for i, (s16, m_, l16) in enumerate(mel):
        parts.append((s16 * st16 * 2, note(S["PULSAR"], m_, l16 * st16 * 2 * 0.95,
                                           lambda t: K(warp=1.0, scan=0.3, fold=0.62, sym=0.45, fenv=0.2),
                                           env=(0.01, 0.4, 0.75, 0.2), seed=i), 1.0))
    ex["07-snarl-lead (FOLDER's snarl, off-centre: SYM)"] = mix(parts, int(9.0 * SR))

    # 8. a ghost choir: the voice-like SKYWAVE tape, gentle grains, a little fold
    ex["08-ghost-choir (the SKYWAVE tape: long grains, light fold, slow)"] = sum(
        note(S["SKYWAVE"], m_, 8.0, lambda t: K(warp=0.75, scan=sweep(t, 8, 0.1, 0.6), fold=0.18, scatter=0.35),
             env=(1.2, 1, 1, 1.5), seed=k) for k, m_ in enumerate((50, 57, 62, 65)))

    # 9. a wreckage pad: GROAN, Dm Bb C, SCAN drifting, fold breathing with the envelope
    parts = []
    for c, (at, ch) in enumerate([(0, Dm), (4, Bb), (6, C), (8, Dm), (12, Bb), (14, C)]):
        tt = at * st16 * 4
        for k, m_ in enumerate(ch):
            parts.append((tt, note(S["GROAN"], m_, (4 if at in (0, 8) else 2) * st16 * 4 * 0.98,
                                    lambda t, tt=tt: K(warp=0.68, scan=0.2 + 0.4 * ((tt + t) / 8), fold=0.3,
                                                       fenv=0.25, scatter=0.45),
                                    env=(0.25, 0.8, 0.8, 0.6), seed=c * 7 + k), 0.6))
    ex["09-wreckage-pad (GROAN, the default song's chords, SCAN drifting)"] = mix(parts, int(8.9 * SR))

    # 10. a groove: DERELICT's hits, the crumbling bass, the wreckage pad, a little metal rain
    n = int(16.5 * SR)
    drums = []
    hit = {}
    for nm in ("01-hull", "02-rivet", "04-relay", "05-valve", "03-pipe"):
        w = wave.open(str(ROOT / f"build/factory/wav/derelict-{nm}.wav"))
        hit[nm] = np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16) / 32768
    for bar in range(8):
        b0 = bar * 16 * st16
        for s16 in (0, 6, 10):
            drums.append((b0 + s16 * st16, hit["01-hull"][: int(0.5 * SR)], 0.9))
        for s16 in (4, 12):
            drums.append((b0 + s16 * st16, hit["02-rivet"], 0.55))
        for s16 in range(0, 16, 2):
            drums.append((b0 + (s16 + 1) * st16, hit["04-relay"], 0.28))
        if bar % 2 == 1:
            drums.append((b0 + 14 * st16, hit["05-valve"], 0.3))
        drums.append((b0 + 7 * st16, hit["03-pipe"][: int(0.3 * SR)], 0.25))
    bass = []
    for bar in range(8):
        root = (38, 34, 36, 38)[bar % 4]
        for s16, d_ in ((0, 0), (3, 0), (6, 12), (8, 0), (11, 0), (14, 7)):
            tt = bar * 16 * st16 + s16 * st16
            bass.append((tt, note(S["DEAD STAR"], root + d_, st16 * 1.2,
                                  lambda t: K(warp=0.72, scan=0.25, fold=0.05, fenv=0.8, scatter=0.35),
                                  env=(0.002, 0.18, 0.15, 0.12), seed=bar * 9 + s16), 0.75))
    pad = []
    for bar in range(8):
        ch = (Dm, Bb, C, Dm)[bar % 4]
        for k, m_ in enumerate(ch):
            pad.append((bar * 16 * st16, note(S["GROAN"], m_ + 12, 16 * st16 * 0.98,
                                              lambda t: K(warp=0.68, scan=0.35, fold=0.3, fenv=0.2, scatter=0.45),
                                              env=(0.3, 0.8, 0.8, 0.4), seed=bar * 5 + k), 0.22))
    rain = [(8 * 16 * st16 * 0.5, note(S["VENT"], 74, 4 * 16 * st16, lambda t: K(warp=0.12, scan=0.5, fold=0.75,
                                                                                    scatter=0.9, jit=0.35, clarity=-0.6),
                                        env=(1.0, 1, 1, 1.0), seed=99), 0.12)]
    groove = mix(drums, n) * 0.9 + norm(mix(bass, n)) * 0.55 + norm(mix(pad, n)) * 0.35 + mix(rain, n)
    ex["10-debris-groove (DERELICT drums, crumbling bass, wreckage pad, metal rain)"] = room(groove, 0.12)

    for name, x in ex.items():
        fn = name.split(" (")[0]
        write(out / f"debris-{fn}.wav", norm(room(x, 0.12) if "groove" not in name else x))
        print(f"  debris-{fn}.wav  {len(x) / SR:.1f} s  {name.split(' (')[1][:-1]}")


if __name__ == "__main__":
    main()
