# Song codes: the share service

SHARE in the groovebox uploads the song, and the samples it actually plays, to this small service. The service files
it under a short code, `SPGB_7K2QX9A`. Anyone who opens `…/Spelunkr_Groovebox/#SPGB_7K2QX9A`, or types the code into
**CODE**, gets the song and its samples in their own groovebox. Without the service, SHARE makes the long `#song=` link
as before (no samples).

It is a [Cloudflare Worker](https://developers.cloudflare.com/workers/) (`src/index.js`) and an R2 bucket to keep the
songs in (Cloudflare's free tier: 10 GB, a million shares and 10 million opens a month; R2 asks for a card on file but
charges nothing inside that). Samples are what take room: about 45 KB a second of sample, against 1-2 KB for a song.
The live service also keeps its first KV namespace bound, so the codes made before the move to R2 (Nov 23 2026) still
open: new songs go to R2, a code is looked for in R2, then in KV. A setup with only KV (no card: 1 GB, 1,000 shares
a day) still works: leave the `[[r2_buckets]]` out of `wrangler.toml`.

## Set it up (once, about 10 minutes)

On a computer with [Node.js](https://nodejs.org) 18 or newer, in this folder:

```sh
npx wrangler login                              # opens the browser: sign in to (or make) a free Cloudflare account
npx wrangler r2 bucket create spgb-songs        # (asks for R2 on your account first: dashboard, Storage & databases > R2)
npx wrangler r2 bucket lifecycle add spgb-songs clear-daily-caps caps/ --expire-days 2   # clears the daily counts
npx wrangler deploy                             # prints the address, e.g. https://spgb-share.<you>.workers.dev
npx wrangler secret put ADMIN_TOKEN             # a long password of your own, for deleting songs (below)
```

Then tell the site where the service is: on GitHub, **Settings → Secrets and variables → Actions → Variables → New
repository variable**, name `SHARE_API`, value the address `wrangler deploy` printed (no slash at the end). Run the
**Pages** workflow again (Actions → Pages → Run workflow), or push anything: SHARE now gives codes.

## What it does

- `POST /s`: a song from the groovebox (gzip, 12 MB at most) gets a new code. Only the pages in `ALLOWED_ORIGINS`
  (`wrangler.toml`) can upload. Each visitor can share 10 times a minute (`[[ratelimits]]`) and 50 times a UTC day
  (`DAILY_SHARES`; counted in the bucket under `caps/`, by a hash of the address, never the address itself).
- `GET /s/SPGB_XXXXXXX`: the song, for anyone.
- `DELETE /s/SPGB_XXXXXXX` with `Authorization: Bearer <ADMIN_TOKEN>`: removes one, if something bad turns up:

  ```sh
  curl -X DELETE -H "Authorization: Bearer YOUR-ADMIN-TOKEN" https://spgb-share.<you>.workers.dev/s/SPGB_XXXXXXX
  ```

Codes are `SPGB_` and 7 of `23456789ABCDEFGHJKLMNPQRSTUVWXYZ` (no 0 O 1 I, easy to read off a screen): 34 billion,
random, so nobody can step through them. Every share is a new code (a song shared again, changed or not, gets another
one; the old code keeps the song as it was). A code lasts `CODE_DAYS` (30) days from the moment it was shared (each code has its own clock; nothing is wiped on a schedule): after that it says it has expired and
is deleted, and the bucket's lifecycle rule `songs-expire` (prefix `SPGB_`, 30 days after each one was written) clears the ones nobody opened. Each is stored with the time it was shared and
the sharer's country, nothing else about them.

**Try it locally**: `npx wrangler dev --port 8787 --var ADMIN_TOKEN:test`, then build the groovebox with
`SHARE_API=http://127.0.0.1:8787 sh tools/build_emu_clang.sh` and serve `build/emu` on port 8769.
