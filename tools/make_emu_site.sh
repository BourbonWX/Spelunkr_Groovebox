#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# The browser emulator as a folder anyone can put on a website (Phantom Tracker's "studio"): the emulator, the guide
# and the manual as pages, this build's complete source (GPL-3.0: whoever hosts it hands the source on with it), the
# licences, and the web servers' settings for .wasm.
#   tools/make_emu_site.sh [OUT]       (default OUT: build/phantom-tracker-web; also OUT.zip)
# Needs what tools/build_emu_clang.sh needs, and python3's markdown for the pages.
set -e
cd "$(dirname "$0")/.."
OUT=${1:-build/spelunkr-groovebox-web}
VERSION=$(sed -n 's/^#define FELUCCA_VERSION "\([^"]*\)".*/\1/p' firmware/src/felucca.c | head -n 1)
SRC="spelunkr-groovebox-$VERSION-source.tar.gz"
rm -rf "$OUT" "$OUT.zip"
EMU_SOURCE="source/$SRC" sh tools/build_emu_clang.sh "$OUT"

# the source: the tree as it is (no build output, no git history)
mkdir -p "$OUT/source"
tar --exclude=./build --exclude=./.git --exclude='*/node_modules' --exclude=./out --exclude=./CLAUDE.md \
    --transform "s,^\.,spelunkr-groovebox-$VERSION," -czf "$OUT/source/$SRC" .

# the licences
cp LICENSE "$OUT/LICENSE.txt"
mkdir -p "$OUT/licenses"
cp LICENSES/*.txt "$OUT/licenses/"
cp CREDITS.md "$OUT/licenses/CREDITS.md"

# the guide and the manual as pages
python3 - "$OUT" "$VERSION" <<'EOF'
import sys, re, markdown
out, version = sys.argv[1], sys.argv[2]
CSS = """:root{--bg:#f6f6f7;--fg:#151517;--dim:#55555a;--rule:#d0d0d4;--code:#ececef;--link:#1d4ed8}
@media (prefers-color-scheme:dark){:root:not([data-theme=light]){--bg:#141415;--fg:#e8e8ea;--dim:#a0a0a6;--rule:#3a3a3e;--code:#232326;--link:#8ab4ff}}
:root[data-theme=dark]{--bg:#141415;--fg:#e8e8ea;--dim:#a0a0a6;--rule:#3a3a3e;--code:#232326;--link:#8ab4ff}
html,body{background:var(--bg);color:var(--fg)}body{margin:0;font:15px/1.6 -apple-system,BlinkMacSystemFont,"Helvetica Neue",Arial,sans-serif}
main{max-width:46em;margin:0 auto;padding:24px 16px 48px}a{color:var(--link)}nav.top{font-size:13px;color:var(--dim);margin-bottom:12px}
code,kbd{font:13px ui-monospace,Menlo,monospace;background:var(--code);padding:0 4px;border-radius:3px}pre{background:var(--code);padding:10px;overflow:auto}
pre code{padding:0}blockquote{margin:0;padding:0 14px;border-left:3px solid var(--rule);color:var(--dim)}
table{border-collapse:collapse;display:block;overflow:auto}td,th{border:1px solid var(--rule);padding:4px 8px;vertical-align:top}
h1,h2,h3{line-height:1.25}hr{border:0;border-top:1px solid var(--rule)}img{max-width:100%}"""
def page(src, dst, title, links):
    md = open(src, encoding="utf-8").read()
    for a, b in links.items():
        md = md.replace(a, b)
    body = markdown.markdown(md, extensions=["tables", "fenced_code", "sane_lists"])
    open(f"{out}/{dst}", "w", encoding="utf-8").write(f"""<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>{title}</title><style>{CSS}</style></head><body><main>
<nav class="top"><a href="index.html">The groovebox</a> · <a href="guide.html">Guide</a> · <a href="manual.html">Full manual</a> · Spelunkr Groovebox {version}</nav>
{body}
</main></body></html>
""")
links = {"(../PHANTOM-COSMONAUT.md)": "(manual.html)", "(PHANTOM-COSMONAUT.md)": "(manual.html)",
         "(DUNGEON-TRACKER-1.5.md)": "(https://github.com/BourbonWX/Dungeon-Tracker)",
         "(docs/DUNGEON-TRACKER-1.5.md)": "(https://github.com/BourbonWX/Dungeon-Tracker)",
         "(docs/QUICKSTART.md)": "(guide.html)", "(QUICKSTART.md)": "(guide.html)"}
page("docs/GROOVEBOX-GUIDE.md", "guide.html", "Spelunkr Groovebox guide", links)   # (the beginner's: the page's own words)
page("PHANTOM-COSMONAUT.md", "manual.html", "The device under the hood: Phantom Tracker's manual", links)   # (POWER USER's)
EOF

# the web servers: .wasm as application/wasm (Apache, Netlify / Cloudflare Pages)
cat > "$OUT/.htaccess" <<'EOF'
AddType application/wasm .wasm
AddType text/javascript .js .mjs
EOF
cat > "$OUT/_headers" <<'EOF'
/*.wasm
  Content-Type: application/wasm
EOF
cp web/emu/HOSTING.md "$OUT/README.md"
sed -i "s/@VERSION@/$VERSION/g; s/@SRC@/$SRC/g" "$OUT/README.md"

(cd "$(dirname "$OUT")" && rm -f "$(basename "$OUT").zip" && zip -qr "$(basename "$OUT").zip" "$(basename "$OUT")")
echo "site: $OUT ($(du -sh "$OUT" | cut -f1)), $OUT.zip ($(du -h "$OUT.zip" | cut -f1))"
