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
// (the FEEDs, their sounds, the song), 1..6 FREQ A..F; a pattern not stored goes as the bank's cleared record, so
// opening the link clears the visitor's own there
export const SONG_IDS = 7;
const BANK_MAGIC = 0x4B4E4252;
function songObj(ex, id) { return id ? ex.web_obj_bank0() + id - 1 : ex.web_obj_autosave(); }
export function getSong(ex) {
  const out = [];
  for (let id = 0; id < SONG_IDS; id++) {
    const n = ex.web_obj_get(songObj(ex, id));
    if (n > 0) out.push([id, new Uint8Array(ex.memory.buffer, ex.web_obj_buf(), n).slice()]);
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
  for (const [id, bytes] of song) {
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
          const { instance } = await WebAssembly.instantiate(m.wasm, WASI_STUB);
          const ex = instance.exports;
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
      if (m.type === "off") { this.dead = true; this.ex = null; return; }   // switched off (the page made another)
      const ex = this.ex;
      if (!ex) return;
      if (m.type === "buttons" || m.type === "keys") this.queue.push(m);
      else if (m.type === "enc") ex.web_enc(m.role, m.n | 0);
      else if (m.type === "master") ex.web_master(m.value | 0);
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
      } else if (m.type === "solo") { ex.web_solo_toggle(m.k);
      } else if (m.type === "tool") { ex.web_tool(m.k, m.which);   // (the page's shortcuts: felucca_web.c)
      } else if (m.type === "undo") { ex.web_undo();
      } else if (m.type === "tempo") { ex.web_tempo(m.bpm);
      } else if (m.type === "swing") { ex.web_swing(m.v);
      } else if (m.type === "key") { ex.web_key(m.root, m.scale, m.snap ? 1 : 0);
      } else if (m.type === "cue") { ex.web_cue(m.p);
      } else if (m.type === "spk") { this.port.postMessage({ type: "spk", k: m.k, rc: spkPut(ex, m.k, m.data, m.meta, m.name) });
      } else if (m.type === "spkuse") { ex.web_spk_use(m.k, m.mode);
      } else if (m.type === "newsong") { ex.web_new_song(m.random ? 1 : 0);
      } else if (m.type === "plain") { if (ex.web_plain) ex.web_plain(m.on ? 1 : 0);
      } else if (m.type === "vapor") { ex.web_vapor(m.k, m.v);
      } else if (m.type === "xy") { ex.web_xy(m.j, m.x, m.y, m.on ? 1 : 0);
      } else if (m.type === "play") {                 // the shared song, once switched on: the song, else the pattern
        ex.web_bounce_go(ex.web_song_sections() ? 1 : 0, 0);
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
      return true;
    }
  }
  registerProcessor("felucca", Felucca);
}
