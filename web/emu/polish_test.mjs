// SPDX-License-Identifier: GPL-3.0-only
// POLISH (felucca_web.c web_polish): a compressor on one track, a 3-band EQ and an exciter on the master.
//   node web/emu/polish_test.mjs [build/emu/felucca.wasm]
// Checks: all off is the device's output bit for bit (and again after turning things on and off); the compressor on
// a track takes it down (reports its gain reduction, makes up some of it, leaves the other tracks alone, works on
// LEAD, on one drum of a kit: KICK / SNARE, PERC 1 / PERC 2); each EQ band moves its part of the spectrum; each exciter mode adds energy where it says; the limiter keeps
// the peaks under 1 with everything boosted; the values are clamped; nothing is NaN.
import fs from "fs";
import { WASI_STUB } from "./worklet.js";

const bytes = fs.readFileSync(process.argv[2] || "build/emu/felucca.wasm");
let fails = 0;
const check = (what, ok, extra = "") => { console.log(`polish: ${what.padEnd(70)} ${ok ? "ok" : "FAIL"} ${extra}`); if (!ok) fails++; };
const dec = new TextDecoder();
const B = { PLAY: 10 };
const P = { CON: 0, CTRK: 1, CSTY: 2, CAMT: 3, CMIX: 4, EON: 5, ELO: 6, EMID: 7, EHI: 8, EMF: 9, XON: 10, XMODE: 11, XAMT: 12, CPART: 13, SON: 14, SMASK: 15, SFEEL: 16, SAMT: 17 };

async function device() {
  const { instance } = await WebAssembly.instantiate(bytes, WASI_STUB);
  const ex = instance.exports, mem = ex.memory;
  ex._initialize();
  ex.web_nor_erase();
  ex.web_boot();
  ex.web_plain(1);
  ex.web_master(1023);
  const d = { ex, mem };
  d.render = (ms) => {                                   // the output of ms milliseconds, L and R
    const L = [], R = [];
    for (let k = 0; k < ms * 44.1; k += 128) {
      ex.web_render(128);
      L.push(...new Float32Array(mem.buffer, ex.web_out_l(), 128));
      R.push(...new Float32Array(mem.buffer, ex.web_out_r(), 128));
    }
    return { L: Float32Array.from(L), R: Float32Array.from(R) };
  };
  d.tap = (b) => { ex.web_buttons(1 << b); d.render(80); ex.web_buttons(0); d.render(120); };
  d.guide = () => JSON.parse(dec.decode(new Uint8Array(mem.buffer, ex.web_guide(), ex.web_guide_len())));
  d.set = (o) => { for (const [k, v] of Object.entries(o)) ex.web_polish(P[k], v); };
  d.render(3000);
  return d;
}
const same = (a, b) => a.L.length === b.L.length && a.L.every((v, i) => v === b.L[i]) && a.R.every((v, i) => v === b.R[i]);
const rms = (x) => Math.sqrt(x.reduce((s, v) => s + v * v, 0) / x.length);
const peak = (o) => { let m = 0; for (const x of [o.L, o.R]) for (const v of x) m = Math.max(m, Math.abs(v)); return m; };
const finite = (o) => o.L.every(Number.isFinite) && o.R.every(Number.isFinite);
// energy in a band: a crude 2-pole high-pass / low-pass (enough to see a shelf of +-12 dB move)
function band(x, lo, hi) {
  const fs = 44100, a = (f) => Math.exp(-2 * Math.PI * f / fs);
  let y = Float32Array.from(x);
  for (const f of lo ? [lo, lo, lo, lo] : []) { const k = a(f); let p = 0, q = 0; y = y.map((v) => { p = k * p + (1 - k) * v; const o = v - p; return o; }); }
  for (const f of hi ? [hi, hi, hi, hi] : []) { const k = a(f); let p = 0; y = y.map((v) => (p = k * p + (1 - k) * v)); }
  return rms(y);
}
const db = (a, b) => 20 * Math.log10(a / b);

// two devices in step: A as it is, B with POLISH
const A = await device(), Bd = await device();
A.tap(B.PLAY); Bd.tap(B.PLAY);
check("two devices in step (the test's premise)", same(A.render(500), Bd.render(500)));
Bd.set({ EON: 1, ELO: 6, XON: 1 }); Bd.render(300); A.render(300);   // (the master's: the device untouched; the
Bd.set({ EON: 0, XON: 0 }); Bd.render(100); A.render(100);             // compressor's sound lives on in the reverb's tail)
check("EQ + EXCITE on, then off again: bit for bit", same(A.render(800), Bd.render(800)));

// the compressor: drums alone (the others muted), fresh pairs (its sound lives on in the reverb's tail)
async function pair(o) {
  const x = await device(), y = await device();
  for (const d of [x, y]) { for (let k = 1; k < 5; k++) d.ex.web_mute(k); d.tap(B.PLAY); }
  y.set(o); x.render(300); y.render(300); y.guide();
  return [x, y];
}
{
  const [x, y] = await pair({ CON: 1, CTRK: 0, CSTY: 2, CAMT: 110, CMIX: 127 });
  x.render(3000); y.render(3000);                       // (the make-up settles over ~2 s)
  y.guide();
  const r = x.render(2000), o = y.render(2000), gr = y.guide().polgr;
  check("SQUASH on the drums: it reports gain reduction", gr > 30, (gr / 10) + " dB");
  const crest = (o) => 20 * Math.log10(peak(o) / rms(o.L));
  // (Ed, Nov 25: "loosen it up": the attacks are longer, so the first ms of each hit get through: the crest is not
  // flattened any more; the body is squeezed and turned back up)
  check("SQUASH: about as loud as without (make-up, within 3 dB)", Math.abs(db(rms(o.L), rms(r.L))) < 3, db(rms(o.L), rms(r.L)).toFixed(1) + " dB");
  check("SQUASH: no NaN", finite(o));
}
{
  const [x, y] = await pair({ CON: 1, CTRK: 0, CSTY: 2, CAMT: 110, CMIX: 0 });
  check("MIX 0: the dry track", same(x.render(800), y.render(800)));
}
{
  const [x, y] = await pair({ CON: 1, CTRK: 2, CSTY: 2, CAMT: 110 });
  check("on another (muted) track: the drums untouched", same(x.render(800), y.render(800)));
  check("... and no gain reduction reported", y.guide().polgr === 0);
}
for (const sty of [0, 1, 3]) {
  const [, y] = await pair({ CON: 1, CTRK: 0, CSTY: sty, CAMT: 100 });
  const o = y.render(1000), gr = y.guide().polgr;
  check(`STYLE ${["GENTLE", "PUNCH", "SQUASH", "PUMP"][sty]}: reduces, no NaN`, gr > 5 && finite(o), gr / 10 + " dB");
}
for (const d of [A, Bd]) for (let k = 1; k < 5; k++) d.ex.web_mute(k);
A.render(300); Bd.render(300);

// the EQ, on the drums' loop
const ref = () => { A.render(50); return A.render(1500); };
function eqRun(o) { Bd.set({ EON: 1, ELO: 0, EMID: 0, EHI: 0, EMF: 1, ...o }); Bd.render(100); A.render(100); return [Bd.render(1500), A.render(1500)]; }
{
  const [o, r] = eqRun({ ELO: 12 });
  check("EQ LOW +12: lows up >= 6 dB", db(band(o.L, 0, 80), band(r.L, 0, 80)) > 6, db(band(o.L, 0, 80), band(r.L, 0, 80)).toFixed(1) + " dB");
}
{
  const [o, r] = eqRun({ EHI: 12 });
  check("EQ HIGH +12: highs up >= 6 dB", db(band(o.L, 12000, 0), band(r.L, 12000, 0)) > 6, db(band(o.L, 12000, 0), band(r.L, 12000, 0)).toFixed(1) + " dB");
}
{
  const [o, r] = eqRun({ EHI: -12 });
  check("EQ HIGH -12: highs down >= 6 dB", db(band(o.L, 12000, 0), band(r.L, 12000, 0)) < -6, db(band(o.L, 12000, 0), band(r.L, 12000, 0)).toFixed(1) + " dB");
}
{
  const [o, r] = eqRun({ EMID: 12, EMF: 2 });
  check("EQ MID +12 at 1.5k: mids up >= 4 dB", db(band(o.L, 1000, 2200), band(r.L, 1000, 2200)) > 4, db(band(o.L, 1000, 2200), band(r.L, 1000, 2200)).toFixed(1) + " dB");
}
Bd.set({ EON: 0 });

// the exciter
const XB = [[8000, 0], [4000, 0], [400, 3000], [150, 500]];
for (let m = 0; m < 4; m++) {
  Bd.set({ XON: 1, XMODE: m, XAMT: 127 }); Bd.render(100); A.render(100);
  const o = Bd.render(1500), r = A.render(1500), [lo, hi] = XB[m], gain = db(band(o.L, lo, hi), band(r.L, lo, hi));
  check(`EXCITE ${["AIR", "SHINE", "WARM", "BASS"][m]}: its band up >= 0.5 dB, no NaN`, gain > 0.5 && finite(o), gain.toFixed(1) + " dB");
}
Bd.set({ XON: 0 });

// the limiter: everything on, every band up, the whole song
for (let k = 1; k < 5; k++) Bd.ex.web_mute(k);
Bd.set({ EON: 1, ELO: 12, EMID: 12, EHI: 12, XON: 1, XMODE: 3, XAMT: 127, CON: 1, CTRK: 0, CSTY: 2, CAMT: 127 });
Bd.render(300);
const hot = Bd.render(3000);
check("everything boosted: peaks under 0.98", peak(hot) <= 0.98, peak(hot).toFixed(4));
check("everything boosted: no NaN", finite(hot));

// one drum of a kit (Nov 25: KICK / SNARE on DRUMS, PERC 1 / PERC 2 on PERC): only that drum changes
{
  const lowShare = (o, r) => { const d = { L: o.L.map((v, i) => v - r.L[i]) }; return band(d.L, 0, 120) / (band(d.L, 2500, 0) + 1e-9); };
  const res = {};
  for (const [nm, part] of [["KICK", 1], ["SNARE", 2]]) {
    const [x, y] = await pair({ CON: 1, CTRK: 0, CSTY: 2, CAMT: 110, CPART: part });
    x.render(2000); y.render(2000); y.guide();
    const r = x.render(2000), o = y.render(2000), gr = y.guide().polgr;
    res[nm] = lowShare(o, r);
    check(`DRUMS ${nm}: on that drum (web_polish_part ${part}), reduces, no NaN`, y.ex.web_polish_part() === part && gr > 20 && finite(o), gr / 10 + " dB");
  }
  check("KICK changes the lows, SNARE the highs (what changed, lows over highs)", res.KICK > res.SNARE * 3, res.KICK.toFixed(2) + " vs " + res.SNARE.toFixed(2));
  const g = (await device()).guide();
  check("the guide says DRUMS is a kit and names PERC's two sounds", g.polk[0] === 1 && g.polk[1].length > 0 && g.polk[2].length > 0, JSON.stringify(g.polk));
  for (const part of [1, 2]) {
    const x = await device(), y = await device();
    for (const d of [x, y]) { for (const k of [0, 1, 2, 3]) d.ex.web_mute(k); d.tap(B.PLAY); }
    y.set({ CON: 1, CTRK: 4, CSTY: 2, CAMT: 120, CPART: part }); x.render(2000); y.render(2000); y.guide();
    const r = x.render(3000), o = y.render(3000), gr = y.guide().polgr;
    check(`PERC ${part}: on that sound, no NaN (${g.polk[part]}${gr ? "" : ", silent in this song"})`, y.ex.web_polish_part() === part && finite(o) && (gr > 0 || same(o, r)), gr / 10 + " dB");
  }
  const z = await device();
  z.set({ CON: 1, CTRK: 1, CPART: 1 });
  check("a part on a synth track: the whole track", z.ex.web_polish_part() === 0);
}

// SIDECHAIN: the synths dip at DRUMS' kicks (DRUMS turned all the way down: only its kicks' timing counts)
{
  async function scPair(o) {
    const x = await device(), y = await device();
    for (const d of [x, y]) { d.ex.web_level(0, 0); d.ex.web_mute(4); d.tap(B.PLAY); }
    y.set(o); x.render(1000); y.render(1000); y.guide();
    return [x, y];
  }
  const win = (a) => { const w = []; for (let i = 0; i + 441 <= a.length; i += 441) w.push(rms(a.subarray(i, i + 441))); return w; };
  {
    const [x, y] = await scPair({ SON: 1, SMASK: 14, SFEEL: 2, SAMT: 127 });
    const r = x.render(3000), o = y.render(3000), sc = y.guide().polsc, wr = win(r.L), wo = win(o.L);
    const ratios = wr.map((v, i) => wo[i] / (v + 1e-9)).filter((v, i) => wr[i] > 1e-4), lo = Math.min(...ratios), hi = Math.max(...ratios);
    check("SIDECHAIN PUMP, full: dips at the kicks and comes back", sc > 150 && lo < 0.3 && hi > 0.9 && finite(o), `${sc / 10} dB, windows ${lo.toFixed(2)}..${hi.toFixed(2)}`);
  }
  {
    const [x, y] = await scPair({ SON: 1, SMASK: 0, SAMT: 127 });
    check("SIDECHAIN on no track: bit for bit", same(x.render(800), y.render(800)) && y.guide().polsc === 0);
  }
  for (const f of [0, 1, 3]) {
    const [, y] = await scPair({ SON: 1, SMASK: 14, SFEEL: f, SAMT: 90 });
    const o = y.render(1500), sc = y.guide().polsc;
    check(`SIDECHAIN ${["TIGHT", "CLASSIC", "PUMP", "SWELL"][f]}: dips, no NaN`, sc > 50 && finite(o), sc / 10 + " dB");
  }
}

// LEAD
const L = await device();
for (let k = 0; k < 5; k++) L.ex.web_mute(k);
L.ex.web_lead_focus(1); L.render(100);
L.set({ CON: 1, CTRK: 5, CSTY: 2, CAMT: 127 });
L.guide();
L.ex.web_keys(1 << 7); L.render(600); L.ex.web_keys(0);
{ const gr = L.guide().polgr; check("on LEAD: a held note is compressed", gr > 30, gr / 10 + " dB"); }

// clamping
L.ex.web_polish(P.ELO, -99); L.ex.web_polish(P.EHI, 99); L.ex.web_polish(P.CTRK, 9); L.ex.web_polish(P.XMODE, 7); L.ex.web_polish(99, 1);
check("values clamped", L.ex.web_polish_get(P.ELO) === -12 && L.ex.web_polish_get(P.EHI) === 12 && L.ex.web_polish_get(P.CTRK) === 5 && L.ex.web_polish_get(P.XMODE) === 3);

console.log(fails ? `polish: ${fails} FAIL` : "polish: all ok");
process.exit(fails ? 1 : 0);
