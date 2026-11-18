# Word & Song for OBS

Scripture and song lyrics for church livestreams, controlled from a dock inside OBS Studio,
with optional **offline** speech detection (Whisper) that suggests verses as the preacher
reads them and follows along with worship lyrics.

- **Scripture** – type `jn 3:16`, `1 cor 13 4-7`, `ps 23` or any words to search. Enter shows it.
  13 public-domain translations are built in (see below); import your church's licensed translation
  (NKJV, NIV, ESV…).
- **Lyrics** – song library + service setlist. Section headers (`[Verse 1]`, `Chorus:`) and blank
  lines become slides; long sections split automatically (2 lines per slide by default).
- **Listen** – picks up "John chapter three verse sixteen", "Romans 8:28", or a verse being quoted
  without a reference. In Worship mode it tracks which slide is being sung. Detections wait for
  you to confirm unless you turn on auto-show. Nothing leaves the computer.
- **Cards** – speaker names and titles, sermon points, or any quick text. Type it live or keep a
  saved list for the service.
- **Graphics** – a playlist of your designed images (PNG/JPG from Canva, Photoshop, etc.). Full-frame
  PNGs with transparency line up exactly, so designed lower thirds work as-is.
- **Themes** – like ProPresenter: pick a theme per tab (Scripture, Lyrics, Cards) and what's on screen
  restyles instantly. Built in: Midnight Card, Gold Bar, Clean White, Red Tag, Blue Split,
  Minimal (text only), Lyrics – Bold Centered, Teal Bar. Save your own from any display.
- **Display source** – a lower third, full-screen or top-of-screen display with fades and
  auto-shrinking text. Each display chooses what it shows (scripture / lyrics / cards / graphics),
  so a livestream lower third and a full-screen projector output can run side by side.
- **Hotkeys** (Settings → Hotkeys): Show preview, Clear, Next, Previous — works with a Stream Deck.

---

## Getting the installers (GitHub builds them for you)

1. Create a new **empty** repository on GitHub (e.g. `word-and-song`).
2. Upload this folder to it:
   ```sh
   cd word-and-song
   git init && git add . && git commit -m "Word & Song 1.0.0"
   git branch -M main
   git remote add origin https://github.com/<you>/word-and-song.git
   git push -u origin main
   ```
3. Open the **Actions** tab. The *Push* workflow builds macOS, Windows and Ubuntu (~15–25 min).
   Each finished run has downloadable artifacts at the bottom of its page.
4. To publish a release with installers attached, push a version tag:
   ```sh
   git tag 1.0.0 && git push origin 1.0.0
   ```
   A draft release appears under **Releases** with
   `word-and-song-1.0.0-macos-universal.pkg` and `word-and-song-1.0.0-windows-x64.zip`.

## Installing

**Windows** – unzip and copy the `word-and-song` folder into
`C:\ProgramData\obs-studio\plugins\` (so you have
`C:\ProgramData\obs-studio\plugins\word-and-song\bin\64bit\word-and-song.dll`). Restart OBS.

**macOS** – run the `.pkg`. The build is not notarised, so the first time macOS may refuse it:
right-click the .pkg → Open, or allow it in System Settings → Privacy & Security. If OBS then
doesn't show the plugin, run
`xattr -dr com.apple.quarantine ~/Library/Application\ Support/obs-studio/plugins/word-and-song.plugin`.

Then in OBS: **Docks → Word and Song**. From the dock's **⋯** menu choose
*Add lower-third display to current scene* (and/or the full-screen one).

## Themes

Each tab's theme picker sets the look for that kind of content, and any display set to
**Follow the dock's themes** (the default) uses it. A full-screen display scales the theme up.

**Theme Editor** – click **✎** next to any theme picker (or **⋯ → Theme editor…**, or **Edit themes…**
in a display's properties). Pick a theme on the left and change its style, font, sizes, colours,
box, alignment and spacing with a live preview. Built-in themes are kept as they are: saving one
makes your own copy. Saving a theme you made updates anything on screen that uses it, straight away.

**Images in themes** (Theme Editor → Images):
- **Box background** – a texture, photo or brand pattern inside the box. The box colour becomes a
  tint over it (set its opacity to 0% to show the image untouched). Fill (crop) or stretch.
- **Logo** – your church or series logo inside the box, left or right of the text.
- **Designed graphic** – import a full-frame lower-third design (e.g. a 1920×1080 PNG from Canva or
  Photoshop, transparent where the video should show) and set the **text area** to where the words
  go; the preview draws it as a dashed box. Scripture, lyrics and names flow into that spot and
  shrink to fit.

Imported images are copied into the plugin's `theme-assets` folder, so themes keep working if the
original file is moved or deleted.

Styles: **Card** (rounded box), **Full-width bar** (edge-to-edge with an accent stripe),
**Tag on top** (reference in a coloured tab), **Split panel** (reference in a coloured side panel),
**Text only** (no box, outlined text — great over motion backgrounds), **Designed graphic**
(your own image, see above).

**Per-display adjustments** (display properties, work with any theme): *Size on this display* and
*Move up / down*. Sizes are designed for 1080p and scale automatically for 720p or 4K canvases.

## Speech detection setup

1. Download a Whisper model (dock → Listen → **Get**):
   - `ggml-base.en.bin` (148 MB) – fast, good on any recent PC or Mac. **Start here.**
   - `ggml-small.en.bin` (488 MB) – more accurate; fine on Apple Silicon or a strong PC.
2. Pick it with the **…** button.
3. **Audio**: use a *direct* feed — an aux send from the mixer with the pastor's mic (Sermon) or
   the lead vocal (Worship) into an audio input in OBS. Room mics and music bleed hurt accuracy a lot.
4. **Start listening**. Detected verses appear under *Detected*: click to preview, double-click to
   show. Leave *auto-show* off until you trust it in your room.

Apple Silicon Macs use the GPU (Metal). Windows uses the CPU (AVX2); base.en needs roughly one
core while listening.

## Built-in translations

All public domain, from [scrollmapper/bible_databases](https://github.com/scrollmapper/bible_databases)
and [TehShrike/world-english-bible](https://github.com/TehShrike/world-english-bible):

| Code | Translation | | Code | Translation |
|---|---|---|---|---|
| KJV | King James Version | | BBE | Bible in Basic English (1949/1964) |
| WEB | World English Bible | | YLT | Young's Literal Translation (1898) |
| BSB | Berean Standard Bible | | WBS | Webster's Bible (1833, revised) |
| ASV | American Standard Version (1901) | | GNV | Geneva Bible (1599) |
| AKJV | American King James Version | | ACV | A Conservative Version |
| UKJV | Updated King James Version | | RV1909 | Reina-Valera 1909 (Español) |
| NHEB | New Heart English Bible | | | |

Modern translations such as NIV, NKJV, ESV, NLT, AMP and MSG are copyrighted, so they can't be
shipped with the plugin. If your church is licensed to display one, import it (below).
Reina-Valera references show Spanish book names on screen ("Juan 3:16"); typing references in the
search box is in English ("jn 3:16").

## Importing a licensed translation

Dock **⋯ → Import a Bible translation…** accepts:

- Tab- or comma-separated text: `Book, Chapter, Verse, Text` (book as a name like `John` /
  `1 Corinthians`, or a number 1–66). A header row is fine.
- JSON in the common `{"books":[{"name":…,"chapters":[{"chapter":…,"verses":[{"verse":…,"text":…}]}]}]}`
  shape, or a flat list of `{book, chapter, verse, text}`.

Only import translations your church is licensed to display. Imported files are stored in the
plugin's data folder (dock **⋯ → Open data folder**).

## Song files

**Import** accepts `.txt` and ChordPro (`.cho`, `.chordpro`, `.pro`). Chord lines and inline chords
are removed. In a `.txt`, the first line is used as the title when a blank line follows it.

## Building locally

```sh
cmake --preset macos         # or windows-x64 / ubuntu-x86_64
cmake --build --preset macos
```
Parser tests (no OBS needed):
```sh
cmake -S . -B build-tests -DBUILD_TESTS=ON -DENABLE_WHISPER=OFF && cmake --build build-tests --target word-and-song-tests
```
`-DENABLE_WHISPER=OFF` builds without speech detection. `-DWHISPER_CPP_SOURCE_DIR=…` uses a local
whisper.cpp checkout instead of downloading v1.8.5.

## Credits & licences

- Plugin: GPL-2.0-or-later (see `LICENSE`), based on the official OBS plugin template.
- Speech recognition: [whisper.cpp](https://github.com/ggml-org/whisper.cpp) (MIT).
- Bible texts: all bundled translations are in the public domain (list above).
