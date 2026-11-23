// SPDX-License-Identifier: GPL-3.0-only
// The Spelunkr Groovebox's share codes: SHARE uploads a song (and the samples it plays) and gets back a short code,
// SPGB_7K2QX9A; opening the code downloads it again. A Cloudflare Worker with an R2 bucket (SONGS: 10 GB free) and / or a
// KV namespace (SONGS_KV: no card needed, 1 GB, 1,000 writes a day) to keep them in (README.md). With both bound, new
// songs go to R2 and a code is looked for in R2, then in KV (the codes made before the move, Nov 23 2026).
//   POST   /s               body: the package (gzip, made by the page), up to MAX_BYTES  ->  {"code": "SPGB_..."}
//   GET    /s/SPGB_XXXXXXX  the package as uploaded
//   DELETE /s/SPGB_XXXXXXX  removes it (moderation): Authorization: Bearer ADMIN_TOKEN
// A code lasts CODE_DAYS (var, 14; Ed, Nov 23 2026): after that GET says it's gone (and deletes it), and the bucket's
// lifecycle rule (songs-expire: SPGB_, 14 days) clears what nobody opened. 0 = codes last until deleted.
// Bindings (wrangler.toml): SONGS (R2) and / or SONGS_KV (KV), UPLOADS (rate limit, optional), ALLOWED_ORIGINS (var),
// DAILY_SHARES (var: shares a visitor may make a UTC day, 0 = no cap; counted in R2 under caps/), ADMIN_TOKEN (secret).

const ALPHABET = "23456789ABCDEFGHJKLMNPQRSTUVWXYZ";   // (no 0 O 1 I: a code read off a stream or a screen)
const CODE_LEN = 7;
const CODE_RE = /^SPGB_[2-9A-HJ-NP-Z]{7}$/;
const MAX_BYTES = 12 * 1024 * 1024;

function newCode() {
  const r = crypto.getRandomValues(new Uint8Array(CODE_LEN));
  let s = "SPGB_";
  for (const b of r) s += ALPHABET[b & 31];                     // (32 letters: the low 5 bits, uniform)
  return s;
}

function cors(req, env) {
  const origin = req.headers.get("Origin") || "";
  const allowed = String(env.ALLOWED_ORIGINS || "").split(",").map((s) => s.trim()).filter(Boolean);
  const ok = req.method === "GET" || allowed.includes(origin) || allowed.includes("*");
  return {
    "Access-Control-Allow-Origin": ok ? (req.method === "GET" ? "*" : origin) : "null",
    "Access-Control-Allow-Methods": "GET, POST, DELETE, OPTIONS",
    "Access-Control-Allow-Headers": "Content-Type, Authorization",
    "Access-Control-Max-Age": "86400",
    Vary: "Origin",
  };
}
// where the songs are kept: R2 first when bound (new songs go there), then KV (older codes; values up to 25 MB, the
// songs here are 12 MB at most)
function store(env) {
  const r2 = env.SONGS && {
    has: async (k) => !!(await env.SONGS.head(k)),
    put: (k, body, meta) => env.SONGS.put(k, body, { httpMetadata: { contentType: "application/gzip" }, customMetadata: meta }),
    get: async (k) => { const o = await env.SONGS.get(k); return o && { body: o.body, created: o.customMetadata?.created }; },
    del: (k) => env.SONGS.delete(k),
  };
  const kv = env.SONGS_KV && {
    has: async (k) => { const s = await env.SONGS_KV.get(k, "stream"); if (s) await s.cancel(); return !!s; },
    put: (k, body, meta) => env.SONGS_KV.put(k, body, { metadata: meta }),
    get: async (k) => { const { value, metadata } = await env.SONGS_KV.getWithMetadata(k, "stream"); return value && { body: value, created: metadata?.created }; },
    del: (k) => env.SONGS_KV.delete(k),
  };
  const all = [r2, kv].filter(Boolean);
  if (!all.length) throw new Error("no song store bound (SONGS or SONGS_KV)");
  return {
    has: async (k) => { for (const s of all) if (await s.has(k)) return true; return false; },
    put: (k, body, meta) => all[0].put(k, body, meta),
    get: async (k) => { for (const s of all) { const b = await s.get(k); if (b) return b; } return null; },
    del: async (k) => { for (const s of all) await s.del(k); },
  };
}
// a visitor's shares today (UTC), counted in R2 under caps/<day>/<a hash of the address> (no address is kept; the
// bucket's lifecycle rule clears caps/ after a couple of days). Returns false once DAILY_SHARES are used up.
async function dailyOk(env, ip) {
  const cap = Number(env.DAILY_SHARES || 0);
  if (!cap || !env.SONGS) return true;
  const day = new Date().toISOString().slice(0, 10);
  const h = new Uint8Array(await crypto.subtle.digest("SHA-256", new TextEncoder().encode(`spgb:${ip}`)));
  const key = `caps/${day}/${[...h.slice(0, 8)].map((b) => b.toString(16).padStart(2, "0")).join("")}`;
  const n = Number((await (await env.SONGS.get(key))?.text()) || 0);
  if (n >= cap) return false;
  await env.SONGS.put(key, String(n + 1));
  return true;
}
const json = (h, status, o) => new Response(JSON.stringify(o), { status, headers: { ...h, "Content-Type": "application/json" } });

export default {
  async fetch(req, env) {
    const url = new URL(req.url), h = cors(req, env), db = store(env);
    if (req.method === "OPTIONS") return new Response(null, { status: 204, headers: h });
    if (url.pathname === "/" || url.pathname === "/health") return json(h, 200, { ok: true, service: "spelunkr-groovebox-share" });

    if (url.pathname === "/s" && req.method === "POST") {
      if (h["Access-Control-Allow-Origin"] === "null") return json(h, 403, { error: "uploads only from the groovebox" });
      const ip = req.headers.get("CF-Connecting-IP") || "unknown";
      if (env.UPLOADS) {
        const { success } = await env.UPLOADS.limit({ key: ip });
        if (!success) return json(h, 429, { error: "too many shares in a minute: wait a little" });
      }
      const said = Number(req.headers.get("Content-Length") || 0);
      if (said > MAX_BYTES) return json(h, 413, { error: "too big to share" });
      const body = new Uint8Array(await req.arrayBuffer());
      if (body.length > MAX_BYTES) return json(h, 413, { error: "too big to share" });
      if (body.length < 18 || body[0] !== 0x1f || body[1] !== 0x8b) return json(h, 400, { error: "not a groovebox song" });
      if (!(await dailyOk(env, ip))) return json(h, 429, { error: "that's the most shares for today: try again tomorrow" });
      for (let tries = 0; tries < 5; tries++) {
        const code = newCode();
        if (await db.has(code)) continue;                         // (35 bits: a clash is rare, but never overwrite)
        await db.put(code, body, { created: new Date().toISOString(), country: String(req.cf?.country || "") });
        return json(h, 201, { code, days: Number(env.CODE_DAYS || 0) });
      }
      return json(h, 503, { error: "no free code: try again" });
    }

    const m = /^\/s\/([^/]+)$/.exec(url.pathname);
    if (m) {
      const code = decodeURIComponent(m[1]).toUpperCase();
      if (!CODE_RE.test(code)) return json(h, 400, { error: "not a share code" });
      if (req.method === "GET") {
        const song = await db.get(code), days = Number(env.CODE_DAYS || 0);
        if (song && days && song.created && Date.now() - Date.parse(song.created) > days * 86400000) {
          await song.body.cancel?.();
          await db.del(code);                                     // (past its days: gone, whatever the sweep's timing)
          return json(h, 404, { error: "that code has expired", expired: true });
        }
        if (!song) return json(h, 404, { error: "no song with that code" });
        return new Response(song.body, { headers: { ...h, "Content-Type": "application/gzip", "Cache-Control": "public, max-age=3600" } });
      }
      if (req.method === "DELETE") {
        const auth = req.headers.get("Authorization") || "";
        if (!env.ADMIN_TOKEN || auth !== `Bearer ${env.ADMIN_TOKEN}`) return json(h, 401, { error: "not allowed" });
        await db.del(code);
        return json(h, 200, { deleted: code });
      }
    }
    return json(h, 404, { error: "not found" });
  },
};
