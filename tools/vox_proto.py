#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Phantom Cosmonaut: VOX, an articulatory voice engine (Ed, Nov 2026: "the four knobs control mouth shape, vocal
chords, tongue placement ... presets for common English mouth sounds like a vocaloid"). A prototype in floating point,
to hear before porting to the firmware's fixed point.

The knobs are a mouth, not filters:
  MOUTH  jaw closed .. wide open        TONGUE  back .. front        LIPS  spread .. rounded
  VOICE  the vocal cords: 0 whisper (no buzz), 0.3 breathy, 0.6 modal, 1 pressed
  EDIT:  NASAL (velum open), TIP (tongue tip curled: R, ER), NOISE (frication), PLACE (where the hiss is made)
The mouth sets four formants (articulation -> F1..F4, fitted to Hillenbrand et al. 1995's adult male vowels); the
voice is a Rosenberg glottal pulse with aspiration; nasals add a murmur and an anti-resonance; fricatives a shaped
noise. Presets are mouth positions (phonemes); GLIDE morphs between them.

  python3 tools/vox_proto.py OUTDIR
"""
import sys
import wave
from pathlib import Path

import numpy as np
from scipy import signal
from scipy.optimize import minimize

SR = 44100

# Hillenbrand et al. (1995), adult male means, F1 F2 F3 (Hz)
TARGET = {
    "EE": (342, 2322, 3000), "IH": (427, 2034, 2684), "EH": (580, 1799, 2605), "AE": (588, 1952, 2601),
    "AH": (768, 1333, 2522), "AW": (652, 997, 2538), "OH": (497, 910, 2459), "UH": (623, 1200, 2550),
    "OO": (378, 997, 2343), "ER": (474, 1379, 1710),
}


def formants(mouth, tongue, lips, tip):
    """articulation (0..1 each) -> F1..F4, bandwidths. Jaw open raises F1; tongue front raises F2 (less so with the
    jaw wide); rounding lowers F2 and F3; a curled tip drops F3 (r-colouring)"""
    f1 = 280 + 520 * mouth - 70 * lips + 30 * tongue
    f2 = 870 + 1550 * tongue * (1 - 0.5 * mouth) + 380 * mouth * (1 - tongue) - 420 * lips * (1 - 0.4 * tongue)
    f3 = 2420 + 520 * tongue - 260 * lips - 900 * tip
    f4 = 3400 + 300 * tongue - 200 * lips - 300 * tip
    bw = (60 + 50 * mouth, 70 + 40 * mouth, 110, 160)
    return (f1, f2, f3, f4), bw


# how rounded each vowel's lips really are (a guide for the fit, so the knobs read as a mouth): English's rounded
# vowels OO OH AW (and ER a little); the rest spread
LIPS_REAL = {"OO": 0.85, "OH": 0.6, "AW": 0.4, "ER": 0.35}


def fit_vowels():
    """the knob positions of each vowel: the articulation whose formants land nearest the target (log error)"""
    out = {}
    for ph, tg in TARGET.items():
        tip0 = 0.75 if ph == "ER" else 0.0
        best = None
        for m0 in (0.1, 0.5, 0.9):
            for t0 in (0.1, 0.5, 0.9):
                for l0 in (0.0, 0.8):
                    def err(x):
                        m, t, l = np.clip(x, 0, 1)
                        f, _ = formants(m, t, l, tip0)
                        return sum((np.log(f[i] / tg[i])) ** 2 * w for i, w in enumerate((1.4, 1.0, 0.5))) + \
                            0.02 * (l - LIPS_REAL.get(ph, 0.0)) ** 2
                    r = minimize(err, [m0, t0, l0], method="Nelder-Mead", options={"xatol": 1e-3, "fatol": 1e-7})
                    if best is None or r.fun < best.fun:
                        best = r
        m, t, l = np.clip(best.x, 0, 1)
        f, _ = formants(m, t, l, tip0)
        out[ph] = dict(mouth=m, tongue=t, lips=l, tip=tip0)
        print(f"  {ph}: MOUTH {m:.2f} TONGUE {t:.2f} LIPS {l:.2f} -> F1 {f[0]:.0f} ({tg[0]}) F2 {f[1]:.0f} ({tg[1]}) "
              f"F3 {f[2]:.0f} ({tg[2]})")
    return out


# the presets: mouth positions. Vowels fitted above; the rest set by hand from their articulation
def presets(v):
    def P(base=None, **k):
        d = dict(mouth=0.3, tongue=0.5, lips=0.0, voice=0.6, nasal=0.0, tip=0.0, noise=0.0, place=0.5, onset=None)
        if base:
            d.update({x: v[base][x] for x in ("mouth", "tongue", "lips", "tip")})
        d.update(k)
        return d
    return {
        # vowels
        "AH": P("AH"), "EE": P("EE"), "IH": P("IH"), "EH": P("EH"), "AE": P("AE"),
        "OO": P("OO"), "OH": P("OH"), "AW": P("AW"), "UH": P("UH"), "ER": P("ER"),
        # nasals: the mouth closed at the lips (M), tongue tip (N), tongue back (NG); the velum open
        "M": P(mouth=0.02, tongue=0.45, lips=0.9, nasal=1.0, voice=0.55),
        "N": P(mouth=0.05, tongue=0.75, lips=0.0, nasal=1.0, voice=0.55),
        "NG": P(mouth=0.08, tongue=0.25, lips=0.0, nasal=1.0, voice=0.55),
        # approximants
        "L": P(mouth=0.3, tongue=0.55, lips=0.0, tip=0.25, voice=0.6),
        "R": P(mouth=0.2, tongue=0.4, lips=0.55, tip=1.0, voice=0.6),
        "W": P(mouth=0.05, tongue=0.15, lips=1.0, voice=0.6),
        "Y": P(mouth=0.03, tongue=1.0, lips=0.0, voice=0.6),
        # fricatives: the hiss's place (0 lips .. 1 teeth/ridge, high), voiced or not
        "S": P(mouth=0.1, tongue=0.85, voice=0.0, noise=1.0, place=1.0),
        "SH": P(mouth=0.15, tongue=0.7, lips=0.6, voice=0.0, noise=1.0, place=0.6),
        "F": P(mouth=0.1, tongue=0.5, voice=0.0, noise=0.7, place=0.15),
        "Z": P(mouth=0.1, tongue=0.85, voice=0.45, noise=0.8, place=1.0),
        "V": P(mouth=0.1, tongue=0.5, voice=0.45, noise=0.5, place=0.15),
        "H": P("AH", mouth=0.45, voice=0.0, noise=0.0),   # a whispered vowel: the breath through the mouth
        # the hard consonants: an ONSET at the note's start, releasing into the knobs' vowel (AH here: "pa", "ba" ..)
        "P": P("AH", onset="P"), "B": P("AH", onset="B"), "T": P("AH", onset="T"), "D": P("AH", onset="D"),
        "K": P("AH", onset="K"), "G": P("AH", onset="G"), "CH": P("AH", onset="CH"), "J": P("AH", onset="J"),
    }


# ---------------------------------------------------------------- the sound
def reson(x, f, bw, zi=None):
    r = np.exp(-np.pi * bw / SR)
    a = [1, -2 * r * np.cos(2 * np.pi * f / SR), r * r]
    b = [sum(a)]                                     # unity gain at DC: a cascade of these is a natural vowel balance
    y, zf = signal.lfilter(b, a, x, zi=zi if zi is not None else np.zeros(2))
    return y, zf


# the hard consonants (Ed: "the presets would include our hard consonant sounds"): movements, not positions. An
# ONSET runs at the note's start: the seal (cl: silent, or a voice bar if voiced), the burst (a puff of noise shaped by
# where the seal was), the aspiration (unvoiced: the breath before the voice starts) or, for CH and J, a frication;
# meanwhile the mouth moves from the seal's place (its locus) to the knobs' vowel in ~50 ms. The knobs set the vowel
# it releases into: P with the knobs at AH says "pa", at EE "pee".
ONSET = {
    "P": dict(cl=0.055, burst=0.012, asp=0.045, voiced=False, bfc=900, bq=0.6, bamp=0.55, locus=dict(mouth=0.0, lips=1.0)),
    "B": dict(cl=0.050, burst=0.008, asp=0.006, voiced=True, bfc=800, bq=0.6, bamp=0.3, locus=dict(mouth=0.0, lips=1.0)),
    "T": dict(cl=0.050, burst=0.012, asp=0.055, voiced=False, bfc=4800, bq=1.2, bamp=0.9, locus=dict(mouth=0.08, tongue=0.95, lips=0.0)),
    "D": dict(cl=0.045, burst=0.008, asp=0.008, voiced=True, bfc=4200, bq=1.2, bamp=0.45, locus=dict(mouth=0.08, tongue=0.95, lips=0.0)),
    "K": dict(cl=0.055, burst=0.016, asp=0.065, voiced=False, bfc="f2", bq=3.0, bamp=0.9, locus=dict(mouth=0.05)),
    "G": dict(cl=0.050, burst=0.010, asp=0.010, voiced=True, bfc="f2", bq=3.0, bamp=0.45, locus=dict(mouth=0.05)),
    "CH": dict(cl=0.050, burst=0.006, asp=0.0, voiced=False, bfc=4000, bq=1.2, bamp=0.6, locus=dict(mouth=0.08, tongue=0.85, lips=0.4),
               fric=dict(dur=0.085, fc=3300, q=1.6, amp=1.0)),
    "J": dict(cl=0.045, burst=0.006, asp=0.0, voiced=True, bfc=3600, bq=1.2, bamp=0.35, locus=dict(mouth=0.08, tongue=0.85, lips=0.4),
              fric=dict(dur=0.06, fc=3000, q=1.6, amp=0.6)),
}
ONSET_MOVE = 0.05                                    # the mouth's move from the seal to the vowel (s)


def onset_state(p, F2):
    """where the onset is at p["ot"] seconds: the articulation, the voice's gain, the breath through the tract, and a
    noise (burst or frication) of its own: (art, vgain, asp, noise (fc, q, amp) or None)"""
    o, ot = ONSET[p["onset"]], p["ot"]
    tgt = dict(mouth=p["mouth"], tongue=p["tongue"], lips=p["lips"])
    loc = {**tgt, **o["locus"]}
    rel = o["cl"]
    k = min(1.0, max(0.0, (ot - rel) / ONSET_MOVE))
    art = {x: loc[x] + (tgt[x] - loc[x]) * k for x in tgt}
    fric = o.get("fric")
    fdur = fric["dur"] if fric else 0.0
    if ot < rel:                                     # the seal: nothing, or the voice bar behind the closed mouth
        return {**art, "mouth": 0.0}, (0.12 if o["voiced"] else 0.0), 0.0, None
    if ot < rel + o["burst"]:                        # the burst
        fc = min(3200, max(1400, F2 * 1.15)) if o["bfc"] == "f2" else o["bfc"]
        dec = 1.0 - 0.6 * (ot - rel) / o["burst"]
        return art, (0.35 if o["voiced"] else 0.0), 0.0, (fc, o["bq"], o["bamp"] * dec)
    if fric and ot < rel + o["burst"] + fdur:        # CH, J: released through a hiss
        return art, (0.6 if o["voiced"] else 0.0), 0.15, (fric["fc"], fric["q"], fric["amp"])
    if ot < rel + o["burst"] + fdur + o["asp"]:      # the aspiration: breath through the opening mouth, no voice yet
        return art, (0.0 if not o["voiced"] else 1.0), (0.0 if o["voiced"] else 0.9), None
    return art, 1.0, 0.0, None


def render(track, f0_track, dur, seed=1):
    """track(t) -> preset dict at time t (s); f0_track(t) -> Hz. Block-wise (64 samples), as the firmware will"""
    rng = np.random.default_rng(seed)
    n = int(dur * SR)
    out = np.zeros(n)
    blk = 64
    phase = 0.0
    zs = [np.zeros(2) for _ in range(4)]
    zn = [np.zeros(2) for _ in range(3)]
    zfr = np.zeros(2)
    zanti = np.zeros(2)
    zbu = np.zeros(2)
    gmute = 1.0
    gvoice = 1.0
    lp_tilt = 0.0
    vib = 0.0
    for s0 in range(0, n, blk):
        t = s0 / SR
        p = track(t)
        f0 = f0_track(t) * (1 + 0.006 * np.sin(2 * np.pi * 5.3 * t) * min(1, t / 0.4)) * (1 + 0.003 * rng.standard_normal())
        m = min(blk, n - s0)
        # the glottis: a Rosenberg pulse; VOICE sets how much of the cycle it is open (breathy: long, soft; pressed:
        # short, bright) and the aspiration noise mixed in
        vq = p["voice"]
        vgain, asp_x, onz = 1.0, 0.0, None
        if p.get("onset"):                           # a hard consonant running (onset_state)
            F_t, _ = formants(p["mouth"], p["tongue"], p["lips"], p["tip"])
            art, vgain, asp_x, onz = onset_state(p, F_t[1])
            p = {**p, **art}
        oq = 0.85 - 0.45 * vq
        ph = (phase + f0 / SR * np.arange(1, m + 1)) % 1.0
        phase = (phase + f0 / SR * m) % 1.0
        tp = 0.66 * oq
        g = np.where(ph < tp, 0.5 * (1 - np.cos(np.pi * ph / tp)),
                     np.where(ph < oq, np.cos(np.pi / 2 * (ph - tp) / (oq - tp)), 0.0))
        dg = np.diff(np.concatenate([[lp_tilt], g]))             # the flow's derivative: what the lips radiate
        lp_tilt = g[-1]
        # the voice's gain moves over ~8 ms (a closure is fast, not instant: an instant one clicks)
        vramp = gvoice + (vgain - gvoice) * np.minimum(1.0, np.arange(1, m + 1) / (0.008 * SR))
        gvoice = float(vramp[-1])
        voiced = dg * (min(1.0, vq / 0.25)) * 18 * vramp
        asp = rng.standard_normal(m) * (0.06 + 0.5 * (1 - vq) ** 2) * (0.4 + 0.6 * g if vq > 0.05 else 1.0)
        src = voiced + asp * (0.35 if vq > 0.05 else 0.9) * (1 - 0.9 * p["noise"]) * vramp   # (a fricative's air
                                                                                  #  goes through its gap, not the tract)
        if asp_x:                                    # an onset's aspiration: breath, the voice not yet on
            src = src + rng.standard_normal(m) * asp_x
        (F, BW) = formants(p["mouth"], p["tongue"], p["lips"], p["tip"])
        y = src
        # nasal: the murmur (a low resonance), the mouth branch damped, an anti-resonance
        nas = p["nasal"]
        for i in range(4):
            bw = BW[i] * (1 + 2.5 * nas * (i > 0))
            y, zs[i] = reson(y, F[i], bw, zs[i])
        if nas > 0:
            mur, zn[0] = reson(src, 260, 80, zn[0])
            mur, zn[1] = reson(mur, 2100, 300, zn[1])
            # an anti-resonance (a notch) at the place: M ~ 1000 Hz, N ~ 1700, NG ~ 3000
            fz = 1000 + 1700 * p["tongue"] * (1 - p["lips"])
            rz = np.exp(-np.pi * 150 / SR)
            bz = [1, -2 * rz * np.cos(2 * np.pi * fz / SR), rz * rz]
            mur_n, zanti = signal.lfilter(bz, [1, 0, 0], mur, zi=zanti)
            y = y * (1 - 0.85 * nas) + mur_n * 1.6 * nas
        # frication: noise at the constriction, shaped by its place (lips: flat and weak; teeth ridge: high and loud)
        if p["noise"] > 0:
            nz = rng.standard_normal(m) * p["noise"]
            # a true band-pass (RBJ, constant 0 dB peak): S high and narrow-ish, SH lower (rounding lowers it more),
            # F and V broad, low and weak
            fc = (1900 + 5200 * p["place"] ** 1.3) * (1 - 0.25 * p["lips"])
            q = 0.55 + 1.3 * p["place"]
            w0 = 2 * np.pi * fc / SR
            al = np.sin(w0) / (2 * q)
            bb = np.array([al, 0, -al]) / (1 + al)
            aa = np.array([1, -2 * np.cos(w0) / (1 + al), (1 - al) / (1 + al)])
            fr, zfr = signal.lfilter(bb, aa, nz, zi=zfr)
            amp = 0.25 + 0.9 * p["place"] ** 1.5
            if p["voice"] > 0.05:                                # voiced fricatives: the hiss pulses with the buzz
                fr = fr * (0.55 + 0.45 * g)
            y = y + fr * amp * 0.9
        if onz:                                      # an onset's burst or frication, at the seal (not through the tract)
            fc, q, amp = onz
            w0 = 2 * np.pi * fc / SR
            al = np.sin(w0) / (2 * q)
            bb = np.array([al, 0, -al]) / (1 + al)
            aa = np.array([1, -2 * np.cos(w0) / (1 + al), (1 - al) / (1 + al)])
            bz_, zbu = signal.lfilter(bb, aa, rng.standard_normal(m), zi=zbu)
            y = y + bz_ * amp * 0.9
        # a silence (or a coda's end): faded over ~15 ms (two pitch periods), never cut (a cut clicks)
        tgt_g = 0.0 if p.get("mute") else 1.0
        ramp = np.clip(gmute + np.sign(tgt_g - gmute) * np.arange(1, m + 1) / (0.015 * SR), 0, 1) \
            if tgt_g != gmute else np.full(m, gmute)
        if tgt_g < gmute:
            ramp = np.maximum(ramp, tgt_g)
        gmute = float(ramp[-1])
        y = y * ramp
        out[s0:s0 + m] = y
    out = signal.sosfilt(signal.butter(2, 60, "high", fs=SR, output="sos"), out)
    return out


def norm(x, db=-1.0):
    return x / (np.abs(x).max() or 1) * 10 ** (db / 20)


def env_note(x, a=0.03, r=0.12):
    n = len(x)
    e = np.ones(n)
    ai, ri = int(a * SR), int(r * SR)
    e[:ai] = np.linspace(0, 1, ai)
    e[-ri:] = np.linspace(1, 0, ri)
    return x * e


def write(path, x):
    y = np.clip(np.round(x * 32767), -32768, 32767).astype("<i2")
    with wave.open(str(path), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes(y.tobytes())


KNOBS = ("mouth", "tongue", "lips", "voice", "nasal", "tip", "noise", "place")


def lerp(a, b, k):
    return {**b, **{x: a[x] + (b[x] - a[x]) * k for x in KNOBS}}


def sung(pre, notes, glide=0.06):
    """notes: [(phoneme, midi, seconds)]: each phoneme held, gliding into the next; the pitch gliding too. A hard
    consonant (P B T D K G CH J) releases into the phoneme after it (its onset carries on through that one); before
    "_" (a silence) or at the end it is a coda: the seal and burst, then quiet. "_" is silence"""
    times = np.cumsum([0] + [d for _, _, d in notes])
    def ph_at(i):
        nm = notes[i][0]
        if nm == "_":
            j = i - 1
            while j >= 0 and notes[j][0] in ONSET:
                j -= 1
            return {**(pre[notes[j][0]] if j >= 0 else pre["AH"]), "mute": True, "onset": None}
        if nm in ONSET:                               # the mouth it releases into: the next phoneme's (or, a coda, the last)
            nx = notes[i + 1][0] if i + 1 < len(notes) else "_"
            if nx == "_" or nx in ONSET:
                j = i - 1
                while j >= 0 and (notes[j][0] in ONSET or notes[j][0] == "_"):
                    j -= 1
                return {**(pre[notes[j][0]] if j >= 0 else pre["AH"]), "onset": nm, "coda": True}
            return {**pre[nx], "onset": nm}
        return {**pre[nm], "onset": None}
    def at(t):
        i = min(np.searchsorted(times, t, side="right") - 1, len(notes) - 1)
        cur = ph_at(i)
        if cur.get("onset") and notes[i][0] in ONSET:
            cur = {**cur, "ot": t - times[i]}
            if cur.get("coda") and t - times[i] > ONSET[notes[i][0]]["cl"] + ONSET[notes[i][0]]["burst"] + 0.03:
                cur["mute"] = True
            return cur
        if i > 0 and notes[i - 1][0] in ONSET and not cur.get("mute"):   # the vowel after a consonant: the onset ends
            return {**cur, "onset": notes[i - 1][0], "ot": t - times[i - 1]}
        k = min(1.0, (t - times[i]) / glide) if i > 0 else 1.0
        prev = ph_at(i - 1) if i > 0 else cur
        if prev.get("mute") or cur.get("mute"):
            return cur
        return lerp(prev, cur, k)
    def f0(t):
        i = min(np.searchsorted(times, t, side="right") - 1, len(notes) - 1)
        hz = 440 * 2 ** ((notes[i][1] - 69) / 12)
        if i > 0:
            ph = 440 * 2 ** ((notes[i - 1][1] - 69) / 12)
            k = min(1.0, (t - times[i]) / (glide * 1.5))
            hz = ph * (hz / ph) ** k
        return hz
    return render(at, f0, times[-1] + 0.15)


def main():
    out = Path(sys.argv[1] if len(sys.argv) > 1 else "build/vox")
    (out / "wav").mkdir(parents=True, exist_ok=True)
    print("fitting the vowels:")
    v = fit_vowels()
    pre = presets(v)
    print("presets:")
    for i, (name, p) in enumerate(pre.items()):
        print(f"  {i + 1:02d} {name:3s} {('ONSET ' + p['onset']) if p['onset'] else '':9s}MOUTH {p['mouth']:.2f} TONGUE {p['tongue']:.2f} LIPS {p['lips']:.2f} "
              f"VOICE {p['voice']:.2f} NASAL {p['nasal']:.1f} TIP {p['tip']:.2f} NOISE {p['noise']:.1f} PLACE {p['place']:.2f}")
        x = render(lambda t, p=p: {**p, "ot": t} if p["onset"] else p, lambda t: 130.8, 1.3, seed=i + 3)  # C3
        write(out / "wav" / f"vox-{i + 1:02d}-{name.lower()}.wav", norm(env_note(x)))
    # the knobs moving: a sweep of each, from AH
    base = pre["AH"]
    for knob, a, b in (("mouth", 0.0, 1.0), ("tongue", 0.0, 1.0), ("lips", 0.0, 1.0), ("voice", 0.0, 1.0)):
        dur = 3.0
        x = render(lambda t, knob=knob, a=a, b=b: {**base, knob: a + (b - a) * t / dur}, lambda t: 130.8, dur)
        write(out / "wav" / f"sweep-{knob}.wav", norm(env_note(x)))
    # sung phrases from the presets alone (no plosives: those need the mouth to move fast, a later step)
    phrases = {
        "phrase-hello-i-see-you": [("H", 57, 0.12), ("EH", 57, 0.3), ("L", 59, 0.12), ("OH", 60, 0.55), ("AH", 62, 0.3),
                                   ("EE", 62, 0.25), ("S", 62, 0.18), ("EE", 64, 0.5), ("Y", 60, 0.12), ("OO", 60, 0.8)],
        "phrase-are-you-there": [("AH", 55, 0.35), ("R", 55, 0.15), ("Y", 57, 0.12), ("OO", 57, 0.45), ("H", 59, 0.1),
                                 ("EH", 59, 0.4), ("R", 59, 0.2), ("ER", 60, 0.6)],
        "phrase-numbers-station": [("N", 50, 0.12), ("AH", 50, 0.3), ("EE", 50, 0.2), ("N", 50, 0.25),
                                   ("F", 50, 0.15), ("AH", 50, 0.2), ("EE", 50, 0.15), ("V", 50, 0.25),
                                   ("S", 50, 0.15), ("EH", 50, 0.25), ("V", 50, 0.1), ("EH", 50, 0.15), ("N", 50, 0.3)],
    }
    phrases.update({
        "phrase-ground-control": [("G", 52, 0.08), ("R", 52, 0.08), ("AW", 52, 0.3), ("OO", 52, 0.12), ("N", 52, 0.12),
                                  ("D", 52, 0.08), ("_", 52, 0.08), ("K", 55, 0.09), ("AH", 55, 0.28), ("N", 55, 0.12),
                                  ("T", 57, 0.09), ("R", 57, 0.08), ("OH", 57, 0.55), ("L", 57, 0.25)],
        "phrase-beep-boop": [("B", 60, 0.07), ("EE", 72, 0.25), ("P", 72, 0.1), ("_", 72, 0.12), ("B", 60, 0.07),
                             ("OO", 60, 0.3), ("P", 60, 0.1), ("_", 60, 0.12), ("B", 67, 0.07), ("EE", 67, 0.12),
                             ("D", 67, 0.06), ("OO", 64, 0.3), ("P", 64, 0.1)],
        "phrase-count-to-ten": [("K", 50, 0.09), ("AH", 50, 0.15), ("OO", 50, 0.12), ("N", 50, 0.1), ("T", 50, 0.09),
                                ("_", 50, 0.1), ("T", 50, 0.09), ("OO", 50, 0.25), ("_", 50, 0.1), ("T", 50, 0.09),
                                ("EH", 50, 0.25), ("N", 50, 0.3), ("_", 50, 0.15), ("CH", 50, 0.09), ("EH", 50, 0.2),
                                ("K", 50, 0.1), ("_", 50, 0.1), ("J", 50, 0.08), ("AH", 50, 0.3), ("B", 50, 0.1)],
    })
    for name, notes in phrases.items():
        write(out / "wav" / f"{name}.wav", norm(sung(pre, notes)))
    print(f"wrote {out / 'wav'}")


if __name__ == "__main__":
    main()
