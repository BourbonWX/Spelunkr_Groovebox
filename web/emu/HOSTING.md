# The Spelunkr Groovebox (@VERSION@)

This folder is a complete website: the Spelunkr Groovebox, Phantom Tracker's firmware running in the browser as a
groovebox laid out like a TV guide. Upload the whole folder to any web host and send people the link. Visitors don't need an FM-1 or an
account, and nothing is uploaded to your server: their work stays in their own browser.

## What visitors can do

- **Play the instrument.** Click the panel, use the computer keyboard (the map is under the instrument), or plug in a
  MIDI keyboard (Chrome and Edge).
- **Record.** This captures exactly what they hear, live, including PHANTOM and live knob moves, and saves it as a
  24-bit WAV.
- **Export song.** This renders the song (GLO > SONG) or the pattern from the top, faster than real time, to a 24-bit WAV.
  It can also make one stem per FEED, delivered in a zip, ready for any DAW.
- **Share link.** This makes a link that holds the whole song: the FEEDs and their sounds, patterns A–F and GLO >
  SONG, compressed into the link itself (a few thousand characters at most). Anyone who opens it hears the song in
  their own browser and can carry on from it. Nothing is stored on your server: the song travels in the link. The
  link points at your site, so it only works once the site is online.
- **Save project file / Open project file.** A `.phantom` file holds everything the emulator has saved: the current
  music, the patterns A–F, the song, saved projects, the user's sounds and settings. Open it on any site that runs
  this emulator to carry on, or to share a song with a friend.
- **Reset.** This forgets that browser's saves and starts from a blank FM-1.

Their work also stays in their browser between visits, saved the way the FM-1 saves: the music when playback stops,
and projects with SAVE.

## Hosting it

**It must be served over HTTPS.** Browsers only run the audio engine (AudioWorklet) on secure pages. Any normal web
host gives you HTTPS. Opening `index.html` straight from your disk won't work. To try it on your own machine, run
`python3 -m http.server` in this folder and open http://localhost:8000.

**Any static host works:** GitHub Pages, Netlify, Cloudflare Pages, Neocities, or your own Apache or nginx server. The
site needs no server-side code. It's about 1.5 MB, plus the source archive.

**The `.wasm` file must be served as `application/wasm`.** Most hosts already do this.
- **Apache:** the `.htaccess` file included here sets it.
- **Netlify and Cloudflare Pages:** the `_headers` file included here sets it.
- **nginx:** check that `mime.types` has the line `application/wasm wasm;`, and add it if it's missing.

**A subfolder is fine.** Every path is relative, so `https://example.com/phantom/` works as well as a domain of its own.

**To put it inside another page,** use an iframe. The `allow` attribute lets MIDI keyboards work inside the frame:

```html
<iframe src="https://example.com/phantom/" width="1040" height="900" style="border:0; max-width:100%"
        allow="midi; autoplay" title="Phantom Tracker"></iframe>
```

## The licence: please keep the source with it

Phantom Tracker is free software under the GNU GPL version 3, like Dungeon Tracker and Felucca before it. If you put it
on a website, you are distributing it, and the GPL asks that the people you give it to can get its source.

This folder already does that:
- `source/@SRC@` is the complete source of exactly this build.
- The page's footer links to it.

**Keep that file and that link.** If you change the code, put your changed source there instead.

Keep the credits in the footer: Felucca by Leo Kuroshita (Hügelton Instruments) and Dungeon Tracker by Bourbon Weather.
The other licences (VULT's MIT, the DotGothic16 font's OFL and others) are in `licenses/`, and the GPL is in
`LICENSE.txt`. M-VAVE and FM-1 are trademarks of their owners. This project isn't affiliated with them, so don't
present the site as theirs.

## Good to know

- **It's the real firmware.** The emulator is the FM-1's own code compiled to WebAssembly, so it sounds and behaves
  like the instrument. Projects made on the instrument and on the website both use the same save format inside.
  There is no way yet to move a project between the two: that needs the web editor.
- **What it can't do:** USB audio, firmware updates, MIDI out, MIDI clock and the serial console. MIDI in works for
  notes and controllers, from a keyboard or controller connected to the computer.
- **Stems won't sum exactly to the mix.** Each stem plays alone through the master limiter, and the kick's ducking only
  happens when the drums are playing. The mix file is the reference.
- **Browsers:** current Chrome, Edge, Firefox and Safari, including iPhone and Android. The page has a layout for
  phones held upright.

## Files

| File | What it is |
|---|---|
| `index.html` | The page: the instrument and the studio bar |
| `felucca.wasm` | The firmware, compiled for the browser |
| `felucca-simd.wasm` | The same firmware using WebAssembly SIMD: lighter on the CPU, and picked automatically where the browser supports it |
| `worklet.js` | Runs the firmware in the browser's audio thread, and handles live recording |
| `bounce.js` | Renders a song faster than real time, in the background |
| `guide.html`, `manual.html` | The quick start and the full manual |
| `fonts/` | The screen's labels (DotGothic16, SIL OFL) |
| `source/` | The complete source of this build |
| `LICENSE.txt`, `licenses/` | The GPL and the other licences, and the credits |
| `.htaccess`, `_headers` | Settings for Apache and for Netlify or Cloudflare |
