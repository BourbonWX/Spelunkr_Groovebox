// SPDX-License-Identifier: GPL-3.0-only
// LEAD (firmware/src/lead.c): PHANTOM recorded into the song and with more sounds, in the web build.
//   node web/emu/lead_test.mjs [build/emu/felucca.wasm]
// Checks: TRACK past the last track focuses LEAD (and web_lead_focus); its list holds MORPH, FM, SAMPLE and GENESIS
// sounds and each one sounds; a take recorded with REC armed lands in this pattern's lane and plays back with LEAD
// not focused and no key held (the tracks muted); CLEAR and UNDO; the lane and the sound survive a power cycle (the
// flash) and travel in a song (the object, id 7); a NEW song clears the lanes.
import fs from "fs";
import { WASI_STUB, storeSectors, restoreSectors, getSong, putSong } from "./worklet.js";

const bytes = fs.readFileSync(process.argv[2] || "build/emu/felucca.wasm");
let fails = 0;
const check = (what, ok, extra = "") => { console.log(`lead: ${what.padEnd(72)} ${ok ? "ok" : "FAIL"} ${extra}`); if (!ok) fails++; };
const B = { REC: 11, PLAY: 10, HOME: 6 };
const dec = new TextDecoder();

async function device(saved, song) {
  const { instance } = await WebAssembly.instantiate(bytes, WASI_STUB);
  const ex = instance.exports, mem = ex.memory;
  ex._initialize();
  ex.web_nor_erase();
  if (saved) restoreSectors(new Uint8Array(mem.buffer, ex.web_nor(), ex.web_nor_size()), saved);
  if (song) putSong(ex, song);
  ex.web_boot();
  ex.web_plain(1);
  const d = { ex, mem, peak: 0 };
  d.render = (ms) => {
    d.peak = 0;
    for (let k = 0; k < ms * 44.1; k += 128) {
      ex.web_render(128);
      const l = new Float32Array(mem.buffer, ex.web_out_l(), 128);
      for (let i = 0; i < 128; i++) d.peak = Math.max(d.peak, Math.abs(l[i]));
    }
    return d.peak;
  };
  d.tap = (b) => { ex.web_buttons(1 << b); d.render(80); ex.web_buttons(0); d.render(120); };
  d.turn = (role, n) => { for (let i = 0; i < Math.abs(n); i++) { ex.web_enc(role, Math.sign(n)); d.render(40); } };
  d.guide = () => JSON.parse(dec.decode(new Uint8Array(mem.buffer, ex.web_guide(), ex.web_guide_len())));
  d.key = (k, ms = 250) => { ex.web_keys(1 << k); const p = d.render(ms); ex.web_keys(0); d.render(60); return p; };
  d.render(3000);
  return d;
}

const a = await device(null);
// focus: TRACK (ALGORITHM, role 1) past the last track
for (let i = 0; i < 6; i++) a.turn(1, 1);
check("TRACK past the last track focuses LEAD", a.guide().lead.f === 1);
a.turn(1, -1);
check("TRACK back leaves it", a.guide().lead.f === 0);
a.ex.web_lead_focus(1); a.render(100);
check("web_lead_focus(1) focuses it", a.guide().lead.f === 1);
check("it starts on the theremin (MORPH)", a.guide().lead.eng === "MORPH", a.guide().lead.snd);
// mute the tracks, so only LEAD sounds
for (let k = 0; k < 5; k++) a.ex.web_mute(k);
a.render(500);
check("muted and stopped: silence", a.render(300) < 0.002, a.render(10).toFixed(4));
check("a key on LEAD sounds (MORPH)", a.key(7, 400) > 0.01);
// the sounds: SOUNDS (PRESETS, role 2) through the whole list
const seen = new Map();
for (let i = 0; i < 80; i++) {
  const g = a.guide().lead;
  if (!seen.has(g.eng)) { const p = a.key(7, 350); a.render(700); seen.set(g.eng, [g.snd, p]); }
  a.turn(2, 1);
}
for (const e of ["MORPH", "FM", "SAMPLE", "GENESIS"])
  check(`the list has ${e} and it sounds`, seen.has(e) && seen.get(e)[1] > 0.005, seen.has(e) ? `${seen.get(e)[0]} ${seen.get(e)[1].toFixed(3)}` : "missing");
// back to a SAMPLE sound for the take
a.turn(2, -200); a.render(100);
while (a.guide().lead.eng !== "SAMPLE") a.turn(2, 1);
const snd = a.guide().lead.snd;
// record: REC while stopped asks (the page answers WITHOUT); then the keys
a.tap(B.REC); a.render(100);
a.ex.web_rec_go(0); a.render(200);
let g = a.guide();
check("REC armed and playing on LEAD: recording", g.lead.rec === 1 && g.play === 1);
for (const k of [0, 4, 7, 4]) { a.key(k, 200); a.render(150); }
a.tap(B.REC); a.render(200);
g = a.guide();
const pat = g.lead.pat;
check("the take is in this pattern's lane (4 notes)", g.lead.n[pat] === 4, JSON.stringify(g.lead.n));
check("REC off: not recording", g.lead.rec === 0);
// playback: LEAD not focused, no key: it still plays
a.ex.web_lead_focus(0); a.render(100);
let pk = 0; for (let i = 0; i < 12; i++) pk = Math.max(pk, a.render(250));
check("the lane plays back (not focused, no keys, the tracks muted)", pk > 0.005, pk.toFixed(3));
// CLEAR (the row tools' CLEAR with LEAD focused) and UNDO
a.ex.web_lead_focus(1); a.render(100);
a.ex.web_tool(0, 3); a.render(100);
check("CLEAR empties the lane", a.guide().lead.n[pat] === 0);
a.render(2500);                                          // (the last notes' release and the reverb's tail)
pk = 0; for (let i = 0; i < 12; i++) pk = Math.max(pk, a.render(250));
check("cleared: silence", pk < 0.002, pk.toFixed(4));
a.ex.web_undo(); a.render(100);
check("UNDO brings it back", a.guide().lead.n[pat] === 4);
// kept: wait for the save, then a fresh device from the flash
a.tap(B.PLAY); a.render(2500);
const sectors = storeSectors(new Uint8Array(a.mem.buffer, a.ex.web_nor(), a.ex.web_nor_size()));
const b = await device(sectors);
g = b.guide();
check("after a power cycle: the lane", g.lead.n[pat] === 4, JSON.stringify(g.lead.n));
check("after a power cycle: the sound", g.lead.snd === snd && g.lead.eng === "SAMPLE", g.lead.snd);
check("the object is there for a song (id 7)", b.ex.web_obj_get(b.ex.web_obj_lead()) > 0);
b.guide().tracks.forEach((t, k) => { if (!t.mute) b.ex.web_mute(k); });
b.tap(B.PLAY);
pk = 0; for (let i = 0; i < 12; i++) pk = Math.max(pk, b.render(250));
check("and it plays back there (as loud as before)", pk > 0.005 && pk < 0.1, pk.toFixed(3));
b.tap(B.PLAY);
// a song (a share link / code): LEAD goes along as id 7; a song from before LEAD (no id 7) clears the visitor's
const song = getSong(b.ex);
check("the song carries LEAD (id 7)", song.some(([id, by]) => id === 7 && by.length > 48));
const c = await device(null, song);
check("opened elsewhere: the lane and the sound", c.guide().lead.n[pat] === 4 && c.guide().lead.snd === snd, JSON.stringify(c.guide().lead));
const d = await device(sectors, song.filter(([id]) => id !== 7));
check("an older song (no LEAD) clears the visitor's lanes", d.guide().lead.n.every((n) => n === 0));
// a NEW song clears the lanes
b.ex.web_new_song(0); b.render(300);
check("NEW song: the lanes empty", b.guide().lead.n.every((n) => n === 0));
console.log(fails ? `lead: ${fails} FAIL` : "lead: all ok");
process.exit(fails ? 1 : 0);
