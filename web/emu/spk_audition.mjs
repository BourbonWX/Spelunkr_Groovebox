// SPDX-License-Identifier: GPL-3.0-only
// The Spelunkr sounds, heard one by one in the built emulator (node): every sampled kit's eight rows and a beat,
// every SAMPLE preset across the keys. Checks each sounds and none clips; with OUTDIR, the WAVs to listen to.
//   node web/emu/spk_audition.mjs [build/emu/felucca.wasm] [OUTDIR]
import fs from "fs";
import { WASI_STUB, getSong, putSong, packSong, unpackSong } from "./worklet.js";

const wasmPath = process.argv[2] || new URL("../../build/emu/felucca.wasm", import.meta.url).pathname;
const out = process.argv[3];
const wasm = fs.readFileSync(wasmPath);
let fails = 0;
const check = (ok, what) => { console.log(`${ok ? "ok  " : "FAIL"} ${what}`); if (!ok) fails++; };
const KIT0 = 40, CKIT = 14, SAMPLE = 4;
const KITS = ["CRATE", "BEDROOM", "VAPOR 808", "LINN", "GATED", "NES", "GAMEBOY", "SNES"];
const ROWS = [["BD", 36], ["SD", 38], ["CP", 39], ["CH", 42], ["OH", 46], ["TM", 45], ["RS", 37], ["CB", 56]];
const PRESETS = ["LOFI PIANO", "MALL EP", "DREAM VIBES", "SMOOTH SAX", "GLASS PAD", "KALIMBA", "MUSIC BOX", "WORN TAPE",
  "MARIMBA", "UPRIGHT", "FM PIANO", "VIBES", "GLOCK", "NIGHT SAX", "8BIT KEYS", "VHS VIBES"];

async function device() {
  const { instance } = await WebAssembly.instantiate(wasm, WASI_STUB);
  const ex = instance.exports;
  ex._initialize();
  ex.web_nor_erase();
  ex.web_seed(7);
  ex.web_boot();
  for (let i = 0; i < 44100 * 1.5 / 128; i++) ex.web_render(128);      // past the splash
  for (let p = 0; p < ex.web_ntrk(); p++) ex.web_test_note(p, 255, 0, 0, 0);
  return ex;
}
// plays events [[t, part, note, vel]] for dur s -> {l, r} Float32Arrays
function play(ex, events, dur) {
  const n = Math.round(dur * 44100), l = new Float32Array(n), r = new Float32Array(n);
  const ev = [...events].sort((a, b) => a[0] - b[0]);
  let e = 0;
  for (let o = 0; o < n; o += 128) {
    while (e < ev.length && ev[e][0] * 44100 <= o) { const [, p, nt, v] = ev[e++]; ex.web_test_note(p, 255, 0, nt, v); }
    const k = Math.min(128, n - o);
    ex.web_render(k);
    l.set(new Float32Array(ex.memory.buffer, ex.web_out_l(), k), o);
    r.set(new Float32Array(ex.memory.buffer, ex.web_out_r(), k), o);
  }
  return { l, r };
}
const peak = ({ l, r }) => { let p = 0; for (let i = 0; i < l.length; i++) p = Math.max(p, Math.abs(l[i]), Math.abs(r[i])); return p; };
const rms = ({ l }) => { let s = 0; for (const v of l) s += v * v; return Math.sqrt(s / l.length); };
function wav(path, { l, r }) {
  const n = l.length, b = Buffer.alloc(44 + n * 4);
  b.write("RIFF", 0); b.writeUInt32LE(36 + n * 4, 4); b.write("WAVEfmt ", 8); b.writeUInt32LE(16, 16); b.writeUInt16LE(1, 20);
  b.writeUInt16LE(2, 22); b.writeUInt32LE(44100, 24); b.writeUInt32LE(44100 * 4, 28); b.writeUInt16LE(4, 32); b.writeUInt16LE(16, 34);
  b.write("data", 36); b.writeUInt32LE(n * 4, 40);
  for (let i = 0; i < n; i++) { b.writeInt16LE(Math.max(-32768, Math.min(32767, Math.round(l[i] * 32767))), 44 + 4 * i); b.writeInt16LE(Math.max(-32768, Math.min(32767, Math.round(r[i] * 32767))), 46 + 4 * i); }
  fs.writeFileSync(path, b);
}
if (out) fs.mkdirSync(out, { recursive: true });

// the kits: each row alone, then a beat
for (let k = 0; k < KITS.length; k++) {
  const ex = await device();
  ex.web_test_note(0, CKIT, KIT0 + k, 0, 0);
  const lv = ROWS.map(([, nt]) => { const a = play(ex, [[0, 0, nt, 100], [0.4, 0, nt, 0]], 0.9); return [peak(a), rms(a)]; });
  check(lv.every(([p]) => p > 0.02), `${KITS[k]}: every row sounds (${ROWS.map(([t], i) => `${t} ${lv[i][0].toFixed(2)}`).join(" ")})`);
  const s16 = 60 / 90 / 4, beat = [];
  const pat = ["BD CH", "CH", "CH", "BD CH", "SD CH", "CH", "BD OH", "", "CH", "BD CH", "CH", "CH RS", "SD CH CP", "CH", "TM", "CB"];
  for (let bar = 0; bar < 2; bar++) pat.forEach((st, i) => st.split(" ").filter(Boolean).forEach((r) => {
    const nt = ROWS.find(([t]) => t === r)[1], t0 = (bar * 16 + i) * s16;
    beat.push([t0, 0, nt, 100], [t0 + s16 * 0.9, 0, nt, 0]);
  }));
  const a = play(ex, beat, 32 * s16 + 1);
  check(peak(a) < 0.99, `${KITS[k]}: the beat does not clip (peak ${peak(a).toFixed(2)}, rms ${rms(a).toFixed(3)})`);
  if (out) wav(`${out}/kit-${String(k + 1).padStart(2, "0")}-${KITS[k].replace(/ /g, "-").toLowerCase()}.wav`, a);
}

// the SAMPLE presets: a phrase across the keys and a chord
for (let k = 0; k < PRESETS.length; k++) {
  const ex = await device();
  ex.web_test_note(1, SAMPLE, k, 0, 0);
  const ev = [], notes = [48, 55, 60, 63, 67, 72, 79];
  notes.forEach((nt, i) => ev.push([i * 0.3, 1, nt, 96], [i * 0.3 + 0.26, 1, nt, 0]));
  [51, 58, 62, 67].forEach((nt) => ev.push([2.4, 1, nt, 90], [4.4, 1, nt, 0]));
  const a = play(ex, ev, 6);
  check(peak(a) > 0.03 && peak(a) < 0.99, `${PRESETS[k]}: sounds, no clip (peak ${peak(a).toFixed(2)}, rms ${rms(a).toFixed(3)})`);
  if (out) wav(`${out}/sample-${String(k + 1).padStart(2, "0")}-${PRESETS[k].replace(/ /g, "-").toLowerCase()}.wav`, a);
}
// VAPOR on the master: SLOWED stretches the pattern, the four together stay clean numbers, a share link keeps them
{
  const ex = await device();
  const base = ex.web_pattern_frames();
  [127, 64, 90, 50].forEach((v, k) => ex.web_vapor(k, v));
  const slow = ex.web_pattern_frames();
  check(Math.abs(slow / base - 1 / 0.5) < 0.01, `SLOWED 127 (half speed, Nov 26): the pattern ${(base / 44100).toFixed(2)} s -> ${(slow / 44100).toFixed(2)} s`);
  ex.web_buttons(1 << 10); play(ex, [], 0.06); ex.web_buttons(0);
  const a = play(ex, [], 3);
  check(a.l.every(Number.isFinite) && peak(a) > 0.02 && peak(a) < 0.99, `the four at once: sounds, finite, no clip (peak ${peak(a).toFixed(2)})`);
  if (out) wav(`${out}/vapor-boot-song.wav`, a);
  ex.web_buttons(1 << 10); play(ex, [], 0.06); ex.web_buttons(0); play(ex, [], 0.5);
  ex.web_save_now();
  const link = await packSong(getSong(ex));
  const b2 = await (async () => { const { instance } = await WebAssembly.instantiate(wasm, WASI_STUB); const e = instance.exports; e._initialize(); e.web_nor_erase(); putSong(e, await unpackSong(link)); e.web_seed(7); e.web_boot(); for (let i = 0; i < 520; i++) e.web_render(128); return e; })();
  const got = [0, 1, 2, 3].map((k) => b2.web_vapor_get(k));
  check(got.join() === "127,64,90,50", `a share link brings VAPOR: ${got.join(" ")}`);
}
console.log(fails ? `${fails} FAILED` : "all ok");
process.exit(fails ? 1 : 0);
