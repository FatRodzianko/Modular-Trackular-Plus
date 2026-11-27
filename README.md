# Modular-Trackular Plus (MT+)

Optional mods for the **Polyend Tracker+**, built into
[Polyend's Tracker Community Edition (CE)](https://github.com/polyend/tracker-ce) firmware.
It continues [Modular-Trackular](https://github.com/FatRodzianko/Modular-Trackular), which
patches stock firmware 1.2.0.

MT+ is unofficial and not affiliated with Polyend, who don't support these builds. Please
don't report problems with them to Polyend.

Everything CE does is here too. There is one firmware file, and each mod has its own
Off/On switch, so there's no separate build for each mix of mods.

## Mods

| Mod | What it does | Settings |
|---|---|---|
| **[Humanize](docs/mods/humanize.md)** | A Humanize button on page 3 of the Pattern screen's bottom bar gives the selected notes small random changes in the Micro-move (`m`) and Volume (`V`) columns. It works in edit mode, and Undo reverts it. | Project Settings → Humanize |
| **[Global Swing](docs/mods/globalswing.md)** | Every take and pattern start uses the project's Swing amount (25–75). A Swing FX still works for the rest of its pattern. | Project Settings → Global Swing |
| **[Master FX](docs/mods/masterfx.md)** | Three slots on the Master screen, Reverb, Delay and Limiter, each playing CE's own effect (Stock) or a plugin (`.ptmfx`) from `/MasterFX/` on the card. The firmware writes its default pack there: reverb Freeverb, Plate, Spring, Shimmer, "Gated" or Reverse, delay Tape or BBD, and the compressors Glue and Compressor for the Limiter slot ([what each does](docs/mods/masterfx.md#the-default-plugins)); anyone can build more ([docs/masterfx_plugins.md](docs/masterfx_plugins.md)). Choose them on the Master screen, also while playing. A slot's page shows the plugin's names for CE's knobs and up to two knobs of its own. Glue has its own Attack, Release and Threshold, and a Tracks list: it compresses the whole mix, or only the tracks you mark, together. A plugin that can duck, such as Compressor, has a Ducks list on the Limiter page: it then turns the tracks you mark down by the key instead of the whole master. Every send goes through the slots as it does through CE's, synths included. Some plugins cost more CPU than CE's own. | Master screen, or Project Settings → Master FX |
| **[Color Song](docs/mods/colorsong.md)** | Song screen blocks get a colour from their content, so the same material is easy to follow through a song. Colours stay put and are saved with the project. | On/Off only |
| **[Variable Track Length](docs/mods/tracklength.md)** | Each track of a pattern can have its own length. A short track loops until the pattern ends, then every track starts again together. Hold Trk Len on page 3 of the Pattern screen's bottom bar and turn the encoder. Past its length a track shows its loop in grey, and CE's conditions on the Chance FX count its own loops. Lengths are saved with the project. | On/Off only |
| **[Copy to New](docs/mods/copytonew.md)** | A Copy to New button on page 3 of the Pattern screen's bottom bar copies the pattern you're on into the next empty pattern, with your unsaved edits and every track's length, playing or stopped. You stay on your pattern. A pattern counts as empty only if it isn't in the song and has nothing in any step, even past its length. With Variable Track Length on, the copy gets the Trk Len lengths too. It works in edit mode. | On/Off only |
| **[Song Loop](docs/mods/songloop.md)** | Repeat a block of song positions. In the Song screen's pattern column, with Rec on, Shift + Down selects the block and Play Selection repeats it; Continue Song plays the block out and goes on. During the loop Up/Down and the jog wheel move the block. The status bar shows the loop. Designed by P-N-E-M for 1.2.0. | On/Off only |
| **[Resample](docs/mods/resample.md)** | Record the tracker itself in the Sample Recorder. With Line In or a mic as the Source, Trk Audio lets you hear the tracker while the recorder is open: Play starts the pattern, and Shift + Rec starts the take and the pattern together. Trk Record (stereo Line In only) puts the tracker's sound in the take, with the Line In added: samples, synths, Synth FX, the delay and reverb, never the metronome's clicks. With Trk Audio on, the recorder's instrument list has every instrument, so you can play samples and synths on the pads while you record. A project that used live resampling with Modular-Trackular on 1.2.0 keeps its buttons. | Sample Recorder → Trk Audio, Trk Record |
| **[Synths](docs/mods/synths.md)** | Three more synth models in the picker: WAVE (wavetable, two oscillators, a filter, three envelopes, two LFOs and a mod matrix), PHASE (phase distortion, after the Casio CZ) and DCO6 (virtual analog after the Juno-106: one oscillator, a resonant low-pass and a high-pass, one envelope, one LFO, with Unison and Vintage). They are `.psyn` plugins, `WAVE.psyn`, `PHASE.psyn` and `DCO6.psyn`, in the card's `System/Synths` folder beside CE's engines (install step 3). Their effects are Synth FX's slots (below). Patches save as CE's do. Off hides the models; a part already playing one keeps it. 1.2.0 projects' WAVE and PHASE parts aren't converted. | Project Settings → Synths |
| **[Synth FX](docs/mods/synthfx.md)** | Two effect slots, FX1 then FX2, on each synth part, every engine (CE's eight, WAVE, PHASE and DCO6). Each slot picks a plugin (`.ptsfx`) from `/SynthFX/` on the card; the firmware writes its default pack there (Overdrive, Distortion, Bit Crusher, Chorus, Flanger, Phaser; [what each does](docs/mods/synthfx.md#the-default-plugins)), and anyone can build more ([docs/synthfx_plugins.md](docs/synthfx_plugins.md)). In the patch editor, button 3 shows the part's slots and the plugin's knobs. The slots belong to the part, not the patch, so loading another patch keeps them, and they are saved with the project; an old WAVE or PHASE patch has its own effects moved into the slots. On PERC the effects reach the main output, not its sends. The Distortion and Phaser cost the most CPU: 8 DIRT voices with both on every part reach about 95 %. A slot always runs on the part, after the engine; WAVE's old Placement Pre is gone, and a project saved with it opens with those slots on the part. | Patch editor → button 3 |

Every mod is tested on hardware.

The chord inversions ([which value plays which chord](docs/mods/chords.md)) and conditional trigs
from the 1.2.0 mods are now part of CE itself, though CE's conditions differ, so a 1.2.0 project's
conditional trigs read as CE's. Still to port, with no
dates: Legato, FX Interpolation,
the pad visualizer, slice Loop and live resampling. The plan is in [`docs/mt/CE_PORT_PLAN.md`](docs/mt/CE_PORT_PLAN.md).

## Turning mods on and off

- **Config → Mods** sets which mods are on in new projects.
- **Project Settings → Mods** sets which are on in the open project. The choice is saved
  with the project, so each project can have its own set.
- With a mod Off, its code doesn't run and that part of the firmware is CE's. One thing
  differs in every MT+ build, whichever mods are on: the mods get 1 MiB of sample memory,
  so the longest stereo recording is 5:50 instead of 5:56.
- A project that used these mods on 1.2.0 brings its Humanize, Global Swing, Master FX, Color
  Song and track length settings with it the first time it opens.

## Install

MT+ is for the Tracker+ only. The Original Tracker and Tracker Mini builds from this branch
have no mods.

1. **Back up your SD card's `Projects` folder.** CE saves patterns in a newer format that
   1.2.0 can't read. `tools/ce_to_120.py` makes a copy of a project that 1.2.0 can open,
   without the things only CE has.
2. **Get the firmware.** Download `TrackerPlus_MT_<release>.ptf` from the
   [latest release](https://github.com/FatRodzianko/Modular-Trackular-Plus/releases/latest).
   Each release is tagged for what it adds: a mod and its version (`resample-1.0`), or a
   merge of CE's updates (`CE_YYYYMMDD`). You can also build it with `make plus` (see
   [Building Firmware](#building-firmware) below), which makes
   `build/TrackerPlus_MT_1.0.0.ptf`.
3. **Copy the `System` folder to the card.** Each release has it in
   `PolyendTracker_CE_SD_Card.zip`: copy the `System` folder in it to the card's root. It holds
   every synth engine, since an engine is a `.psyn` file on the card and not part of the
   firmware: CE's (ACD, FAT, VAP, WTFM, PERC, DIRT and CHIP) and the Synths mod's WAVE, PHASE and
   DCO6, in `System/Synths`. Without them, a project's synth parts can open on another engine,
   since the project stores each part's engine by its place in the list. It also holds the
   Sample Editor effects, the words for new project names, the help texts for the FX and
   Config screens, the themes and the New font. `make psyn` puts the engines in
   `build/System/Synths/`.
   **Updating from an earlier release:** CE now reads all of this from `System` only. Move
   your own `.psyn` files from `/Synths` to `/System/Synths`, your synth patches from
   `/Patches` to `/System/Synths/Patches`, synth samples from `/SynthSamples` to
   `/System/Synths/Samples`, and your own effects (with `Presets`), themes and fonts from
   `/Effects`, `/Themes` and `/Fonts` into the folders of the same name in `System`. Your
   settings carry over by themselves, and projects stay in `/Projects`. The mods' own
   folders (`/MasterFX`, `/SynthFX`, `/Mods`) stay where they are.
4. **Flash it** as in
   [Flashing on Hardware & Emergency Recovery](#4-flashing-on-hardware--emergency-recovery)
   below. Config then shows the version as `1.0.0 MT`.

## Development

- `modular`, this branch, holds the mods. `main` is an untouched copy of polyend/tracker-ce.
- The mods are in [`core/mods/`](core/mods), and changes to CE's own files are marked
  `// MT:`.
- Tests are in `core/mods/tests/` (on the computer) and `tools/emu/` (in an emulator).
- [`CLAUDE.md`](CLAUDE.md) has the rules for adding code: what runs in the audio interrupt,
  the display limit and memory.
- [`docs/mt/CE_FIRMWARE_MAP.md`](docs/mt/CE_FIRMWARE_MAP.md) maps CE on the Tracker+.

## MT+ license

CE's [`LICENSE`](LICENSE) and [`NOTICE.md`](NOTICE.md) apply here, as in
[License](#license) below. They don't allow selling `.ptf` builds. The closed synth
libraries in `synth/lib/` have a license of their own.

---

*The rest of this page is CE's own README, unchanged. On this branch its build output is
named `TrackerPlus_MT_*.ptf` rather than `TrackerPlus_CE_*.ptf`.*

# Polyend Tracker Community Edition (CE)

Welcome to the **Polyend Tracker Community Edition** repository!

Built directly from the original Polyend firmware source code, Tracker CE opens up the Polyend Tracker platform to musicians, sound designers, and developers around the world. Whether you are here to flash the latest Community Edition firmware, want to build your own idea on the Tracker, design custom themes and fonts, write Sample Editor plugins, or build new synthesizer engines, we are excited to have you here.

### Download Firmware:

##### Stable Releases: 
* See [Releases](https://github.com/polyend/tracker-ce/releases) for the latest official downloads for all devices.
* See [`CHANGELOG.md`](CHANGELOG.md) for the full list of changes in the official tagged releases.

##### Latest Beta Builds: 
Automatically built from the latest changes in `main` (for all 3 devices):

* Download the latest [Beta Firmware builds (`.zip`)](https://nightly.link/polyend/tracker-ce/workflows/build.yml/main/polyend-tracker-ce-firmware.zip)
* Download the latest [SD Card Content (`.zip`)](https://nightly.link/polyend/tracker-ce/workflows/build.yml/main/polyend-tracker-ce-sd-card.zip) (`/System/`)
* See [`UPCOMING.md`](UPCOMING.md) to find out what's new in these Beta Builds.

---

## Notice: Official Support, Warranty & Hardware Disclaimer

> **A Quick Note Before Flashing or Modifying Firmware**
>
> Thanks to the Tracker's isolated bootloader and hardware emergency recovery button (see [Flashing on Hardware & Emergency Recovery](#4-flashing-on-hardware--emergency-recovery)), experimenting with Community Edition builds is very safe. Even if a custom build fails to boot, you can easily re-flash a stable `.ptf` image from your microSD card. That said, we still need to include the standard legal disclaimer just in case:
>
> * **Provided "AS IS" Without Support Obligation**: While the foundation of this repository was originally developed and released by **Polyend**, the **Polyend Tracker Community Edition (CE)** firmware, source code, and any community-developed modifications or plugins are provided **"AS IS"**, without warranty of any kind, express or implied. **Polyend has no obligation to provide official technical support, maintenance, bug fixes, or customer service** for Community Edition firmware or custom builds.
> * **Use at Your Own Risk**: Flashing, modifying, or running custom firmware is done at your own discretion and risk. Polyend is not liable for any data loss, project corruption, malfunction, or hardware damage (including "bricking" a device or issues caused by code modifications) arising from the use of this software.
> * **Out-of-Warranty Repairs Available**: In the unlikely event that something ever goes wrong with your hardware while using custom firmware, it is **not covered under standard warranty**. However, **Polyend is always happy to help get your instrument back up and running through its official out-of-warranty repair service** (standard service, parts, and shipping fees apply. See [How to Contact Polyend Customer Support](https://backstage.polyend.com/t/how-to-contact-the-polyend-customer-support/15510)).

---

## Repository Structure

This unified codebase powers all three hardware instruments in the Tracker family:
* **Original Polyend Tracker** (`NXP Kinetis MK66FX1M0`, Cortex-M4F @ 180 MHz)
* **Polyend Tracker+ & Tracker Mini** (`NXP i.MX RT1051/RT1064`, Cortex-M7 @ 600 MHz)


```text
.
├── Makefile                # Unified top-level build & version management
├── version.mk              # Centralized version & build number configuration
├── CHANGELOG.md            # Stable release history & feature highlights
├── UPCOMING.md             # Unreleased changes currently in the beta builds
├── CONTRIBUTING.md         # Contribution rules & hardware testing workflow
├── LICENSE                 # MIT License (+ No Wholesale Commercial Clones & Precompiled Synth Terms)
├── NOTICE.md               # Third-party open-source attributions
├── core/                   # Shared Tracker application, audio engine, UI (GlobalFileBrowser, editors) & synth host glue
├── platforms/
│   ├── mk66/               # Original Tracker (Kinetis MK66) HAL, drivers & linker scripts
│   └── imx/                # Tracker+ & Tracker Mini (i.MX RT1051) HAL, drivers & linker scripts
├── synth/
│   ├── include/PolySynth/  # PolySynth public headers & Open Synth SDK (synth/include/PolySynth/sdk/)
│   ├── source/             # Open-source PolySynth host, PsynLoader, PolyDSP (Amplifier/Mixer) & Master FX
│   └── prebuilt/           # Precompiled factory .psyn synth plugins + LICENSE-PROPRIETARY.txt
├── synth-plugin-sdk/       # .psyn loadable Synth Engine SDK, builder & engines/ sources
├── util/                   # Shared C++ utility headers & third-party header libraries (ETL, magic_enum)
├── editor-fx-sdk/          # .ptfx Sample Editor plugin SDK, builder & effects/ sources
└── docs/                   # User guides, SDK tutorials & architecture documentation
```

---

## Guiding Principles of the Community Edition Firmware

This repository prioritizes workflow continuity: our aim is to respect existing muscle memory, introduce features, optimizations and improvements that fit the original vision of the device, while fixing bugs along the way.

If you have ideas for major workflow adjustments or structural changes, please open a [GitHub Discussion](https://github.com/polyend/tracker-ce/discussions) or a topic on the [Polyend Backstage Forum](https://backstage.polyend.com/) first, so we can align on direction together with the community.

For radical experiments or reimagined interfaces, we actively encourage community forks!

---

## Contributing

We welcome community contributions! ❤️

From bug fixes and workflow improvements to new open-source synth engines and `.ptfx` Sample Editor plugins. Please read [`CONTRIBUTING.md`](CONTRIBUTING.md) for our project philosophy, pull request rules, hardware testing workflow, and technical checklist before opening a Pull Request.

---

## Building Firmware

### 1. Prerequisites
* **[GNU Arm Embedded Toolchain `10.3-2021.10`](https://developer.arm.com/downloads/-/gnu-rm)**:
  * Direct official downloads from Arm:
    * **Linux (`x86_64`)**: [`gcc-arm-none-eabi-10.3-2021.10-x86_64-linux.tar.bz2`](https://developer.arm.com/-/media/Files/downloads/gnu-rm/10.3-2021.10/gcc-arm-none-eabi-10.3-2021.10-x86_64-linux.tar.bz2)
    * **Linux (`aarch64` / ARM64)**: [`gcc-arm-none-eabi-10.3-2021.10-aarch64-linux.tar.bz2`](https://developer.arm.com/-/media/Files/downloads/gnu-rm/10.3-2021.10/gcc-arm-none-eabi-10.3-2021.10-aarch64-linux.tar.bz2)
    * **macOS (`x86_64` / Apple Silicon via Rosetta)**: [`gcc-arm-none-eabi-10.3-2021.10-mac.tar.bz2`](https://developer.arm.com/-/media/Files/downloads/gnu-rm/10.3-2021.10/gcc-arm-none-eabi-10.3-2021.10-mac.tar.bz2)
    * **Windows (ZIP)**: [`gcc-arm-none-eabi-10.3-2021.10-win32.zip`](https://developer.arm.com/-/media/Files/downloads/gnu-rm/10.3-2021.10/gcc-arm-none-eabi-10.3-2021.10-win32.zip)
  * Ensure `arm-none-eabi-gcc` is on your `PATH`, extract the toolchain into `./agt/10.3/`, or pass `TOOLCHAIN=/path/to/arm-none-eabi-` to `make`.
* `make` (for building firmware) and `python3` (used by `make ptfx` to package `.ptfx` Sample Editor plugins and by the `make bump-*` version helpers; standard library only).

### 2. Build Commands
From the repository root:

```bash
# Build all three devices (Original Tracker, Tracker+, Tracker Mini)
make all

# Or build a single target:
make original   # -> build/PolyendTracker_CE_1.0.0.ptf
make plus       # -> build/TrackerPlus_CE_1.0.0.ptf
make mini       # -> build/TrackerMini_CE_1.0.0.ptf

# Build reference .ptfx Sample Editor effects into editor-fx-sdk/build/System/Effects/:
make ptfx

# Build open-source .psyn synths (CHIP.psyn; MT+: WAVE.psyn, PHASE.psyn and DCO6.psyn) and stage all factory + open-source .psyn plugins into build/System/Synths/:
make psyn

# Or compile only the synth-plugin-sdk reference plugin (CHIP.psyn -> synth-plugin-sdk/build/):
make -C synth-plugin-sdk

# Clean all build directories:
make clean
```

### 3. Version Management (`version.mk`)
All firmware version numbers are defined in [`version.mk`](version.mk). The **Build** identifier displayed on the hardware under **Config > Firmware > Build** is automatically populated from the short Git commit hash (`git rev-parse --short=7 HEAD`):

```bash
# Show current version, Git build hash & output filenames
make version

# Increment patch version (e.g. 1.0.0 -> 1.0.1)
make bump-patch TARGET=all        # or TARGET=original | TARGET=plus | TARGET=mini

# Increment minor version (e.g. 1.0.x -> 1.1.0)
make bump-minor TARGET=all        # or TARGET=original | TARGET=plus | TARGET=mini
```

### 4. Flashing on Hardware & Emergency Recovery

1. Copy the generated `.ptf` file from `build/` (`PolyendTracker_*.ptf`, `TrackerPlus_*.ptf`, or `TrackerMini_*.ptf`) to the `/Firmware/` folder on your Tracker's microSD card.
   * *Note*: Keep `.ptf` filenames at **31 characters or fewer** so the Original Tracker's 32-byte `/Firmware/_fwinfo` bootloader buffer remains null-terminated.
2. On the device, open **Config > Firmware > Firmware update**, select the `.ptf` file, and confirm.

If you prefer a video, have a look here: https://youtu.be/1F6wx2ftFVA

> **Bootloader Resilience & Emergency Firmware Recovery**:
> * The Polyend Tracker bootloader is isolated from the application firmware:
>   * On the **Original Tracker (`MK66`)**, the bootloader runs on a separate dedicated microcontroller (`kBootloaderTrig`).
>   * On **Tracker+ and Tracker Mini (`IMX`)**, the bootloader resides in a protected region at the bottom of QSPI Flash (`0x60000000`–`0x6002FFFF`), while `.ptf` application builds are linked starting at `0x60030000` (do not modify the flash `ORIGIN` address in the linker scripts).
> * **Emergency Recovery**: If a custom firmware build ever hangs or fails to boot into the UI, you can always recover your device:
>   1. Place a known-good, stable `.ptf` firmware file for your device inside the `/Firmware/` folder on your microSD card (ideally as the only `.ptf` file in `/Firmware/`).
>   2. Insert the microSD card into your Tracker and power it on.
>   3. Use a paperclip or SIM-eject pin to press the **recessed hardware Reset / Emergency Bootloader pinhole button** (located on the bottom of the Original Tracker / Tracker+, or next to the USB-C port on Tracker Mini). The hardware bootloader will automatically pick up the `.ptf` file from `/Firmware/` and flash it.

---

## Documentation & Developer Guides

Full user and developer documentation is available in [`docs/`](docs/Home.md) and on the repository **Wiki**:

* **User Guides**:
  * [Custom Themes & Fonts](docs/custom_themes_and_fonts.md) + [Live Web Editor](https://polyend.github.io/tracker-theme-editor/) ([`polyend/tracker-theme-editor`](https://github.com/polyend/tracker-theme-editor))
  * [Wavetable Envelopes, LFOs & Step FX](docs/wavetable_envelopes.md)
  * [File Manager & Built-In File Viewer](docs/info_viewer.md)
* **Developer & Architecture Guides**:
  * [Firmware Memory Map, Budgets & Subsystem Limits](docs/memory_map.md)
  * [Firmware Architecture, Hardware Limits & Developer Gotchas](docs/developer_guide.md)
  * [Global File Browser Developer Guide](docs/global_file_browser.md)
  * [Custom Synth Engine & `.psyn` Plugin SDK](docs/custom_synth_sdk.md)
  * [Modular Sample Editor Effects & `.ptfx` Developer SDK](docs/modular_effects_system.md)

---

## Polyend Tracker Open-Source Ecosystem

This repository is part of a growing open-source ecosystem around the Polyend Tracker family:

* **[`polyend/tracker-theme-editor`](https://github.com/polyend/tracker-theme-editor)** ([Live Web App](https://polyend.github.io/tracker-theme-editor/)): Browser-based visual **Theme & Font Editor** with a hardware screen simulator.
* **[`polyend/tracker-lib`](https://github.com/polyend/tracker-lib)**: Open-source library and tools for reading, writing, and inspecting Polyend Tracker project (`.mt`), pattern (`.mtp`), and instrument (`.pti`) files.
* **[Polyend Backstage Forum](https://backstage.polyend.com/)**: Community forum for sharing custom themes, fonts, `.ptfx` plugins, synth engines, and discussing workflows.
* **[The Tracker Overflow (Discord)](https://discord.com/invite/uEPTnwckJV)**: Unofficial Polyend Tracker Discord where users and developers can join, chat, and find like-minded people.

---

## License

* **Source Code, SDKs, Open Synth Engines (`synth-plugin-sdk/`), `.ptfx` Sample Editor SDK (`editor-fx-sdk/`), PolySynth Host & DSP (`synth/source/`), Drivers & Utilities**: Licensed under the **MIT License with a Wholesale Commercial Clone Restriction** (see [`LICENSE`](LICENSE)).
  * **Musicians & Creators**: Using the firmware for commercial music production, albums, paid live performances/gigs, sample packs, and monetized streaming/YouTube content is **expressly permitted**.
  * **Developers & Plugin Authors**: Reusing individual source files, drivers, DSP algorithms, UI widgets, or open-source C++ synth engines (`CHIP`, etc.) in your own open-source or commercial projects—and selling custom `.ptfx` Sample Editor or custom `.psyn` synth plugins—is **expressly permitted** under the MIT License.
  * **No Wholesale Commercial Clones**: You may **not** sell compiled `.ptf` firmware builds, manufacture/sell commercial clone hardware running the Tracker firmware, or sell a direct port/wholesale clone of the complete Tracker application as a paid software product.
* **Precompiled Factory Synth Plugins (`synth/prebuilt/*.psyn`)**: Provided under the [Polyend Precompiled Factory Synthesizer Plugins License](synth/prebuilt/LICENSE-PROPRIETARY.txt), permitting free use on authentic Polyend hardware as well as personal/DIY non-commercial projects (commercial embedding or paid distribution of these proprietary factory `.psyn` binaries is prohibited).
* **Third-Party Components**: See [`NOTICE.md`](NOTICE.md).
