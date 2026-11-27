// SPDX-License-Identifier: GPL-3.0-only
// The Spelunkr groovebox's user samples (Ed, Nov 22 2026): made in the page (the SAMPLES panel), played by the
// device (felucca_web.c web_spk_*, core.h SPK_NU). A sample is mono, 22.05 kHz: the whole (trimmed) and its slices,
// each encoded IMA ADPCM from a fresh state. Kept in the page: the browser's storage (IndexedDB) and the project file.
// The ADPCM is the FM-1 editor's (web/editor.html imaEncode, a port of tools/sampleio.py).

export const RATE = 22050, NSLOT = 8, NSLICE = 12, MAX_SECONDS = 30, CHOP_KEY = 48;
// (Nov 27 2026, Ed: "keep chopped samples to just 12 per track and then also apply end marks to chops": a CHOP is a
// start and an end of its own, gaps allowed; 12 a sample, one octave, C3..B3; a track's BANK B, another sample's 12, on
// C4..B4: eng_sample.c. The device still takes up to 24, so a slot made before plays as it did until it's edited)

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

/* starts (ascending; each runs to the next or the end) -> chops [[start, end], ..] (the form before end marks) */
export function chopsOfStarts(starts, len) {
  const s = [...new Set(starts.map((v) => Math.max(0, Math.min(len - 2, Math.round(v)))))].sort((a, b) => a - b);
  return s.map((a, i) => [a, i + 1 < s.length ? s[i + 1] : len]);
}
/* chops as played: in order of their starts, each inside 0..len and at least 32 samples long, NSLICE at most */
export function chopsClean(chops, len) {
  return chops.map(([a, b]) => [Math.max(0, Math.min(len - 32, Math.round(a))), Math.max(0, Math.min(len, Math.round(b)))])
    .filter(([a, b]) => b - a >= 32).sort((x, y) => x[0] - y[0]).slice(0, NSLICE);
}
/* a sample: pcm (Float32 at RATE, already trimmed), its chops ([start, end] sample indexes into pcm; plain starts, as
 * before end marks, run each to the next), root (MIDI note) -> {data, meta} for web_spk_* */
export function buildSlot(pcm, chops, root) {
  const c = chopsClean(chops.length && typeof chops[0] === "number" ? chopsOfStarts(chops, pcm.length) : chops, pcm.length);
  const blocks = [imaEncode(toInt16(pcm, 64))], meta = [pcm.length, Math.round(root * 16), c.length, 0];
  let off = blocks[0].length + (blocks[0].length & 1);
  for (let i = 0; i < c.length; i++) {
    const [a, end] = c[i], b = imaEncode(toInt16(pcm.subarray(a, end)));
    meta.push(off, end - a);
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
/* chops: n equal parts of len samples, end to end */
export function chopEqual(len, n) { return chopsOfStarts(sliceEqual(len, n), len); }
/* chops at the hits (sliceTransients), each ending where its sound dies away (its level under a tenth of its own peak
 * for 30 ms: the tail it rings with kept), or at the next hit */
export function chopHits(pcm, max = NSLICE) {
  const st = sliceTransients(pcm, max), hop = 128, quiet = Math.round(0.03 * RATE / hop);
  return st.map((a, i) => {
    const lim = i + 1 < st.length ? st[i + 1] : pcm.length;
    let pk = 0, end = lim, run = 0;
    for (let h = a; h + hop <= lim; h += hop) {
      let e = 0;
      for (let j = h; j < h + hop; j++) e = Math.max(e, Math.abs(pcm[j]));
      pk = Math.max(pk, e);
      if (h - a > 0.05 * RATE && e < pk * 0.1) { if (++run >= quiet) { end = Math.min(lim, h + hop); break; } } else run = 0;
    }
    return [a, end];
  });
}

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

/* the stored form (IndexedDB, the project file): {name, root, chops, starts (the chops' starts, for a page before end
 * marks), pcm (Int16 at RATE, base64 in JSON)}; read back: chops null in one stored before end marks (the page makes them) */
export function toStored(s) {
  return { name: s.name, root: s.root, chops: s.chops.map((c) => c.slice()), starts: s.chops.map((c) => c[0]), pcm16: toInt16(s.pcm, 0) };
}
export function fromStored(o) {
  const p16 = o.pcm16 instanceof Int16Array ? o.pcm16 : new Int16Array(o.pcm16), pcm = new Float32Array(p16.length);
  for (let i = 0; i < p16.length; i++) pcm[i] = p16[i] / 32768;
  return { name: String(o.name || ""), root: o.root ?? 60, starts: Array.isArray(o.starts) ? o.starts.slice() : [0],
           chops: Array.isArray(o.chops) ? o.chops.filter((c) => Array.isArray(c) && c.length === 2).map((c) => [+c[0], +c[1]]) : null, pcm };
}
export function b64of(u8) { let s = ""; for (let i = 0; i < u8.length; i += 4096) s += String.fromCharCode.apply(null, u8.subarray(i, i + 4096)); return btoa(s); }
export function u8of(b64) { return Uint8Array.from(atob(b64), (c) => c.charCodeAt(0)); }

/* ---- WAV files read here, not by the browser (Ed, Nov 22: a 100 MB+ file came out as static): a browser decodes the
 * whole file before the first 30 seconds are taken (seconds of waiting, hundreds of MB), and some WAVs it gets wrong
 * (64-bit float: Firefox plays noise, Chrome refuses it). Here only the header and the first maxSec seconds are read
 * (File.slice), any PCM (8, 16, 24, 32-bit) or float (32, 64-bit), plain, WAVE_FORMAT_EXTENSIBLE or RF64; mixed to
 * mono. Returns { rate, frames (Float32Array, mono), seconds (the whole file's) } or throws Error(what's wrong). */
const ascii = (u8, o, n) => String.fromCharCode(...u8.subarray(o, o + n));
export async function readWav(file, maxSec = MAX_SECONDS) {
  let head = new Uint8Array(await file.slice(0, Math.min(file.size, 1 << 16)).arrayBuffer());
  const riff = ascii(head, 0, 4);
  if (head.length < 12 || (riff !== "RIFF" && riff !== "RF64") || ascii(head, 8, 4) !== "WAVE") throw new Error("not a WAV file");
  let fmt = null, dataAt = -1, dataLen = 0, ds64 = 0;
  for (let o = 12; ;) {
    if (o + 8 > head.length) {                                   // (a big chunk before the data: read further)
      if (o + 8 > file.size || head.length >= 1 << 24) break;
      head = new Uint8Array(await file.slice(0, Math.min(file.size, Math.max(o + 4096, head.length * 4))).arrayBuffer());
      continue;
    }
    const dv = new DataView(head.buffer), id = ascii(head, o, 4), sz = dv.getUint32(o + 4, true);
    if (id === "ds64" && o + 8 + 16 <= head.length) ds64 = dv.getUint32(o + 8 + 8, true) + dv.getUint32(o + 8 + 12, true) * 2 ** 32;
    if (id === "fmt " && o + 8 + 16 <= head.length) {
      let tag = dv.getUint16(o + 8, true);
      const bits = dv.getUint16(o + 22, true);
      if (tag === 0xFFFE && sz >= 40 && o + 8 + 26 <= head.length) tag = dv.getUint16(o + 8 + 24, true);   // (the subformat)
      fmt = { tag, ch: dv.getUint16(o + 10, true), rate: dv.getUint32(o + 12, true), align: dv.getUint16(o + 20, true), bits };
    }
    if (id === "data") {
      dataAt = o + 8;
      dataLen = sz === 0xFFFFFFFF && ds64 ? ds64 : sz;
      if (!dataLen || dataAt + dataLen > file.size) dataLen = file.size - dataAt;   // (a size left 0 or too big: to the end)
      break;
    }
    o += 8 + sz + (sz & 1);
  }
  if (!fmt) throw new Error("its header has no format");
  if (dataAt < 0) throw new Error("it has no sound in it");
  const { tag, ch, rate, bits } = fmt, bps = bits >> 3, align = fmt.align || ch * bps;
  const ok = (tag === 1 && [8, 16, 24, 32].includes(bits)) || (tag === 3 && (bits === 32 || bits === 64));
  if (!ok || !ch || !rate || rate < 1000 || rate > 768000) throw new Error(`its kind of WAV (format ${tag}, ${bits}-bit) can't be read: save it as a 16 or 24-bit WAV`);
  const total = Math.floor(dataLen / align), n = Math.min(total, Math.ceil(rate * maxSec));
  const raw = new Uint8Array(await file.slice(dataAt, dataAt + n * align).arrayBuffer()), dv = new DataView(raw.buffer);
  const frames = new Float32Array(Math.floor(raw.length / align)), g = 1 / ch;
  for (let i = 0; i < frames.length; i++) {
    let sum = 0;
    for (let c = 0, o = i * align; c < ch; c++, o += bps) {
      let v;
      if (tag === 3) v = bits === 32 ? dv.getFloat32(o, true) : dv.getFloat64(o, true);
      else if (bits === 8) v = (raw[o] - 128) / 128;
      else if (bits === 16) v = dv.getInt16(o, true) / 32768;
      else if (bits === 24) v = ((raw[o] | raw[o + 1] << 8 | raw[o + 2] << 16) << 8 >> 8) / 8388608;
      else v = dv.getInt32(o, true) / 2147483648;
      sum += Number.isFinite(v) ? v : 0;
    }
    frames[i] = sum * g;
  }
  return { rate, frames, seconds: total / rate };
}
/* mono frames at `rate` -> RATE: the browser's own resampler (an OfflineAudioContext), else linear */
export async function resample(frames, rate) {
  if (rate === RATE || !frames.length) return frames.slice();
  const n = Math.max(1, Math.round(frames.length * RATE / rate));
  try {
    const off = new OfflineAudioContext(1, n, RATE), buf = off.createBuffer(1, frames.length, rate);
    buf.copyToChannel(frames, 0);
    const src = off.createBufferSource();
    src.buffer = buf; src.connect(off.destination); src.start();
    return (await off.startRendering()).getChannelData(0).slice();
  } catch {
    const out = new Float32Array(n), k = rate / RATE;
    for (let i = 0; i < n; i++) { const x = i * k, j = Math.floor(x), f = x - j; out[i] = (frames[j] || 0) * (1 - f) + (frames[j + 1] ?? frames[j] ?? 0) * f; }
    return out;
  }
}
/* the share of samples that aren't numbers, and those set to 0 (a file the browser got wrong) */
export function cleanPcm(pcm) {
  let bad = 0;
  for (let i = 0; i < pcm.length; i++) if (!Number.isFinite(pcm[i])) { pcm[i] = 0; bad++; }
  return pcm.length ? bad / pcm.length : 0;
}
