#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Phantom Cosmonaut 0.9.4: NUMBERS' word bank for the firmware (firmware/src/numbers_tables.h), from the prototype
(tools/numbers_proto.py: Flite's rms voice -> 8 kHz LPC-10, 25 ms frames; Ed: "YES, MAN! ... that creepy vocal vibe").

Each word a bitstream of frames, most significant bit first:
  energy 5 bits: 0 = a silent frame (nothing else); else
  repeat 1 bit:  1 = the previous frame's shape held: pitch 4 bits (signed, 1.5 semitone steps) follows, nothing else;
                 0 = voiced 1 bit, pitch 4 bits, K1..K10 (6 6 5 5 4 4 4 4 3 3 bits), voiced or not
(0.9.4, Ed: "I can't really hear what the voice is saying ... pretty muffled": the voicing is smoothed over five
frames (a median: Flite's frames flicker voiced / unvoiced inside a vowel), and an unvoiced frame keeps all ten
coefficients (it kept four: a flickering vowel lost its shape). Word accuracy by a speech recogniser, the 16 phrases:
0.63 -> 0.80; the bank 10.5 -> 13.2 KB.)
K: indices into an arcsine-even table per width (KQ3..KQ6, Q15), each coefficient's index Huffman-coded (canonical
codes per coefficient, NW_HN / NW_HS: the clearer bank of 0.9.4 in less room than the old one); energy: an amplitude
table (Q15 at full scale x
the bank's gain). Words cut 45 dB under their peak; the reference the firmware's engine is tested against: the same
decode in Python (decode() below).

  python3 tools/gen_numbers.py firmware/src/numbers_tables.h       (needs flite)
"""
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
import numbers_proto as P  # noqa: E402

KB = P.QBITS["k"]
PHRASES = [
    "I'M IN OUTER SPACE", "GROUND CONTROL COME IN", "CAN ANYONE HEAR ME", "THIS IS CAPSULE SEVEN OVER",
    "SIGNAL LOST", "OXYGEN FAILURE REPEAT OXYGEN FAILURE", "WE ARE STILL HERE", "IS ANYONE THERE",
    "MAYDAY MAYDAY MAYDAY", "I'M NOT ALONE", "GOODBYE EARTH", "DRIFTING FOREVER", "WHO IS CALLING",
    "STANDBY RECEIVING TRANSMISSION", "NINER FOUR SEVEN ZERO", "COLD DARK FAR FROM HOME",
]
PHRASE_NAMES = ["OUTER SPACE", "GROUND CTRL", "HEAR ME", "CAPSULE 7", "SIGNAL LOST", "OXYGEN", "STILL HERE",
                "ANYONE", "MAYDAY", "NOT ALONE", "GOODBYE", "DRIFTING", "WHO CALLS", "RECEIVING", "COUNTDOWN",
                "FAR HOME"]


def kq_index(k, bits):
    n = 2 ** bits
    th = np.arcsin(np.clip(k, -0.999, 0.999))
    return int(np.round((th / (np.pi / 2) + 1) / 2 * (n - 1)))


def kq_value(q, bits):
    n = 2 ** bits
    return np.sin((q / (n - 1) * 2 - 1) * np.pi / 2)


class Bits:
    def __init__(self):
        self.b = []

    def put(self, v, n):
        for i in range(n - 1, -1, -1):
            self.b.append((v >> i) & 1)

    def bytes(self):
        out = []
        for i in range(0, len(self.b), 8):
            chunk = self.b[i:i + 8] + [0] * (8 - len(self.b[i:i + 8]))
            out.append(int("".join(map(str, chunk)), 2))
        return out


def smooth_voicing(frames, n=5):
    """the voiced flags, a median over n frames (Flite's flicker inside a vowel)"""
    v = np.array([f[2] for f in frames]).astype(int)
    vv = [int(np.median(v[max(0, i - n // 2): i + n // 2 + 1])) for i in range(len(v))]
    return [(k, l, bool(vv[i]), f) for i, (k, l, _, f) in enumerate(frames)]


def huffman_lengths(freq, maxlen=15):
    """code lengths for a {symbol: count} (Huffman; flattened to maxlen if a code runs long)"""
    import heapq
    if len(freq) == 1:
        return {next(iter(freq)): 1}
    h = [(c, i, (s,)) for i, (s, c) in enumerate(sorted(freq.items()))]
    heapq.heapify(h)
    ln = {s: 0 for s in freq}
    n = len(h)
    while len(h) > 1:
        c1, _, a = heapq.heappop(h)
        c2, _, b = heapq.heappop(h)
        for s in a + b:
            ln[s] += 1
        heapq.heappush(h, (c1 + c2, n, a + b))
        n += 1
    while max(ln.values()) > maxlen:                     # (flatten: the rarest lengthened less)
        freq = {s: c + 1 for s, c in freq.items()}
        return huffman_lengths({s: c * 2 for s, c in freq.items()}, maxlen)
    return ln


def canonical(ln):
    """canonical codes: {symbol: (code, length)}, the symbols by (length, value)"""
    code, prev, out = 0, 0, {}
    for s in sorted(ln, key=lambda s: (ln[s], s)):
        code <<= ln[s] - prev
        out[s] = (code, ln[s])
        code += 1
        prev = ln[s]
    return out


HC = None                                                # the codes per coefficient, set by main()


FW = KB + [5, 4]                                         # the fields coded: K1..K10, the energy, the pitch movement


def put_k(b, i, q):
    if HC is None:
        b.put(q, FW[i])
    else:
        c, n = HC[i][q]
        b.put(c, n)


def k_indices(frames):
    """the fields of a word's frames as encode() writes them, for the codes' counts: [K1..K10] of a full frame, (10, e)
    every frame's energy, (11, pitch) every sounding frame's"""
    frames = smooth_voicing(frames)
    f0s = [f for _, _, v, f in frames if v and f > 0]
    med = np.median(f0s) if f0s else 100.0
    out, prev = [], None
    for k, loud, v, f in frames:
        e = int(np.clip(np.round((20 * np.log10(loud + 1e-9) + 80) / 3), 0, 31)) if loud > 0 else 0
        out.append((10, e))
        if e == 0:
            continue
        st = int(np.clip(np.round(12 * np.log2(f / med) / 1.5), -8, 7)) if v and f > 0 else 0
        out.append((11, st & 15))
        kq = [kq_index(k[i], KB[i]) for i in range(10)]
        if prev is not None and v == prev[1] and max(abs(kq_value(kq[i], KB[i]) - kq_value(prev[0][i], KB[i]))
                                                     for i in range(10)) < 0.08:
            continue
        out.append(kq)
        prev = (kq, v)
    return out


def field_sum(frames):
    """every field encode() writes, in order, as engines_test reads them back: sum of (n + 1) * value over the n-th"""
    tot, n = 0, 0
    frames = smooth_voicing(frames)
    f0s = [f for _, _, v, f in frames if v and f > 0]
    med = np.median(f0s) if f0s else 100.0
    prev = None
    for k, loud, v, f in frames:
        e = int(np.clip(np.round((20 * np.log10(loud + 1e-9) + 80) / 3), 0, 31)) if loud > 0 else 0
        vals = [e]
        if e:
            kq = [kq_index(k[i], KB[i]) for i in range(10)]
            st = int(np.clip(np.round(12 * np.log2(f / med) / 1.5), -8, 7)) if v and f > 0 else 0
            if prev is not None and v == prev[1] and max(abs(kq_value(kq[i], KB[i]) - kq_value(prev[0][i], KB[i]))
                                                         for i in range(10)) < 0.08:
                vals += [1, st & 15]
            else:
                vals += [0, 1 if v else 0, st & 15] + kq
                prev = (kq, v)
        for x in vals:
            n += 1
            tot += n * x
    return tot


def encode(frames):
    """the prototype's analysis -> a word's bits"""
    frames = smooth_voicing(frames)
    f0s = [f for _, _, v, f in frames if v and f > 0]
    med = np.median(f0s) if f0s else 100.0
    b = Bits()
    prev = None
    nfr = 0
    for k, loud, v, f in frames:
        e = int(np.clip(np.round((20 * np.log10(loud + 1e-9) + 80) / 3), 0, 31)) if loud > 0 else 0
        nfr += 1
        if e == 0:
            put_k(b, 10, 0)
            continue
        kq = [kq_index(k[i], KB[i]) for i in range(10)]
        st = int(np.clip(np.round(12 * np.log2(f / med) / 1.5), -8, 7)) if v and f > 0 else 0
        put_k(b, 10, e)
        if prev is not None and v == prev[1] and max(abs(kq_value(kq[i], KB[i]) - kq_value(prev[0][i], KB[i]))
                                                     for i in range(10)) < 0.08:
            b.put(1, 1)
            put_k(b, 11, st & 15)
            continue
        b.put(0, 1)
        b.put(1 if v else 0, 1)
        put_k(b, 11, st & 15)
        for i in range(10):
            put_k(b, i, kq[i])
        prev = (kq, v)
    return b, nfr


def abbreviations(names):
    """three letters a word, all different: the first three, else the first two and a later letter, else the first
    and two later ones"""
    out, used = [], set()
    for w in names:
        w2 = w.replace("'", "") if len(w) > 3 else w
        cons = [c for c in w2[1:] if c not in "AEIOU"]
        cands = [w[:3]] if len(w) <= 3 else []
        cands += [w2[:3]]
        if len(cons) >= 2:                               # (OUTER: OTR, STATION: STN, CONTACT: CNT, CONTROL: CNL)
            cands += [w2[0] + cons[0] + cons[-1]]
        cands += [w2[:2] + c for c in w2[2:]] + [w2[0] + a + b for i, a in enumerate(w2[1:]) for b in w2[i + 2:]]
        pick = next(c for c in cands if c not in used)
        used.add(pick)
        out.append(pick.ljust(3)[:3])
    return out


def main():
    out = Path(sys.argv[1] if len(sys.argv) > 1 else "firmware/src/numbers_tables.h")
    global HC
    stream, idx, names, fsum = [], [], [], []
    an = {w: P.analyse(P.trim(P.flite(P.SAY.get(w, w.lower())))) for w in P.WORDS}
    from collections import Counter
    cnt = [Counter() for _ in range(12)]
    for w in P.WORDS:
        for kq in k_indices(an[w]):
            if isinstance(kq, tuple):
                cnt[kq[0]][kq[1]] += 1
                continue
            for i in range(10):
                cnt[i][kq[i]] += 1
    HC = [canonical(huffman_lengths(dict(c))) for c in cnt]
    for w in P.WORDS:
        b, nfr = encode(an[w])
        bit0 = len(stream) * 8
        stream += b.bytes()
        idx.append((bit0, nfr))
        fsum.append(field_sum(an[w]))
        assert bit0 % 8 == 0 and bit0 // 8 < 65536
        names.append(w)
    word_of = {w: i for i, w in enumerate(P.WORDS)}
    phr = []
    for p in PHRASES:
        ws = [word_of[w] for w in p.split()]
        assert len(ws) <= 8, p
        phr.append(ws + [255] * (8 - len(ws)))
    L = []
    L.append("/* SPDX-License-Identifier: GPL-3.0-only")
    L.append(" * Phantom Cosmonaut 0.9.4: NUMBERS' word bank, generated by tools/gen_numbers.py (do not edit). The words are")
    L.append(" * Flite's (CMU, BSD-style licence) rms voice, coded as LPC-10 frames at 8 kHz, 25 ms each (eng_numbers.c) */")
    L.append(f"#define NW_N {len(names)}u")
    L.append(f"#define NW_BYTES {len(stream)}u")
    L.append("static const uint8_t NW_BITS[NW_BYTES] = {")
    for i in range(0, len(stream), 24):
        L.append("    " + ", ".join(str(v) for v in stream[i:i + 24]) + ",")
    L.append("};")
    L.append("/* each word: its first byte in NW_BITS (words start on a byte), its frames */")
    L.append("static const uint16_t NW_OFF[NW_N] = {")
    for i in range(0, len(idx), 16):
        L.append("    " + " ".join(f"{a // 8}," for a, n in idx[i:i + 16]))
    L.append("};")
    L.append("static const uint8_t NW_NF[NW_N] = {")
    for i in range(0, len(idx), 24):
        L.append("    " + " ".join(f"{n}," for a, n in idx[i:i + 24]))
    L.append("};")
    L.append("static const char *const NW_NAME[NW_N + 1] = {")
    for i in range(0, len(names), 10):
        L.append("    " + " ".join(f'"{n}",' for n in names[i:i + 10]))
    L.append("    0,")
    L.append("};")
    # the codes: per coefficient, how many of each length (1..15), then its symbols by (length, value)
    L.append("/* the K indices' canonical Huffman codes, per coefficient: NW_HN[i][l - 1] codes of length l, NW_HS[NW_HO[i]..]")
    L.append(" * their symbols in code order */")
    L.append("static const uint8_t NW_HN[12][15] = {   /* K1..K10, the energy, the pitch movement */")
    syms, offs = [], []
    for i in range(12):
        hist = [0] * 15
        for q, (c, n) in HC[i].items():
            hist[n - 1] += 1
        L.append("    {" + ", ".join(map(str, hist)) + "},")
        offs.append(len(syms))
        syms += sorted(HC[i], key=lambda q: (HC[i][q][1], q))
    L.append("};")
    L.append(f"static const uint16_t NW_HO[12] = {{{', '.join(map(str, offs))}}};")
    L.append(f"static const uint8_t NW_HS[{len(syms)}] = {{{', '.join(map(str, syms))}}};")
    for bits in (3, 4, 5, 6):
        vals = [int(np.round(kq_value(q, bits) * 32767)) for q in range(2 ** bits)]
        L.append(f"static const int16_t NW_KQ{bits}[{2 ** bits}] = {{{', '.join(map(str, vals))}}};")
    amps = [0] + [int(np.round(10 ** ((e * 3 - 80) / 20) * 32767 * 8)) for e in range(1, 32)]   # (x 8: the gain)
    L.append(f"static const uint32_t NW_AMP[32] = {{{', '.join(map(str, amps))}}};   /* 10^((3 e - 80) / 20) x 8, Q15 */")
    rat = [int(np.round(2 ** ((s * 1.5) / 12) * 65536)) for s in list(range(0, 8)) + list(range(-8, 0))]
    L.append(f"static const uint32_t NW_ST[16] = {{{', '.join(map(str, rat))}}};   /* 2^(1.5 s / 12), Q16; s as 4 bits */")
    # KIT (0.9.4, Ed: "more like a drum kit ... all possible words in 1 preset"): the words in the bank's order, 27 a
    # page (the keys), the pages by OCT; their names, and each word's three letters for the GRID
    pages = ["NUMBERS + RADIO", "SPACE + STATUS", "WHO + GLUE", "SENTENCE + MOOD"]
    L.append(f"#define NW_KPAGE 27u")
    L.append("static const char *const NW_PAGE_NAME[4] = {" + ", ".join(f'"{p}"' for p in pages) + "};")
    ab = abbreviations(names)
    ex = [(i, a) for i, a in enumerate(ab) if a != names[i][:3].ljust(3)[:3]]
    L.append("/* the GRID's: a word in three letters, its first three but for these */")
    L.append(f"#define NW_NABX {len(ex)}u")
    L.append("static const struct { uint8_t w; char a[3]; } NW_ABX[NW_NABX] = {")
    L.append("    " + " ".join("{%d, {'%s', '%s', '%s'}}," % (i, a[0], a[1], a[2]) for i, a in ex))
    L.append("};")
    L.append(f"#define NW_FSUM {sum(fsum) & 0xFFFFFFFF}u   /* (the fields as written, summed: engines_test decodes them) */")
    L.append(f"#define NW_NPHR {len(phr)}u")
    L.append("static const uint8_t NW_PHRASE[NW_NPHR][8] = {   /* the words of each (255: the end) */")
    for p, txt in zip(phr, PHRASES):
        L.append(f"    {{{', '.join(map(str, p))}}},   /* {txt} */")
    L.append("};")
    L.append("static const char *const NW_PHRASE_NAME[NW_NPHR + 1] = {")
    L.append("    " + " ".join(f'"{n}",' for n in PHRASE_NAMES) + " 0,")
    L.append("};")
    out.write_text("\n".join(L) + "\n")
    print(f"{out}: {len(names)} words, {len(stream)} bytes of frames, {len(phr)} phrases")


if __name__ == "__main__":
    main()
