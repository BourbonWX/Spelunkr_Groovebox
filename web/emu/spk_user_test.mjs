// SPDX-License-Identifier: GPL-3.0-only
// The user samples in the built emulator (node): a made-up sample of four tones (220 440 880 1760 Hz, a quarter
// second each) into slot 0, then played as a synth (pitched), as chops (a slice a key from C3), as a kit (slices on
// the rows); cleared; one too long refused; a bounce's copy of the device has it.
//   node web/emu/spk_user_test.mjs [build/emu/felucca.wasm]
import fs from "fs";
import { WASI_STUB } from "./worklet.js";
import { RATE, buildSlot, putSlot, sliceEqual, sliceTransients, rootFromName, chopHits, chopEqual, chopsOfStarts } from "./spk_samples.js";

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
// (Nov 27: from the keys, a chop plays its own slice whatever the KEY: "keys stay in key" in C minor moved C#3 down
// onto C3's slice before, so slice 2 never played from the keys)
{
  const hearKey = (k, dur = 0.2) => {
    const out = new Float32Array(Math.round(dur * 44100));
    ex.web_keys(1 << k);
    for (let o = 0; o < out.length; o += 128) {
      ex.web_render(128);
      out.set(new Float32Array(ex.memory.buffer, ex.web_out_l(), Math.min(128, out.length - o)), o);
    }
    ex.web_keys(0);
    for (let i = 0; i < 2.5 * 44100 / 128; i++) ex.web_render(128);
    return out;
  };
  ex.web_key(0, 2, 1);                                   // (C minor, every key snapped onto it)
  for (let i = 0; i < 50; i++) ex.web_render(128);
  const keys = [0, 1, 2, 3].map((k) => hz(hearKey(k)));
  check(keys.every((f, k) => near(f, TONES[k])), `as chops from the keys with KEY C minor snapped: C3 C#3 D3 D#3 still the four slices (${keys.map((f) => f.toFixed(0)).join(" ")} Hz)`);
  ex.web_key(0, 0, 0);
  for (let i = 0; i < 50; i++) ex.web_render(128);
}
// (Nov 27: ONE-SHOT, END MARKS, BANK B) a tap of 20 ms plays the chop to its end; a chop's own end mark (a gap after
// it) stops it there; a second sample on BANK B plays on C4.. at its own pitch, BANK A stays on C3..
{
  const keyFor = (k, holdS, totalS) => {
    const out = new Float32Array(Math.round(totalS * 44100));
    ex.web_keys(1 << k);
    for (let o = 0; o < out.length; o += 128) {
      if (o >= holdS * 44100) ex.web_keys(0);
      ex.web_render(128);
      out.set(new Float32Array(ex.memory.buffer, ex.web_out_l(), Math.min(128, out.length - o)), o);
    }
    ex.web_keys(0);
    for (let i = 0; i < 2.5 * 44100 / 128; i++) ex.web_render(128);
    return out;
  };
  const rms = (x, a, b) => { let e = 0; for (let i = Math.round(a * 44100); i < Math.round(b * 44100); i++) e += x[i] * x[i]; return Math.sqrt(e / ((b - a) * 44100)); };
  ex.web_spk_use(0, 1);
  for (let i = 0; i < 100; i++) ex.web_render(128);
  const tap = keyFor(0, 0.02, 0.3);
  check(rms(tap, 0.12, 0.2) > 0.02, `ONE-SHOT: a 20 ms tap on C3 still sounds at 0.12-0.2 s (${rms(tap, 0.12, 0.2).toFixed(3)})`);
  // end marks: chop 1 the first tone's first 60 ms only, chop 2 the second tone whole (a gap between)
  const ends = SPK_buildEnds();
  check(putSlot(ex, 0, ends, "ends") === 0, "a sample with end marks (a gap between two chops) takes slot 0");
  for (let i = 0; i < 100; i++) ex.web_render(128);
  const short = keyFor(0, 0.3, 0.3);
  check(rms(short, 0.01, 0.05) > 0.02 && rms(short, 0.09, 0.2) < rms(short, 0.01, 0.05) / 6,   // (what is left: the reverb's tail)
    `END MARK: chop 1 sounds to 60 ms, then only the reverb (${rms(short, 0.01, 0.05).toFixed(3)} then ${rms(short, 0.09, 0.2).toFixed(4)})`);
  check(near(hz(keyFor(1, 0.2, 0.2)), 440), "chop 2 after the gap: the second tone");
  // BANK B: slot 1, two tones of its own (330, 660), on the same track
  const T2 = [330, 660], seg2 = Math.round(RATE / 4), p2 = new Float32Array(seg2 * 2);
  T2.forEach((f, k) => { for (let i = 0; i < seg2; i++) p2[k * seg2 + i] = 0.8 * Math.sin(2 * Math.PI * f * i / RATE) * Math.exp(-i / (RATE * 0.4)); });
  check(putSlot(ex, 1, buildSlot(p2, [[0, seg2], [seg2, 2 * seg2]], 60), "two") === 0, "slot 1 takes a second sample");
  ex.web_spk_use(1, 3);
  for (let i = 0; i < 100; i++) ex.web_render(128);
  const banks = ex.web_spk_banks();
  check((banks & 255) === 1 && banks >> 8 === 2, `the track's banks: A slot 1, B slot 2 (${banks & 255}, ${banks >> 8})`);
  const bA = hz(keyFor(1, 0.2, 0.2)), bB = [12, 13].map((k) => hz(keyFor(k, 0.2, 0.2)));
  check(near(bA, 440) && near(bB[0], 330) && near(bB[1], 660),
    `BANK A on C#3 (${bA.toFixed(0)} Hz), BANK B on C4 C#4 at their own pitch (${bB.map((f) => f.toFixed(0)).join(" ")} Hz)`);
  check((ex.web_spk_in_use() & 3) === 3, `SHARE takes both banks' samples (mask ${ex.web_spk_in_use()})`);
  {                                                      // PADS: a pad plays its chop on the picked track, whatever the octave
    const padHear = (note) => {
      const out = new Float32Array(Math.round(0.2 * 44100));
      ex.web_chop_pad(note, 110);
      for (let o = 0; o < out.length; o += 128) { ex.web_render(128); out.set(new Float32Array(ex.memory.buffer, ex.web_out_l(), Math.min(128, out.length - o)), o); }
      ex.web_chop_pad(note, 0);
      for (let i = 0; i < 2.5 * 44100 / 128; i++) ex.web_render(128);
      return out;
    };
    const pa = hz(padHear(49)), pb = hz(padHear(61));
    check(near(pa, 440) && near(pb, 660), `PADS: A's pad 2 (${pa.toFixed(0)} Hz), B's pad 2 (${pb.toFixed(0)} Hz)`);
  }
  ex.web_spk_use(0, 1);
  for (let i = 0; i < 100; i++) ex.web_render(128);
  check(ex.web_spk_banks() >> 8 === 2, "BANK A again over a chop track: its BANK B stays");
  check(putSlot(ex, 0, built, "tones") === 0, "slot 0 back to the four tones");
  ex.web_spk_clear(1);
  ex.web_spk_use(0, 1);
}
function SPK_buildEnds() {
  return buildSlot(pcm, [[0, Math.round(0.06 * RATE)], [seg, 2 * seg]], 57);
}

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
