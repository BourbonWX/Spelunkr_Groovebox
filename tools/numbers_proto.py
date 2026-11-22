#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Phantom Cosmonaut: NUMBERS, the prototype (Ed, Nov 2026: VOX "not landing": "if I wanted to make it say 'I'm in
outer space' using the tracker"; NUMBERS picked from the workshop). A numbers-station / Speak & Spell voice: a step
speaks a whole word.

How it would work on the FM-1, and here:
  1. The word bank, made once on a computer: each word spoken by Flite (CMU's voices, BSD-style licence: shippable),
     resampled to 8 kHz and coded as LPC frames the way the TI Speak & Spell did: every 25 ms ten reflection
     coefficients (the vocal tract's shape), a loudness, voiced or not, and the word's own pitch movement; quantised
     to bits (QBITS) and measured: what 100 words cost in flash.
  2. On the device: the frames, interpolated, drive a lattice filter (ten multiplies a sample, cheaper than VOX),
     excited by a buzz at the NOTE's pitch (or noise where the word hisses), at 8 kHz: the radio is in the method.
  3. The knobs: RADIO (band-limit, a carrier's hum, static, a squelch tail after each word), SPEED (the frames read
     faster or slower, the pitch kept), HUMAN (0: a robot, a fixed chirp at the note, the Speak & Spell; 1: the word's
     own rise and fall around the note, a softer pulse, a little jitter), GLIDE (the pitch slides note to note).
  Two modes: WORD (each step its own word) and PHRASE (each note the next word of a phrase).

  python3 tools/numbers_proto.py OUTDIR        (needs flite; the backing track from factory_render's demo)
"""
import subprocess
import sys
import tempfile
import wave
from pathlib import Path

import numpy as np
from scipy import signal

SR8 = 8000
FR = 200                       # 25 ms frames at 8 kHz
ORDER = 10
OUT_SR = 44100
ROOT = Path(__file__).resolve().parent.parent
# bits per frame (TMS5220's budget, roughly): energy, voiced, pitch movement, K1..K10
QBITS = dict(energy=5, voiced=1, pitch=4, k=[6, 6, 5, 5, 4, 4, 4, 4, 3, 3])

WORDS = ("ZERO ONE TWO THREE FOUR FIVE SIX SEVEN EIGHT NINER TEN HUNDRED "
         "ALPHA DELTA ECHO VICTOR "
         "CALLING COME OVER OUT REPEAT CONFIRM NEGATIVE AFFIRMATIVE ROGER STANDBY COPY MAYDAY ATTENTION RECEIVING "
         "ORBIT CAPSULE STATION SIGNAL CONTACT SPACE OUTER EARTH MOON STARS SATELLITE ROCKET LAUNCH GROUND CONTROL TRANSMISSION "
         "SYSTEM POWER OXYGEN FAILURE WARNING ERROR ONLINE LOST DRIFTING SILENCE STATIC DAMAGE "
         "COSMONAUT PHANTOM GHOST CREW VOICE HEART "
         "I I'M YOU WE IT IS ARE THE A THIS IN ON TO FROM NO YES NOT HERE THERE STILL CAN HEAR ME MY WHO ANYONE "
         "COLD DARK FAR ALONE HOME HELLO GOODBYE NEVER FOREVER WAITING").split()
SAY = {"NINER": "niner", "I'M": "I'm", "A": "uh", "I": "eye", "THE": "the"}


# ------------------------------------------------------------------------------------------------ the word bank ---
def flite(text, voice="rms"):
    with tempfile.NamedTemporaryFile(suffix=".wav") as f:
        subprocess.run(["flite", "-voice", voice, "-t", text, "-o", f.name], check=True, capture_output=True)
        w = wave.open(f.name)
        x = np.frombuffer(w.readframes(w.getnframes()), np.int16).astype(float) / 32768
        return signal.resample_poly(x, SR8, w.getframerate())


def trim(x):
    e = np.sqrt(np.convolve(x ** 2, np.ones(80) / 80, "same"))
    on = np.where(e > e.max() * 0.03)[0]
    a, b = max(0, on[0] - 80), min(len(x), on[-1] + 160)
    return x[a:b]


def levinson(r, p):
    a = np.zeros(p + 1); a[0] = 1.0; k = np.zeros(p); err = r[0]
    for i in range(1, p + 1):
        acc = r[i] + np.dot(a[1:i], r[i - 1:0:-1])
        ki = -acc / err if err > 0 else 0.0
        a_prev = a.copy()
        for j in range(1, i):
            a[j] = a_prev[j] + ki * a_prev[i - j]
        a[i] = ki
        k[i - 1] = ki
        err *= (1 - ki * ki)
    return a, k, err


def analyse(x):
    """LPC frames: (k[10], gain, voiced, f0 Hz or 0)"""
    pre = np.append(x[0], x[1:] - 0.9375 * x[:-1])
    frames = []
    win = np.hamming(2 * FR)
    for s in range(0, len(pre) - FR, FR):
        seg = pre[max(0, s - FR // 2): s + FR + FR // 2]
        if len(seg) < 2 * FR:
            seg = np.pad(seg, (0, 2 * FR - len(seg)))
        seg = seg * win
        r = np.correlate(seg, seg, "full")[len(seg) - 1: len(seg) + ORDER] + 1e-9
        r[0] *= 1.0001
        a, k, err = levinson(r, ORDER)
        g = np.sqrt(max(err, 1e-12) / len(seg))
        # voicing / pitch: the residual's autocorrelation, 70..300 Hz
        res = signal.lfilter(a, [1.0], seg)
        ac = np.correlate(res, res, "full")[len(res) - 1:]
        lo, hi = SR8 // 300, SR8 // 70
        lag = lo + int(np.argmax(ac[lo:hi]))
        v = ac[lag] / (ac[0] + 1e-12)
        rms = np.sqrt(np.mean(x[s:s + FR] ** 2)) if s + FR <= len(x) else 0
        loud = np.sqrt(np.mean(pre[s:s + FR] ** 2))          # what the frame must sound at (the emphasised speech)
        voiced = v > 0.3 and rms > 0.01
        frames.append((k, loud, voiced, SR8 / lag if voiced else 0.0))
    peak = max(f[1] for f in frames) if frames else 1.0
    return [(k, (l if l > peak * 10 ** (-45 / 20) else 0.0), v, f) for k, l, v, f in frames]   # (45 dB down: silence)


def quantise(frames):
    """the frames as the firmware would hold them, and their size in bits (repeat frames: energy + a flag)"""
    out, bits, prev = [], 0, None
    f0s = [f for _, _, v, f in frames if v]
    med = np.median(f0s) if f0s else 100.0
    for k, g, v, f in frames:
        e = int(np.clip(np.round((20 * np.log10(g + 1e-9) + 80) / 3), 0, 31)) if g > 0 else 0   # 3 dB steps
        kq = []
        for i, b in enumerate(QBITS["k"]):
            th = np.arcsin(np.clip(k[i], -0.999, 0.999))                         # arcsine: even steps in sound
            n = 2 ** b
            kq.append(np.sin((np.round((th / (np.pi / 2) + 1) / 2 * (n - 1)) / (n - 1) * 2 - 1) * np.pi / 2))
        st = int(np.clip(np.round(12 * np.log2(f / med) / 1.5), -8, 7)) if v else 0  # 1.5 semitone steps
        kq = np.array(kq)
        lv = 10 ** ((e * 3 - 80) / 20) if e else 0.0
        frame = (kq, lv * np.sqrt(np.prod(1 - kq ** 2)), v, st * 1.5)   # the excitation's level: the loudness x the
                                                         # filter's own gain undone (the prediction error's power)
        if e == 0:
            bits += QBITS["energy"]                                              # silence
        elif prev is not None and v == prev[2] and np.max(np.abs(frame[0] - prev[0])) < 0.08:
            bits += QBITS["energy"] + 1 + QBITS["pitch"]                         # repeat: the shape held
            frame = (prev[0], frame[1], v, frame[3])
        else:
            bits += QBITS["energy"] + 1 + QBITS["voiced"] + QBITS["pitch"] + sum(QBITS["k"] if v else QBITS["k"][:4])
        out.append(frame)
        prev = frame
    return out, bits


def bank(voice="rms"):
    b, total = {}, 0
    for w in WORDS:
        fr, bits = quantise(analyse(trim(flite(SAY.get(w, w.lower()), voice))))
        b[w] = fr
        total += bits
    return b, total


# --------------------------------------------------------------------------------------------- the synthesis ---
def speak(frames, f0, speed=1.0, human=0.3, seed=0, next_f0=None, glide=0.0, length=None):
    """one word at f0 Hz (8 kHz), interpolated frames, lattice filter; length: cut at (s) like a keyed radio"""
    rng = np.random.default_rng(seed)
    nfr = len(frames)
    n = int(nfr * FR / speed)
    if length is not None:
        n = min(n, int(length * SR8))
    y = np.zeros(n)
    b = np.zeros(ORDER + 1)          # the lattice's backward state
    ph = 0.0
    prev_exc = 0.0
    for i in range(n):
        pos = i * speed / FR
        j = min(int(pos), nfr - 1); jn = min(j + 1, nfr - 1); t = pos - int(pos)
        k = frames[j][0] * (1 - t) + frames[jn][0] * t
        g = frames[j][1] * (1 - t) + frames[jn][1] * t
        voiced = frames[j][2] if t < 0.5 else frames[jn][2]
        st = (frames[j][3] * (1 - t) + frames[jn][3] * t) * human
        f = f0 * 2 ** (st / 12)
        if next_f0 and glide > 0 and i > n * (1 - glide):
            f *= (next_f0 / f0) ** ((i - n * (1 - glide)) / (n * glide))
        f *= 1 + human * 0.004 * rng.standard_normal()
        if voiced:
            ph += f / SR8
            if ph >= 1.0:
                ph -= 1.0
            per = SR8 / f
            if human < 0.5:      # the robot: a short chirp each period (the Speak & Spell's excitation)
                c = int(ph * per)
                CH = (0.0, 0.8, -0.6, 0.5, -0.4, 0.3, -0.2, 0.1)
                e = CH[c] * np.sqrt(per / 1.59) if c < len(CH) else 0.0   # (unit power: sum CH^2 = 1.59)
            else:                # human: a glottal pulse's derivative (Rosenberg), softer
                oq = 0.6
                if ph < oq * 0.66:
                    e = np.sin(np.pi * ph / (oq * 0.66)) * 0.5
                elif ph < oq:
                    e = -np.sin(np.pi / 2 * (ph - oq * 0.66) / (oq * 0.34))
                else:
                    e = 0.0
                e *= np.sqrt(1 / 0.15)                       # (unit power: the pulse's mean square 0.15)
            mix = (1 - human) * 1.0 + human * 0.0  # (a breath under the human voice)
            e = e + human * 0.08 * rng.standard_normal()
        else:
            e = rng.standard_normal()
        x = e * g
        # all-pole lattice (k: reflection, as analysed with a = [1, k..])
        fwd = x
        for m in range(ORDER - 1, -1, -1):
            fwd = fwd - k[m] * b[m]
            b[m + 1] = b[m] + k[m] * fwd
        b[0] = fwd
        y[i] = fwd
    y = signal.lfilter([1.0], [1.0, -0.9375], y)             # de-emphasis
    fade = min(len(y), int(0.012 * SR8))
    if fade:
        y[-fade:] *= np.linspace(1, 0, fade)
    return y


def radio(x, amt, seed=1, tail=True):
    """the radio, 44.1 kHz: band-limit 300 .. 3 kHz, a carrier's hum, static, a squelch tail after the word"""
    if amt <= 0:
        return x
    rng = np.random.default_rng(seed)
    bp = signal.butter(2, [300, 3000 - 1200 * amt], "bandpass", fs=OUT_SR, output="sos")
    y = signal.sosfilt(bp, x) * (1 + 1.5 * amt)
    y = np.tanh(y * (1 + 3 * amt)) / (1 + 1.5 * amt)
    y = y * (1 - amt * 0.35) + x * (1 - amt) * 0.0
    n = len(y)
    st = signal.sosfilt(signal.butter(2, [800, 5000], "bandpass", fs=OUT_SR, output="sos"), rng.standard_normal(n))
    crack = (rng.random(n) < 0.0004 * amt) * rng.standard_normal(n) * 0.6
    env = np.abs(signal.hilbert(x)) if n < 600000 else np.abs(x)
    env = np.convolve(env, np.ones(2205) / 2205, "same")
    gate = np.clip(env * 30, 0, 1)                          # the squelch: open while the voice is
    y = y + (st * 0.05 * amt) * (0.3 + 0.7 * gate) + crack * amt
    y += 0.01 * amt * np.sin(2 * np.pi * 60 * np.arange(n) / OUT_SR)
    return y


def up(x):
    return signal.resample_poly(x, OUT_SR // 100, SR8 // 100)


def norm(x, db=-1.0):
    return x / (np.abs(x).max() + 1e-9) * 10 ** (db / 20)


def write(path, x):
    y = np.clip(np.round((x.T if x.ndim == 2 else x) * 32767), -32768, 32767).astype("<i2")
    with wave.open(str(path), "wb") as w:
        w.setnchannels(2 if x.ndim == 2 else 1)
        w.setsampwidth(2)
        w.setframerate(OUT_SR)
        w.writeframes(y.tobytes())


def hz(m):
    return 440 * 2 ** ((m - 69) / 12)


# ------------------------------------------------------------------------------------------------- the demos ---
def line(B, events, total, speed=1.0, human=0.3, rad=0.4, glide=0.0, seed=0):
    """events: (t s, word, midi); each word cut by the next (PHRASE / WORD mode alike), the radio over the whole"""
    out = np.zeros(int(total * OUT_SR) + OUT_SR)
    for i, (t, w, m) in enumerate(events):
        nxt = events[i + 1] if i + 1 < len(events) else None
        length = (nxt[0] - t) if nxt else None
        y = speak(B[w], hz(m), speed, human, seed + i, hz(nxt[2]) if nxt else None, glide, length)
        y = up(y)
        s = int(t * OUT_SR)
        out[s:s + len(y)] += y[: len(out) - s]
    return radio(out, rad, seed)


def main():
    out = Path(sys.argv[1] if len(sys.argv) > 1 else "build/numbers")
    out.mkdir(parents=True, exist_ok=True)
    print("the word bank (flite rms -> LPC) ..")
    B, bits = bank("rms")
    secs = sum(len(f) for f in B.values()) * FR / SR8
    print(f"  {len(B)} words, {secs:.1f} s of speech, {bits / 8 / 1024:.1f} KB as LPC frames "
          f"({bits / secs:.0f} bit/s)")
    longest = sorted(B, key=lambda w: -len(B[w]))[:5]
    print("  longest:", ", ".join(f"{w} {len(B[w]) * FR / SR8:.2f} s" for w in longest))

    bpm = 120
    q = 60 / bpm
    e8 = q / 2

    # 1. WORD mode: a numbers station. A chime, ATTENTION, five-figure groups, monotone, the radio up
    ev = []
    t = 3.0
    for w in ("ATTENTION", "ATTENTION"):
        ev.append((t, w, 50)); t += 4 * e8
    groups = ["SEVEN FOUR NINER ZERO TWO", "ONE ONE EIGHT FIVE SIX", "THREE NINER ZERO ZERO FOUR"]
    for g in groups:
        for w in g.split():
            ev.append((t, w, 50)); t += 2 * e8
        t += 2 * e8
    ev.append((t, "REPEAT", 50)); t += 3 * e8
    for w in groups[0].split():
        ev.append((t, w, 50)); t += 2 * e8
    t += 2 * e8
    for w in ("OUT",):
        ev.append((t, w, 50)); t += 4 * e8
    total = t + 1.5
    v = line(B, ev, total, speed=0.95, human=0.15, rad=0.85, seed=3)
    chime = np.zeros_like(v)
    for i, (tt, m) in enumerate([(0.2, 74), (0.7, 69), (1.2, 77), (1.7, 72)]):   # an interval sign of its own
        n = int(0.9 * OUT_SR); s = int(tt * OUT_SR)
        ts = np.arange(n) / OUT_SR
        tone = (np.sin(2 * np.pi * hz(m) * ts) + 0.3 * np.sin(2 * np.pi * hz(m) * 2.76 * ts) * np.exp(-ts * 6)) * np.exp(-ts * 3.2)
        chime[s:s + n] += tone * 0.25
    v = v + radio(chime, 0.6, 9)
    write(out / "numbers-01-station.wav", norm(v))

    # 2. PHRASE mode over the transmission: each note the next word; D minor, the voice sung
    back = ROOT / "build/v093/demo-noise.wav"
    w = wave.open(str(back))
    bk = np.frombuffer(w.readframes(w.getnframes()), np.int16).astype(float).reshape(-1, 2) / 32768
    st16 = 60 / 120 / 4
    phr = [  # (16th, word, midi)
        (0, "I'M", 62), (4, "IN", 65), (8, "OUTER", 69), (12, "SPACE", 67),
        (32, "CAN", 62), (34, "ANYONE", 65), (40, "HEAR", 69), (44, "ME", 70),
        (64, "GROUND", 69), (68, "CONTROL", 67), (72, "COME", 65), (76, "IN", 64),
        (96, "I'M", 62), (100, "STILL", 65), (104, "HERE", 69), (112, "OVER", 62),
    ]
    ev = [(0.5 + s * st16, wd, m) for s, wd, m in phr]
    voc = line(B, ev, bk.shape[0] / OUT_SR, speed=0.85, human=0.75, rad=0.35, glide=0.25, seed=5)
    voc = norm(voc, -6)
    n = min(len(voc), bk.shape[0])
    mix = bk[:n] * 0.55 + np.stack([voc[:n], voc[:n]], 1) * 0.9
    write(out / "numbers-02-phrase-over-transmission.wav", norm(mix.T, -1.0))

    # 3. the knobs: GROUND CONTROL, COME IN, OVER, four ways
    ev0 = [(0.2, "GROUND", 55), (0.65, "CONTROL", 55), (1.3, "COME", 55), (1.6, "IN", 55), (2.2, "OVER", 55)]
    parts = []
    for sp, hu, ra, lab in ((1.0, 0.0, 0.0, "robot, dry"), (1.0, 1.0, 0.0, "human, dry"),
                            (1.0, 0.4, 1.0, "radio full"), (0.6, 0.4, 0.4, "speed slow"), (1.5, 0.4, 0.4, "speed fast")):
        ev = [(t / sp if sp < 1 else t / sp, wd, m) for t, wd, m in ev0]
        parts.append(norm(line(B, ev, 3.3 / min(sp, 1.0), speed=sp, human=hu, rad=ra, seed=7), -2))
        parts.append(np.zeros(int(0.5 * OUT_SR)))
    write(out / "numbers-03-knobs.wav", np.concatenate(parts))

    # 4. the bank: all 100 words, a beat each, plain (robot 0.3, radio 0.3)
    ev = [(0.3 + i * 0.75, wd, 52) for i, wd in enumerate(WORDS)]
    write(out / "numbers-04-all-100-words.wav", norm(line(B, ev, ev[-1][0] + 1.2, human=0.4, rad=0.3, seed=11)))
    print("  wrote", ", ".join(p.name for p in sorted(out.glob("numbers-*.wav"))))


if __name__ == "__main__":
    main()
