#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
"""Pre-rasterise TTF fonts into anti-aliased 4-bit alpha glyph tables for the UI (run by
tools/build.py generate() with --preset inter-tight).

  gen_aa_font.py OUT.h --preset standin|inter-tight [--tracking EM] [--gamma G] [--kern-min N]
  gen_aa_font.py OUT.h --face NAME=FONT[#index]:PX:FIRST-LAST[+0xNN,...] ...   (explicit faces)

Faces of the presets (S labels, M values/headers, L big numerals / titles):
  standin     an installed Inter (~/Library/Fonts/Inter.ttc, else Inter in fonts/ next to tools/) with
              -2.5 % tracking: a stand-in for Inter Tight, for previews only.
  inter-tight assets/fonts/InterTight[wght].ttf (SIL OFL 1.1, Google Fonts) at weights 400 / 500 / 600.

Output (a C header; the glyph / font types live in the renderer, firmware/src/gfx.c):
  AF_<N>_DATA   4-bit alpha, trimmed to the ink box, rows back to back, each glyph a byte-aligned bit stream
                (MSB first) of canonical Huffman codes (AF_<N>_HC); a symbol is a value and a repeat count:
                a run of 1..16 zeros or 15s, or one value 1..14 (huff_pack; about 28 % less than 2 px per byte)
  AF_<N>_HC     the code: the number of codes of each length 1..15, then the symbols in code order
                ((count - 1) << 4 | value)
  AF_<N>_G      aag_t {off, adv (1/16 px), bx, by, bw, bh} per glyph and phase (PHASES of S M, PHASES_L of L:
                the glyph drawn at a pen of +0, +1/phases .. px, next to each other), ASCII 32..126, then the
                extras (Latin-1 by their code; the ellipsis U+2026 as code 0x85, as in Windows-1252)
  AF_<N>_KERN / _KD  kerning pairs (key = a<<8|b, sorted) and their deltas in 1/16 px (GPOS of the weight)
  AF_<N>        aafont_t (its last field: log2 of the phases)
Prints the byte cost of every face.
"""
import argparse
import heapq
import os
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import aa_raster as ar  # noqa: E402

FONTS = Path(__file__).resolve().parents[1] / "fonts"
LOCAL_INTER = os.path.expanduser("~/Library/Fonts/Inter.ttc")   # 4.001: 0 Regular, 10 Medium, 12 SemiBold
EXTRAS = [0xA9, 0xB0, 0xB7, 0xC4, 0xD6, 0xDC, 0xE4, 0xF6, 0xFC, 0x2026]  # (c) deg . A: O: U: a: o: u: (Latin-1), ...
CODE = {0x2026: 0x85, 0x25CF: 0x86}    # characters outside Latin-1 -> their one-byte code in the firmware
PIXEL_EXTRAS = EXTRAS + [0x25CF]       # Phantom Cosmonaut: the INGEST dot ("\x86" in a string)
PHASES, PHASES_L = 4, 2                # horizontal phases per glyph (aa_raster.raster_font): S M, L
# faces stored Huffman-coded (--huff): M and L. S, the most drawn and the least compressible (-15 %), stays
# 2 px per byte, so the labels draw at full speed
HUFF = [("M", "L")]
# L draws only "FELUCCA", the UPDATE MODE countdown digit, the calibration's control names (panel.c
# B_NAME / E_NAME) and MENU > LARGE's card values and page titles (ui_draw.c draw_column_tall, ui_graph.c
# graph_title: # . / J W too; a value with another character is set in M): a sparse face of those glyphs, the
# space as its range and the rest as extras
# (ui_test.c checks that every L string is covered)
L_CHARS = " #+-./0123456789ABCDEFGHIJKLMNOPQRSTUVWXY"


# Phantom Cosmonaut: DotGothic16 (SIL OFL 1.1, assets/fonts/DotGothic16-Latin1.ttf, Latin-1 + a few signs), a pixel
# font. Its dots are ~73 units of its 1000-unit em, and its outlines sit 28 units left of the origin and below the
# baseline: at 14 px a dot is ~1 px and at 28 px ~2 px, so M (14) and L (28) come out nearly crisp once every outline
# is moved by those 28 units onto the pixel grid (SHIFT); S (12, 6 px a character) is the one size off it, for the
# dense labels. Every advance is a whole number of pixels (6, 7, 14): one phase. Each face's line is Inter Tight's
# (ascent, descent), so the layout keeps its rows
PIXEL_METRICS = {12: (12, 3), 14: (15, 4), 28: (28, 7)}
PIXEL_OFFSET = 28                      # font units the outlines sit left of the origin and below the baseline


# the Spelunkr groovebox: VT323 (SIL OFL 1.1, assets/fonts/VT323-Regular.ttf), drawn at twice the size (SCALE): the
# web build's screen is 480 x 480 for the layout's 240 x 240 (gfx.c GFX_SCALE). VT323 is monospaced at 0.4 em, so
# S M L at 15 / 17.5 / 35 px (30 / 35 / 70 drawn) keep DotGothic16's advances (6, 7, 14 px) and the layout fits as it
# did; the lines are the layout's (PIXEL_METRICS) at twice the size. Glyph boxes are in drawn pixels, the rest in layout
# pixels (emit)
SCALE = [1]
VT_PX = {"S": (30, (24, 6)), "M": (35, (30, 8)), "L": (70, (56, 14))}


def preset(name):
    if name == "spelunkr":
        v = str(FONTS.parent / "assets" / "fonts" / "VT323-Regular.ttf")
        SCALE[0] = 2
        for nm, (px, m) in VT_PX.items():
            ar.METRICS[(v, px)] = m
        return [("S", v, VT_PX["S"][0], (32, 126), PIXEL_EXTRAS, 0.0, 1),
                ("M", v, VT_PX["M"][0], (32, 126), PIXEL_EXTRAS, 0.0, 1),
                ("L", v, VT_PX["L"][0], (32, 32), [ord(c) for c in L_CHARS[1:]], 0.0, 1)]
    if name == "phantom":
        v = str(FONTS.parent / "assets" / "fonts" / "DotGothic16-Latin1.ttf")
        for px, m in PIXEL_METRICS.items():
            ar.METRICS[(v, px)] = m
            sh = round(PIXEL_OFFSET * px * 16 / 1000)
            ar.SHIFT[(v, px)] = (-sh, -sh)
        return [("S", v, 12, (32, 126), PIXEL_EXTRAS, 0.0, 1),
                ("M", v, 14, (32, 126), PIXEL_EXTRAS, 0.0, 1),
                ("L", v, 28, (32, 32), [ord(c) for c in L_CHARS[1:]], 0.0, 1)]
    if name == "inter-tight":
        v = str(FONTS.parent / "assets" / "fonts" / "InterTight[wght].ttf")   # SIL OFL 1.1, Google Fonts
        return [("S", v + "@400", 12, (32, 126), EXTRAS, 0.0, PHASES),
                ("M", v + "@500", 15, (32, 126), EXTRAS, 0.0, PHASES),
                ("L", v + "@600", 28, (32, 32), [ord(c) for c in L_CHARS[1:]], 0.0, PHASES_L)]
    if name == "standin":
        if os.path.exists(LOCAL_INTER):
            reg, med, semi = LOCAL_INTER + "#0", LOCAL_INTER + "#10", LOCAL_INTER + "#12"
        else:                                  # the repo's Inter 3.019 (only Regular + Light)
            reg = med = semi = str(FONTS / "Inter-Regular.ttf")
        t = -0.025                             # Inter Tight is Inter with tighter spacing
        return [("S", reg, 12, (32, 126), EXTRAS, t, PHASES), ("M", med, 15, (32, 126), EXTRAS, t, PHASES),
                ("L", semi, 28, (32, 32), [ord(c) for c in L_CHARS[1:]], t, PHASES_L)]
    raise SystemExit(f"unknown preset {name}")


def parse_face(s):
    name, _, rest = s.partition("=")
    font, px, rng = rest.rsplit(":", 2)
    extras = []
    if "+" in rng:
        rng, ex = rng.split("+", 1)
        extras = [int(x, 0) for x in ex.split(",") if x]
    a, b = rng.split("-")
    return (name, font, int(px), (int(a), int(b)), extras, None, PHASES)


HC_MAXLEN = 15


def huff_symbols(v):
    """a glyph's alpha values -> symbols (count - 1) << 4 | value: runs of 1..16 zeros or 15s, single others"""
    out, i = [], 0
    while i < len(v):
        j = i + 1
        if v[i] in (0, 15):
            while j < len(v) and v[j] == v[i] and j - i < 16:
                j += 1
        out.append((j - i - 1) << 4 | v[i])
        i = j
    return out


def huff_lengths(count):
    """symbol -> code length (Huffman), at most HC_MAXLEN"""
    heap = [(c, k, (s,)) for k, (s, c) in enumerate(sorted(count.items()))]
    heapq.heapify(heap)
    length = {s: 0 for s in count}
    if len(heap) == 1:
        return {s: 1 for s in count}
    k = len(heap)
    while len(heap) > 1:
        a, b = heapq.heappop(heap), heapq.heappop(heap)
        for s in a[2] + b[2]:
            length[s] += 1
        k += 1
        heapq.heappush(heap, (a[0] + b[0], k, a[2] + b[2]))
    assert max(length.values()) <= HC_MAXLEN, "code too long: limit the lengths"
    return length


def huff_pack(glyph_values):
    """[alpha values per glyph] -> (hc table bytes, [bytes per glyph]): canonical Huffman, MSB first"""
    syms = [huff_symbols(v) for v in glyph_values]
    count = {}
    for g in syms:
        for s in g:
            count[s] = count.get(s, 0) + 1
    length = huff_lengths(count)
    order = sorted(count, key=lambda s: (length[s], s))
    code, c, prev = {}, 0, length[order[0]]
    for s in order:                                    # canonical: by length, then symbol
        c <<= length[s] - prev
        prev = length[s]
        code[s] = c
        c += 1
    hc = [sum(1 for s in order if length[s] == n) for n in range(1, HC_MAXLEN + 1)] + order
    out = []
    for g in syms:
        acc, nb, b = 0, 0, bytearray()
        for s in g:
            acc, nb = (acc << length[s]) | code[s], nb + length[s]
            while nb >= 8:
                nb -= 8
                b.append((acc >> nb) & 255)
        if nb:
            b.append((acc << (8 - nb)) & 255)
        out.append(bytes(b))
    return hc, out


def huff_unpack(hc, data, n):
    """the decoder (gfx.c cv_alpha_hc), for the self-check: n values from the bit stream data"""
    out, pos = [], 0
    while len(out) < n:
        code = first = idx = 0
        for ln in range(HC_MAXLEN):
            code |= (data[pos >> 3] >> (7 - (pos & 7))) & 1
            pos += 1
            if code - first < hc[ln]:
                break
            idx += hc[ln]
            first = (first + hc[ln]) << 1
            code <<= 1
        s = hc[HC_MAXLEN + idx + code - first]
        out += [s & 15] * ((s >> 4) + 1)
    assert len(out) == n
    return out


def emit(face, tracking, gamma, kern_min):
    name, font, px, (first, last), extras, ftrack, phases = face
    chars = [chr(c) for c in range(first, last + 1)] + [chr(c) for c in extras]
    r = ar.raster_font(font, px, chars, tracking if ftrack is None else ftrack, gamma, True, kern_min, phases)
    sc = SCALE[0]
    if sc > 1:                                         # the layout's pixels: line, baseline, advances, kerning
        r["h"], r["asc"] = (r["h"] + sc - 1) // sc, r["asc"] // sc
        r["glyphs"] = {c: (int(round(a / sc)), ph) for c, (a, ph) in r["glyphs"].items()}
        r["kern"] = {k: int(round(d / sc)) for k, d in r["kern"].items() if round(d / sc)}
    data, table, lines = bytearray(), [], []
    for ch in chars:
        adv16, ph = r["glyphs"][ch]
        off = len(data)
        for bx, by, bw, bh, b in ph:
            table.append((len(data), adv16, bx, by, bw, bh))
            data += b
        table += [(off,) + table[-1][1:]] * (phases - len(ph))   # one phase (aa_raster.FIXED): the same bitmap
    raw, hc = bytes(data), []
    if name in HUFF[0]:
        # Huffman-code every distinct bitmap (phases may share one), byte-aligned per glyph
        offs = sorted({e[0] for e in table if e[4]})
        vals = []
        for off in offs:
            e = next(t for t in table if t[0] == off and t[4])
            v = []
            for byte in raw[off:off + (e[4] * e[5] + 1) // 2]:
                v += [byte >> 4, byte & 15]
            vals.append(v[:e[4] * e[5]])
        hc, packed = huff_pack(vals)
        data, newoff = bytearray(), {}
        for off, v, b in zip(offs, vals, packed):
            assert huff_unpack(hc, b, len(v)) == v
            newoff[off] = len(data)
            data += b
        table = [(newoff.get(off, 0),) + t[1:] for t in table for off in (t[0],)]
        data += bytes(2)                               # gfx.c cv_alpha_hc reads up to 2 bytes ahead
    assert len(data) < 65536, "glyph data over 64 KiB: split the face"
    kern = sorted(r["kern"].items())
    keys = [(ord(a) << 8) | ord(b) for (a, b), _ in kern if ord(a) < 256 and ord(b) < 256]
    deltas = [d for (a, b), d in kern if ord(a) < 256 and ord(b) < 256]
    order = sorted(range(len(keys)), key=lambda i: keys[i])
    keys, deltas = [keys[i] for i in order], [deltas[i] for i in order]
    psh = phases.bit_length() - 1
    lines.append(f"/* {name}: {os.path.basename(font.split('#')[0])} {px}px, h {r['h']}, baseline {r['asc']}, "
                 f"{len(chars)} glyphs x {phases} phases, {len(data)} B data"
                 + (f" (Huffman; {len(raw)} B as nibbles) */" if hc else " */"))
    lines.append(ar.c_array(f"AF_{name}_DATA", "uint8_t", list(data), 24, "0x{:02x}"))
    if hc:
        lines.append(ar.c_array(f"AF_{name}_HC", "uint8_t", hc, 24))
    lines.append(f"static const aag_t AF_{name}_G[{len(table)}] = {{")
    for off, adv, bx, by, bw, bh in table:
        lines.append(f"    {{{off}, {adv}, {bx}, {by}, {bw}, {bh}}},")
    lines.append("};")
    if keys:
        lines.append(ar.c_array(f"AF_{name}_KERN", "uint16_t", keys, 12))
        lines.append(ar.c_array(f"AF_{name}_KD", "int8_t", deltas, 24))
    ex = ", ".join(str(CODE.get(c, c)) for c in extras) or "0"
    lines.append(f"static const uint8_t AF_{name}_EX[{max(1, len(extras))}] = {{{ex}}};")
    lines.append(f"static const aafont_t AF_{name} = {{{r['h']}, {r['asc']}, {first}, {last}, {len(extras)}, {len(keys)}, "
                 f"AF_{name}_G, AF_{name}_DATA, {'AF_%s_KERN' % name if keys else '0'}, "
                 f"{'AF_%s_KD' % name if keys else '0'}, AF_{name}_EX, {psh}, {'AF_%s_HC' % name if hc else '0'}}};")
    if "H" in chars:                                  # the capitals' band (gfx.c CAP_IN: centring text by it)
        cap = table[chars.index("H") * phases]
        sc = SCALE[0]
        cy, ch_ = cap[3] // sc, (cap[3] + cap[5] + sc - 1) // sc - cap[3] // sc
        lines.append(f"#define AF_{name}_CAP_Y {cy}   /* the ink of H: rows {cy} .. {cy + ch_ - 1} of the line (layout px) */")
        lines.append(f"#define AF_{name}_CAP_H {ch_}")
    cost = {"data": len(data) + len(hc), "glyph_table": 8 * len(table), "kern": 3 * len(keys), "extras": len(extras),
            "glyphs": len(chars), "phases": phases, "pairs": len(keys), "h": r["h"], "asc": r["asc"], "px": px,
            "font": os.path.basename(font.split("#")[0]), "adv_digit": r["glyphs"]["0"][0] / 16}
    cost["total"] = cost["data"] + cost["glyph_table"] + cost["kern"] + cost["extras"]
    return "\n".join(lines) + "\n", cost


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("out")
    ap.add_argument("--preset", choices=["standin", "inter-tight", "phantom", "spelunkr"])
    ap.add_argument("--face", action="append", default=[])
    ap.add_argument("--tracking", type=float, default=0.0, help="extra advance per glyph in em")
    ap.add_argument("--gamma", type=float, default=1.0, help="coverage curve; <1 thickens light-on-dark text")
    ap.add_argument("--kern-min", type=int, default=1, help="smallest kerning pair kept, in 1/16 px (0 pairs = no kerning)")
    ap.add_argument("--huff", default=",".join(HUFF[0]), help="faces stored Huffman-coded (comma list, '' = none)")
    a = ap.parse_args()
    HUFF[0] = tuple(x for x in a.huff.split(",") if x)
    faces = ([parse_face(s) for s in a.face] if a.face else []) or (preset(a.preset) if a.preset else None)
    if not faces:
        ap.error("--preset or --face is required")
    out = ["/* generated by tools/gen_aa_font.py: 4-bit alpha glyphs (Inter Tight, SIL OFL 1.1, "
           "Copyright 2022 The Inter Project Authors; see assets/fonts/OFL.txt) */",
           "#pragma once", "#include <stdint.h>", ""]
    if SCALE[0] > 1 or (a.preset == "spelunkr"):
        pass
    out.append(f"#define AF_SCALE {SCALE[0]}   /* glyph bitmaps drawn at this many pixels a layout pixel (gfx.c GFX_SCALE) */\n")
    total = 0
    for f in faces:
        text, c = emit(f, a.tracking, a.gamma, a.kern_min)
        out.append(text)
        total += c["total"]
        print(f"face {f[0]}: {c['font']} {c['px']}px h{c['h']} base{c['asc']}  {c['glyphs']} glyphs x {c['phases']}  "
              f"data {c['data']} B + table {c['glyph_table']} B + kern {c['kern']} B ({c['pairs']} pairs) "
              f"= {c['total']} B   digit adv {c['adv_digit']:.2f}px")
    Path(a.out).write_text("\n".join(out))
    print(f"fonts total {total} B  -> {a.out}")


if __name__ == "__main__":
    main()
