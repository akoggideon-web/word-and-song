# Word & Song for OBS

Scripture and song lyrics for church livestreams, controlled from a dock inside OBS Studio,
with optional **offline** speech detection (Whisper) that suggests verses as the preacher
reads them and follows along with worship lyrics.

- **Scripture** – type `jn 3:16`, `1 cor 13 4-7`, `ps 23` or any words to search. Enter shows it.
  KJV, ASV, WEB and BSB are built in; import your church's licensed translation (NKJV, NIV, ESV…).
- **Lyrics** – song library + service setlist. Section headers (`[Verse 1]`, `Chorus:`) and blank
  lines become slides; long sections split automatically (2 lines per slide by default).
- **Listen** – picks up "John chapter three verse sixteen", "Romans 8:28", or a verse being quoted
  without a reference. In Worship mode it tracks which slide is being sung. Detections wait for
  you to confirm unless you turn on auto-show. Nothing leaves the computer.
- **Display source** – a lower third, full-screen or top-of-screen card with fade, auto-shrinking
  text, your fonts and colours. Use one source for scripture and another for lyrics if you like.
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
- Bible texts: King James Version, American Standard Version (1901), World English Bible and
  Berean Standard Bible are in the public domain.
