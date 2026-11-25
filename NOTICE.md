# Third-Party Notices & Attributions

Polyend Tracker Community Edition (CE) incorporates or links against the following third-party open-source and binary components:

---

## 1. ARM CMSIS & CMSIS-DSP
* **Location**: `platforms/imx/CMSIS/` (CMSIS Core(M) V5.0.3), `platforms/mk66/3rdparty/teensy_core/arm_math.h` (CMSIS-DSP V1.1.0), `platforms/imx/3rdparty/math/libarm_cortexM4lf_math.a`, `platforms/mk66/3rdparty/teensy_core/libarm_cortexM4lf_math.a`
* **License**: Apache License 2.0 (`platforms/imx/CMSIS/`) / BSD-3-Clause (legacy CMSIS-DSP V1.1.0)
* **Copyright**: Copyright (c) ARM Limited. All rights reserved.

## 2. NXP MCUXpresso SDK & Runtime
* **SDK & Startup Location**: `platforms/imx/drivers/`, `platforms/imx/device/`, `platforms/imx/startup/`, `platforms/imx/usb/`, `platforms/imx/sdmmc/`, `core/include/cr_section_macros.h`, `core/include/cr_mtb_buffer.h`, `platforms/mk66/3rdparty/SD/`, `platforms/mk66/3rdparty/I2C/`, `platforms/mk66/3rdparty/SDRAM/`
* **SDK License**: BSD-3-Clause
* **MCUXpresso IDE Runtime & Linker Scripts**: `platforms/imx/linker/libcr_newlib_nohost.a` (prebuilt newlib nohost syscall stubs from NXP MCUXpresso IDE) and generated linker scripts in `platforms/imx/linker/` — subject to the NXP MCUXpresso IDE End User License Agreement (permitting binary distribution targeting NXP silicon).
* **Copyright**: Copyright (c) Code Red Technologies Ltd. / Freescale Semiconductor, Inc. / NXP Semiconductors.

## 3. FatFs - Generic FAT Filesystem Module
* **Location**: `platforms/imx/fatfs/`, `platforms/mk66/3rdparty/SD/fatfs/`
* **License**: FatFs License (BSD-style)
* **Copyright**: Copyright (C) ChaN, 2019. All rights reserved.

## 4. Teensy Cores & Teensy Audio Library (PJRC)
* **Location**: `platforms/mk66/3rdparty/teensy_core/`, `platforms/mk66/3rdparty/Audio/`, `core/libs/Audio/`
* **License**: MIT License / PJRC Permissive License (note: `AudioStream.{h,cpp}` and Teensyduino core headers include PJRC's Clause 2 target-device condition)
* **Copyright**: Copyright (c) 2014-2018 Paul Stoffregen, PJRC.COM, LLC.

## 5. Mutable Instruments DSP (Polyverb Reverb Core)
* **Location**: `core/libs/Audio/Polyverb/` (`reverb.h`, `fx_engine.h`, `cosine_oscillator.h`, `util.h`)
* **License**: MIT License
* **Copyright**: Copyright 2014 Emilie Gillet (adapted for Polyverb by Wojciech Jakóbczyk).

## 6. UI Bitmap Fonts (Roboto Mono & Liberation Sans)
* **Roboto Mono**:
  * **Location**: `core/libs/fonts/RobotoMono-*_L4.{h,cpp}` (4-bit `L4` antialiased glyph bitmaps)
  * **License**: Apache License 2.0 (v1.x) / SIL Open Font License 1.1 (v2+)
  * **Copyright**: Designed by Christian Robertson / Google.
* **Liberation Sans**:
  * **Location**: `sd-card/System/Fonts/New.ptfont` (formerly `core/libs/fonts/LiberationSans-*_L4.{h,cpp}` in v1.0.0)
  * **License**: SIL Open Font License 1.1 (OFL-1.1)
  * **Copyright**: Digitized data copyright (c) 2010 Google Corporation; Copyright (c) 2012 Red Hat, Inc.

## 7. FastCRC
* **Location**: `platforms/mk66/3rdparty/FastCRC/`
* **License**: MIT / GPLv3 dual-licensed (used under MIT terms)
* **Copyright**: Copyright (c) 2014-2016 Frank Boesing.

## 8. Utility & Test Libraries (`util/3rdparty/`)
* **nanopb** (`util/3rdparty/nanopb/`): zlib License — Copyright (c) 2011 Petteri Aimonen.
* **fmt** (`util/3rdparty/fmt/`): MIT License — Copyright (c) 2012-present Victor Zverovich.
* **constexpr-string** (`util/3rdparty/constexpr-string/`): MIT License — Copyright (c) 2019 Marek Bogusovsky.
* **Boost / tiny** (`util/3rdparty/boost/`, `util/3rdparty/tiny/`): Boost Software License 1.0.
* **Catch2** (`util/3rdparty/catch2/`): Boost Software License 1.0.
* **GoogleTest / GoogleMock** (`util/3rdparty/googletest/`, `util/3rdparty/googlemock/`): BSD-3-Clause.

## 9. Polyend Precompiled Factory Synthesizer Plugins (`*.psyn`)
* **Location**: `synth/prebuilt/`
* **License**: Polyend Precompiled Factory Synthesizer Plugins License (`synth/prebuilt/LICENSE-PROPRIETARY.txt`)
* **Copyright**: Copyright (c) 2023-2026 Polyend Sp. z o.o. All rights reserved.
