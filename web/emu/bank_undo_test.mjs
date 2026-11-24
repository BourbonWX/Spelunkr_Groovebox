// SPDX-License-Identifier: GPL-3.0-only
// The patterns page in the web build (firmware/src/song_bank.c, SPK_NU): hold one pattern's key and tap another: a copy
// there (nothing else happens); an empty pattern tapped: a new EMPTY pattern there, the current one; a key tapped alone
// acts when let go. And UNDO takes back each (a copy, a new pattern, a clear), UNDO again puts it back.
//   node web/emu/bank_undo_test.mjs [build/emu/felucca.wasm]
import fs from "fs";
import { WASI_STUB } from "./worklet.js";
const bytes = fs.readFileSync(process.argv[2] || "build/emu/felucca.wasm");
let fails = 0;
const check = (what, ok, extra = "") => { console.log(`patterns: ${what.padEnd(68)} ${ok ? "ok" : "FAIL"} ${extra}`); if (!ok) fails++; };
const { instance } = await WebAssembly.instantiate(bytes, WASI_STUB);
const ex = instance.exports, mem = ex.memory, dec = new TextDecoder();
ex._initialize(); ex.web_nor_erase(); ex.web_boot(); ex.web_plain(1);
const render = (ms) => { for (let k = 0; k < ms * 44.1; k += 128) ex.web_render(128); };
const g = () => JSON.parse(dec.decode(new Uint8Array(mem.buffer, ex.web_guide(), ex.web_guide_len())));
const pats = () => { const x = g(); return { cur: x.cur, used: x.used.toString(2).padStart(6, "0").split("").reverse().join("") }; };
const dens = (p) => g().tracks[0].pats[p];               // the drums' steps in pattern p (-1: no pattern)
const KEY = [0, 2, 4, 5, 7, 9];                           // A..F: C3 D3 E3 F3 G3 A3
const tap = (p) => { ex.web_keys(1 << KEY[p]); render(150); ex.web_keys(0); render(250); };
const holdTap = (a, b) => { ex.web_keys(1 << KEY[a]); render(200); ex.web_keys(1 << KEY[a] | 1 << KEY[b]); render(150);
  ex.web_keys(1 << KEY[a]); render(150); ex.web_keys(0); render(250); };
render(3000);
ex.web_buttons(1 << 5); render(100); ex.web_buttons(0); render(300);   // MIX: the PATTERNS page
const a0 = dens(0);
check("at first: A with steps, current", pats().cur === 0 && pats().used === "100000" && a0 > 0, `${JSON.stringify(pats())} ${a0}`);
// an empty pattern tapped: a new EMPTY one, the current one
tap(1);
check("B tapped (empty): a new pattern B, current", pats().cur === 1 && pats().used === "110000", JSON.stringify(pats()));
check("... and it is empty (no steps)", dens(1) === 0, `${dens(1)}`);
ex.web_undo(); render(200);
check("UNDO: B gone, A current again", pats().cur === 0 && pats().used === "100000", JSON.stringify(pats()));
ex.web_undo(); render(200);
check("UNDO again: B back (empty) and current", pats().cur === 1 && pats().used === "110000" && dens(1) === 0, JSON.stringify(pats()));
tap(0);
check("A tapped: A current again (acts when let go)", pats().cur === 0, JSON.stringify(pats()));
// hold A, tap C (empty): A copied into C, A stays current
holdTap(0, 2);
check("hold A + tap C: C a copy of A, A still current", pats().cur === 0 && pats().used === "111000" && dens(2) === a0, `${JSON.stringify(pats())} ${dens(2)}`);
ex.web_undo(); render(200);
check("UNDO: C as it was (none)", pats().used === "110000", JSON.stringify(pats()));
// hold B (empty pattern) + tap A: A becomes empty (copied onto the one playing), UNDO brings A's steps back
holdTap(1, 0);
check("hold B + tap A: A now a copy of the empty B", dens(0) === 0 && pats().cur === 0, `${dens(0)}`);
ex.web_undo(); render(200);
check("UNDO: A's steps back", dens(0) === a0, `${dens(0)}`);
// OCT- held + B: cleared; UNDO brings it back
ex.web_buttons(1 << 12); render(150); ex.web_keys(1 << KEY[1]); render(150); ex.web_keys(0); render(100); ex.web_buttons(0); render(250);
check("OCT- held + B: B cleared", pats().used === "100000", JSON.stringify(pats()));
ex.web_undo(); render(200);
check("UNDO: B back", pats().used === "110000", JSON.stringify(pats()));
// playing: an empty one tapped is queued (new, empty), not switched to at once
ex.web_buttons(1 << 10); render(100); ex.web_buttons(0); render(300);
tap(3);
const q = g();
check("playing, D tapped (empty): new D, next", q.queue === 3 && q.cur === 0, `cur ${q.cur} queue ${q.queue}`);
ex.web_buttons(1 << 10); render(100); ex.web_buttons(0); render(300);
console.log(fails ? `patterns: ${fails} FAIL` : "patterns: all ok");
process.exit(fails ? 1 : 0);
