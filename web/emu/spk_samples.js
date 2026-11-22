// SPDX-License-Identifier: GPL-3.0-only
// The Spelunkr groovebox's user samples (Ed, Nov 22 2026): made in the page (the SAMPLES panel), played by the
// device (felucca_web.c web_spk_*, core.h SPK_NU). A sample is mono, 22.05 kHz: the whole (trimmed) and its slices,
// each encoded IMA ADPCM from a fresh state. Kept in the page: the browser's storage (IndexedDB) and the project file.
// The ADPCM is the FM-1 editor's (web/editor.html imaEncode, a port of tools/sampleio.py).

export const RATE = 22050, NSLOT = 8, NSLICE = 24, MAX_SECONDS = 30, CHOP_KEY = 48;

const IMA_STEP = [7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88,
  97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658,
  724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660,
  4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818,
  18500, 20350, 22385, 24623, 27086, 29794, 32767];
const IMA_IDX = [-1, -1, -1, -1, 2, 4, 6, 8];

/* Int16 samples -> IMA ADPCM bytes, 4 bit, low nibble first, from predictor 0 / index 0 */
export function imaEncode(s) {
  let pred = 0, idx = 0;
  const out = new Uint8Array((s.length + 1) >> 1);
  for (let n = 0; n < s.length; n++) {
    const step = IMA_STEP[idx];
    let diff = s[n] - pred, code = 0;
    if (diff < 0) { code = 8; diff = -diff; }
    let vd = step >> 3;
    if (diff >= step) { code |= 4; diff -= step; vd += step; }
    if (diff >= step >> 1) { code |= 2; diff -= step >> 1; vd += step >> 1; }
    if (diff >= step >> 2) { code |= 1; vd += step >> 2; }
    pred = Math.max(-32768, Math.min(32767, code & 8 ? pred - vd : pred + vd));
    idx = Math.max(0, Math.min(88, idx + IMA_IDX[code & 7]));
    out[n >> 1] |= n & 1 ? code << 4 : code;
  }
  return out;
}

/* Float32 (-1..1) -> Int16, a short fade at both ends (no click where a slice was cut) */
export function toInt16(x, fade = 32) {
  const n = x.length, out = new Int16Array(n), f = Math.min(fade, n >> 1);
  for (let i = 0; i < n; i++) {
    let g = 1;
    if (i < f) g = i / f;
    else if (i >= n - f) g = (n - 1 - i) / f;
    out[i] = Math.max(-32768, Math.min(32767, Math.round(x[i] * g * 32767)));
  }
  return out;
}

/* a sample: pcm (Float32 at RATE, already trimmed), slice starts (sample indexes into pcm, ascending, the first 0
 * or later; each runs to the next or the end), root (MIDI note) -> {data, meta} for web_spk_* */
export function buildSlot(pcm, starts, root) {
  const s = [...new Set(starts.map((v) => Math.max(0, Math.min(pcm.length - 2, Math.round(v)))))].sort((a, b) => a - b).slice(0, NSLICE);
  const blocks = [imaEncode(toInt16(pcm, 64))], meta = [pcm.length, Math.round(root * 16), s.length, 0];
  let off = blocks[0].length + (blocks[0].length & 1);
  for (let i = 0; i < s.length; i++) {
    const end = i + 1 < s.length ? s[i + 1] : pcm.length, b = imaEncode(toInt16(pcm.subarray(s[i], end)));
    meta.push(off, end - s[i]);
    blocks.push(b);
    off += b.length + (b.length & 1);
  }
  const data = new Uint8Array(off);
  let o = 0;
  for (const b of blocks) { data.set(b, o); o += b.length + (b.length & 1); }
  return { data, meta };
}

/* slot k of the device (the wasm's exports) <- a built slot and a name; 0 ok, 1 refused (too long) */
export function putSlot(ex, k, built, name) {
  ex.web_spk_clear(k);
  if (!built) return 0;
  if (built.data.length > ex.web_spk_cap()) return 1;
  new Uint8Array(ex.memory.buffer, ex.web_spk_data(k), built.data.length).set(built.data);
  new Uint32Array(ex.memory.buffer, ex.web_spk_meta(), built.meta.length).set(built.meta);
  const nm = new TextEncoder().encode(String(name || "").toUpperCase().replace(/[^\x20-\x7E]/g, "").slice(0, 8) + "\0");
  new Uint8Array(ex.memory.buffer, ex.web_spk_name(), 16).set(nm.subarray(0, 16));
  return ex.web_spk_commit(k);
}

/* slice starts: n equal parts of len samples */
export function sliceEqual(len, n) { return Array.from({ length: n }, (_, i) => Math.round(i * len / n)); }

/* slice starts at the hits: where the short-term energy jumps over the slower one (an onset), at least minGap
 * seconds apart, the strongest up to max; always one at 0 */
export function sliceTransients(pcm, max = 16, minGap = 0.08) {
  const hop = 256, nh = Math.floor(pcm.length / hop), e = new Float32Array(nh);
  for (let h = 0; h < nh; h++) { let s = 0; for (let i = h * hop; i < (h + 1) * hop; i++) s += pcm[i] * pcm[i]; e[h] = Math.sqrt(s / hop); }
  const cand = [], emax = e.reduce((m, v) => Math.max(m, v), 0) || 1;
  let slow = e[0] || 0;
  for (let h = 1; h < nh; h++) {                  // (a rise of 8 % of the loudest, a third over the recent level)
    const rise = e[h] - e[h - 1];
    if (rise > 0.08 * emax && e[h] > slow * 1.3) cand.push([rise, h * hop]);
    slow = slow * 0.85 + e[h] * 0.15;
  }
  cand.sort((a, b) => b[0] - a[0]);
  const gap = minGap * RATE, picked = [0];
  for (const [, at] of cand) {
    if (picked.length >= max) break;
    if (picked.every((p) => Math.abs(p - at) >= gap)) picked.push(Math.max(0, at - 64));   // (a little before the hit)
  }
  return picked.sort((a, b) => a - b);
}

/* where the sound is: from a little before the first sample over rel of the peak to the last one */
export function autoTrim(pcm, rel = 0.02) {
  let pk = 0;
  for (const v of pcm) pk = Math.max(pk, Math.abs(v));
  const th = pk * rel;
  let a = 0, b = pcm.length;
  while (a < b && Math.abs(pcm[a]) < th) a++;
  while (b > a && Math.abs(pcm[b - 1]) < th) b--;
  return [Math.max(0, a - Math.round(0.005 * RATE)), Math.min(pcm.length, b + Math.round(0.02 * RATE))];
}

/* the root from a file name ("piano_C4", "F#3"; C4 = 60), else 60 */
export function rootFromName(name) {
  const m = /(?:^|[^A-Za-z])([A-Ga-g])([#b]?)(-?\d)(?!\d)/.exec(String(name).replace(/\.[^.]*$/, ""));
  if (!m) return 60;
  const NOTE = { C: 0, D: 2, E: 4, F: 5, G: 7, A: 9, B: 11 };
  return Math.max(0, Math.min(127, 12 * (+m[3] + 1) + NOTE[m[1].toUpperCase()] + (m[2] === "#" ? 1 : m[2] === "b" ? -1 : 0)));
}

/* the stored form (IndexedDB, the project file): {name, root, starts, pcm (Int16 at RATE, base64 in JSON)} */
export function toStored(s) {
  return { name: s.name, root: s.root, starts: s.starts.slice(), pcm16: toInt16(s.pcm, 0) };
}
export function fromStored(o) {
  const p16 = o.pcm16 instanceof Int16Array ? o.pcm16 : new Int16Array(o.pcm16), pcm = new Float32Array(p16.length);
  for (let i = 0; i < p16.length; i++) pcm[i] = p16[i] / 32768;
  return { name: String(o.name || ""), root: o.root ?? 60, starts: Array.isArray(o.starts) ? o.starts.slice() : [0], pcm };
}
export function b64of(u8) { let s = ""; for (let i = 0; i < u8.length; i += 4096) s += String.fromCharCode.apply(null, u8.subarray(i, i + 4096)); return btoa(s); }
export function u8of(b64) { return Uint8Array.from(atob(b64), (c) => c.charCodeAt(0)); }
