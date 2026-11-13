# Upcoming Changes (Beta / Unreleased)

> **ℹ️ Note:** The features and fixes listed below have been merged since the **[v1.0.0](CHANGELOG.md)** stable release. They are **not yet in the stable release download**, but you can try them today via the **[Latest Beta Builds](README.md#latest-beta-builds)**.
>
> Looking for what is included in the current stable release? See **[`CHANGELOG.md`](CHANGELOG.md)**.

---

### Added

1. **Loadable `.psyn` Synth Plugin SDK (Tracker+ & Tracker Mini)**
	* **Dynamic SD-Card Synth Plugins (`/Synths/*.psyn`)**: All synthesizer engines now load dynamically as standalone relocatable `.psyn` plugins from `/Synths/*.psyn` on the microSD card without recompiling firmware. Developers can write custom C++20 or Pure C synth engines using [`synth-plugin-sdk/`](synth-plugin-sdk/) (`psyn_cpp_bridge.h` / `psyn_plugin.h`).
	* **Sample-Based Synth Engines (`SampleSlot` & `4 MB` `SamplePool`)**: Added shared reference-counted SDRAM sample storage for sample/wavetable-based `.psyn` engines loading `.wav` assets from `/SynthSamples/<ENGINE>/` or anywhere on the SD card, plus a `1,920 KB` executable plugin arena with boot preloading and LRU eviction.
	* **100% Open-Source Firmware Link (`PolySynth`, `PolyDSP` & Master FX)**: Moved the `PolySynth` host/sequencer framework, `PolyDSP` (`Amplifier` & `Mixer`), and Master FX (`Delay`, `Reverb`) to source compilation under the MIT License with zero precompiled `.a` binary libraries linked into the firmware (only the prebuilt factory `.psyn` plugin binaries in `synth/prebuilt/` remain under [`synth/prebuilt/LICENSE-PROPRIETARY.txt`](synth/prebuilt/LICENSE-PROPRIETARY.txt)).
	* See [`docs/custom_synth_sdk.md`](docs/custom_synth_sdk.md) and [`synth-plugin-sdk/README.md`](synth-plugin-sdk/README.md).

2. **Unified Global File Browser (All 3 Devices)**
	* Replaced fragmented single-level file pickers with [`GlobalFileBrowser`](docs/global_file_browser.md) across Project **`Open`** / **`Save As`**, **`.mod` / `.it` Import**, Synth Patch **`Load`** / **`Save`**, Synth **Sample Slot** `.wav` browsing, and Sample Editor **`.ptfx` Presets & File Parameters**.
	* See [`docs/global_file_browser.md`](docs/global_file_browser.md).

3. **Per-Sample SD Streaming Controls & RAM Default in Sample Loader (All 3 Devices)**
	* **RAM by Default**: Newly imported samples now default to loading fully into **RAM** (`Mem: RAM`) rather than streaming automatically, giving you full control over when SD streaming is used.
	* **Import Memory Mode Selector**: Added a memory mode button on the Sample Loader screen (`Mem: RAM` / `Mem: Auto` / `Mem: [S]`) to choose whether newly loaded samples go to RAM, auto-stream when `256 KB` or larger, or always stream when eligible.
	* **Per-Instrument RAM / Stream Toggle**: Highlighting an existing instrument in the right-hand slot list changes the button to **`Mode: RAM`** or **`Mode: [S]`**, allowing you to switch individual instruments between RAM and SD streaming on the fly.
	* **Crop & Stream**: Added an **`Import Stream`** option in the partial sample crop/import screen alongside `Import` and `Import Low quality`.

4. **"Add to Empty Slot" in Sample Loader (All 3 Devices)**
	* Holding **`[Shift]`** while browsing files in the Sample Loader changes the `Add` action to **`Add to Empty Slot`** (or `Next Slot`), automatically selecting the first available empty instrument slot and importing the sample there.

5. **38 New Chord Inversions in the `Chord` (`f`) Step FX (All 3 Devices)**
	* Expanded the **`Chord` (`f`)** effect from 30 to **68 chord voicings** by adding 38 inversions across major, minor, seventh, diminished, suspended, and `m7b5` chords.

6. **Sample Editor - New Plugins & Tons of Plugin Developer Goodies**
	* **`Transient Shaper` (`transient_shaper.ptfx`) (All 3 Devices)**: New modular Sample Editor effect with bipolar `Attack` (`-100%` to `+100%`) and `Sustain` (`-100%` to `+100%`) controls for sculpting drum hits and loops.
	* **`Haas Effect` (`haas.ptfx`) (Tracker+ & Tracker Mini)**: New psychoacoustic stereo widener plugin that delays the Left or Right channel (`1–100 ms`) with an appended tail fade-out.
	* **Automatic `.ptp` Effect Presets (All 3 Devices)**: Added automatic preset management (**`Load`**, **`Save`**, **`Reset`** via **`More`**) stored under `/Effects/Presets/<PluginName>/*.ptp`, saving and restoring both parameter values and selected file paths without extra plugin code required by developers.
	* **File-Picker & Secondary `.wav` Parameters (`PTFX_PARAM_FILE` / `PTFX_PARAM_SAVE_FILE`) (All 3 Devices)**: Plugins can now declare input/output file parameters backed by `GlobalFileBrowser`, automatic secondary `.wav` decoding (`get_secondary_sample()`), and direct SD file I/O (`read_file()`, `write_file()`).
	* **Mono-to-Stereo `.ptfx` Support (Tracker+ & Tracker Mini)**: Applying a stereo `.ptfx` plugin (such as `Haas Effect` or `Stereo Delay`) to a mono sample now automatically promotes the instrument to stereo when the processed left and right channels differ.
	* See [`docs/modular_effects_system.md`](docs/modular_effects_system.md).

7. **Clearer `[m | V]` vs `[Fx1 | Fx2]` Pattern Editor View (All 3 Devices)**
	* Empty step slots in the **`[m | V]`** pattern view now render dimmed **`m--`** and **`V--`** indicators instead of `---`, making it immediately clear at a glance whether you are viewing `[m | V]` or `[Fx1 | Fx2]`.

8. **USB Serial Development Console & Release Logging (Tracker+ & Tracker Mini)**
	* Added an opt-in USB CDC-ACM serial console (`ENABLE_DEV_CONSOLE` when a `/.develop` file is present on the SD card) with commands for automated host testing (`ping`, `storage on|off`, `flash <name>`, `reboot`, `help`) and a ring-buffered `ENABLE_LOG` build flag that preserves release optimization levels.
	* See [`docs/developer_guide.md`](docs/developer_guide.md) for usage details.

### Changed & Fixed

* **`CHIP` Synth Engine Performance Overhaul (Tracker+ & Tracker Mini)**:
	* Replaced heavy per-sample standard math calls (`std::pow`, `std::sin`, `std::fmod`, `std::exp`) in the audio interrupt with fast, high-precision approximations and hoisted block-level calculations. This eliminates audio interrupt overruns and freezes when playing 8-voice `CHIP` chords.

* **UI/UX, Sequencer & Filesystem Optimizations (All 3 Devices)**:
	* Optimized display SPI bus transfers and pattern editor rendering for snappier UI responsiveness.
	* Moved `Sequencer::play_microStep` into fast `AUDIO_CORE_RAM`, skipped redundant `endFx` resets in the audio sequencer, and removed redundant filesystem checks and full-buffer zeroing during file loads.

* **USB Mass Storage Mode Safety & SD Remounting (Tracker+ & Tracker Mini)**:
	* Entering USB Storage mode now stops active playback, armed recordings, held notes, and background SD sample streaming, and flushes pending workspace/config saves so the device and host computer never access the SD card simultaneously.
	* Leaving USB Storage mode now remounts the SD card volume and rebuilds cluster maps for streamed samples so changes made from a computer are safely recognized.

* **USB Audio & MIDI Class Compliance Fixes (Tracker+ & Tracker Mini)**:
	* **Fixed Inverted `Input` / `Output` USB Audio Modes & Clarified Labels**: Fixed swapped USB Audio direction mapping where selecting `Input` enabled the 28-channel output stream and `Output` enabled the 2-channel input stream. Updated the Config menu options to **`Off`**, **`Input (2 in)`**, **`Output (28 out)`**, and **`In/Out (2 in, 28 out)`** so the direction and channel count are explicit from the Tracker's perspective.
	* **Fixed Single-Direction USB Audio & MIDI Enumeration**: Fixed composite USB configuration descriptor generation where `bInterfaceNumber` fields were updated after copying descriptors into the configuration buffer, and prevented disabled USB class drivers (`0xFF` interface numbers) from initializing endpoints or intercepting `SetInterface` requests.
	* **Improved Portable USB Host Compatibility**: Reduced advertised USB `bMaxPower` from `500 mA` to `100 mA` for self-powered operation so battery-powered USB hosts (such as the Teenage Engineering OP-1 Field) do not reject enumeration due to bus power limits.

* **Audio Engine & Playback Fixes**:
	* **Fixed Delay/Reverb Send Leak on First Note After Stop (Tracker+ & Tracker Mini)**: Cleared both left and right send channels on note start so an instrument with no send no longer echoes briefly in the right channel on the first note after stopping playback.
	* **Fixed Playhead Escaping Very Short Loops & Grains (All 3 Devices)**: Prevented backward loops, ping-pong loops, and granular playheads from running out of bounds into memory (causing noise bursts) when the pitch step exceeds a very short loop or grain length.
	* **Fixed Sequencer Tune Modulation Bug (All 3 Devices)**: Fixed an issue where sequencer modulation on `Tune` checked the `wavetablePosition` flag instead of `tune`.

* **Sequencer, MIDI & Pattern Editor Fixes (All 3 Devices)**:
	* **Fixed Freeze After Leaving Performance Mode (Tracker+ & Tracker Mini)**: Moved the pending pattern reload on sequencer stop out of the audio interrupt callback and into the main loop, fixing a freeze when previewing steps or receiving MIDI Stop after exiting Performance Mode.
	* **Smoother Song Playback & External `MIDI Start` Reliability**: Fixed freezes when background auto-saving overlapped a pattern change during Song playback or when receiving `MIDI Start` on the Song screen, and ensured playback stops cleanly if the next pattern cannot be loaded in time instead of playing or saving the wrong pattern.
	* **Full 255-Slot Song Fixes**: Fixed freezes and unexpected jumps back to Slot 1 when playing to the end of, or deleting slots from, a full 255-slot song, and ignored Song slot paste when nothing has been copied yet.
	* **Pattern Editor Cursor, Fill, Pad & Roll Fixes**: Fixed the cursor jumping past the end of a pattern after pasting long selections with a large Edit Step, kept `Note Up` Rolls on the highest note (`G9`) instead of wrapping down to `C0`, fixed `Random Fx` and `Off` ranges in the Fill popup, ignored unused pads `47–48` in the `FX` pad shortcut, and kept pads in normal note layout when a MIDI or Synth instrument is selected.
	* **Fixed Synth FX MIDI CC Control in Performance Mode**: Restored MIDI CC control over synth effect parameters in Performance Mode.
	* **Fixed Song Mode Block Copy/Paste**: Fixed copying and pasting song arrangement blocks smaller than 32 steps.
	* **Fixed Beat-Slice Pad Guide Lights in Live Recording Mode (Tracker & Tracker+)**: Enabled beat-slice pad guide lights in the Pattern Editor when Live Recording (`Rec + Play`) is active, matching step-record mode and the Instrument Parameters screen.

* **Performance Mode Fixes (All 3 Devices)**:
	* **Fixed Pattern Edits Resetting After Visiting `Perform` While Stopped**: Leaving `Instrument Parameters` or `Master` after opening them from `Perform` while playback is stopped now exits Performance Mode immediately, preventing the pattern from reloading from the SD card and undoing recent edits on the next note preview.
	* **Fixed FX Slot Swapping & MIDI CC Row Handling**: Swapping two FX slots (`Shift + Left/Right`) now moves their selected row, live value, MIDI CC state, and pad lights together, and turning the encoder on a MIDI CC row no longer overwrites neighboring slot values.
	* **Safe Missing/Unreadable Pattern Fallback**: Selecting a missing or unreadable pattern for a Performance track now plays silence instead of triggering unexpected notes.

* **Sample Loader, Sample Playback, Sample Editor & Sample Recorder Fixes (All 3 Devices)**:
	* **Sample Loader Folder, Import & `Delete Unused` Fixes**: Fixed opening folders with parentheses in their names (such as `Loops (120-140 BPM)`), handled unreadable or deeply nested folders gracefully, prevented multi-file imports from exceeding Slot 48 or available sample memory, fixed time labels above `100s` on the crop-import screen, fixed renaming instruments when a MIDI/Synth instrument was previously active, and fixed `Delete unused` removing samples only used on the last step of a track, in Pattern 255, or in unsaved edits of the current pattern.
	* **Sample Playback & Sample Editor Fixes**: Fixed `Add` slice corrupting slice markers after `Slice Evenly` or `Auto Slice`, improved zoomed waveform display when switching from a long sample to a short one, and prevented selected MIDI/Synth instruments from interfering with Sample Playback controls and MIDI CC.
	* **Sample Recorder Fixes**: Pressing `Record` again now cancels a count-in waiting on a missing external MIDI/USB clock, project samples are reliably reloaded if recording is interrupted by standby or USB Storage, and the previously selected instrument is always restored on exit.
	* **`Analog Kick Gen` `.ptfx` Click Fix**: Removed an audible click at the end of the kick decay in the `Analog Kick Gen` Sample Editor generator plugin.

* **File Screen, Instrument Parameters & Synth Patch Editor Stability Fixes**:
	* **File Screen Reliability (All 3 Devices)**: Fixed `Save As` and `Export` hanging if a folder could not be created (such as on a full or write-protected SD card), prevented `Cancel` in `Save As` from triggering an earlier `New` or `Open` action later on, and properly closed hidden file lists after `Overwrite` and `Save` prompts.
	* **Navigation & Synth Patch Editor Stability**: Preserved "return to previous screen" behavior after brief standby taps or USB Storage sessions on all devices, prevented removing the SD card during `Init patch` on Tracker+ / Tracker Mini from locking up the controls, and fixed several display and Synth Patch Editor stability edge cases.

* **Hardened File Validation, WAV Parsing, SD I/O & Sample Recorder Stop**:
	* Fixed Sample Recorder stop deadlocks on stereo buffer imbalance and full sample memory.
	* Hardened WAV header parsing and dynamic `data` chunk offset detection for custom wavetable envelopes.
	* Added header bounds validation for `.pti` instruments, `.mtp` patterns, and projects, and preserved signed SD I/O error codes across `FileSystem`.
	* Fixed pattern rename not marking pattern metadata as dirty for project saving.
