# Polyend Loadable Synth Plugin SDK (`.psyn`)

Build standalone SD-card synthesizer engines (`/Synths/<NAME>.psyn`) that run across:
- **Polyend Synth**
- **Polyend Play+ & Original Polyend Play (IMX)**
- **Polyend Tracker+ & Polyend Tracker Mini**

## Overview

All synthesizer engines on Polyend Community Edition firmware are loaded dynamically from `/Synths/*.psyn` on the SD card into an external SDRAM plugin pool (`1.92 MB`).

You can write `.psyn` engines in:
1. **C++20 (`include/psyn/psyn_cpp_bridge.h`)** — using the `polysynth::InstrumentVoice` / `MetadataBuilder` / `Patch` SDK interface (`PSYN_REGISTER_CPP_VOICE(MySynthVoice)`).
2. **Pure C (`include/psyn/psyn_plugin.h`)** — implementing `psyn_plugin_entry(const PsynHostApi* host)` directly.

## Building

```bash
make
```

Output `.psyn` binaries are placed in `build/` (e.g. `build/CHIP.psyn`). Copy any `.psyn` file to `/Synths/` on your device's SD card and its presets to `/Patches/<CODE_NAME>/`.
