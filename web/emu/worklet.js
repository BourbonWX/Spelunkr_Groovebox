// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
// Felucca in an AudioWorklet: felucca.wasm is the whole FM-1 firmware against a simulated FM-1
// (web/emu/felucca_web.c). Each render quantum asks it for 128 frames, which runs the device's clock forward;
// between quanta the page's input goes in and, about 30 times a second (4 when the page is hidden), the screen,
// the lights and any changed flash go out. After X0X's web/emu/worklet.js (charlesvestal/fm1-x0x, GPL-3.0).
// WASI imports of the clang/wasi-libc build: none is reached at run time (no files, no exit), so each is a no-op.
export const WASI_STUB = { wasi_snapshot_preview1: new Proxy({}, { get: () => () => 0 }) };

// The flash the page keeps: storage.c's areas (projects, user presets and their FM6 patches, the user sample
// slots 0x97000..0xDFFFF; the autosave 0xE5000..0xE6FFF (1.2) and FM6's voice bank 0xE7000 (1.4.1); the settings and
// the second FM6 copy 0xFC000..0xFFFFF), only the sectors that hold something (an erased sector is all 0xFF). {offset: bytes}
export const STORE_AREAS = [[0x97000, 0xE0000], [0xE5000, 0xE8000], [0xFC000, 0x100000]];
export function storeSectors(nor) {
  const out = {};
  for (const [a, b] of STORE_AREAS)
    for (let off = a; off < b; off += 4096) {
      const s = nor.subarray(off, off + 4096);
      if (s.some((v) => v !== 0xFF)) out[off] = s.slice();
    }
  return out;
}
export function restoreSectors(nor, sectors) {
  for (const [k, v] of Object.entries(sectors || {})) {
    const off = +k, b = v instanceof Uint8Array ? v : new Uint8Array(v);
    if (STORE_AREAS.some(([a, e]) => off >= a && off + 4096 <= e && off % 4096 === 0) && b.length === 4096)
      nor.set(b, off);
  }
}

export const HOLD_MS = 24;
// a song for a link (felucca_web.c "the share link"): [id, bytes] for the music's storage objects, id 0 the autosave
// (the FEEDs, their sounds, the song), 1..6 FREQ A..F, 7 LEAD (its sound and recorded lanes: lead.c); a pattern not
// stored goes as the bank's cleared record, LEAD as an empty one, so opening the link clears the visitor's own there
export const SONG_IDS = 8;
const LEAD_ID = 7;
const BANK_MAGIC = 0x4B4E4252;
function songObj(ex, id) { return id === LEAD_ID ? ex.web_obj_lead() : id ? ex.web_obj_bank0() + id - 1 : ex.web_obj_autosave(); }
export function getSong(ex) {
  const out = [];
  for (let id = 0; id < SONG_IDS; id++) {
    if (id === LEAD_ID && !ex.web_obj_lead) continue;      // (a build before LEAD)
    const n = ex.web_obj_get(songObj(ex, id));
    if (n > 0) out.push([id, new Uint8Array(ex.memory.buffer, ex.web_obj_buf(), n).slice()]);
    else if (id === LEAD_ID) out.push([id, new Uint8Array(8)]);
    else if (id) {
      const b = new Uint8Array(8);
      new DataView(b.buffer).setUint32(0, BANK_MAGIC, true);
      b[4] = id - 1;
      out.push([id, b]);
    }
  }
  return out;
}
export function putSong(ex, song) {
  if (ex.web_obj_lead && !song.some(([id]) => id === LEAD_ID)) song = [...song, [LEAD_ID, new Uint8Array(8)]];   // (a song from
  for (const [id, bytes] of song) {                                                       // before LEAD: none here either)
    if (id < 0 || id >= SONG_IDS || bytes.length > ex.web_obj_max()) continue;
    new Uint8Array(ex.memory.buffer, ex.web_obj_buf(), bytes.length).set(bytes);
    ex.web_obj_put(songObj(ex, id), bytes.length);
  }
}
// the link's text: "PT" 1, then id, length (2 bytes) and the bytes of each; deflated; base64url
export async function packSong(song) {
  const n = song.reduce((s, [, b]) => s + 3 + b.length, 3), raw = new Uint8Array(n);
  raw.set([0x50, 0x54, 1]);
  let o = 3;
  for (const [id, b] of song) { raw[o] = id; raw[o + 1] = b.length & 255; raw[o + 2] = b.length >> 8; raw.set(b, o + 3); o += 3 + b.length; }
  const z = new Uint8Array(await new Response(new Blob([raw]).stream().pipeThrough(new CompressionStream("deflate-raw"))).arrayBuffer());
  let s = "";
  for (let i = 0; i < z.length; i += 4096) s += String.fromCharCode.apply(null, z.subarray(i, i + 4096));
  return btoa(s).replace(/\+/g, "-").replace(/\//g, "_").replace(/=+$/, "");
}
/* a user sample into slot k (felucca_web.c web_spk_*; web/emu/spk_samples.js putSlot, inline: no imports in here):
 * data null clears it. 0 ok, 1 refused */
export function spkPut(ex, k, data, meta, name) {
  ex.web_spk_clear(k);
  if (!data) return 0;
  if (data.length > ex.web_spk_cap()) return 1;
  new Uint8Array(ex.memory.buffer, ex.web_spk_data(k), data.length).set(data);
  new Uint32Array(ex.memory.buffer, ex.web_spk_meta(), meta.length).set(meta);
  const nm = String(name || "").toUpperCase().replace(/[^\x20-\x7E]/g, "").slice(0, 8), out = new Uint8Array(ex.memory.buffer, ex.web_spk_name(), 16);
  out.fill(0);                                  // (no TextEncoder in an AudioWorklet: ASCII by hand)
  for (let i = 0; i < nm.length; i++) out[i] = nm.charCodeAt(i);
  return ex.web_spk_commit(k);
}

export async function unpackSong(text) {
  const b = atob(text.replace(/-/g, "+").replace(/_/g, "/"));
  const z = Uint8Array.from(b, (c) => c.charCodeAt(0));
  let raw;
  try {
    raw = new Uint8Array(await new Response(new Blob([z]).stream().pipeThrough(new DecompressionStream("deflate-raw"))).arrayBuffer());
  } catch (e) {                                 // (a cut deflate stream throws with no message in some browsers and node)
    throw new Error("the link is cut short or damaged");
  }
  if (raw[0] !== 0x50 || raw[1] !== 0x54 || raw[2] !== 1) throw new Error("not a Phantom Tracker song link");
  const song = [];
  for (let o = 3; o + 3 <= raw.length;) {
    const len = raw[o + 1] | raw[o + 2] << 8;
    if (o + 3 + len > raw.length) throw new Error("the link is cut short");
    song.push([raw[o], raw.slice(o + 3, o + 3 + len)]);
    o += 3 + len;
  }
  if (!song.some(([id]) => id === 0)) throw new Error("the link holds no song");
  return song;
}
export const REC_CHUNK = 22016;                // the recorder's chunks: 172 quanta, ~half a second

// The panel's knobs from a MIDI controller (the page's Web MIDI in): CC 20..26 turn SELECT, ALGORITHM, PRESETS,
// KNOB 1..4 (the encoder roles), a detent a step; CC 27 sets MASTER (a pot on the FM-1: absolute in every mode). The
// firmware's CC map (midi_control.c) has none of them, so they stay here. mode, as the controller sends a turn:
//   abs  absolute (the default): by how far the value moves, so it takes over from anywhere without a jump; last:
//        the value each knob CC sent before (the first one only takes its place)
//   bin  relative, 64 +- n (65 = +1, 63 = -1)
//   twos relative, two's complement (1 = +1, 127 = -1)
//   sign relative, sign bit (1 = +1, 65 = -1)
// -> {role, n} | {master: 0..1023} | null (not one of them: on to the firmware)
export const KNOB_CC = 20, MASTER_CC = 27, KNOB_MODES = ["abs", "bin", "twos", "sign"];
export function midiKnob(last, cc, value, mode = "abs") {
  if (cc === MASTER_CC) return { master: Math.round(value * 1023 / 127) };
  const role = cc - KNOB_CC;
  if (role < 0 || role >= 7) return null;
  if (mode === "bin") return { role, n: value - 64 };
  if (mode === "twos") return { role, n: value < 64 ? value : value - 128 };
  if (mode === "sign") return { role, n: value < 64 ? value : 64 - value };
  const was = last[role];
  last[role] = value;
  return { role, n: was === undefined ? 0 : value - was };
}

// The warm-up (Ed, Nov 23 2026: "some hiccups ... on a lower powered thin pc running Edge"). Browsers compile a
// WebAssembly function the first time it runs, in the thread that runs it: here the audio thread. So the first visit to
// each page, sound and effect stalled the audio while its code compiled (measured: 15-19 audio blocks late in the first
// minute, up to 25 ms on a fast PC; several times that on a slow one). A second, throwaway copy of the device sharing
// the compiled module plays this script during the power-on splash, a slice of each audio block at a time, so the real
// one finds everything compiled (measured: 0-1 late blocks).
const WARM_BLOCK = 128;
export function* warmScript(ex) {
  function* run(sec) {
    const n = Math.ceil(sec * 44100 / WARM_BLOCK);
    for (let i = 0; i < n; i++) {
      ex.web_render(WARM_BLOCK);
      yield;
    }
  }
  function* tap(b, hold = 0.06, after = 0.12) {
    ex.web_buttons(1 << b);
    yield* run(hold);
    ex.web_buttons(0);
    yield* run(after);
  }
  const [FX, SCL, ENV, LFO, EDIT, GLO, HOME, SAVE, ARP, SEQ, PLAY, REC, OCTDN, OCTUP] = [...Array(14).keys()];
  yield* run(2.6);                                         // the splash
  for (let s = 0; s < 4; s++) {                           // four random songs: their engines, kits, effects
    if (ex.web_seed) ex.web_seed(0x5EED + s);
    if (ex.web_new_song) ex.web_new_song(1);
    yield* tap(PLAY);
    yield* run(0.8);
    for (const b of [SEQ, SEQ, SEQ, HOME, GLO, GLO, GLO, HOME, EDIT, EDIT, EDIT, HOME, SCL, HOME, ENV, HOME, LFO, HOME,
                     ARP, HOME, FX, FX, HOME, OCTUP, OCTDN]) yield* tap(b);
    if (ex.web_guide) ex.web_guide();
    if (ex.web_enc) for (let r = 0; r < 7; r++) { ex.web_enc(r, 2); yield* run(0.05); ex.web_enc(r, -2); yield* run(0.05); }
    yield* tap(PLAY);
  }
  if (ex.web_vapor) {                                      // VAPOR's four
    for (let k = 0; k < 4; k++) ex.web_vapor(k, 90);
    yield* tap(PLAY);
    yield* run(0.6);
    for (let k = 0; k < 4; k++) ex.web_vapor(k, 0);
    yield* tap(PLAY);
  }
  yield* tap(HOME, 1.0, 0.2);                             // MENU (HOME held), then out
  yield* tap(HOME);
  yield* tap(REC);                                         // REC armed, then off
  yield* tap(REC);
}

// The start-up chime (Ed, Nov 25 2026: the PS1's boot fanfare "wasn't a recording but was synthesized live every time
// it turned on"; his pick, MALL, "fade out sooner ... starts at 4 and ends before six"). Like the PS1, it is played
// live, by a third copy of the device sharing the compiled module (never the visitor's song): a slowed electric-piano
// maj9 rolled in (SAMPLE MALL EP), a glass pad swelling under it (SAMPLE GLASS PAD), one crystal bell (FM), the HALL
// reverb and VAPOR's SLOWED + WOW. chimeScript yields after each block it renders; chimeGain is the fade.
export const CHIME = { fadeAt: 4.0, endAt: 5.8, level: 1.4, delay: 0.25 };   // (level: about as loud as a song at the
// same VOLUME; delay: Ed, Nov 25: "the first two notes glitch ... a .25 second delay": its start waits that long after
// SWITCH ON, so the device's own start and the warm-up's first, heaviest slices are past; its times count from there)
export function chimeGain(sec) {
  if (sec <= CHIME.fadeAt) return CHIME.level;
  if (sec >= CHIME.endAt) return 0;
  const x = (sec - CHIME.fadeAt) / (CHIME.endAt - CHIME.fadeAt);
  return CHIME.level * 0.5 * (1 + Math.cos(Math.PI * x));
}
export function* chimeScript(ex) {
  const X = -1000, ev = [];
  const note = (t, p, n, hold, v) => { ev.push([t, p, n, v], [t + hold, p, n, 0]); };
  ex.web_test_reverb(2, 115);                                                        // HALL, big
  ex.web_test_note(2, 4, 1, 0, 0); ex.web_test_fx(2, 70, 40, 0, X, X, 80, X);         // SAMPLE MALL EP
  ex.web_test_note(3, 4, 4, 0, 0); ex.web_test_fx(3, 95, 60, -20, X, 70, 95, X); ex.web_level(3, 92);   // SAMPLE GLASS PAD
  ex.web_test_note(4, 24, 0, 0, 0); ex.web_test_fx(4, 110, X, 25, X, X, 90, 30);      // FM CRYSTAL BELL
  ex.web_vapor(0, 40); ex.web_vapor(1, 35);                                          // VAPOR: SLOWED, WOW
  [53, 60, 64, 67, 69, 74].forEach((n, k) => note(0.1 + 0.08 * k, 2, n, 3.4, 92 - k * 4));
  note(0.45, 3, 65, 3.6, 80); note(0.5, 3, 72, 3.6, 70);
  note(1.5, 4, 86, 0.5, 90);
  ev.sort((a, b) => a[0] - b[0]);
  let e = 0, t = 0;
  while (t < CHIME.endAt) {
    while (e < ev.length && ev[e][0] <= t) { const [, p, n, v] = ev[e++]; ex.web_test_note(p, 255, 0, n, v); }
    ex.web_render(WARM_BLOCK);
    t += WARM_BLOCK / 44100;
    yield t;
  }
}

if (typeof registerProcessor === "function") {
  const clock = globalThis.performance ? () => globalThis.performance.now() : () => Date.now();
  class Felucca extends AudioWorkletProcessor {
    constructor() {
      super();
      this.ex = null;
      this.sentDraws = -1;
      this.sentWrites = -1;
      this.sentLeds = "";
      this.lastFrame = 0;
      this.period = 1 / 30;
      this.busy = 0;
      this.frames = 0;
      this.queue = [];                                // button / key changes, each held >= HOLD_MS of device time
      this.nextOk = 0;
      this.rec = null;                                // the studio's recorder: {l, r, n} filling, sent each half second
      this.port.onmessage = (e) => this.onMessage(e.data);
    }

    async onMessage(m) {
      if (m.type === "load") {
        try {
          const module = await WebAssembly.compile(m.wasm);
          const ex = (await WebAssembly.instantiate(module, WASI_STUB)).exports;
          if (m.chime) try {                            // the start-up chime's own copy (process() mixes it in)
            const cx = (await WebAssembly.instantiate(module, WASI_STUB)).exports;
            if (cx.web_test_fx) {
              cx._initialize();
              cx.web_nor_erase();
              cx.web_seed(7);
              cx.web_boot();
              cx.web_master(m.master ?? 700);
              for (let p = 0; p < 5; p++) cx.web_test_note(p, 255, 0, 0, 0);
              this.cx = cx;
              this.chime = chimeScript(cx);
            }
          } catch { this.chime = null; }
          if (!m.nowarm) try {                          // the warm-up's copy (process() plays it a slice at a time)
            const wx = (await WebAssembly.instantiate(module, WASI_STUB)).exports;
            wx._initialize();
            wx.web_nor_erase();
            if (wx.web_plain) wx.web_plain(0);          // (POWER USER: every page there is)
            if (wx.web_seed) wx.web_seed(0x5EED);
            wx.web_boot();
            this.warm = warmScript(wx);
            this.warmStart = clock();
          } catch { this.warm = null; }
          ex._initialize();
          ex.web_nor_erase();
          restoreSectors(new Uint8Array(ex.memory.buffer, ex.web_nor(), ex.web_nor_size()), m.sectors);
          if (m.song) putSong(ex, m.song);                // a shared link's music, over the visitor's (index.html)
          ex.web_master(m.master ?? 700);
          if (ex.web_plain) ex.web_plain(m.plain ?? 1 ? 1 : 0);   // the screen's words: plain unless POWER USER
          if (ex.web_seed) ex.web_seed((Math.random() * 4294967296) >>> 0);   /* (each power-on its own song) */
          ex.web_boot();
          this.ex = ex;
          this.fbSide = ex.web_screen_px ? ex.web_screen_px() : 240;
          this.fbN = this.fbSide * this.fbSide;
          this.sentWrites = ex.web_flash_writes_count();
          this.port.postMessage({ type: "ready", rate: ex.web_sample_rate(), side: this.fbSide });
        } catch (err) {
          this.port.postMessage({ type: "error", message: String(err && err.message || err) });
        }
        return;
      }
      if (m.type === "off") { this.dead = true; this.ex = null; this.warm = null; this.chime = null; return; }   // switched off (the page made another)
      const ex = this.ex;
      if (!ex) return;
      if (m.type === "buttons" || m.type === "keys") this.queue.push(m);
      else if (m.type === "enc") ex.web_enc(m.role, m.n | 0);
      else if (m.type === "master") { ex.web_master(m.value | 0); if (this.cx) this.cx.web_master(m.value | 0); }
      else if (m.type === "batt") { if (ex.web_batt) ex.web_batt(m.level | 0); }   // (the box: its cells' charge, 0 on DC)
      else if (m.type === "midi") ex.web_midi(m.data[0] | 0, m.data[1] | 0, m.data[2] | 0);
      else if (m.type === "visible") this.period = m.on ? 1 / 30 : 1 / 4;
      else if (m.type === "rec") {                    // the studio: record what plays (on), or send the rest (off)
        if (m.on) this.rec = { l: new Float32Array(REC_CHUNK), r: new Float32Array(REC_CHUNK), n: 0 };
        else if (this.rec) { this.recFlush(true); this.rec = null; }
      } else if (m.type === "snapshot") {             // the studio's bounce: the whole device as it is now
        this.port.postMessage({ type: "snapshot", id: m.id, mem: ex.memory.buffer.slice(0) });
      } else if (m.type === "share") {                // the studio's share link: the music's objects
        const rc = ex.web_save_now();
        this.sentWrites = ex.web_flash_writes_count();
        this.port.postMessage({ type: "share", id: m.id, rc, song: rc ? null : getSong(ex), spk: ex.web_spk_in_use ? ex.web_spk_in_use() : 0 });
      } else if (m.type === "track") { ex.web_track(m.k);                      // the Spelunkr page's controls
      } else if (m.type === "mute") { ex.web_mute(m.k);
      } else if (m.type === "pan") { if (ex.web_pan) ex.web_pan(m.k, m.v | 0);   // (the page's mixer: P_PAN)
      } else if (m.type === "solo") { ex.web_solo_toggle(m.k);
      } else if (m.type === "palette") { if (ex.web_palette) ex.web_palette(m.i);       // (the screen's colours: the page's THEME)
      } else if (m.type === "level") { if (ex.web_level) ex.web_level(m.k, m.v);           // (the page's mixer: P_LEVEL)
      } else if (m.type === "tool") { ex.web_tool(m.k, m.which);   // (the page's shortcuts: felucca_web.c)
      } else if (m.type === "leadfocus") { if (ex.web_lead_focus) ex.web_lead_focus(m.on ? 1 : 0);   // (LEAD: lead.c)
      } else if (m.type === "leadclear") { if (ex.web_lead_clear) ex.web_lead_clear();
      } else if (m.type === "undo") { ex.web_undo();
      } else if (m.type === "touch") { if (ex.web_touch) ex.web_touch(m.phase, m.x, m.y);
      } else if (m.type === "midisel") { if (ex.web_midi_sel_set) ex.web_midi_sel_set(m.on ? 1 : 0);
      } else if (m.type === "recgo") { ex.web_rec_go(m.c);
      } else if (m.type === "tempo") { ex.web_tempo(m.bpm);
      } else if (m.type === "swing") { ex.web_swing(m.v);
      } else if (m.type === "key") { ex.web_key(m.root, m.scale, m.snap ? 1 : 0);
      } else if (m.type === "cue") { ex.web_cue(m.p);
      } else if (m.type === "spk") { this.port.postMessage({ type: "spk", k: m.k, rc: spkPut(ex, m.k, m.data, m.meta, m.name) });
      } else if (m.type === "spkuse") { ex.web_spk_use(m.k, m.mode);
      } else if (m.type === "newsong") { ex.web_new_song(m.random ? 1 : 0);
      } else if (m.type === "fxkeys") { if (ex.web_fx_mode) ex.web_fx_mode(m.on ? 1 : 0, m.open ? 1 : 0);   // (FX: the keys, or the effects)
      } else if (m.type === "plain") { if (ex.web_plain) ex.web_plain(m.on ? 1 : 0);
      } else if (m.type === "vapor") { ex.web_vapor(m.k, m.v);
      } else if (m.type === "polish") { if (ex.web_polish) ex.web_polish(m.id, m.v | 0);   // (POLISH: felucca_web.c)
      } else if (m.type === "xy") { ex.web_xy(m.j, m.x, m.y, m.on ? 1 : 0, m.k ?? 255);
      } else if (m.type === "play") {                 // the shared song, once switched on: the song, else the pattern
        ex.web_bounce_go(ex.web_song_sections() ? 1 : 0, 0);
      } else if (m.type === "stop") {                 // (the playlist's player: the next song)
        ex.web_bounce_stop();
      } else if (m.type === "info") {                 // the studio's export dialog: what there is to bounce
        this.port.postMessage({ type: "info", id: m.id, sections: ex.web_song_sections(), bpm: ex.web_bpm(),
                                loop: ex.web_pattern_frames() / ex.web_sample_rate() });
      } else if (m.type === "save") {                 // the studio's project export: the music into the flash first
        const rc = ex.web_save_now ? ex.web_save_now() : 2;
        this.sentWrites = ex.web_flash_writes_count();
        this.port.postMessage({ type: "saved", id: m.id, rc,
                                sectors: storeSectors(new Uint8Array(ex.memory.buffer, ex.web_nor(), ex.web_nor_size())) });
      }
    }

    recFlush(last) {
      const r = this.rec;
      if (!r.n && !last) return;
      const l = r.l.slice(0, r.n), rr = r.r.slice(0, r.n);
      this.port.postMessage({ type: "rec", l, r: rr, last: !!last }, [l.buffer, rr.buffer]);
      r.n = 0;
    }

    publish() {
      const ex = this.ex, mem = ex.memory.buffer;
      const msg = { type: "frame" };
      msg.pk = [this.pkL || 0, this.pkR || 0];
      this.pkL = this.pkR = 0;
      const leds = [ex.web_lit_buttons(), ex.web_lit_keys(), ex.web_dim_buttons(), ex.web_dim_keys(), ex.web_dim_level(),
                    ex.web_breath_buttons(), ex.web_breath_keys(), ex.web_mid_keys ? ex.web_mid_keys() : 0];
      const an = ex.web_anim_levels ? ex.web_anim_levels() : 0;   // the power-on sweep: each LED's level
      const anim = an ? Array.from(new Uint8Array(mem, an, 41)) : null;
      const key = leds.join(",") + (anim ? ";" + anim.join(",") : "");
      if (key !== this.sentLeds) {
        this.sentLeds = key;
        msg.leds = leds;
        msg.anim = anim;
      }
      if (this.frames >= 44100) {                     // the share of real time spent running the device
        msg.load = this.busy / (this.frames / 44.1);
        this.busy = this.frames = 0;
      }
      const transfer = [];
      const draws = ex.web_screen_draws();
      if (draws !== this.sentDraws) {                 // only the rows that changed (felucca_web.c web_screen_rows)
        const side = this.fbSide, rows = ex.web_screen_rows ? ex.web_screen_rows() : 0;
        let y0 = 0, y1 = side;
        if (this.sentDraws !== undefined && ex.web_screen_rows) { y0 = rows & 0xFFFF; y1 = rows >>> 16; }
        this.sentDraws = draws;
        if (y1 > y0) {
          msg.fb = new Uint16Array(mem, ex.web_screen() + y0 * side * 2, (y1 - y0) * side).slice();
          msg.fbY = y0;
          msg.fbSide = side;
          transfer.push(msg.fb.buffer);
        }
      }
      const writes = ex.web_flash_writes_count();
      if (writes !== this.sentWrites) {               // a save is done within one main-loop pass: whole here
        this.sentWrites = writes;
        msg.sectors = storeSectors(new Uint8Array(mem, ex.web_nor(), ex.web_nor_size()));
      }
      const gnow = clock();                           // the Spelunkr page's guide: the state as JSON, ~8 a second
      if (ex.web_guide && gnow - (this.lastGuide || 0) > 120) {
        this.lastGuide = gnow;
        msg.guide = new Uint8Array(mem, ex.web_guide(), ex.web_guide_len()).slice();   // (no TextDecoder here: bytes)
        transfer.push(msg.guide.buffer);
      }
      if (msg.leds || msg.fb || msg.sectors || msg.guide || msg.load !== undefined) this.port.postMessage(msg, transfer);
    }

    // A click's down and up can arrive within one render quantum, but the FM-1's contacts never close for less than
    // its debounce, and the UI reads them once a frame: each change of the buttons or the keys stays HOLD_MS of the
    // device's time before the next one goes in (a tap: down for a frame and more, then up).
    input() {
      const ex = this.ex, now = ex.web_now_ms();
      while (this.queue.length && now - this.nextOk >= 0) {
        const m = this.queue.shift();
        if (m.type === "buttons") ex.web_buttons(m.mask >>> 0);
        else ex.web_keys(m.mask >>> 0);
        this.nextOk = now + HOLD_MS;
      }
    }

    process(inputs, outputs) {
      const ex = this.ex;
      if (this.dead) return false;
      if (!ex) return true;
      if (this.queue.length) this.input();
      const out = outputs[0], n = out[0].length;
      const t0 = clock();
      ex.web_render(n);
      this.busy += clock() - t0;
      this.frames += n;
      out[0].set(new Float32Array(ex.memory.buffer, ex.web_out_l(), n));
      if (out[1]) out[1].set(new Float32Array(ex.memory.buffer, ex.web_out_r(), n));
      if (this.chime && n === WARM_BLOCK && (this.chimeWait = (this.chimeWait ?? Math.round(CHIME.delay * sampleRate)) - n) < 0) {
        try {                                         // (the start-up chime over the splash, after CHIME.delay, faded out by CHIME.endAt)
          const step = this.chime.next();
          if (step.done) { this.chime = null; this.cx = null; }
          else {
            const g = chimeGain(step.value), cm = this.cx.memory.buffer;
            const cl = new Float32Array(cm, this.cx.web_out_l(), n), cr = new Float32Array(cm, this.cx.web_out_r(), n);
            for (let i = 0; i < n; i++) { out[0][i] += cl[i] * g; if (out[1]) out[1][i] += cr[i] * g; }
          }
        } catch { this.chime = null; this.cx = null; }
      }
      {                                               // the output's peaks, L R, for the page's OUT meter (publish)
        let pl = this.pkL || 0, pr = this.pkR || 0;
        const L = out[0], R = out[1] || out[0];
        for (let i = 0; i < n; i++) { const a = Math.abs(L[i]), b = Math.abs(R[i]); if (a > pl) pl = a; if (b > pr) pr = b; }
        this.pkL = pl; this.pkR = pr;
      }
      if (this.rec) {
        const r = this.rec, take = Math.min(n, REC_CHUNK - r.n);
        r.l.set(out[0].subarray(0, take), r.n);
        r.r.set((out[1] || out[0]).subarray(0, take), r.n);
        r.n += take;
        if (r.n >= REC_CHUNK) this.recFlush(false);
      }
      if (currentTime - this.lastFrame >= this.period) {
        this.lastFrame = currentTime;
        this.publish();
      }
      if (this.warm) this.warmSlice();
      return true;
    }

    // a slice of the warm-up after the real block: generous during the splash (nothing playing yet), then a small
    // share of each block's time, so the real device always comes first
    warmSlice() {
      const now = clock(), end = now + (this.chime ? 0.8 : now - this.warmStart < 2500 ? 6 : 0.8);   // (the chime playing: a
      // small share, as when a song plays: 6 ms a 2.9 ms block starved the audio and the chime's first notes broke up)
      try {
        while (clock() < end)
          if (this.warm.next().done) { this.warm = null; this.port.postMessage({ type: "warm", ms: Math.round(clock() - this.warmStart) }); break; }
      } catch { this.warm = null; }
    }
  }
  registerProcessor("felucca", Felucca);
}
