# Polyend Tracker CE: what it means for Modular-Trackular

Status: review only, 2026-11-02. No code changed. Nothing cloned, downloaded or built:
everything below comes from reading the repository online (GitHub pages, raw files, the
GitHub API). Where a claim needs a build or a hardware run to confirm, it says so.

Repository: [polyend/tracker-ce](https://github.com/polyend/tracker-ce), tag
[`v1.0.0`](https://github.com/polyend/tracker-ce/tree/v1.0.0) (commit `9b9a13b`, `main` on
2026-11-02). Links below point at that tag so the line numbers stay valid.

**License parts updated 2026-11-04.** Polyend changed the license in commit
[`9cc7aae`](https://github.com/polyend/tracker-ce/commit/9cc7aae) on 2026-11-03. The
license statements here, and the links in [section 1](#1-license), now follow that commit.
`LICENSE`, `CONTRIBUTING.md` and the library license are unchanged since, up to `main` at
`7fd6c2c`. Everything else still describes v1.0.0.

**This is not legal advice.** It is a careful reading of the license text by someone who
isn't a lawyer. If anything here decides whether you publish, ask one, or ask Polyend.

## Summary

- **License.** Since 2026-11-03: MIT, plus a "Wholesale Commercial Clone Restriction".
  You may modify CE and publish your changes, source and binaries, with the copyright
  notice, the permission notice and Part 2's conditions attached. You may not sell
  firmware builds, sell the complete application as a paid product, or make commercial
  clone hardware. There is no copyleft (no duty to publish source), no trademark grant
  and no CLA, but a pull request to Polyend gives them the right to relicense your code.
  Two closed synth libraries have their own license: free use on Tracker+ and Mini and in
  non-commercial projects, no fee, no reverse engineering. [Section 1](#1-license)
- **Tracker+ features.** CE 1.0.0 is past 1.2.0: it has 1.2.0's features (synths, USB
  audio In+Out) and adds eleven new ones plus fixes, including the 44.1 kHz tempo fix,
  per-step `m`/`V` columns, slice Play Thru, more Chance conditions and SD streaming. It
  saves patterns in a new format (version 6) that 1.2.0 can't read. A ready-made
  `TrackerPlus_CE_1.0.0.ptf` is published, and everything needed to build one is in the
  repository. [Section 2](#2-whats-new-on-the-tracker-compared-with-stock-120)
- **Porting.** Possible for all 15 mods. CE already has slicethru's main idea (Play
  Thru) and a different version of condtrig, whose Chance values clash with ours. The
  other mods become ordinary source changes. The pick-and-choose model survives best as
  runtime switches in Config in one build. That ends the 2^N mix testing, but every mod
  needs a new hardware test, and each CE release needs another one.
  [Section 3](#3-porting-modular-trackular-to-ce)
- **Changing Polyend's code.** Allowed, except for the two closed synth/DSP libraries. CE already fixes the 44.1 kHz issue. The jacks going silent with
  USB In+Out, RAM limits and the master effects all become source edits. The bootloader
  and the stock synth engines stay out of reach. [Section 4](#4-modifying-polyends-own-code)
- **Recommendation.** Run both for a while. Keep shipping on 1.2.0, try a CE fork with
  one small mod, and decide once CE has settled. [Section 5](#5-recommendation)

---

## 1. License

### What changed on 2026-11-03

Before `9cc7aae`, CE was PolyForm Noncommercial 1.0.0 with Polyend's extra permissions, and
only `editor-fx-sdk/` was MIT. In practice:

- **Free forks and free `.ptf` builds:** still allowed. We lose nothing, and nothing in the
  fork has to change.
- **Selling:** selling builds or the complete application is still banned. Commercial use
  of individual parts is now allowed: drivers, DSP, UI helpers, SDKs and open synth engines
  can go into your own products, paid ones included, if they aren't clones of the Tracker.
- **Reusing CE code elsewhere:** the license no longer stops it. CE code can be copied into
  other projects with the notices. Our own rule that CE work stays in the fork still stands
  (see [below](#what-it-changes-for-no-polyend-code)).
- **Closed libraries:** the clause barring them from anything that "does not run on
  authentic Polyend hardware" is gone. They may now also be used in personal, educational,
  research, hobbyist or open-source DIY non-commercial projects, if no fee is charged. So
  **running them in our PC emulator is now allowed on a plain reading**, but reverse
  engineering them is still banned (see [the emulator row](#publishing-a-custom-firmware-on-github)).
- **Original Tracker:** the library license no longer stops a free, non-commercial project
  linking them for that device. It still wouldn't work: a read-only check on 2026-11-02
  found them built for the Cortex-M7 and too big for its free flash.
- **Gone with PolyForm:** "competing product", the `Required Notice:` line and the 32-day
  period to fix a violation. The new text has no such period.
- **Unchanged:** the relicensing right on pull requests, no trademark grant, third-party
  code under its own licenses, and stock 1.2.0 outside all of this.

### What the license is

The [`LICENSE`](https://github.com/polyend/tracker-ce/blob/9cc7aae/LICENSE) file has three parts, and two more documents add terms:

| Part | Covers | Terms |
|---|---|---|
| Part 1 | Source code, headers, SDKs (`editor-fx-sdk/`, `synth/include/`), open synth engines (`core/synth/engines/`), `platforms/`, `util/`, `core/`, documentation | MIT, with one extra condition: Part 2's conditions travel with every copy |
| Part 2 | The complete firmware/application as a whole | Lists what's expressly allowed, and bans commercial clones and selling builds |
| Part 3 | `synth/lib/` and third-party code | Points to the two documents below |
| [`synth/lib/LICENSE-PROPRIETARY.txt`](https://github.com/polyend/tracker-ce/blob/9cc7aae/synth/lib/LICENSE-PROPRIETARY.txt) | `libPolySynth.a`, `libPolyDSP.a` (binary only) | Polyend proprietary: use, link and redistribute unmodified, for free firmware on Tracker+ and Mini or for non-commercial projects; no fee; no reverse engineering |
| [`NOTICE.md`](https://github.com/polyend/tracker-ce/blob/v1.0.0/NOTICE.md) | Third-party code (NXP SDK, CMSIS, FatFs, Teensy Audio, ETL, fonts…) | Each keeps its own license (BSD-3, Apache-2.0, MIT, OFL) |
| [`CONTRIBUTING.md`](https://github.com/polyend/tracker-ce/blob/9cc7aae/CONTRIBUTING.md#L66-L67) | Pull requests to polyend/tracker-ce | Your code goes in under the same license, and Polyend may relicense it |

### The clauses this relies on

- Part 1 gives the standard MIT rights to "use, copy, modify, merge, publish, distribute,
  sublicense, and/or sell", subject to Part 2 only for the complete integrated application
  ([L18–28](https://github.com/polyend/tracker-ce/blob/9cc7aae/LICENSE#L18-L28)).
- Notices: "The above copyright notice, this permission notice, and the conditions in
  Part 2" go in "all copies or substantial portions"
  ([L30–31](https://github.com/polyend/tracker-ce/blob/9cc7aae/LICENSE#L30-L31)). This is wider than plain MIT, although
  `CONTRIBUTING.md` and Part 2 itself say "pure MIT" for some parts. The cautious reading:
  keep Part 2's text with reused code too.
- Expressly allowed: making money with music made on it, selling `.ptfx` plugins, and
  reusing individual components in your own open-source or commercial software, or in
  hardware that isn't a clone ([L45–62](https://github.com/polyend/tracker-ce/blob/9cc7aae/LICENSE#L45-L62)).
- Not allowed: commercial hardware clones, paid ports or derivatives of the complete
  application, and to "Sell or charge a fee for downloading or obtaining compiled `.ptf`
  firmware images or source distributions"
  ([L64–79](https://github.com/polyend/tracker-ce/blob/9cc7aae/LICENSE#L64-L79)).
- Trademarks: no right to the names, logos, boot screens or trade dress "in connection with
  any third-party commercial product" ([L80–84](https://github.com/polyend/tracker-ce/blob/9cc7aae/LICENSE#L80-L84)).
- Closed libraries, what's allowed: free firmware builds for Tracker+ and Mini, and
  "personal, educational, research, hobbyist, or open-source DIY non-commercial hardware or
  software projects (provided that no fee is charged …)"
  ([L10–20](https://github.com/polyend/tracker-ce/blob/9cc7aae/synth/lib/LICENSE-PROPRIETARY.txt#L10-L20)).
- Closed libraries, what isn't: anything paid or commercial, charging for builds that
  contain them, and to "decompile, disassemble, reverse-engineer, or attempt to derive the
  source code of the proprietary algorithms"
  ([L22–32](https://github.com/polyend/tracker-ce/blob/9cc7aae/synth/lib/LICENSE-PROPRIETARY.txt#L22-L32)).
- The README invites forks: "we actively encourage community forks!"
  ([README L57](https://github.com/polyend/tracker-ce/blob/9cc7aae/README.md?plain=1#L57)).

### Publishing a custom firmware on GitHub

| Question | Answer | How sure |
|---|---|---|
| Fork it, change it, publish the source on GitHub | Allowed, with LICENSE kept. Charging for a source distribution isn't | Clear |
| Publish built `.ptf` binaries (GitHub Releases, Pages) | Allowed if free. A CE build includes the closed `.a` libraries; their license allows redistributing them unmodified in free firmware for Tracker+ and Mini | Clear |
| Publish a modified `.ptf` built from **CE** | Allowed if free, same as above. This is the big change from today | Clear |
| Publish a modified `.ptf` built from **stock 1.2.0** | Not covered. The 1.2.0 binary was never released under these terms, so the repo's rule (patches only) still stands for it | Ambiguous at best; treat as not allowed |
| Copyleft: must you publish your source? | No. MIT has no source-sharing duty. Binaries alone would be allowed | Clear |
| Your own new files in a fork | Yours; you may keep them MIT. Changes to CE files are derivatives and stay under CE's license, as does the fork as a whole | Clear in principle; where a file stops being "yours" is a judgment call |
| Sublicensing | MIT allows it for Part 1 material, but the notices and Part 2's conditions must stay with it. The closed libraries' license has no sublicensing right: pass them on unmodified under their own terms | Clear |
| Reusing individual CE parts in other projects | Allowed, commercial products included, if the result isn't a Tracker clone. Keep the notices and Part 2's text | Clear |
| Attribution | Keep the copyright notice, the permission notice and Part 2 (in practice, LICENSE) with every copy, including binary downloads. Third-party BSD-3/Apache code in the image wants its notices shipped with binaries too, so ship `NOTICE.md` beside the `.ptf`. The fork's CI already puts `LICENSE` and `NOTICE.md` in the firmware artifact | Clear for Polyend's part; third-party part is ordinary practice |
| Selling | Free downloads and hobby work are fine. Selling builds, paid downloads, paid early access: not allowed. The closed libraries add "no fee" for any project that uses them | Clear |
| Donations, GitHub Sponsors, Patreon | Not addressed. A voluntary donation isn't a fee "for downloading or obtaining" a build on a plain reading; anything that buys access to a build is. Sponsorship could still make a project look commercial, which matters for the closed libraries | **Ambiguous**, narrower than before. Closed for this project: none planned (2026-11-04) |
| Commercial clone restriction | Every ban in Part 2 is about commercial or paid products. A free fork of the complete firmware isn't caught. "Competing product" is no longer in the license | Clear |
| Device locking | None any more for free, non-commercial work: the closed libraries are allowed on Tracker+ and Mini, and in non-commercial projects generally. Firmware for the original Tracker doesn't link them | Clear on the text |
| Names and trademarks | No trademark grant. Saying "for the Polyend Tracker+" to describe compatibility is ordinary nominative use; a fork named "Polyend …" or "Tracker CE …" isn't safe, though the license's wording only mentions commercial products. The `.ptf` file name must start with `TrackerPlus` for the updater to list it, which is technical, not branding | Grant: clear (none). Naming: judgment, keep the "not affiliated" line |
| CLA | None for forks. A pull request to polyend/tracker-ce gives Polyend the right to "use, distribute, and relicense" your change, commercially too | Clear |
| Reverse engineering | Fine for the Part 1 material (you have the source anyway). Forbidden for `libPolySynth.a`/`libPolyDSP.a` "except to the extent" local law overrides it | Clear |
| Running a CE image, closed libraries included, in our PC emulator | Allowed on a plain reading. Section 1(b) grants "use" of the unmodified libraries in personal, research or hobbyist non-commercial software projects where no fee is charged, and the old ban on software that doesn't run on Polyend hardware is gone. Section 2(c) still applies: no disassembly, and no tracing or probing to work out how they work. So call them through their public interface, leave their code unpatched, and put no hooks inside it. Matters for synth tests, see [the emulator](#the-emulator) | Running them: clear on the text. Where testing becomes reverse engineering: judgment |
| Warranty and support | None. Polyend says custom firmware isn't covered by warranty; out-of-warranty repair is offered | Clear (README) |

### What it changes for "No Polyend code"

The rule in `CLAUDE.md` exists because nothing allowed us to copy or ship Polyend code.
CE changes that only for CE material, under CE's terms:

**Allowed now that wasn't before:**
- A separate fork of tracker-ce containing Polyend's source and our changes, released as
  source and as `.ptf` binaries under CE's license.
- Shipping firmware binaries at all (for CE-based builds), which ends the "patches only,
  never `.ptf`" constraint for that line.
- Reading CE to understand 1.2.0, and writing what we learn into our notes in our own
  words (struct layouts, what a function does, names for functions in
  `ghidra/names_1.2.0.tsv`). This was already fine for public material; CE just makes
  far more of it public.

**Still not allowed:**
- Pasting CE code into this MIT repository. This is now our own rule, not the license's:
  CE code could be copied here with its notices and Part 2's text. Keep this repo
  Polyend-code-free anyway and put CE-derived code in the fork.
- Shipping modified stock 1.2.0 `.ptf` files: CE's license doesn't cover the 1.2.0
  binary.
- Reverse engineering the two closed libraries (under CE's license). Our earlier work on
  1.2.0's synth code wasn't done under that license. Whether it binds future work on
  1.2.0's copy of the same code is unclear; the cautious reading is to stop digging into
  the synth internals.

A rule that fits both lines: "This repository holds no Polyend code. CE-based work lives
in a separate fork under CE's license."

---

## 2. What's new on the Tracker+ compared with stock 1.2.0

### Version and base

- CE calls itself **1.0.0** for all three devices (its own numbering, in
  [`version.mk`](https://github.com/polyend/tracker-ce/blob/v1.0.0/version.mk)). Config's
  version line is built from that plus the edition name `CE`, and the build line shows
  the Git commit.
- It is **built from 1.2.0-era source, then taken past it.** It contains 1.2.0's
  Tracker+ features: the synth engines and USB audio's Input + Output mode, including
  the exact jacks-play-the-USB-return rule we found in 1.2.0 (Modular-Trackular's `docs/reports/no_audio_1.2.0.md`).
  [`CHANGELOG.md`](https://github.com/polyend/tracker-ce/blob/v1.0.0/CHANGELOG.md) then
  lists changes on top.
- I couldn't pin an exact base: the history is one squashed "Initial commit" plus a CI
  fix, so there's no record of which 1.2.0 build it started from.
- The repo is new: created 2026-10-26, made public with v1.0.0 today. Expect fast
  churn.
- **Projects are one-way.** Patterns saved on CE use format version 6, which 1.2.0 can't
  read ([CHANGELOG L10–14](https://github.com/polyend/tracker-ce/blob/v1.0.0/CHANGELOG.md?plain=1#L10-L14)).
  Back up `Projects/` before trying CE, or Save As into a new project.

### Changes that apply to the Tracker+

From the changelog, checked against the code where it touches our work:

| Area | Change | Touches our mods |
|---|---|---|
| Synths | Open Synth SDK: new engines are source files registered in `core/synth/registry.cpp`. New `CHIP` (open source) and `DIRT` (wave-shaping, closed: it's in `libPolySynth.a`, [registry L16](https://github.com/polyend/tracker-ce/blob/v1.0.0/core/synth/registry.cpp#L16)) | synths |
| Sample Editor | Loadable effect/generator plugins from `/Effects/*.ptfx`, MIT-licensed SDK, 6 examples | none |
| Modulation | Wavetable envelopes and LFOs on 7 instrument parameters, step FX `w` and `y` | none |
| UI | Custom themes (`/Themes/*.json`) and fonts (`/Fonts/*.ptfont`), web editor | colorsong (different idea) |
| Performance mode | Toggle between sample tracks and MIDI/synth tracks | none (not in our hardware scope) |
| Samples | Samples of 256 KB and up stream from SD (One-Shot, loops, Slice, Beat Slice); only the attack stays in RAM | resample, slicethru |
| Slices | **Play Thru** per Slice/Beat Slice instrument: a slice plays on to note-off or the end of the sample ([CHANGELOG L54–56](https://github.com/polyend/tracker-ce/blob/v1.0.0/CHANGELOG.md?plain=1#L54-L56); `slicePlayThru` in the instrument, [mtStructs.h L654](https://github.com/polyend/tracker-ce/blob/v1.0.0/core/source/mtStructs.h#L654)) | slicethru (mostly the same) |
| Pattern | Dedicated per-step Micro-move `m` and Volume `V` columns, freeing both FX slots. Live recording writes there. Fill (Random etc.) works on them | humanize |
| Granular | Granular Length automation and step FX `z` | none |
| Chance FX | New conditions on values 101–174: `GL`, `!L`, Play N/Skip M, Skip N/Play M ([mtSequencer.h L258–269](https://github.com/polyend/tracker-ce/blob/v1.0.0/core/source/mtSequencer.h#L258-L269)) | **condtrig (value clash)** |
| Images | JPEG viewer under Config → Info | none |
| Recording | Tighter live recording on single-armed tracks; first note after pre-roll | tracklen (live recording) |
| Timing | Exact 44,100 Hz in real time and in export; MIDI clock reworked ("~15× tighter"), Continue and Song Position Pointer, sync fixes ([CHANGELOG L85–98](https://github.com/polyend/tracker-ce/blob/v1.0.0/CHANGELOG.md?plain=1#L85-L98)) | swing, interpolate, legato (sequencer timing) |
| Files | Projects open without copying samples to the Workspace; Save writes only changed samples (Save As still copies everything) | every mod with a settings file, resample |
| Memory | NES games removed. Their 768 KB of SDRAM is now a shared scratch area; ~21 KB DTCM and ~22 KB ITCM free; up to ~29 MB SDRAM reclaimable by shrinking sample memory ([developer guide §1](https://github.com/polyend/tracker-ce/blob/v1.0.0/docs/developer_guide.md#1-memory-budget--free-headroom)) | all (RAM was our tightest limit: 832 B of startup RAM left) |
| Audio | "Optimized real-time audio rendering" (no detail given) | masterfx, synths budgets |
| USB | Windows 11 MIDI enumeration fix | none |

### Is a Tracker+ binary published, and can we build one?

- **Yes, published:** [release v1.0.0](https://github.com/polyend/tracker-ce/releases/tag/v1.0.0)
  has `TrackerPlus_CE_1.0.0.ptf` (4,713,628 bytes; stock 1.2.0 is 4,586,889). It's
  built by GitHub Actions from the tag
  ([`build.yml`](https://github.com/polyend/tracker-ce/blob/v1.0.0/.github/workflows/build.yml)),
  and every pull request gets built `.ptf` files as a CI artifact.
- **Building needs nothing secret:**
  - The closed libraries are in the repo as `.a` files.
  - The toolchain is GNU Arm Embedded **10.3-2021.10**, pinned in CI.
  - You also need `make` and Python 3.
  - It's a Linux-style build (`find`, `cp`, `date` in the makefiles). On Windows that
    means WSL or MSYS2; Git Bash alone has no `make`.
- **Toolchain.** The `arm-gnu-toolchain-15.3` next to this repo is a different compiler.
  The prebuilt `.a` libraries were compiled with some GCC version, so mixing compilers
  is a risk; I'd use 10.3. Building in GitHub Actions on a fork needs no local install
  at all. **Installing 10.3 locally needs your OK.**
- **Output format.** The `.ptf` comes from `objcopy -O ihex`, Intel HEX at `0x60030000`
  like stock, so `tools/ptf.py` should read it. Unverified: I didn't download the
  binary.

---

## 3. Porting Modular-Trackular to CE

### Mod by mod

"Source change" means editing CE's C++ in a fork: no hooks, no relocation, no RAM
regions. The mod's logic is ours and ports; the glue to the firmware is rewritten
against real function names.

| Mod | In CE already? | Port | Effort |
|---|---|---|---|
| colorsong | No (themes recolour the whole UI, not by content) | Source change in the Song screen drawing (`core/libs/NewDisplay/songPlayer.cpp`, `core/interface/songEditor/`). Our colour rules port as is | Medium |
| humanize | Partly: the new `m`/`V` columns plus Fill → Random give random micro-move and volume over a selection | Source change. Write into `m`/`V` instead of the FX columns (better: FX slots stay free). Keep the one-button action, per-project ranges, default velocities | Small–medium |
| interpolate | No | Source change in the sequencer's FX pass (`core/source/mtSequencer.cpp`) and the synth macro path (`core/synth/Synth.cpp`, public SDK API) | Medium |
| masterfx | No | The master reverb (Polyverb) and delay are **open source** in CE (`core/libs/Audio/Polyverb/`, `MixerAndFxs`). Swapping in our effects is a graph change; our DSP is ours | Medium (easier than today) |
| swing | No (Swing FX works as in stock) | Small source change in the sequencer's swing reset | Small |
| visualizer | No | Source change in the pad backlight code (`core/source/mtPadsBacklight.cpp`) | Small–medium |
| condtrig | **Partly, and clashing**: CE puts its own conditions on Chance 101–174; ours use 101–137 for different ones (`1st`, `!1s`, `A:B`) | Either adopt CE's set and drop ours, or move ours above 174. Either way, projects saved with condtrig play differently on CE (our `1st` = 101 reads as CE's `GL`), so they need a one-off converter. Pass counting per track (tracklen) is ours to keep | Medium + a decision |
| synths (WAVE, PHASE) | No, but CE has an SDK for exactly this | Rewrite as SDK engines (`InstrumentVoice`/`CustomSynthVoice`). The DSP, Mod Matrix, LFOs and chorus port; params, pages and saving move to the SDK's metadata. They'd be **extra models** with new `InstrumentTypeId`s rather than replacing a stock model, so the per-project swap goes away. Projects saved with the swap need converting | Large (but cleaner) |
| legato | No | Source change in note-on/note-off order for MIDI and synth tracks (`core/AudioEngine/mtAudioSequencer.cpp`). Synth legato still relies on the closed engines' Glide Mode, as today | Small–medium |
| copypattern | No (Duplicate doubles a pattern's length; Copy/Paste Pattern is stock) | Source change in the Pattern screen's action buttons | Small |
| slicethru | **Mostly yes** (Play Thru) | Drop ours, add **Loop** (whole sample) on top of Play Thru. Under SD streaming the loop must go through `VoiceStreamManager`'s loop-wrap, which loop modes already use. CE saves Play Thru per instrument (in the `.pti`), ours per project | Small |
| resample | No | Source change in the Sample Recorder (sources) and the audio graph routing. Its "free part of sample memory" logic must be redone for streaming; CE has far more free SDRAM | Medium–large |
| tracklen | No: CE forces every track to track 1's length ([mtSequencer.cpp L3092–3098](https://github.com/polyend/tracker-ce/blob/v1.0.0/core/source/mtSequencer.cpp#L3092-L3098)); only Performance mode has per-track lengths | Source change in the sequencer and grid. The pattern file already has a length per track, and CE's format 6 already breaks compatibility, so lengths could live in the pattern itself instead of a side file | Medium |
| songloop | No (the Song screen previews a selection once) | Source change in the Song screen and song-mode pattern change | Medium |
| chords | No: same 30 chords (`MAX_CHORD = 29`, [mtChordDefs.h L5](https://github.com/polyend/tracker-ce/blob/v1.0.0/core/source/mtChordDefs.h#L5)) | Extend the table and the limit. Values 30+ are free in CE today. Unchecked: how CE plays a stored value past its table (ours go to 236) | Small |

**Things common to all of them:**
- **Settings files and Save As.** Every mod that saves per-project settings (swing,
  humanize, interpolate, masterfx, synths, slicethru, resample, tracklen…) needs its
  Save/Save As handling redone in CE's new project saver
  (`core/FileManager/ProjectSave.cpp`). That's where we lost settings before (the New
  Project and Save As fixes).
- **Upstreaming.** Several mods (chords, copypattern, tracklen, songloop) look like what
  CE's maintainers say they want. Getting them merged would put them in everyone's CE
  and spare us maintaining them, at the price of the relicensing grant and their review
  rules: one feature per PR, all three devices must build.

### Keeping pick-and-choose: three models

| | A. Compile-time flags | B. Runtime switches in Config | C. Binary-patch CE builds |
|---|---|---|---|
| How | `#ifdef MT_SWING` etc. (CE already takes `CUSTOM_DEFINES`); one compile per selection, on CI or a server | Every mod compiled in, each with an Off/On item in Config, saved in the config file or per project | Today's tools aimed at a CE `.ptf` |
| What the user downloads | A build per selection: 2^15 = 32,768 possible, so built on demand (a CI run per request, minutes, needs an account or server) or a short fixed menu | One `.ptf` | Their mix, built in the browser as now |
| Writing a mod | Normal C++ in CE's files, guarded | Normal C++, plus an Off path that's exactly CE's own code (an `if`) | Hooks again, now against CE's addresses, which change with every CE build |
| Testing | Compile every selection, or at least every pair; behaviour tests per build; the page can't check a hash it never saw unless builds are reproducible | Behaviour tests on **one** build, with switch states as test inputs; no layout, RAM-region, chain or byte-identity tests at all | Everything we test today, redone for each CE release |
| Cost per extra mod | Doubles the selections | One more switch | Doubles the mixes |
| Downside | Infrastructure (on-demand builds), and selections nobody tested | All mods' RAM and code always present (fine: ~21 KB DTCM and MBs of SDRAM free); a mod's Off path must truly be stock | Keeps all of today's costs and adds a moving target |

**Recommendation: B.**
- Our `SCALING_PLAN.md` turned switches down for 1.2.0 for two reasons: each Off path
  is new binary-patched code needing hardware tests, and switches need a new settings
  file with Save As care.
- In source, an Off path is CE's own line of code behind an `if`, and Config items are
  a table entry ([`configMenuDefs.h`](https://github.com/polyend/tracker-ce/blob/v1.0.0/core/interface/configEditor/configMenuDefs.h)).
  The first reason mostly goes away; the second remains but is ordinary source work.
- The web patcher becomes a download page, or a Releases link.

### Testing under CE: what gets easier, what gets harder

**Easier (most of today's release tier goes away):**
- Today's release run is 43.2 min. Of that, the web harness (all 16,383 mixes) is about
  30 min and the cover-set stage about 12 min. With one build (model B), all of it goes,
  along with:
  - `test_layout.py`, `verify.js` and `verify_corrupt.mjs`;
  - the reordered bundles, RAM-region bookkeeping, hook chains and ITCM stubs;
  - `expected.json`;
  - the 15-mod `MAX_COMPARE` ceiling and the naming work for mod 16+.
- What stays is each mod's behaviour tests on one image, a few minutes each. I can't
  give a measured total until tests exist for CE.
- RAM stops being the blocker: ~21 KB DTCM free instead of 832 B of startup RAM.
- Finding where to change something is grep, not Ghidra.

**Harder, or new:**
- **Hardware re-testing, all of it.** Every mod becomes new code on a new base, so all
  15 need your hardware checklist again. CE itself is a day old and untested by us, and
  its own behaviour has changed (streaming, the new saver, MIDI timing, pattern format
  6). The emulator results stop being "against stock 1.2.0" and become "against CE
  1.0.0".
- **Keeping up with CE.** 1.2.0 is frozen, so a hardware test there stays valid forever.
  A CE fork must:
  - merge each CE release into files our mods edit (the sequencer, Pattern screen and
    Config are exactly where CE is most active), so expect merge conflicts;
  - rebuild;
  - re-test on hardware each time.

  Staying on an older CE gives up its fixes.
- **Tests rewritten.** Every `tools/emu/test_*.py` uses 1.2.0 addresses. They'd be
  rewritten against symbol names from our own build's `.axf`, which CI doesn't publish,
  so we'd build. Logic-only parts (chord tables, condition rules, swing maths) could
  become host tests compiled for the PC, much faster than Unicorn. Whether CE's core
  compiles for a PC without the hardware layer is unchecked.
- **Reproducible builds.** For a page or anyone else to check a download's hash, a
  build from the same commit must give the same bytes.
  - In its favour: the pinned compiler, a build ID taken from the commit, no
    `__DATE__`/`__FILE__` in release builds as far as I saw, and debug info that
    doesn't reach the `.ptf`.
  - Checked 2026-11-02: the fork's own Actions build of `9b9a13b` gave a
    `TrackerPlus_CE_1.0.0.ptf` with the same SHA-256 as Polyend's release
    (`FE0BEC9C…DCAC4F0`). So CI builds are reproducible.
  - Not shown yet: a local build (another machine, OS or folder) matching CI.
- **Flashing risk.** Our patched builds change little of a tested image. A CE fork
  build is a whole new image every time. The pinhole recovery is the same as today (CE
  README §4, our FINDINGS section 3), but you'd be flashing more often.

### The emulator

**Partly reusable:**
- `machine.py` should load a CE build. The chip, memory map, Intel HEX at
  `0x60030000` and MCUXpresso startup copy table are all the same family as 1.2.0.
  Worth a smoke test before relying on it.
- `eve.py` (the display chip, FT812, is unchanged) and `fakefs.py`'s idea (FatFs is the
  same) carry over.

**Must be redone:**
- Everything that names an address: `seq_timing.py`, `synth_rig.py`, `reverb_rig.py`
  and every test.
- With our own `.axf`, tests can call functions by name, which is a big simplification.

**License and synth tests:**
- `synth_rig.py` and the synths, legato and interpolate tests run the stock synth
  engines. In a CE build those are the closed libraries.
- Running them in a PC emulator is now allowed on a plain reading (section 1(b) of the
  library license, since `9cc7aae`). Stepping through their code, or disassembling it, may
  still count as reverse engineering (section 2(c)).
- So the stock engines may run in the emulator as a black box: call them through the
  public interface and check what comes out. Don't study or patch their code.
- Decided 2026-11-04: tests move to full builds, with the libraries in, and sound tests
  are built with the mods that need them. See [SOUND_TEST_PLAN.md](SOUND_TEST_PLAN.md).

---

## 4. Modifying Polyend's own code

Yes, CE lets you change built-in behaviour directly, anywhere except the two closed
libraries. Selling the result as firmware stays banned. Some examples follow.

### The 44.1 kHz issue

**What it is (from our docs).** Stock 1.2.0's audio block converts its 128 frames into
time for the sequencer with a fixed **44,122.65 Hz**, while the codec really runs at
exactly 44,100 Hz (Modular-Trackular's `docs/TIMING_HEADROOM_PLAN.md` section 2; `docs/FINDINGS.md`,
`app_audio_block` and "The deadline").
- The sequencer therefore thinks each block is 0.05 % shorter than it is, and the tempo
  runs about **0.05 % slow**.
- At 120 BPM that is about 1.9 s per hour, or one 16th note behind a free-running
  reference after about 4 minutes.
- It shows when the Tracker free-runs next to other gear or a DAW grid. It doesn't when
  the Tracker sends MIDI clock (others follow it) or receives it.
- The offline export uses the same constant.

**How CE derives the clocks:**
- **Audio clock.** The audio PLL (PLL4) runs at 24 MHz × (37 + 15168/24000) / 2. The SAI
  then divides it by 4 × 10 to 11.2896 MHz = 256 × 44,100 Hz exactly
  ([clock_config.c](https://github.com/polyend/tracker-ce/blob/v1.0.0/platforms/imx/board/clock_config.c#L161-L170)).
- **Sequencer tick.** Each audio callback passes `128 / kAudioSampleRate` seconds to
  `Sequencer::tick`. On the Tracker+ that rate is now **44,100.0**, with a comment
  deriving it from the PLL; 44,117.647 is kept only for the original Tracker
  ([Tracker.cpp L183–194](https://github.com/polyend/tracker-ce/blob/v1.0.0/core/source/Tracker.cpp#L183-L194)).
- **Accumulator.** `tick` adds the block's µs to a **double** and runs one micro-step
  per whole period, so no rounding builds up. 1.2.0 used a float. Each micro-step also
  passes its offset inside the block on to MIDI clock output
  ([mtSequencer.cpp L89–113](https://github.com/polyend/tracker-ce/blob/v1.0.0/core/source/mtSequencer.cpp#L89-L113)).
- **Export.** The changelog says export now matches real-time 44,100 Hz.

So **CE already fixes it**; nothing for us to do there. Unconfirmed until measured on
hardware (for example, the `timing-tests` recordings method against a reference).

If we stay on 1.2.0, the same fix is a tiny patch: replace the 44,122.65 literal used
by `app_audio_block`, and the one the export loop passes, with 44,100.0. Its literal
addresses aren't in FINDINGS yet. It would need one hardware tempo check.

### Other fixes and optimizations CE makes possible

- **USB In+Out silences the jacks.** The same rule is in CE, now readable: with USB
  audio mode 3 and a host attached, the output source switches to the USB input "to
  handle usb feedback loop"
  ([mtAudioEngine.cpp L851–865](https://github.com/polyend/tracker-ce/blob/v1.0.0/core/AudioEngine/mtAudioEngine.cpp#L851-L865)).
  A fix (keep the master on the jacks, or make it a setting) is a few lines.
- **RAM for features.**
  - ~21 KB DTCM and ~22 KB ITCM are free.
  - Up to ~29 MB of SDRAM is reclaimable by lowering `SAMPLE_MEMORY_SIZE`.
  - The 768 KB scratch area is shared between modal features.
  - Hot code can be moved into ITCM on purpose (`AUDIO_CORE_RAM`).
- **Master effects.** The reverb and delay are source (Polyverb, the short delay), so
  their cost and sound can be tuned directly. masterfx's measured savings would no
  longer need hooks.
- **The sequencer's cost.** Sequencer code is source: chance, rolls, arps and FX
  dispatch can be profiled with CE's `Profiler` (Config's CPU lines) and optimized.
  `patch/headroom`'s job becomes a few lines in the profiler.
- **Stock quirks we documented** become fixes in place, for example the pending-delay
  quirk (a pending delayed note drops the next step's note, FINDINGS). Whether CE still
  has it is unchecked.
- **The CMSIS-DSP library** in the IMX build is the prebuilt Cortex-M4 one
  (`libarm_cortexM4lf_math.a`). An M7 build could be a little faster, but it's ARM's
  open-source code, so we'd build it ourselves. Small and optional.

### Still out of reach, or not worth it

- **Stock synth engines and the synth framework** (`libPolySynth.a`, `libPolyDSP.a`):
  ACD, FAT, VAP, WTFM, PERC, DIRT, the voice allocator, patch saving and the macro
  matrix. No source, and the license forbids reverse engineering them. Our earlier
  findings (idle voices cost as much as playing ones, the fp16 mod bus, per-sample
  divides) stay unfixable. You'd already ruled out synth performance mods anyway.
- **The bootloader** (`0x60000000`–`0x6002FFFF`) isn't in the repo, and CE warns not to
  move the app's flash origin. Recovery is unchanged, which is good.
- **Core clock.** CE runs the core at **528 MHz**: ARM PLL 1056 MHz / 2
  ([clock_config.c L137–140](https://github.com/polyend/tracker-ce/blob/v1.0.0/platforms/imx/board/clock_config.c#L137-L140),
  [L209](https://github.com/polyend/tracker-ce/blob/v1.0.0/platforms/imx/board/clock_config.c#L209)).
  The build targets `MIMXRT1051CVL5B`, which as far as I know is NXP's 528 MHz
  industrial grade; CE's README says "600 MHz" for the family. Overclocking isn't a
  safe optimization; don't.
- **Third-party binaries:** the newlib C library (`libcr_newlib_nohost.a`) and CMSIS,
  above. Both are replaceable but there's no reason to.

---

## 5. Recommendation

**Run both for a while.**
- Keep 1.2.0 patching as the shipping line.
- Start a CE fork as an experiment with one small mod.
- Decide on a full move once CE has had a few releases and you've lived with it on
  hardware.

Why not move now:
- CE is one day old, with two commits.
- Its pattern format is one-way.
- Every mod would need a new hardware test against a base we've never tested.

Why not ignore it:
- It removes our two hardest limits, RAM and test scaling.
- It ends the "patches only" restriction.
- It already contains two of our features in some form.

### Options and their costs

| | Stay on 1.2.0 | Move to CE now | Both for a while (recommended) |
|---|---|---|---|
| Users | Unchanged; web patcher as is | Must move to CE; projects become format 6 | Unchanged; CE builds offered as experimental, clearly labelled |
| Testing | Release tier ~43 min and growing with each mod (mixes double, `MAX_COMPARE` caps at 15) | Mix testing gone; all tests rewritten; full hardware re-test of 15 mods | Today's tiers for 1.2.0, plus a small CE test set for whatever is ported |
| Hardware sessions | Per mod as today | All 15 mods, then again for each CE release we adopt | CE stock first (does it work for you?), then one ported mod |
| RAM | 832 B of startup RAM left: each new mod is a squeeze | Plenty | Plenty on CE |
| Maintenance | Frozen base: tests stay valid | Merging CE releases into a fork forever (or upstreaming) | Both lines for a while, the real cost of this option |
| Legal | As today (patches only) | CE license, notices, no selling builds | Both |
| New features (tracklen-like work) | Hooks, RE, ITCM stubs | Plain C++ | Write new big ones for CE only? (decision below) |

**Suggested first steps:**
1. **You:** back up `Projects/`, flash stock CE 1.0.0 (the published `.ptf`) and use it
   for a session. Stay out of performance mode and Break FX, as usual. Pinhole recovery
   to 1.2.0 is there.
2. **Me, needs your OK to install or use a fork's CI:**
   - fork tracker-ce under your account;
   - build it in Actions;
   - build it twice to check reproducibility;
   - load it in `machine.py` to check the emulator boots its startup path.
3. Port **chords** as a runtime-switched source change: the smallest mod, and no clash.
   Measure what writing and testing it actually cost.
4. Then decide on the rest from real numbers.

### Questions for you

1. **License terms.** Are you comfortable releasing a fork under CE's license (no selling
   builds, keep the notices and Part 2)? Donations and sponsorship: answered 2026-11-04,
   none planned.
2. **Binaries.** For CE builds, publish `.ptf` files (allowed), or keep a patch-style
   distribution anyway?
3. **Where the work lives.** A separate fork repository (my suggestion), keeping this
   repo MIT and Polyend-code-free, and its name (no "Polyend" or "Tracker CE" in it)?
4. **Modular model on CE.** Runtime switches in one build (recommended), compile-time
   selections built on demand, or binary patches?
5. **condtrig.** Adopt CE's conditions and convert old projects, or keep ours on values
   above 174?
6. **synths.** OK to make WAVE and PHASE extra models through CE's SDK, instead of
   replacing a stock model per project?
7. **Upstream.** Offer some mods to Polyend's repo (they'd get relicensing rights, and
   we'd stop maintaining them), or fork only?
8. **The 1.2.0 line.** Keep adding mods there (chords into patch/all is in flight), or
   freeze it to fixes only? And do you want the one-word 44.1 kHz tempo fix for 1.2.0?
9. **Tools.** Install GNU Arm 10.3 and `make` (WSL or MSYS2) locally, or build only in
   GitHub Actions?
10. **Closed libraries in tests.** Answered 2026-11-04: include them. Tests move to full
    builds, with closed engines as a black box ([plan](SOUND_TEST_PLAN.md)).
