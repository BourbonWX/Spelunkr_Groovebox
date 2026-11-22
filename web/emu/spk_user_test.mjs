// SPDX-License-Identifier: GPL-3.0-only
// The user samples in the built emulator (node): a made-up sample of four tones (220 440 880 1760 Hz, a quarter
// second each) into slot 0, then played as a synth (pitched), as chops (a slice a key from C3), as a kit (slices on
// the rows); cleared; one too long refused; a bounce's copy of the device has it.
//   node web/emu/spk_user_test.mjs [build/emu/felucca.wasm]
import fs from "fs";
import { WASI_STUB } from "./worklet.js";
import { RATE, buildSlot, putSlot, sliceEqual, sliceTransients, rootFromName } from "./spk_samples.js";

const wasm = fs.readFileSync(process.argv[2] || new URL("../../build/emu/felucca.wasm", import.meta.url).pathname);
let fails = 0;
const check = (ok, what) => { console.log(`${ok ? "ok  " : "FAIL"} ${what}`); if (!ok) fails++; };

async function device() {
  const { instance } = await WebAssembly.instantiate(wasm, WASI_STUB);
  const ex = instance.exports;
  ex._initialize(); ex.web_nor_erase(); ex.web_seed(7); ex.web_boot();
  for (let i = 0; i < 44100 * 1.5 / 128; i++) ex.web_render(128);
  return ex;
}
// note n on part p for dur s -> the left channel
function hear(ex, p, n, dur = 0.2) {
  const out = new Float32Array(Math.round(dur * 44100));
  ex.web_test_note(p, 255, 0, n, 100);
  for (let o = 0; o < out.length; o += 128) {
    ex.web_render(128);
    out.set(new Float32Array(ex.memory.buffer, ex.web_out_l(), Math.min(128, out.length - o)), o);
  }
  ex.web_test_note(p, 255, 0, n, 0);
  for (let i = 0; i < 2.5 * 44100 / 128; i++) ex.web_render(128);   // (let it and the reverb ring out)
  return out;
}
// the pitch (Hz) by the zero crossings of the middle of x
function hz(x) {
  const a = Math.floor(x.length * 0.2), b = Math.floor(x.length * 0.9);
  let z = 0, pk = 0;
  for (let i = a + 1; i < b; i++) { if ((x[i - 1] < 0) !== (x[i] < 0)) z++; pk = Math.max(pk, Math.abs(x[i])); }
  return pk < 0.005 ? 0 : z / 2 / ((b - a) / 44100);
}
const near = (f, want) => Math.abs(f / want - 1) < 0.08;

// the sample: four tones, a decay each (a hit)
const TONES = [220, 440, 880, 1760], seg = Math.round(RATE / 4), pcm = new Float32Array(seg * 4);
TONES.forEach((f, k) => { for (let i = 0; i < seg; i++) pcm[k * seg + i] = 0.8 * Math.sin(2 * Math.PI * f * i / RATE) * Math.exp(-i / (RATE * 0.4)); });
const built = buildSlot(pcm, sliceEqual(pcm.length, 4), 57);   // root A3 (220 Hz)

const ex = await device();
check(putSlot(ex, 0, built, "tones") === 0 && ex.web_spk_used(0) === 1, "slot 0 takes the sample (4 slices)");
ex.web_track(1); for (let i = 0; i < 20; i++) ex.web_render(128);

ex.web_spk_use(0, 0);
for (let i = 0; i < 200; i++) ex.web_render(128);
let a = hz(hear(ex, 1, 57, 0.1)), b = hz(hear(ex, 1, 69, 0.1));   // (the first tone: an eighth of a second at A4)
check(near(a, 220) && near(b, 440), `as a synth: A3 plays it at its root (${a.toFixed(0)} Hz), A4 an octave up (${b.toFixed(0)} Hz)`);

ex.web_spk_use(0, 1);
for (let i = 0; i < 200; i++) ex.web_render(128);
const chops = [48, 49, 50, 51].map((n) => hz(hear(ex, 1, n)));
check(chops.every((f, k) => near(f, TONES[k])) && hz(hear(ex, 1, 52)) === 0,
  `as chops: C3 C#3 D3 D#3 the four slices (${chops.map((f) => f.toFixed(0)).join(" ")} Hz), E3 none`);

ex.web_spk_use(0, 2);
for (let i = 0; i < 200; i++) ex.web_render(128);
const rows = [36, 38, 39, 42].map((n) => hz(hear(ex, 1, n)));
check(rows.every((f, k) => near(f, TONES[k])) && hz(hear(ex, 1, 46)) === 0,
  `as a kit: BD SD CP CH the four slices (${rows.map((f) => f.toFixed(0)).join(" ")} Hz), OH (slice 5) none`);

check(ex.web_spk_in_use() === 1, `SHARE takes slot 0 only: the song plays it (mask ${ex.web_spk_in_use()})`);
ex.web_track(2); for (let i = 0; i < 20; i++) ex.web_render(128);
ex.web_spk_use(3, 1); for (let i = 0; i < 20; i++) ex.web_render(128);
check(ex.web_spk_in_use() === (1 | 8), `a chop of slot 3 on another track: slots 0 and 3 (mask ${ex.web_spk_in_use()})`);
ex.web_new_song(0); for (let i = 0; i < 200; i++) ex.web_render(128);
check(ex.web_spk_in_use() === 0, "NEW SONG: no samples played");
ex.web_track(1); for (let i = 0; i < 20; i++) ex.web_render(128);
ex.web_spk_use(0, 2); for (let i = 0; i < 200; i++) ex.web_render(128);
const mem = ex.memory.buffer.slice(0);                        // a bounce's copy of the device has the slot
const { instance } = await WebAssembly.instantiate(wasm, WASI_STUB);
new Uint8Array(instance.exports.memory.buffer).set(new Uint8Array(mem));
check(instance.exports.web_spk_used(0) === 1 && near(hz(hear(instance.exports, 1, 36)), 220), "a copy of the device (the bounce's) plays it too");

ex.web_spk_clear(0);
check(ex.web_spk_used(0) === 0 && hz(hear(ex, 1, 36)) === 0, "cleared: the kit's rows silent");
ex.web_spk_use(0, 0); for (let i = 0; i < 200; i++) ex.web_render(128);
check(hz(hear(ex, 1, 57)) === 0, "cleared: the synth silent too (a shared song without the sample: no placeholder tone)");
const big = buildSlot(new Float32Array(RATE * 70).fill(0.1), [0], 60);
check(putSlot(ex, 1, big, "big") === 1 && ex.web_spk_used(1) === 0, "70 s: refused, the slot stays empty");

check(sliceTransients(pcm, 8).length === 4, `transients: the four hits found (${sliceTransients(pcm, 8).join(" ")})`);
check(rootFromName("piano_C4.wav") === 60 && rootFromName("Bass F#2") === 42 && rootFromName("loop") === 60, "a root from a file name");
console.log(fails ? `${fails} FAILED` : "all ok");
process.exit(fails ? 1 : 0);
