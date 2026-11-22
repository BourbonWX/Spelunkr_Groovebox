// SPDX-License-Identifier: GPL-3.0-only
// The Spelunkr Groovebox's share codes: SHARE uploads a song (and the samples it plays) and gets back a short code,
// SPGB_7K2QX9A; opening the code downloads it again. A Cloudflare Worker with a KV namespace (SONGS_KV, no card
// needed) or an R2 bucket (SONGS) to keep them in (README.md).
//   POST   /s               body: the package (gzip, made by the page), up to MAX_BYTES  ->  {"code": "SPGB_..."}
//   GET    /s/SPGB_XXXXXXX  the package as uploaded
//   DELETE /s/SPGB_XXXXXXX  removes it (moderation): Authorization: Bearer ADMIN_TOKEN
// Bindings (wrangler.toml): SONGS_KV (KV) or SONGS (R2), UPLOADS (rate limit, optional), ALLOWED_ORIGINS (var),
// ADMIN_TOKEN (secret).

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
// where the songs are kept: R2 when bound, else KV (values up to 25 MB; the songs here are 12 MB at most)
function store(env) {
  if (env.SONGS) return {
    has: async (k) => !!(await env.SONGS.head(k)),
    put: (k, body, meta) => env.SONGS.put(k, body, { httpMetadata: { contentType: "application/gzip" }, customMetadata: meta }),
    get: async (k) => (await env.SONGS.get(k))?.body ?? null,
    del: (k) => env.SONGS.delete(k),
  };
  if (env.SONGS_KV) return {
    has: async (k) => { const s = await env.SONGS_KV.get(k, "stream"); if (s) await s.cancel(); return !!s; },
    put: (k, body, meta) => env.SONGS_KV.put(k, body, { metadata: meta }),
    get: (k) => env.SONGS_KV.get(k, "stream"),
    del: (k) => env.SONGS_KV.delete(k),
  };
  throw new Error("no song store bound (SONGS_KV or SONGS)");
}
const json = (h, status, o) => new Response(JSON.stringify(o), { status, headers: { ...h, "Content-Type": "application/json" } });

export default {
  async fetch(req, env) {
    const url = new URL(req.url), h = cors(req, env), db = store(env);
    if (req.method === "OPTIONS") return new Response(null, { status: 204, headers: h });
    if (url.pathname === "/" || url.pathname === "/health") return json(h, 200, { ok: true, service: "spelunkr-groovebox-share" });

    if (url.pathname === "/s" && req.method === "POST") {
      if (h["Access-Control-Allow-Origin"] === "null") return json(h, 403, { error: "uploads only from the groovebox" });
      if (env.UPLOADS) {
        const ip = req.headers.get("CF-Connecting-IP") || "unknown";
        const { success } = await env.UPLOADS.limit({ key: ip });
        if (!success) return json(h, 429, { error: "too many shares in a minute: wait a little" });
      }
      const said = Number(req.headers.get("Content-Length") || 0);
      if (said > MAX_BYTES) return json(h, 413, { error: "too big to share" });
      const body = new Uint8Array(await req.arrayBuffer());
      if (body.length > MAX_BYTES) return json(h, 413, { error: "too big to share" });
      if (body.length < 18 || body[0] !== 0x1f || body[1] !== 0x8b) return json(h, 400, { error: "not a groovebox song" });
      for (let tries = 0; tries < 5; tries++) {
        const code = newCode();
        if (await db.has(code)) continue;                         // (35 bits: a clash is rare, but never overwrite)
        await db.put(code, body, { created: new Date().toISOString(), country: String(req.cf?.country || "") });
        return json(h, 201, { code });
      }
      return json(h, 503, { error: "no free code: try again" });
    }

    const m = /^\/s\/([^/]+)$/.exec(url.pathname);
    if (m) {
      const code = decodeURIComponent(m[1]).toUpperCase();
      if (!CODE_RE.test(code)) return json(h, 400, { error: "not a share code" });
      if (req.method === "GET") {
        const body = await db.get(code);
        if (!body) return json(h, 404, { error: "no song with that code" });
        return new Response(body, { headers: { ...h, "Content-Type": "application/gzip", "Cache-Control": "public, max-age=3600" } });
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
