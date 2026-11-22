// SPDX-License-Identifier: GPL-3.0-only
// The studio's checks (index.html's Record / Export song / project files), on the built emulator in node:
//   node web/emu/studio_test.mjs [build/emu/felucca.wasm] [OUTDIR: the bounces as WAV]
import fs from "fs";
import { WASI_STUB, storeSectors, restoreSectors, getSong, putSong, packSong, unpackSong } from "./worklet.js";
import { bounceRun, zip } from "./bounce.js";

const wasmPath = process.argv[2] || new URL("../../build/emu/felucca.wasm", import.meta.url).pathname;
const out = process.argv[3];
const wasm = fs.readFileSync(wasmPath);
let fails = 0;
const check = (ok, what) => { console.log(`${ok ? "ok  " : "FAIL"} ${what}`); if (!ok) fails++; };

async function device(sectors, song, seed = 4242) {
  const { instance } = await WebAssembly.instantiate(wasm, WASI_STUB);
  const ex = instance.exports;
  ex._initialize();
  ex.web_nor_erase();
  if (sectors) restoreSectors(new Uint8Array(ex.memory.buffer, ex.web_nor(), ex.web_nor_size()), sectors);
  if (song) putSong(ex, song);
  ex.web_seed(seed);
  ex.web_boot();
  return ex;
}
const run = (ex, s) => { for (let i = 0; i < Math.round(44100 * s / 128); i++) ex.web_render(128); };
const press = (ex, bit) => { ex.web_buttons(1 << bit); run(ex, 0.06); ex.web_buttons(0); run(ex, 0.06); };
const PLAY = 10;
const peakOf = (buf) => { const b = new Uint8Array(buf); let p = 0; for (let o = 44; o + 2 < b.length; o += 3) { let x = b[o] | b[o + 1] << 8 | b[o + 2] << 16; if (x & 0x800000) x -= 1 << 24; p = Math.max(p, Math.abs(x)); } return p / 8388608; };
const secs = (buf) => (buf.byteLength - 44) / 6 / 44100;
const same = (a, b) => { const x = new Uint8Array(a), y = new Uint8Array(b); return x.length === y.length && x.every((v, i) => v === y[i]); };
const NAMES = ["DRUMS", "SYNTH 1", "SYNTH 2", "SYNTH 3", "PERC"];

// 1. the pattern, mid-play: a clone from the top, the same twice, stems
{
  const ex = await device();
  run(ex, 1.5);
  press(ex, PLAY);
  run(ex, 2.3);
  const mem = ex.memory.buffer.slice(0);
  const loop = ex.web_pattern_frames() / 44100;
  const job = { song: false, loops: 2, tail: 3, stems: true, names: NAMES, base: "pattern" };
  const t0 = Date.now();
  const a = await bounceRun({ ...job, wasm, mem: mem.slice(0) });
  const ms = Date.now() - t0;
  const b = await bounceRun({ ...job, stems: false, wasm, mem: mem.slice(0) });
  check(loop > 1 && loop < 20, `one loop of the pattern ${loop.toFixed(2)} s`);
  check(secs(a.files[0].data) >= 2 * loop && secs(a.files[0].data) <= 2 * loop + 3.1, `two loops and the tail: ${secs(a.files[0].data).toFixed(2)} s`);
  check(peakOf(a.files[0].data) > 0.05, `the mix sounds (peak ${peakOf(a.files[0].data).toFixed(3)})`);
  check(same(a.files[0].data, b.files[0].data), "the same mix from the same moment, twice");
  check(a.files.length === 6 && a.files.slice(1).every((f) => peakOf(f.data) > 0.002), `a stem a FEED (${a.files.length - 1}), each sounds`);
  check(a.files[1].name === "pattern-01-drums.wav" && a.files[2].name === "pattern-02-perc.wav" && a.files[5].name === "pattern-05-synth-3.wav", "stems named in the panel's order: " + a.files.slice(1).map((f) => f.name).join(" "));
  check(ex.web_playing() === 1, "the live device plays on, untouched");
  console.log(`     (${(6 * secs(a.files[0].data)).toFixed(0)} s of audio in ${ms} ms)`);
  const z = new Uint8Array(await zip(a.files).arrayBuffer());
  check(z[0] === 0x50 && z[1] === 0x4b && z.length > a.files.reduce((s, f) => s + f.data.byteLength, 0), "the stems' zip");
  if (out) { fs.mkdirSync(out, { recursive: true }); for (const f of a.files) fs.writeFileSync(`${out}/${f.name}`, Buffer.from(f.data)); }

  // no song yet: a clear refusal
  let err = "";
  try { await bounceRun({ ...job, song: true, stems: false, wasm, mem: mem.slice(0) }); } catch (e) { err = e.message; }
  check(/no sections/.test(err), `the song with no sections refused: "${err}"`);
}

// 2. a song (RANDOM SONG fills GLO > SONG): once through, then it stops and rings out
{
  const ex = await device();
  run(ex, 1.5);
  ex.web_test_random_song();
  run(ex, 0.5);
  const sections = ex.web_song_sections(), bpm = ex.web_bpm();
  const mem = ex.memory.buffer.slice(0);
  const job = { song: true, loops: 1, tail: 4, stems: false, names: NAMES, base: "song" };
  const a = await bounceRun({ ...job, wasm, mem: mem.slice(0) });
  const s = secs(a.files[0].data), bar = 240 / bpm;
  check(sections > 1, `RANDOM SONG made ${sections} sections at ${bpm} BPM`);
  check(s > sections * bar && s < 30 * 60, `the song once through: ${s.toFixed(1)} s (${(s / bar).toFixed(1)} bars)`);
  check(peakOf(a.files[0].data) > 0.05, "the song sounds");
  if (out) fs.writeFileSync(`${out}/${a.files[0].name}`, Buffer.from(a.files[0].data));
}

// 3. project files: refused while playing, saved stopped, and the music back on another device
{
  const ex = await device();
  run(ex, 1.5);
  ex.web_test_random_song();
  run(ex, 0.3);
  press(ex, PLAY);
  run(ex, 0.5);
  check(ex.web_save_now() === 1, "Save project file while playing: stop first");
  press(ex, PLAY);
  run(ex, 0.3);
  check(ex.web_save_now() === 0, "stopped: saved");
  const sectors = storeSectors(new Uint8Array(ex.memory.buffer, ex.web_nor(), ex.web_nor_size()));
  const json = JSON.stringify(Object.fromEntries(Object.entries(sectors).map(([k, v]) => [k, Buffer.from(v).toString("base64")])));
  const back = Object.fromEntries(Object.entries(JSON.parse(json)).map(([k, v]) => [k, new Uint8Array(Buffer.from(v, "base64"))]));
  const ex2 = await device(back);
  run(ex2, 1.5);
  check(ex2.web_test_bank_used() === ex.web_test_bank_used() && ex.web_test_bank_used() > 1, `FREQ patterns back: ${ex2.web_test_bank_used().toString(2)} of ${ex.web_test_bank_used().toString(2)}`);
  const job = { song: true, loops: 1, tail: 2, stems: false, names: NAMES, base: "again" };
  const s1 = await bounceRun({ ...job, wasm, mem: ex.memory.buffer.slice(0) }), s2 = await bounceRun({ ...job, wasm, mem: ex2.memory.buffer.slice(0) });
  check(Math.abs(secs(s1.files[0].data) - secs(s2.files[0].data)) < 0.5, `the song plays as long on the other device: ${secs(s1.files[0].data).toFixed(1)} / ${secs(s2.files[0].data).toFixed(1)} s`);
  check(ex2.web_bpm() === ex.web_bpm() && ex2.web_song_sections() === ex.web_song_sections() &&
        ex2.web_pattern_frames() === ex.web_pattern_frames(),
        `the music back: ${ex2.web_bpm()} BPM, ${ex2.web_song_sections()} sections, the same pattern (${Object.keys(sectors).length} sectors, ${json.length} B of JSON)`);
}

// 4. a song as a link: A's music into B's emulator, B's own settings kept, B's own FREQ patterns not mixed in
{
  const a = await device(null, null, 1);
  run(a, 1.5);
  a.web_test_random_song();
  run(a, 0.5);
  check(a.web_save_now() === 0, "A saves the song");
  const link = await packSong(getSong(a));
  check(/^[A-Za-z0-9_-]+$/.test(link) && link.length < 6000, `the link: ${link.length} characters, URL-safe`);

  // B: a session of its own (the power-on song, FREQ A and E stored, a setting changed), then opens the link
  const b0 = await device(null, null, 99);
  run(b0, 1.5);
  b0.web_obj_buf();
  const sectorsB = storeSectors(new Uint8Array(b0.memory.buffer, b0.web_nor(), b0.web_nor_size()));
  const settingsB = b0.web_obj_get(0) > 0 ? new Uint8Array(b0.memory.buffer, b0.web_obj_buf(), b0.web_obj_get(0)).slice() : null;
  const song = await unpackSong(link);
  const b = await device(sectorsB, song, 99);
  run(b, 1.5);
  check(b.web_bpm() === a.web_bpm() && b.web_song_sections() === a.web_song_sections() && b.web_pattern_frames() === a.web_pattern_frames(),
        `B hears A's song: ${b.web_bpm()} BPM, ${b.web_song_sections()} sections`);
  check(b.web_test_bank_used() === a.web_test_bank_used(), `B's FREQ patterns are A's: ${b.web_test_bank_used().toString(2)}`);
  const settingsB2 = b.web_obj_get(0) > 0 ? new Uint8Array(b.memory.buffer, b.web_obj_buf(), b.web_obj_get(0)).slice() : null;
  check(!settingsB || (settingsB2 && same(settingsB.buffer, settingsB2.buffer)), "B's settings untouched");
  const job = { song: true, loops: 1, tail: 2, stems: false, names: NAMES, base: "link" };
  const ra = await bounceRun({ ...job, wasm, mem: a.memory.buffer.slice(0) }), rb = await bounceRun({ ...job, wasm, mem: b.memory.buffer.slice(0) });
  check(Math.abs(secs(ra.files[0].data) - secs(rb.files[0].data)) < 0.5, `the song as long on B: ${secs(rb.files[0].data).toFixed(1)} s`);
  let bad = "";
  try { await unpackSong(link.slice(0, link.length >> 1)); } catch (e) { bad = e.message; }
  check(bad !== "", `a link cut short is refused (${bad})`);
}

console.log(fails ? `${fails} FAILED` : "studio: all ok");
process.exit(fails ? 1 : 0);
