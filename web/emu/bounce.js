// SPDX-License-Identifier: GPL-3.0-only
// The studio's bounce, in a worker: the device cloned from the worklet's memory (felucca_web.c "the studio"), played
// from the top faster than real time with nothing on its screen, the mix and (asked for) one stem a FEED, as WAV files.
//   in:  {wasm: ArrayBuffer, mem: ArrayBuffer, song: bool, loops, tail (s), fade (s, 0 none), stems: bool, names: [NTRK by index]}
//   out: {type: "progress", done, of, what} ... {type: "done", files: [{name, data: ArrayBuffer}], seconds} | {type: "error"}
import { WASI_STUB } from "./worklet.js";

const BLOCK = 1024, QUIET = 1 / 8192;          // -78 dB: what counts as the music ringing out

let ex = null, memory = null, snapshot = null, rate = 44100;

function restore() {
  const need = snapshot.byteLength - memory.buffer.byteLength;
  if (need > 0) memory.grow(Math.ceil(need / 65536));
  new Uint8Array(memory.buffer).set(new Uint8Array(snapshot));
}

// frames out of the clone, appended to a growing pair of Float32Arrays
class Take {
  constructor() { this.l = new Float32Array(rate * 30); this.r = new Float32Array(rate * 30); this.n = 0; }
  push(n) {
    if (this.n + n > this.l.length) {
      const grow = (a) => { const b = new Float32Array(a.length * 2); b.set(a); return b; };
      this.l = grow(this.l); this.r = grow(this.r);
    }
    this.l.set(new Float32Array(memory.buffer, ex.web_out_l(), n), this.n);
    this.r.set(new Float32Array(memory.buffer, ex.web_out_r(), n), this.n);
    this.n += n;
  }
  peak(from) {
    let p = 0;
    for (let i = from; i < this.n; i++) p = Math.max(p, Math.abs(this.l[i]), Math.abs(this.r[i]));
    return p;
  }
}

// render n frames, thrown away, until a quarter second is quiet (or max frames): the live session's tails
function ringOut(max) {
  let quiet = 0;
  for (let done = 0; done < max && quiet < rate / 4; done += BLOCK) {
    ex.web_render(BLOCK);
    const l = new Float32Array(memory.buffer, ex.web_out_l(), BLOCK), r = new Float32Array(memory.buffer, ex.web_out_r(), BLOCK);
    let p = 0;
    for (let i = 0; i < BLOCK; i++) p = Math.max(p, Math.abs(l[i]), Math.abs(r[i]));
    quiet = p < QUIET ? quiet + BLOCK : 0;
  }
}

// one pass: the device restored, stopped, rung out, then PLAY from the top with `mute` left out
function pass(job, mute, progress) {
  restore();
  ex.web_bounce_stop();
  ringOut(rate * 8);
  const rc = ex.web_bounce_go(job.song ? 1 : 0, mute);
  if (rc) throw new Error(rc === 1 ? "GLO > SONG has no sections" : rc >= 3 ? `the song names FREQ ${"ABCDEF"[rc - 3]}, which is empty` : "the song could not start");
  const take = new Take();
  const loop = ex.web_pattern_frames();
  const length = job.song ? rate * 60 * 30 : Math.max(1, job.loops) * loop;   // a song: until it stops (30 min at most)
  let started = false;
  while (take.n < length) {
    ex.web_render(BLOCK);
    take.push(BLOCK);
    if (ex.web_playing()) started = true;
    else if (job.song && started) break;
    if ((take.n & 0x3FFFF) < BLOCK) progress(take.n / rate);
  }
  if (!job.song) take.n = Math.min(take.n, length);
  // the tail: up to `tail` seconds while it still sounds, the transport stopped (a pattern: at the loops' end)
  if (ex.web_playing()) ex.web_bounce_stop();
  const end = take.n, tail = Math.round(job.tail * rate);
  let quiet = 0;
  while (take.n - end < tail && quiet < rate / 2) {
    ex.web_render(BLOCK);
    const at = take.n;
    take.push(BLOCK);
    quiet = take.peak(at) < QUIET ? quiet + BLOCK : 0;
  }
  return take;
}

// FADE OUT (Nov 25): the last `frames` of the take down to silence (a cosine), so a looped pattern doesn't stop dead
export function fadeOut(take, frames) {
  const f = Math.min(frames, take.n), s = take.n - f;
  for (let i = 0; i < f; i++) {
    const g = 0.5 * (1 + Math.cos(Math.PI * (i + 1) / f));
    take.l[s + i] *= g; take.r[s + i] *= g;
  }
}

// a stereo WAV, 24-bit PCM
export function wav24(l, r, n, sr) {
  const buf = new ArrayBuffer(44 + n * 6), v = new DataView(buf);
  const str = (o, s) => { for (let i = 0; i < s.length; i++) v.setUint8(o + i, s.charCodeAt(i)); };
  str(0, "RIFF"); v.setUint32(4, 36 + n * 6, true); str(8, "WAVE");
  str(12, "fmt "); v.setUint32(16, 16, true); v.setUint16(20, 1, true); v.setUint16(22, 2, true);
  v.setUint32(24, sr, true); v.setUint32(28, sr * 6, true); v.setUint16(32, 6, true); v.setUint16(34, 24, true);
  str(36, "data"); v.setUint32(40, n * 6, true);
  const b = new Uint8Array(buf);
  for (let i = 0, o = 44; i < n; i++)
    for (const s of [l[i], r[i]]) {
      const x = Math.max(-8388608, Math.min(8388607, Math.round(s * 8388608)));
      b[o++] = x & 255; b[o++] = (x >> 8) & 255; b[o++] = (x >> 16) & 255;
    }
  return buf;
}

// a zip of files, stored (no compression: WAV hardly compresses)
const CRC = (() => { const t = new Uint32Array(256); for (let n = 0; n < 256; n++) { let c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320 ^ (c >>> 1) : c >>> 1; t[n] = c >>> 0; } return t; })();
function crc32(u8) { let c = 0xFFFFFFFF; for (let i = 0; i < u8.length; i++) c = CRC[(c ^ u8[i]) & 255] ^ (c >>> 8); return (c ^ 0xFFFFFFFF) >>> 0; }
export function zip(files) {
  const enc = new TextEncoder(), parts = [], central = [], now = new Date();
  const dt = (now.getHours() << 11) | (now.getMinutes() << 5) | (now.getSeconds() >> 1);
  const dd = ((now.getFullYear() - 1980) << 9) | ((now.getMonth() + 1) << 5) | now.getDate();
  let off = 0;
  for (const f of files) {
    const name = enc.encode(f.name), data = new Uint8Array(f.data), crc = crc32(data);
    const h = new DataView(new ArrayBuffer(30));
    h.setUint32(0, 0x04034b50, true); h.setUint16(4, 20, true); h.setUint16(8, 0, true); h.setUint16(10, dt, true); h.setUint16(12, dd, true);
    h.setUint32(14, crc, true); h.setUint32(18, data.length, true); h.setUint32(22, data.length, true);
    h.setUint16(26, name.length, true);
    parts.push(new Uint8Array(h.buffer), name, data);
    const c = new DataView(new ArrayBuffer(46));
    c.setUint32(0, 0x02014b50, true); c.setUint16(4, 20, true); c.setUint16(6, 20, true); c.setUint16(12, dt, true); c.setUint16(14, dd, true);
    c.setUint32(16, crc, true); c.setUint32(20, data.length, true); c.setUint32(24, data.length, true);
    c.setUint16(28, name.length, true); c.setUint32(42, off, true);
    central.push(new Uint8Array(c.buffer), name);
    off += 30 + name.length + data.length;
  }
  const size = central.reduce((s, a) => s + a.length, 0), e = new DataView(new ArrayBuffer(22));
  e.setUint32(0, 0x06054b50, true); e.setUint16(8, files.length, true); e.setUint16(10, files.length, true);
  e.setUint32(12, size, true); e.setUint32(16, off, true);
  return new Blob([...parts, ...central, new Uint8Array(e.buffer)], { type: "application/zip" });
}

// the whole job: the mix, then the stems asked for -> {files, seconds}; progress(done, of, what, seconds)
export async function bounceRun(job, progress = () => {}) {
  const { instance } = await WebAssembly.instantiate(job.wasm, WASI_STUB);
  ex = instance.exports;
  ex._initialize();
  memory = ex.memory;
  snapshot = job.mem;
  rate = ex.web_sample_rate();
  const ntrk = ex.web_ntrk(), all = (1 << ntrk) - 1;
  const order = [...Array(ntrk).keys()].sort((a, b) => ex.web_feed_pos(a) - ex.web_feed_pos(b));
  const runs = [{ what: "mix", mute: 0 }];
  if (job.stems) for (const k of order) runs.push({ what: job.names[k] || `FEED ${k + 1}`, mute: all & ~(1 << k), k });
  const files = [];
  let seconds = 0;
  for (let i = 0; i < runs.length; i++) {
    const run = runs[i];
    const take = pass(job, run.mute, (s) => progress(i, runs.length, run.what, s));
    if (job.fade > 0) fadeOut(take, Math.round(job.fade * rate));
    if (i === 0) seconds = take.n / rate;
    progress(i + 1, runs.length, run.what, take.n / rate);
    if (i > 0 && take.peak(0) < QUIET / 4) continue;          // a FEED that plays nothing: no stem
    const name = i === 0 ? `${job.base}.wav` : `${job.base}-${String(ex.web_feed_pos(run.k) + 1).padStart(2, "0")}-${run.what.replace(/[^A-Za-z0-9]+/g, "-").toLowerCase()}.wav`;
    files.push({ name, data: wav24(take.l, take.r, take.n, rate) });
  }
  return { files, seconds };
}

if (typeof WorkerGlobalScope !== "undefined" && self instanceof WorkerGlobalScope) {
  self.onmessage = async (e) => {
    try {
      const r = await bounceRun(e.data, (done, of, what, seconds) => self.postMessage({ type: "progress", done, of, what, seconds }));
      self.postMessage({ type: "done", ...r }, r.files.map((f) => f.data));
    } catch (err) {
      self.postMessage({ type: "error", message: String(err && err.message || err) });
    }
  };
}
